#pragma once

#include <JuceHeader.h>

class TestSynthAudioProcessor;

class TestSynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TestSynthAudioProcessorEditor (TestSynthAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    TestSynthAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestSynthAudioProcessorEditor)
};
