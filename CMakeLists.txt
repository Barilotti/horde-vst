cmake_minimum_required(VERSION 3.22)
project(HORDE VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: use ./JUCE if present, otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(HORDE
  COMPANY_NAME "HordeAudio"
  BUNDLE_ID com.hordeaudio.horde
  PRODUCT_NAME "HORDE"
  PLUGIN_MANUFACTURER_CODE Hrda
  PLUGIN_CODE Hrd1
  FORMATS VST3 Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Spatial)

target_sources(HORDE PRIVATE
  Source/CrowdEngine.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(HORDE PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HORDE
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(HORDE_BUILD_TESTS "Build offline render test tool" OFF)
if(HORDE_BUILD_TESTS)
  juce_add_console_app(HordeRender)
  target_sources(HordeRender PRIVATE Test/Render.cpp Source/CrowdEngine.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(HordeRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="HORDE" JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(HordeRender PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
if(HORDE_BUILD_TESTS)
  juce_add_gui_app(HordeSnapshot)
  target_sources(HordeSnapshot PRIVATE Test/Snapshot.cpp Source/CrowdEngine.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(HordeSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="HORDE" JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(HordeSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
