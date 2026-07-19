cmake_minimum_required(VERSION 3.20)
project(xloc2emu CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Firmware source root (must contain software/src). Overridable so
# scripts/build-core.sh can build a core module from any Phazerville
# checkout (upstream release, fork, branch) with patches/ applied.
set(XLOC2_FW_DIR "${CMAKE_SOURCE_DIR}/firmware" CACHE PATH
    "Phazerville firmware source root (contains software/src)")
# Output name of the loadable core module (phz_core -> phz_core.so/.dylib).
set(XLOC2_CORE_NAME "phz_core" CACHE STRING "Core module output name")
# Optional git ref / label baked into the core's build_info string.
set(XLOC2_CORE_REF "" CACHE STRING "Git ref/label for the core build_info")

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

target_compile_options(firmware_core PRIVATE
  -Wall -Wno-unused-variable -Wno-unused-function -fno-exceptions
  # the Arduino/Teensy toolchain builds with -fpermissive; the firmware
  # relies on it (const-correctness shortcuts etc.)
  -fpermissive -Wno-write-strings
  -fmax-errors=20
  # No STB_GNU_UNIQUE symbols: they would mark the core module NODELETE,
  # breaking true dlclose-unload on hot reload. Each loaded core instance is
  # deliberately fully isolated (RTLD_LOCAL), so uniqueness is unwanted.
  $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>
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
  $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
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

# desktop app (JUCE) — off for pure headless CI runs
option(XLOC2_BUILD_APP "Build the JUCE desktop app" ON)
if(XLOC2_BUILD_APP)
  add_subdirectory(app)
endif()
