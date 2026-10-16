cmake_minimum_required(VERSION 3.25)
project(BwowPitch2 VERSION 1.0.0)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BwowPitch2
    COMPANY_NAME "Bwow"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Bwow
    PLUGIN_CODE Bwp2
    FORMATS VST3
    VST3_CATEGORIES Fx Pitch Shift
    PRODUCT_NAME "Bwow Pitch 2"
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(BwowPitch2)

target_sources(BwowPitch2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(BwowPitch2 PRIVATE cxx_std_17)
target_compile_definitions(BwowPitch2 PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BwowPitch2
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(core_test tests/core_test.cpp)
target_compile_features(core_test PRIVATE cxx_std_17)
set_target_properties(core_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/tests")
