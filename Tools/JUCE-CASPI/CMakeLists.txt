# JUCE-CASPI: optional, header-only adapters bridging JUCE plugin types and
# CASPI's framework-agnostic MIDI/audio types.
#
# This module intentionally has no fetch/find logic of its own: it expects
# the CASPI and JUCE (juce::juce_audio_basics) targets to already exist in
# the including project, so CASPI itself never depends on JUCE.

add_library(JUCE-CASPI INTERFACE)

target_include_directories(JUCE-CASPI INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(JUCE-CASPI INTERFACE
        CASPI
        juce::juce_audio_basics
)
