# XLOC2 emulator desktop app (JUCE)
include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.8
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Embedded assets (real front-panel artwork from Calsynth)
# panel.svg = vector outlines (crisp at any scale); panel.png kept as fallback
juce_add_binary_data(xloc2_assets SOURCES Assets/panel.svg Assets/panel.png)

juce_add_gui_app(xloc2
  VERSION 0.1.0
  PRODUCT_NAME "XLOC2"
  COMPANY_NAME "Calsynth"
  BUNDLE_ID com.calsynth.xloc2emu
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "XLOC2 uses your audio interface for CV and audio I/O."
)

target_sources(xloc2 PRIVATE
  Source/Main.cpp
  Source/EmuEngine.cpp
  Source/CoreLoader.cpp
  Source/CorePanel.cpp
  Source/PanelComponent.cpp
  Source/OledComponent.cpp
  Source/RoutingPanel.cpp
  Source/TestBenchPanel.cpp
)

target_compile_definitions(xloc2 PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:xloc2,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="0.1.0"
)

target_link_libraries(xloc2 PRIVATE
  xloc2_assets
  juce::juce_gui_extra
  juce::juce_audio_devices
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)

# The app does NOT link the firmware — it dlopens the phz_core module at
# runtime. Bundle a default core with the app so it runs out of the box:
# macOS: XLOC2.app/Contents/Resources/phz_core.dylib; else next to the
# binary. The core comes from the in-tree phz_core target, or (macOS
# two-pass build, XLOC2_BUILD_CORE=OFF) from XLOC2_PREBUILT_CORE.
if(APPLE)
  set(_core_dest "$<TARGET_BUNDLE_CONTENT_DIR:xloc2>/Resources/phz_core.dylib")
else()
  set(_core_dest "$<TARGET_FILE_DIR:xloc2>/phz_core.so")
endif()

if(XLOC2_PREBUILT_CORE)
  if(NOT EXISTS "${XLOC2_PREBUILT_CORE}")
    message(FATAL_ERROR "XLOC2_PREBUILT_CORE=${XLOC2_PREBUILT_CORE} not found")
  endif()
  add_custom_command(TARGET xloc2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${XLOC2_PREBUILT_CORE}" "${_core_dest}")
elseif(TARGET phz_core)
  add_dependencies(xloc2 phz_core)
  add_custom_command(TARGET xloc2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:phz_core> "${_core_dest}")
else()
  message(WARNING
    "No firmware core will be bundled with the app (XLOC2_BUILD_CORE=OFF "
    "and XLOC2_PREBUILT_CORE not set). The app will start core-less.")
endif()
