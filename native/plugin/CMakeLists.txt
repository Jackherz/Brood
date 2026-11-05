# JUCE front panel: standalone app + VST3.
# Set -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build against a local checkout.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

# The shared panel kit: the look, the fonts, the preset browser and the Bench.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../kit ${CMAKE_CURRENT_BINARY_DIR}/kit)
set(BROOD_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND BROOD_FORMATS AU)
endif()

juce_add_plugin(Brood
  VERSION 0.1.0
  COMPANY_NAME "Brood Audio Objects"
  BUNDLE_ID "audio.brood.brood"
  PLUGIN_MANUFACTURER_CODE Broo
  PLUGIN_CODE BrMd
  FORMATS ${BROOD_FORMATS}
  PRODUCT_NAME "Brood"
  DESCRIPTION "Wet channel + micro-looper"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Brood needs audio input to process your instrument."
  VST3_CATEGORIES Fx Delay Reverb
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Brood PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(Brood PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../kit/include)
target_include_directories(Brood PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(Brood PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0 _CRT_SECURE_NO_WARNINGS=1)
target_link_libraries(Brood
  PRIVATE juce::juce_audio_utils kit_fonts
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(BROOD_BUILD_PLUGIN_TESTS "Build headless processor integration tests" OFF)
if(BROOD_BUILD_PLUGIN_TESTS)
  add_executable(plugin_tests ../tests/plugin_tests.cpp)
  target_include_directories(plugin_tests PRIVATE ${JUCE_SOURCE_DIR}/modules ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../kit/include)
  get_target_property(brood_plugin_defs Brood COMPILE_DEFINITIONS)
  target_compile_definitions(plugin_tests PRIVATE ${brood_plugin_defs})
  target_link_libraries(plugin_tests PRIVATE Brood kit_fonts)
  add_test(NAME plugin_tests COMMAND plugin_tests)
  # Not a test: renders the editor to a PNG so a layout change can be checked headless.
  add_executable(editor_snapshot ../tests/editor_snapshot.cpp)
  target_include_directories(editor_snapshot PRIVATE ${JUCE_SOURCE_DIR}/modules ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../kit/include)
  target_compile_definitions(editor_snapshot PRIVATE ${brood_plugin_defs})
  target_link_libraries(editor_snapshot PRIVATE Brood kit_fonts)
endif()

option(BROOD_BUILD_VST3_HOST_TESTS "Build a JUCE-based VST3 hosting smoke test" OFF)
if(BROOD_BUILD_VST3_HOST_TESTS)
  juce_add_console_app(vst3_host_tests PRODUCT_NAME "Brood VST3 Host Tests" VERSION 0.1.0)
  target_sources(vst3_host_tests PRIVATE ../tests/vst3_host_tests.cpp)
  add_dependencies(vst3_host_tests Brood_VST3)
  target_compile_definitions(vst3_host_tests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(vst3_host_tests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
  add_test(NAME vst3_host_tests COMMAND vst3_host_tests "$<TARGET_FILE_DIR:Brood_VST3>/../..")
endif()
