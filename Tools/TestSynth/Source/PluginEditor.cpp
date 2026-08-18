#include "PluginEditor.h"
#include "PluginProcessor.h"

TestSynthAudioProcessorEditor::TestSynthAudioProcessorEditor (TestSynthAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (400, 200);
}

void TestSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("CASPI Test Synth", getLocalBounds(), juce::Justification::centred, 1);
}
