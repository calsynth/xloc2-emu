cmake_minimum_required(VERSION 3.20)
project(xloc2emu CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build selection.
#
# The firmware core ALWAYS builds with GCC, on every platform — it matches
# the Teensy (arm-none-eabi-gcc) toolchain the firmware is written for;
# clang hard-errors on several firmware idioms and patching upstream for
# clang would grow the patch set forever. The JUCE app builds with the
# platform default compiler (AppleClang on macOS — required for ObjC++).
# On macOS this means a two-pass build:
#   pass 1 (Homebrew g++-NN):  -DXLOC2_BUILD_APP=OFF            -> phz_core
#   pass 2 (AppleClang):       -DXLOC2_BUILD_CORE=OFF
#                              -DXLOC2_PREBUILT_CORE=<dylib>    -> app
# (scripts/build-core.sh and CI do this automatically.)
# ---------------------------------------------------------------------------
option(XLOC2_BUILD_CORE "Build the firmware core module (requires GCC)" ON)
option(XLOC2_BUILD_APP "Build the JUCE desktop app" ON)
# Pre-built core module to bundle with the app when the core targets are
# not built in this tree (macOS two-pass build).
set(XLOC2_PREBUILT_CORE "" CACHE FILEPATH
    "Path to a pre-built phz_core module to bundle with the app")

# Firmware source root (must contain software/src). Overridable so
# scripts/build-core.sh can build a core module from any Phazerville
# checkout (upstream release, fork, branch) with patches/ applied.
set(XLOC2_FW_DIR "${CMAKE_SOURCE_DIR}/firmware" CACHE PATH
    "Phazerville firmware source root (contains software/src)")
# Output name of the loadable core module (phz_core -> phz_core.so/.dylib).
set(XLOC2_CORE_NAME "phz_core" CACHE STRING "Core module output name")
# Optional git ref / label baked into the core's build_info string.
set(XLOC2_CORE_REF "" CACHE STRING "Git ref/label for the core build_info")

if(XLOC2_BUILD_CORE)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR
    "The firmware core must be built with GCC (matches the Teensy "
    "toolchain; clang rejects several firmware idioms). Current compiler: "
    "${CMAKE_CXX_COMPILER_ID}.\n"
    "On macOS:  brew install gcc && cmake -DCMAKE_C_COMPILER=gcc-14 "
    "-DCMAKE_CXX_COMPILER=g++-14 -DXLOC2_BUILD_APP=OFF ...\n"
    "(then build the app in a second tree with the default compiler and "
    "-DXLOC2_BUILD_CORE=OFF -DXLOC2_PREBUILT_CORE=<path to phz_core>; "
    "scripts/build-core.sh does the core pass for you.)")
endif()

set(FW ${XLOC2_FW_DIR}/software/src)
set(STUBS ${CMAKE_SOURCE_DIR}/third_party/teensy-x86-stubs/src)
if(NOT EXISTS ${FW})
  message(FATAL_ERROR "XLOC2_FW_DIR=${XLOC2_FW_DIR} does not contain software/src")
endif()

# ---------------------------------------------------------------------------
# Firmware sources (T4.1 / CalSynthXL target), minus hardware-only TUs that
# the shim layer replaces.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE FW_SOURCES CONFIGURE_DEPENDS ${FW}/*.cpp)

set(FW_EXCLUDES
  # replaced by shim/driver emulations
  ${FW}/src/drivers/SH1106_128x64_driver.cpp
  ${FW}/OC_ADC.cpp
  ${FW}/AudioIO.cpp
  ${FW}/Main.cpp
)
list(REMOVE_ITEM FW_SOURCES ${FW_EXCLUDES})
# drop hardware-only driver subdirs wholesale (ADC DMA, FreqMeasure)
list(FILTER FW_SOURCES EXCLUDE REGEX "src/drivers/ADC/")
list(FILTER FW_SOURCES EXCLUDE REGEX "src/drivers/FreqMeasure/")

add_library(firmware_core STATIC ${FW_SOURCES}
  shim/shim_arduino.cpp
  shim/shim_drivers.cpp
  shim/shim_audio.cpp
  core/emu.cpp
  core/emu_main.cpp
  ${STUBS}/Print.cpp
  ${STUBS}/WString.cpp
  ${STUBS}/Stream.cpp
  ${STUBS}/hardware_serial.cpp
  ${STUBS}/AudioStream.cpp
  ${STUBS}/digital.cpp
)

# The static lib is linked into the phz_core MODULE: build it PIC with hidden
# visibility so the module exports only its single C entry point.
set_target_properties(firmware_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(firmware_core BEFORE PUBLIC
  ${CMAKE_SOURCE_DIR}/shim        # our shims win over stubs where both exist
  ${STUBS}
  ${FW}
  ${FW}/extern
)

target_compile_definitions(firmware_core PUBLIC
  EMULATOR
  ARDUINO_TEENSY41
  __IMXRT1062__
  TEENSYDUINO=159
  ARDUINO=10819
  F_CPU=600000000
  USB_MIDI
  # env:T41 applet set
  DRUMMAP_GRIDS2
  ENABLE_APP_CALIBR8OR
  ENABLE_APP_SCENES
  ENABLE_APP_PONG
  OC_VERSION_EXTRA="_XLOC2emu"
)

# GCC-only flags (the core is GCC-only — enforced above — but keep them
# conditional so a stray non-GNU configure stops at the FATAL_ERROR, not at
# an unknown-flag error):
#   -fpermissive/-fmax-errors: Arduino/Teensy toolchain parity (the firmware
#   relies on -fpermissive const-correctness shortcuts)
#   -fno-gnu-unique: ELF-only (meaningless on Mach-O); STB_GNU_UNIQUE symbols
#   would mark the core module NODELETE, breaking true dlclose-unload on hot
#   reload. Each loaded core instance is deliberately isolated (RTLD_LOCAL).
target_compile_options(firmware_core PRIVATE
  -Wall -Wno-unused-variable -Wno-unused-function -fno-exceptions
  -Wno-write-strings
  $<$<CXX_COMPILER_ID:GNU>:-fpermissive -fmax-errors=20>
  $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-fno-gnu-unique>
)

# ---------------------------------------------------------------------------
# phz_core — the dynamically loadable firmware core module. The app (and the
# headless test) dlopen this; neither links firmware_core directly.
# ---------------------------------------------------------------------------
add_library(phz_core MODULE core/xloc2_core_api.cpp)
target_link_libraries(phz_core PRIVATE firmware_core)
set_target_properties(phz_core PROPERTIES
  PREFIX ""                       # phz_core.so, not libphz_core.so
  OUTPUT_NAME "${XLOC2_CORE_NAME}"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_compile_options(phz_core PRIVATE
  $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-fno-gnu-unique>)
if(APPLE)
  # Homebrew GCC builds the core, but end users don't have Homebrew's
  # libstdc++/libgcc — link them statically so the dylib is self-contained.
  # CI verifies with `otool -L`: no /opt/homebrew or /usr/local paths.
  target_link_options(phz_core PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(XLOC2_CORE_REF)
  target_compile_definitions(phz_core PRIVATE
    XLOC2_CORE_GIT_REF="${XLOC2_CORE_REF}")
endif()

# ---------------------------------------------------------------------------
# test harnesses
# ---------------------------------------------------------------------------
# headless exercises the real deployment path: it dlopens phz_core.
add_executable(headless test/headless.cpp)
target_include_directories(headless PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(headless PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(headless phz_core)

# dbg links the firmware statically (breakpoint-friendly single binary).
add_executable(dbg test/dbg.cpp)
target_link_libraries(dbg firmware_core)
if(APPLE)
  # allow the core's low-address fake-peripheral mappings (see app/CMakeLists)
  target_link_options(headless PRIVATE -Wl,-pagezero_size,0x4000)
  target_link_options(dbg PRIVATE -Wl,-pagezero_size,0x4000)
endif()

endif()  # XLOC2_BUILD_CORE

# desktop app (JUCE) — off for pure headless CI runs
if(XLOC2_BUILD_APP)
  add_subdirectory(app)
endif()
