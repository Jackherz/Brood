# JUCE front panel: standalone app + VST3.
# Set -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build against a local checkout.
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

# The shared panel kit: the look, the fonts, the preset browser and the Bench.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../kit/CMakeLists.txt")
  message(FATAL_ERROR "The kit is missing. Run: git submodule update --init")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../kit ${CMAKE_CURRENT_BINARY_DIR}/kit)
set(BROOD_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND BROOD_FORMATS AU)
endif()

juce_add_plugin(Brood
  VERSION 0.3.1
  COMPANY_NAME "Glyph Audio"
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
# Brood's panel faces (B612 Mono, Overpass Medium, Chivo Mono), fetched into assets/fonts by
# scripts/fetch-fonts.sh. Without them it still builds, in the kit's VT323 and the system sans.
set(BROOD_FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fonts)
set(BROOD_FONT_FILES ${BROOD_FONT_DIR}/B612Mono-Regular.ttf ${BROOD_FONT_DIR}/Overpass-Medium.ttf ${BROOD_FONT_DIR}/ChivoMono-Regular.ttf)
set(BROOD_HAS_FONTS ON)
foreach(f ${BROOD_FONT_FILES})
  if(NOT EXISTS ${f})
    set(BROOD_HAS_FONTS OFF)
  endif()
endforeach()
if(BROOD_HAS_FONTS)
  juce_add_binary_data(brood_fonts HEADER_NAME BroodFonts.h NAMESPACE BroodFonts SOURCES ${BROOD_FONT_FILES})
  set_target_properties(brood_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(Brood PRIVATE brood_fonts)
  target_compile_definitions(Brood PUBLIC BROOD_PANEL_FONTS=1)
else()
  message(WARNING "Brood's panel fonts are missing (bash scripts/fetch-fonts.sh); building with the kit's faces.")
  target_compile_definitions(Brood PUBLIC BROOD_PANEL_FONTS=0)
endif()
# The demo loops the standalone plays (Jack Herz's recordings), fetched into assets/demos by
# scripts/fetch-demos.sh. Without them it still builds; the standalone then has no demo.
set(BROOD_DEMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/demos)
set(BROOD_DEMO_FILES)
foreach(name ambiotica feedback-ambience tela-tape-loop habit-and-keys-1 habit-and-keys-2 chroma-rhodes nylon-tines)
  list(APPEND BROOD_DEMO_FILES ${BROOD_DEMO_DIR}/${name}.flac)
endforeach()
set(BROOD_HAS_DEMOS ON)
foreach(f ${BROOD_DEMO_FILES})
  if(NOT EXISTS ${f})
    set(BROOD_HAS_DEMOS OFF)
  endif()
endforeach()
if(BROOD_HAS_DEMOS)
  juce_add_binary_data(brood_demos HEADER_NAME BroodDemos.h NAMESPACE BroodDemos SOURCES ${BROOD_DEMO_FILES})
  set_target_properties(brood_demos PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(Brood PRIVATE brood_demos)
  target_compile_definitions(Brood PUBLIC BROOD_DEMOS=1)
else()
  message(WARNING "Brood's demo loops are missing (bash scripts/fetch-demos.sh); the standalone will have no demo.")
  target_compile_definitions(Brood PUBLIC BROOD_DEMOS=0)
endif()
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
  if(BROOD_HAS_FONTS)
    target_link_libraries(plugin_tests PRIVATE brood_fonts)
  endif()
  if(BROOD_HAS_DEMOS)
    target_link_libraries(plugin_tests PRIVATE brood_demos)
  endif()
  add_test(NAME plugin_tests COMMAND plugin_tests)
  # Not a test: renders the editor to a PNG so a layout change can be checked headless.
  add_executable(editor_snapshot ../tests/editor_snapshot.cpp)
  target_include_directories(editor_snapshot PRIVATE ${JUCE_SOURCE_DIR}/modules ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../kit/include)
  target_compile_definitions(editor_snapshot PRIVATE ${brood_plugin_defs})
  target_link_libraries(editor_snapshot PRIVATE Brood kit_fonts)
  if(BROOD_HAS_FONTS)
    target_link_libraries(editor_snapshot PRIVATE brood_fonts)
  endif()
  if(BROOD_HAS_DEMOS)
    target_link_libraries(editor_snapshot PRIVATE brood_demos)
  endif()
endif()

option(BROOD_BUILD_VST3_HOST_TESTS "Build a JUCE-based VST3 hosting smoke test" OFF)
if(BROOD_BUILD_VST3_HOST_TESTS)
  juce_add_console_app(vst3_host_tests PRODUCT_NAME "Brood VST3 Host Tests" VERSION 0.3.1)
  target_sources(vst3_host_tests PRIVATE ../tests/vst3_host_tests.cpp)
  add_dependencies(vst3_host_tests Brood_VST3)
  target_compile_definitions(vst3_host_tests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(vst3_host_tests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
  add_test(NAME vst3_host_tests COMMAND vst3_host_tests "$<TARGET_FILE_DIR:Brood_VST3>/../..")
endif()
