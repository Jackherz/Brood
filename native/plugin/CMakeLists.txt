# JUCE front panel: standalone app + VST3.
# Set -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build against a local checkout.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

# The interface fonts are binary, so they are fetched rather than committed
# (scripts/fetch-fonts.sh). Embed them when they are there; otherwise the editor
# falls back to a system monospaced face and the build still succeeds.
set(BROOD_FONT_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fonts/VT323-Regular.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fonts/PressStart2P-Regular.ttf)
set(BROOD_HAVE_FONTS TRUE)
foreach(brood_font IN LISTS BROOD_FONT_FILES)
  if(NOT EXISTS "${brood_font}")
    set(BROOD_HAVE_FONTS FALSE)
  endif()
endforeach()
if(BROOD_HAVE_FONTS)
  juce_add_binary_data(BroodFonts SOURCES ${BROOD_FONT_FILES})
else()
  message(WARNING "assets/fonts/*.ttf missing: the panel will use a system font. "
                  "Run 'bash scripts/fetch-fonts.sh' and rebuild for the pixel face.")
  add_library(BroodFonts INTERFACE)
endif()
set(BROOD_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND BROOD_FORMATS AU)
endif()

juce_add_plugin(MoodEmu
  VERSION 0.1.0
  COMPANY_NAME "Brood Audio Objects"
  BUNDLE_ID "audio.brood.mood"
  PLUGIN_MANUFACTURER_CODE Broo
  PLUGIN_CODE BrMd
  FORMATS ${BROOD_FORMATS}
  PRODUCT_NAME "Brood Mood"
  DESCRIPTION "Wet channel + micro-looper emulator (unofficial, inspired by the Chase Bliss MOOD)"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Brood Mood needs audio input to process your instrument."
  VST3_CATEGORIES Fx Delay Reverb
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MoodEmu PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(MoodEmu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(MoodEmu PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0 _CRT_SECURE_NO_WARNINGS=1)
if(BROOD_HAVE_FONTS)
  target_compile_definitions(MoodEmu PUBLIC BROOD_HAVE_FONTS=1)
endif()
target_link_libraries(MoodEmu
  PRIVATE juce::juce_audio_utils BroodFonts
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(BROOD_BUILD_PLUGIN_TESTS "Build headless processor integration tests" OFF)
if(BROOD_BUILD_PLUGIN_TESTS)
  add_executable(plugin_tests ../tests/plugin_tests.cpp)
  target_include_directories(plugin_tests PRIVATE ${JUCE_SOURCE_DIR}/modules ${CMAKE_CURRENT_SOURCE_DIR}/..)
  get_target_property(brood_plugin_defs MoodEmu COMPILE_DEFINITIONS)
  target_compile_definitions(plugin_tests PRIVATE ${brood_plugin_defs})
  target_link_libraries(plugin_tests PRIVATE MoodEmu)
  add_test(NAME plugin_tests COMMAND plugin_tests)
endif()

option(BROOD_BUILD_VST3_HOST_TESTS "Build a JUCE-based VST3 hosting smoke test" OFF)
if(BROOD_BUILD_VST3_HOST_TESTS)
  juce_add_console_app(vst3_host_tests PRODUCT_NAME "Brood VST3 Host Tests" VERSION 0.1.0)
  target_sources(vst3_host_tests PRIVATE ../tests/vst3_host_tests.cpp)
  add_dependencies(vst3_host_tests MoodEmu_VST3)
  target_compile_definitions(vst3_host_tests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(vst3_host_tests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
  add_test(NAME vst3_host_tests COMMAND vst3_host_tests "$<TARGET_FILE_DIR:MoodEmu_VST3>/../..")
endif()
