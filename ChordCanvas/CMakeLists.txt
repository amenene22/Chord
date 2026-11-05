cmake_minimum_required(VERSION 3.22)
project(ChordCanvas VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: ローカルに JUCE_DIR があればそれを使い、なければ GitHub から取得
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ChordCanvas
    COMPANY_NAME "ChordCanvas"
    PLUGIN_MANUFACTURER_CODE Ccvs
    PLUGIN_CODE Chcv
    FORMATS VST3
    PRODUCT_NAME "ChordCanvas"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(ChordCanvas PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ChordCanvas PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChordCanvas
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
