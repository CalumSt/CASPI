cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This is the internal name of the project and the name of JUCE's shared code target.
# It cannot contain spaces. JUCE's PRODUCT_NAME (below) can.
set(PROJECT_NAME "TestSynth")
set(COMPANY_NAME "CASPI")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

project(${PROJECT_NAME} VERSION 0.1)

# ============================================================
# Dependencies
# ============================================================

include(FetchContent)

FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Use CASPI directly from the repo root rather than a vendored copy.
# CASPI itself has no JUCE dependency; JUCE-CASPI below is the only bridge.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/CASPI)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE-CASPI ${CMAKE_CURRENT_BINARY_DIR}/JUCE-CASPI)

# ============================================================
# Plugin target
# ============================================================

juce_add_plugin(${PROJECT_NAME}
        COMPANY_NAME ${COMPANY_NAME}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD TRUE
        PLUGIN_MANUFACTURER_CODE Cspi
        PLUGIN_CODE Tsyn
        FORMATS VST3 Standalone
        PRODUCT_NAME "CASPI Test Synth"
)

juce_generate_juce_header(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        JUCE-CASPI
        juce::juce_audio_basics
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
