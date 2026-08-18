#pragma once

#include <JuceHeader.h>

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "midi/caspi_Midi.h"
#include "synthesizers/caspi_Voice.h"
#include "synthesizers/caspi_Engine.h"

#include "JuceCaspi/juce_caspi.h"

/**
 * Minimal JUCE synth plugin wrapping CASPI::Engine.
 *
 * Voice graph per note: BlepOscillator (Saw) -> ADSR.
 * The ADSR reads the oscillator's audio input and multiplies it by the
 * envelope value (VCA-when-connected behaviour), so the ADSR node is both
 * the amplitude shaper and the voice's output node.
 */
class TestSynthAudioProcessor final : public juce::AudioProcessor
{
public:
    TestSynthAudioProcessor();
    ~TestSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    static constexpr std::size_t kNumVoices = 8;

    struct SynthConfig : CASPI::DefaultSynthConfig
    {
        static constexpr float MasterGain = 0.25f;
        static constexpr bool  HardClip   = true;
    };

    CASPI::Graph::NodeId oscNodeId { CASPI::Graph::INVALID_NODE_ID };
    CASPI::Graph::NodeId envNodeId { CASPI::Graph::INVALID_NODE_ID };

    CASPI::Engine<float, kNumVoices, SynthConfig> engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestSynthAudioProcessor)
};
