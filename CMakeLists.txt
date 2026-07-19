cmake_minimum_required(VERSION 3.20)
project(xloc2emu CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FW ${CMAKE_SOURCE_DIR}/firmware/software/src)
set(STUBS $ENV{HOME}/teensy-x86-stubs/src)

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
)

# headless test harness
add_executable(headless test/headless.cpp)
target_link_libraries(headless firmware_core)
