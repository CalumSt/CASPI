#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    CASPI::VoiceConfig<float> makeVoiceGraph()
    {
        using namespace CASPI;

        Graph::AudioGraph<float> g;

        auto [oscId, osc] = g.emplace<Oscillators::BlepOscillator<float>>();
        auto [envId, env] = g.emplace<Envelope::ADSR<float>>();

        osc.setShape (Oscillators::WaveShape::Saw);
        env.setADSR (0.01f, 0.15f, 0.7f, 0.3f);

        g.connect (oscId, envId); // osc output -> env's audio input (VCA)

        return VoiceConfig<float> { std::move (g), envId, envId };
    }
}

TestSynthAudioProcessor::TestSynthAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , engine (kNumVoices, [] { return makeVoiceGraph(); })
{
    // AudioGraph hands out NodeIds sequentially from 0, and every voice graph
    // is built the same way, so these IDs are the same for every voice.
    oscNodeId = 0;
    envNodeId = 1;

    engine.onNoteOn = [this] (uint8_t note, uint8_t /*vel*/, uint8_t /*ch*/, std::size_t voiceIdx)
    {
        auto* voiceGraph = engine.getVoiceManager().getVoiceGraph (voiceIdx);
        if (auto* osc = voiceGraph->template getNodeAs<CASPI::Oscillators::BlepOscillator<float>> (oscNodeId))
            osc->setFrequency (CASPI::Midi::noteToFrequency<float> (note));
    };
}

void TestSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (static_cast<std::size_t> (getTotalNumOutputChannels()),
                     static_cast<std::size_t> (samplesPerBlock),
                     sampleRate);
}

bool TestSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TestSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    CASPI::JuceAdapters::pushMidiBuffer (engine, midiMessages);

    engine.process();

    CASPI::JuceAdapters::copyToJuceBuffer (engine.getOutputBuffer(), buffer);
}

juce::AudioProcessorEditor* TestSynthAudioProcessor::createEditor()
{
    return new TestSynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynthAudioProcessor();
}
