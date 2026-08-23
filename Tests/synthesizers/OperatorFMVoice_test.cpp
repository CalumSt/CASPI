/*
 * @file OperatorFMVoice_test.cpp
 *
 * End-to-end proof that a polyphonic FM voice can be built entirely from
 * graph-native pieces (Operator + Multiply + Envelope::ADSR, wired with
 * plain Graph::AudioGraph::connect()) and driven through Engine/VoiceManager
 * like any other CASPI voice — the concrete instrument this library's FM
 * architecture was aiming at, not just a proof that the DSP matches theory.
 *
 * VOICE TOPOLOGY (per voice, built once by the factory lambda):
 *
 *   modulator (Operator, no ports) --> carrier (Operator, 1 port, Phase mode)
 *                                           |
 *                                           v
 *                              carrier --> Multiply(2) <-- ADSR
 *                                           |
 *                                        voice output
 *
 * The ADSR is both the voice's audible amplitude envelope (via Multiply,
 * a real VCA) AND the lifecycle envelope VoiceManager polls for isIdle() —
 * one envelope serving both roles, so Engine's onNoteOff needs no manual
 * per-operator forwarding: VoiceManager's built-in dispatch already drives
 * the ADSR, and the ADSR shapes the audio directly.
 *
 * -----------------------------------------------------------------------
 * 1. SingleNoteProducesFundamentalAtNoteFrequency
 * 2. EnvelopeShapesAmplitudeAcrossAttackAndRelease
 * 3. TwoSimultaneousNotesProduceTwoDistinctPitches
 * 4. VoiceIsReusableAfterFullRelease
 * 5. MoreNotesThanVoicesDoesNotCrash
 */

#include "analysis/caspi_SpectralProfile.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Multiply.h"
#include "controls/caspi_Envelope.h"
#include "midi/caspi_Midi.h"
#include "oscillators/caspi_Operator.h"
#include "synthesizers/caspi_Engine.h"
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace CASPI;
using namespace CASPI::Graph;

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr std::size_t kChannels = 1;
    constexpr std::size_t kFrames   = 512;
    constexpr std::size_t kMaxVoices = 4;

    constexpr NodeId kModId  = 0;
    constexpr NodeId kCarId  = 1;
    constexpr NodeId kAdsrId = 2;
    constexpr NodeId kMulId  = 3;

    constexpr float kModRatio = 2.0f;  // modulator frequency = carrier frequency * ratio
    constexpr float kModDepth = 3.0f;  // classical FM modulation index (edge weight)

    constexpr float kAttack  = 0.01f;
    constexpr float kDecay   = 0.05f;
    constexpr float kSustain = 0.7f;
    constexpr float kRelease = 0.05f;

    VoiceConfig<float> makeFMVoice()
    {
        AudioGraph<float> graph;

        auto [modId, modNode] = graph.emplace<Operator<float>>();
        modNode.setModulationDepth (1.0f);

        auto [carId, carNode] = graph.emplace<Operator<float>> (1u); // 1 modulation port
        carNode.setModulationMode (ModulationMode::Phase);
        carNode.setModulationIndex (1.0f);
        carNode.setModulationPortWeight (0, kModDepth);

        auto [adsrId, adsrNode] = graph.emplace<Envelope::ADSR<float>>();
        adsrNode.setAttackTime (kAttack);
        adsrNode.setDecayTime (kDecay);
        adsrNode.setSustainLevel (kSustain);
        adsrNode.setReleaseTime (kRelease);

        auto [mulId, mulNode] = graph.emplace<Multiply<float>> (2u);

        EXPECT_EQ (modId, kModId);
        EXPECT_EQ (carId, kCarId);
        EXPECT_EQ (adsrId, kAdsrId);
        EXPECT_EQ (mulId, kMulId);

        auto connect1 = graph.connect (Port (modId), Port (carId, 0));
        auto connect2 = graph.connect (Port (carId), Port (mulId, 0));
        auto connect3 = graph.connect (Port (adsrId), Port (mulId, 1));
        EXPECT_TRUE (connect1.has_value());
        EXPECT_TRUE (connect2.has_value());
        EXPECT_TRUE (connect3.has_value());

        return VoiceConfig<float> (std::move (graph), mulId, adsrId);
    }

    using EngineT = Engine<float, kMaxVoices>;

    // Engine doesn't know which nodes are oscillators; the voice's own
    // NodeIds are deterministic (fresh AudioGraph per voice, same
    // construction order every time), so the constants above resolve them
    // without per-voice bookkeeping. Wired after construction since the
    // lambda needs to capture the already-constructed engine.
    void wireNoteOn (EngineT& engine)
    {
        engine.onNoteOn = [&engine] (uint8_t note, uint8_t /*vel*/, uint8_t /*ch*/, std::size_t vi)
        {
            auto* graph = engine.getVoiceManager().getVoiceGraph (vi);
            if (graph == nullptr) return;

            auto* car = graph->getNodeAs<Operator<float>> (kCarId);
            auto* mod = graph->getNodeAs<Operator<float>> (kModId);
            if (car == nullptr || mod == nullptr) return;

            const float carrierFreq = Midi::noteToFrequency<float> (note);
            car->setFrequency (carrierFreq);
            mod->setFrequency (carrierFreq * kModRatio);
        };
    }

    std::vector<float> renderBlocks (EngineT& engine, int numBlocks)
    {
        std::vector<float> out;
        out.reserve (static_cast<std::size_t> (numBlocks) * kFrames);
        for (int i = 0; i < numBlocks; ++i)
        {
            engine.process();
            const auto& buf = engine.getOutputBuffer();
            for (std::size_t f = 0; f < kFrames; ++f)
            {
                out.push_back (buf.sample (0, f));
            }
        }
        return out;
    }

    float rmsOf (const std::vector<float>& v, std::size_t begin, std::size_t end)
    {
        double sum = 0.0;
        for (std::size_t i = begin; i < end; ++i)
        {
            sum += static_cast<double> (v[i]) * v[i];
        }
        return static_cast<float> (std::sqrt (sum / static_cast<double> (end - begin)));
    }

    struct EngineFixture : ::testing::Test
    {
        EngineT engine { kMaxVoices, &makeFMVoice };

        void SetUp() override
        {
            wireNoteOn (engine);
            engine.prepare (kChannels, kFrames, kSampleRate);
        }
    };
} // namespace

TEST_F (EngineFixture, SingleNoteProducesFundamentalAtNoteFrequency)
{
    constexpr uint8_t note = 69; // A4 = 440 Hz
    const float expectedFreq = Midi::noteToFrequency<float> (note);

    ASSERT_TRUE (engine.pushNoteOn (0, note, 100));

    // 32 blocks x 512 frames @ 44100 Hz -> ~2.7 Hz/bin resolution.
    const auto samples = renderBlocks (engine, 32);
    std::vector<double> d (samples.begin(), samples.end());
    SpectralProfile profile (d, kSampleRate);

    EXPECT_TRUE (profile.hasPeakAt (expectedFreq, 5.0));
}

TEST_F (EngineFixture, EnvelopeShapesAmplitudeAcrossAttackAndRelease)
{
    constexpr uint8_t note = 60;
    ASSERT_TRUE (engine.pushNoteOn (0, note, 100));

    const auto attackSamples = renderBlocks (engine, 1);
    const float earlyRms = rmsOf (attackSamples, 0, 50); // right at attack start
    const float lateRms  = rmsOf (attackSamples, attackSamples.size() - 50, attackSamples.size());

    EXPECT_LT (earlyRms, lateRms) << "Amplitude should rise during the attack phase";

    // Let it reach sustain, then release.
    renderBlocks (engine, 20);
    ASSERT_TRUE (engine.pushNoteOff (0, note));

    // Release time is 0.05s; render well past it (44100 * 0.3s worth of blocks).
    const auto releaseSamples = renderBlocks (engine, 30);
    const float tailRms = rmsOf (releaseSamples, releaseSamples.size() - 50, releaseSamples.size());

    EXPECT_LT (tailRms, 0.01f) << "Output should have decayed to silence after release";
}

TEST_F (EngineFixture, TwoSimultaneousNotesProduceTwoDistinctPitches)
{
    const uint8_t noteA = 60; // C4
    const uint8_t noteB = 72; // C5
    const float freqA = Midi::noteToFrequency<float> (noteA);
    const float freqB = Midi::noteToFrequency<float> (noteB);

    ASSERT_TRUE (engine.pushNoteOn (0, noteA, 100));
    ASSERT_TRUE (engine.pushNoteOn (0, noteB, 100));

    const auto samples = renderBlocks (engine, 32);
    std::vector<double> d (samples.begin(), samples.end());
    SpectralProfile profile (d, kSampleRate);

    EXPECT_TRUE (profile.hasPeakAt (freqA, 5.0));
    EXPECT_TRUE (profile.hasPeakAt (freqB, 5.0));
}

TEST_F (EngineFixture, VoiceIsReusableAfterFullRelease)
{
    constexpr uint8_t noteA = 60;
    constexpr uint8_t noteB = 67;

    ASSERT_TRUE (engine.pushNoteOn (0, noteA, 100));
    renderBlocks (engine, 5);
    ASSERT_TRUE (engine.pushNoteOff (0, noteA));

    // Render well past attack+decay+release so the voice's ADSR goes idle
    // and VoiceManager frees it, rather than relying on voice stealing.
    renderBlocks (engine, 30);

    ASSERT_TRUE (engine.pushNoteOn (0, noteB, 100));
    const auto samples = renderBlocks (engine, 32);
    std::vector<double> d (samples.begin(), samples.end());
    SpectralProfile profile (d, kSampleRate);

    EXPECT_TRUE (profile.hasPeakAt (Midi::noteToFrequency<float> (noteB), 5.0));
}

TEST_F (EngineFixture, MoreNotesThanVoicesDoesNotCrash)
{
    for (uint8_t note = 60; note < 60 + kMaxVoices + 2; ++note)
    {
        ASSERT_TRUE (engine.pushNoteOn (0, note, 100));
    }

    const auto samples = renderBlocks (engine, 8);
    for (float s : samples)
    {
        EXPECT_FALSE (std::isnan (s));
        EXPECT_TRUE (std::isfinite (s));
        // Engine applies no master limiting; up to kMaxVoices unweighted voices
        // can sum constructively, so bound generously rather than at unity.
        EXPECT_LE (std::abs (s), static_cast<float> (kMaxVoices) + 1.0f);
    }
}
