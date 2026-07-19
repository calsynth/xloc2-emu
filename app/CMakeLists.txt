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
  firmware_core
  xloc2_assets
  juce::juce_gui_extra
  juce::juce_audio_devices
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)
