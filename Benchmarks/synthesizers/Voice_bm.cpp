/*******************************************************************************
 * Benchmarks — Graph / VoiceManager / Engine real-time headroom
 *
 * Unlike the base/ SIMD micro-benchmarks, these exercise the "heavier"
 * composite classes end-to-end: a realistic per-voice signal chain
 * (oscillator -> filter -> ADSR-modulated VCA) run through AudioGraph,
 * VoiceManager (polyphonic mixdown), and Engine (MIDI dispatch + mixdown).
 *
 * All engine/voice-manager benchmarks use a fixed 512-frame stereo block
 * at 48kHz — a typical real-time audio callback. items_per_second reports
 * blocks/sec; a dedicated docs-page chart compares that against the
 * 48000/512 ~= 93.75 blocks/sec required to keep pace with real-time.
 ******************************************************************************/

#include <benchmark/benchmark.h>

#include "controls/caspi_Envelope.h"
#include "core/caspi_Graph.h"
#include "filters/caspi_StateVariable.h"
#include "oscillators/caspi_BlepOscillator.h"
#include "synthesizers/caspi_Engine.h"
#include "synthesizers/caspi_Voice.h"

namespace
{
    using namespace CASPI;
    using namespace CASPI::Graph;

    static constexpr double kSampleRate  = 48000.0;
    static constexpr std::size_t kBlock  = 512u;
    static constexpr std::size_t kChans  = 2u;
    static constexpr std::size_t kMaxVoices = 64u;

    /*----------------------------------------------------------------------
     * MultiplyNode — sample-by-sample product of two audio inputs.
     * Used here as the envelope-modulated VCA stage (filter x ADSR).
     *---------------------------------------------------------------------*/
    template <typename FloatType>
    class MultiplyNode : public AudioNode<MultiplyNode<FloatType>, FloatType>
    {
        public:
            MultiplyNode() : AudioNode<MultiplyNode<FloatType>, FloatType> (2, 1) {}

            void processImpl (AudioContext<FloatType>& ctx) noexcept
            {
                const auto* a = ctx.getAudioInput (this->getId(), 0);
                const auto* b = ctx.getAudioInput (this->getId(), 1);
                if (a == nullptr || b == nullptr)
                {
                    this->outputBuffer.clear();
                    return;
                }
                for (std::size_t ch = 0; ch < this->outputBuffer.numChannels(); ++ch)
                {
                    for (std::size_t fr = 0; fr < this->outputBuffer.numFrames(); ++fr)
                    {
                        this->outputBuffer.sample (ch, fr) = a->sample (ch, fr) * b->sample (ch, fr);
                    }
                }
            }
    };

    /*----------------------------------------------------------------------
     * Builds one voice: BlepOscillator -> StateVariable -> MultiplyNode(ADSR).
     * Representative of a minimal subtractive-synth voice.
     *---------------------------------------------------------------------*/
    VoiceConfig<float> makeVoice (float frequencyHz)
    {
        AudioGraph<float> graph;

        auto osc = graph.emplace<Oscillators::BlepOscillator<float>>();
        osc.node.setShape (Oscillators::WaveShape::Saw);
        osc.node.setFrequency (frequencyHz);

        auto filter = graph.emplace<Filters::StateVariable<float>>();
        filter.node.setCutoff (1500.f);
        filter.node.setQ (0.7071067811865476f);

        auto env = graph.emplace<Envelope::ADSR<float>>();
        env.node.setADSR (0.005f, 0.08f, 0.7f, 0.2f);

        auto mul = graph.emplace<MultiplyNode<float>>();

        (void) graph.connect (osc.id, filter.id);
        (void) graph.connect (filter.id, { mul.id, 0 });
        (void) graph.connect (env.id, { mul.id, 1 });

        return VoiceConfig<float> (std::move (graph), mul.id, env.id);
    }

    /*----------------------------------------------------------------------
     * BM_VoiceGraph_Render — single voice, varying block size.
     * Baseline cost of the per-voice signal chain in isolation.
     *---------------------------------------------------------------------*/
    void BM_VoiceGraph_Render (benchmark::State& state)
    {
        const auto numFrames = static_cast<std::size_t> (state.range (0));

        auto config = makeVoice (220.f);
        AudioGraph<float> graph = std::move (config.graph);
        (void) graph.prepare (kChans, numFrames, kSampleRate);

        auto* env = graph.getNodeAs<Envelope::ADSR<float>> (config.envelopeNodeId);
        env->noteOn();

        for (auto _ : state)
        {
            graph.process();
        }
        state.SetItemsProcessed (state.iterations());
    }
    BENCHMARK (BM_VoiceGraph_Render)->Arg (64)->Arg (128)->Arg (256)->Arg (512)->Arg (1024)->Arg (2048);

    /*----------------------------------------------------------------------
     * BM_VoiceManager_Process — N fully active voices mixed per block.
     * Direct measurement of polyphonic mixdown cost.
     *---------------------------------------------------------------------*/
    void BM_VoiceManager_Process (benchmark::State& state)
    {
        const auto polyphony = static_cast<std::size_t> (state.range (0));

        int noteIdx = 0;
        VoiceManager<float, kMaxVoices> vm (polyphony, [&noteIdx] ()
        {
            const float freq = 110.f * std::pow (2.f, static_cast<float> (noteIdx++ % 24) / 12.f);
            return makeVoice (freq);
        });
        vm.prepare (kChans, kBlock, kSampleRate);

        for (std::size_t i = 0; i < polyphony; ++i)
        {
            vm.noteOn (static_cast<int> (36 + i), 100);
        }

        AudioGraph<float>::BufferType mix;
        mix.resize (kChans, kBlock);

        for (auto _ : state)
        {
            vm.process (mix);
        }
        state.SetItemsProcessed (state.iterations());
    }
    BENCHMARK (BM_VoiceManager_Process)
        ->Arg (1)->Arg (2)->Arg (4)->Arg (8)->Arg (16)->Arg (32)->Arg (64);

    /*----------------------------------------------------------------------
     * BM_Engine_Process — full MIDI-driven engine, steady polyphony.
     * A handful of CCs are pushed each block to exercise dispatch alongside
     * rendering, matching a realistic per-callback MIDI load.
     *---------------------------------------------------------------------*/
    template <typename Config>
    void runEngineBenchmark (benchmark::State& state)
    {
        const auto polyphony = static_cast<std::size_t> (state.range (0));

        int noteIdx = 0;
        Engine<float, kMaxVoices, Config> engine (polyphony, [&noteIdx] ()
        {
            const float freq = 110.f * std::pow (2.f, static_cast<float> (noteIdx++ % 24) / 12.f);
            return makeVoice (freq);
        });
        engine.onNoteOn = [] (uint8_t note, uint8_t, uint8_t, std::size_t) { (void) note; };

        engine.prepare (kChans, kBlock, kSampleRate);

        for (std::size_t i = 0; i < polyphony; ++i)
        {
            engine.pushNoteOn (0, static_cast<uint8_t> (36 + i), 100);
        }
        engine.process();

        uint8_t ccVal = 0;
        for (auto _ : state)
        {
            engine.pushCC (0, 74, ccVal);
            engine.pushPitchBend (0, static_cast<int16_t> ((ccVal - 64) * 128));
            ccVal = static_cast<uint8_t> ((ccVal + 1) & 0x7Fu);

            engine.process();
            benchmark::DoNotOptimize (engine.getOutputBuffer());
        }
        state.SetItemsProcessed (state.iterations());
    }

    struct SimpleConfig : DefaultSynthConfig
    {
    };

    struct SampleAccurateConfig : DefaultSynthConfig
    {
        static constexpr bool SampleAccurate = true;
    };

    void BM_Engine_Process_Simple (benchmark::State& state)
    {
        runEngineBenchmark<SimpleConfig> (state);
    }
    BENCHMARK (BM_Engine_Process_Simple)
        ->Arg (1)->Arg (2)->Arg (4)->Arg (8)->Arg (16)->Arg (32)->Arg (64);

    void BM_Engine_Process_SampleAccurate (benchmark::State& state)
    {
        runEngineBenchmark<SampleAccurateConfig> (state);
    }
    BENCHMARK (BM_Engine_Process_SampleAccurate)
        ->Arg (1)->Arg (2)->Arg (4)->Arg (8)->Arg (16)->Arg (32)->Arg (64);

} // namespace
