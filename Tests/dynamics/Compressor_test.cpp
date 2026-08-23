/*
 * @file Compressor_test.cpp
 *
 * Unit tests for:
 *   CASPI::Dynamics::DynamicsBase<Derived, FloatType>
 *   CASPI::Dynamics::Compressor<FloatType>
 *
 * Section 1: Gain computer (defaultGainReductionDb) — state-box, no graph.
 * Section 2: Parameter API get/set round trips.
 * Section 3: Detector ballistics through a live AudioGraph (attack/release).
 * Section 4: Sidechain routing through a live AudioGraph.
 */

#include "core/caspi_Graph.h"
#include "dynamics/caspi_Compressor.h"
#include <gtest/gtest.h>

using namespace CASPI;
using namespace CASPI::Graph;

namespace
{
    template <typename FloatType>
    class ConstantNode : public AudioNode<ConstantNode<FloatType>, FloatType>
    {
        public:
            FloatType fillValue = FloatType (0);

            explicit ConstantNode (FloatType value = FloatType (0))
                : AudioNode<ConstantNode<FloatType>, FloatType> (0, 1)
                , fillValue (value)
            {
            }

            void processImpl (AudioContext<FloatType>&) noexcept
            {
                this->outputBuffer.fill (fillValue);
            }
    };

    constexpr std::size_t kCh     = 1;
    constexpr std::size_t kFrames = 64;
    constexpr double kRate        = 48000.0;
}

// =================================================================================================
// Section 1: Gain computer
// =================================================================================================

TEST (CompressorGainComputer, BelowKneeGivesNoReduction)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setRatio (4.0);
    comp.setKnee (6.0);

    EXPECT_DOUBLE_EQ (comp.computeGainReductionDb (-40.0), 0.0);
    EXPECT_DOUBLE_EQ (comp.computeGainReductionDb (-21.0), 0.0); // exactly at threshold - halfKnee
}

TEST (CompressorGainComputer, AboveKneeMatchesRatioFormula)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setRatio (4.0);
    comp.setKnee (6.0);

    // overshoot = 12, halfKnee = 3 -> above the knee, straight-line ratio.
    const double reduction = comp.computeGainReductionDb (-6.0);
    EXPECT_NEAR (reduction, 12.0 * (1.0 - 1.0 / 4.0), 1e-9);
}

TEST (CompressorGainComputer, RatioOfOneGivesNoReductionRegardless)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setRatio (1.0);
    comp.setKnee (0.0);

    EXPECT_NEAR (comp.computeGainReductionDb (0.0), 0.0, 1e-9);
    EXPECT_NEAR (comp.computeGainReductionDb (-6.0), 0.0, 1e-9);
}

TEST (CompressorGainComputer, HardKneeIsContinuousAtThreshold)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setRatio (4.0);
    comp.setKnee (0.0);

    EXPECT_NEAR (comp.computeGainReductionDb (-18.0), 0.0, 1e-9);
    EXPECT_GT (comp.computeGainReductionDb (-17.999), 0.0);
}

TEST (CompressorGainComputer, KneeBlendIsContinuousAtBothEdges)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setRatio (4.0);
    comp.setKnee (6.0);

    const double belowEdge = comp.computeGainReductionDb (-21.0); // threshold - halfKnee
    const double aboveEdge = comp.computeGainReductionDb (-15.0); // threshold + halfKnee
    const double straightLineAtAboveEdge = 3.0 * (1.0 - 1.0 / 4.0);

    EXPECT_NEAR (belowEdge, 0.0, 1e-9);
    EXPECT_NEAR (aboveEdge, straightLineAtAboveEdge, 1e-6);
}

TEST (CompressorGainComputer, HigherRatioReducesMore)
{
    Dynamics::Compressor<double> comp;
    comp.setThreshold (-18.0);
    comp.setKnee (0.0);

    comp.setRatio (2.0);
    const double lowRatioReduction = comp.computeGainReductionDb (0.0);

    comp.setRatio (10.0);
    const double highRatioReduction = comp.computeGainReductionDb (0.0);

    EXPECT_GT (highRatioReduction, lowRatioReduction);
}

// =================================================================================================
// Section 2: Parameter API
// =================================================================================================

TEST (CompressorParameters, SettersUpdateGetters)
{
    Dynamics::Compressor<float> comp;
    comp.setThreshold (-12.0f);
    comp.setRatio (8.0f);
    comp.setKnee (3.0f);
    comp.setAttackTime (0.02f);
    comp.setReleaseTime (0.25f);
    comp.setMakeupGain (6.0f);

    EXPECT_FLOAT_EQ (comp.getThreshold(), -12.0f);
    EXPECT_FLOAT_EQ (comp.getRatio(), 8.0f);
    EXPECT_FLOAT_EQ (comp.getKnee(), 3.0f);
    EXPECT_FLOAT_EQ (comp.getAttackTime(), 0.02f);
    EXPECT_FLOAT_EQ (comp.getReleaseTime(), 0.25f);
    EXPECT_FLOAT_EQ (comp.getMakeupGain(), 6.0f);
}

TEST (CompressorParameters, ResetZerosGainReductionReading)
{
    Dynamics::Compressor<float> comp;
    comp.reset();
    EXPECT_FLOAT_EQ (comp.getGainReductionDb(), 0.0f);
}

// =================================================================================================
// Section 3: Detector ballistics (live AudioGraph)
// =================================================================================================

struct CompressorGraphFixture : ::testing::Test
{
    AudioGraph<float> graph;
    NodeId sourceId {};
    NodeId sideChainId {};
    NodeId compId {};

    void buildGraph (float mainValue, bool withSidechain = false)
    {
        sourceId = graph.emplace<ConstantNode<float>> (mainValue).id;
        compId = graph.emplace<Dynamics::Compressor<float>>().id;

        auto& comp = *graph.getNodeAs<Dynamics::Compressor<float>> (compId);
        comp.setThreshold (-18.0f);
        comp.setRatio (4.0f);
        comp.setKnee (0.0f);
        comp.setAttackTime (0.005f);
        comp.setReleaseTime (0.05f);

        ASSERT_TRUE (graph.connect (Port (sourceId, 0), Port (compId, 0)).has_value());

        if (withSidechain)
        {
            sideChainId = graph.emplace<ConstantNode<float>> (0.0f).id;
            ASSERT_TRUE (graph.connect (Port (sideChainId, 0), Port (compId, 1)).has_value());
        }

        ASSERT_TRUE (graph.prepare (kCh, kFrames, kRate).has_value());
    }

    float getGainReductionDb()
    {
        return graph.getNodeAs<Dynamics::Compressor<float>> (compId)->getGainReductionDb();
    }
};

TEST_F (CompressorGraphFixture, LoudSignalGraduallyIncreasesGainReduction)
{
    buildGraph (1.0f); // 0 dBFS, well above -18 dB threshold

    float previous = 0.0f;
    bool sawIncrease = false;
    for (int block = 0; block < 40; ++block)
    {
        graph.process();
        const float current = getGainReductionDb();
        EXPECT_GE (current, previous - 1e-4f); // monotonic rise toward steady state
        if (current > previous + 1e-4f)
        {
            sawIncrease = true;
        }
        previous = current;
    }

    EXPECT_TRUE (sawIncrease);
    // Steady-state reduction for 0 dBFS in, threshold -18, ratio 4: 18 * (1 - 1/4) = 13.5 dB.
    EXPECT_NEAR (previous, 13.5f, 0.5f);
}

TEST_F (CompressorGraphFixture, GainReductionRecoversAfterSignalDrops)
{
    buildGraph (1.0f);

    for (int block = 0; block < 60; ++block)
    {
        graph.process();
    }
    const float reductionAtLoud = getGainReductionDb();
    EXPECT_GT (reductionAtLoud, 5.0f);

    // Drop the source to silence and let the release stage recover.
    graph.getNodeAs<ConstantNode<float>> (sourceId)->fillValue = 0.0f;

    for (int block = 0; block < 200; ++block)
    {
        graph.process();
    }

    EXPECT_LT (getGainReductionDb(), 1.0f);
}

TEST_F (CompressorGraphFixture, QuietSignalStaysUnreduced)
{
    buildGraph (0.01f); // well below -18 dB threshold

    for (int block = 0; block < 20; ++block)
    {
        graph.process();
    }

    EXPECT_NEAR (getGainReductionDb(), 0.0f, 1e-3f);
}

// =================================================================================================
// Section 4: Sidechain routing
// =================================================================================================

TEST_F (CompressorGraphFixture, SidechainDrivesDetectionInsteadOfMainInput)
{
    // Quiet main signal, loud sidechain: the detector should key off the
    // sidechain and reduce gain even though the main signal itself never
    // crosses the threshold.
    buildGraph (0.01f, /*withSidechain=*/true);
    graph.getNodeAs<ConstantNode<float>> (sideChainId)->fillValue = 1.0f;

    for (int block = 0; block < 60; ++block)
    {
        graph.process();
    }

    EXPECT_GT (getGainReductionDb(), 5.0f);

    // The output should still carry the (attenuated) quiet main signal.
    const auto* out = graph.getNode (compId)->getOutputBuffer (0);
    ASSERT_NE (out, nullptr);
    EXPECT_GT (out->sample (0, 0), 0.0f);
    EXPECT_LT (out->sample (0, 0), 0.01f);
}

// =================================================================================================
// Section 5: Standalone processSample() (no graph)
// =================================================================================================

TEST (CompressorStandalone, QuietSampleStaysUnattenuated)
{
    Dynamics::Compressor<float> comp;
    comp.setSampleRate (48000.0f);
    comp.setThreshold (-18.0f);
    comp.setRatio (4.0f);

    float out = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        out = comp.processSample (0.01f);
    }
    EXPECT_NEAR (out, 0.01f, 1e-4f);
}

TEST (CompressorStandalone, LoudSampleIsGraduallyAttenuated)
{
    Dynamics::Compressor<float> comp;
    comp.setSampleRate (48000.0f);
    comp.setThreshold (-18.0f);
    comp.setRatio (4.0f);
    comp.setAttackTime (0.005f);

    const float first = comp.processSample (1.0f);
    for (int i = 0; i < 500; ++i)
    {
        comp.processSample (1.0f);
    }
    const float settled = comp.processSample (1.0f);

    EXPECT_LT (settled, first);
    EXPECT_GT (comp.getGainReductionDb(), 5.0f);
}

TEST (CompressorStandalone, TwoArgOverloadKeysOffSidechainNotMain)
{
    Dynamics::Compressor<float> comp;
    comp.setSampleRate (48000.0f);
    comp.setThreshold (-18.0f);
    comp.setRatio (4.0f);
    comp.setAttackTime (0.005f);

    float out = 0.0f;
    for (int i = 0; i < 500; ++i)
    {
        out = comp.processSample (0.01f, 1.0f); // quiet main, loud sidechain
    }

    EXPECT_GT (comp.getGainReductionDb(), 5.0f);
    EXPECT_GT (out, 0.0f);
    EXPECT_LT (out, 0.01f);
}

TEST (CompressorStandalone, MatchesGraphPathForEquivalentInput)
{
    // Same params, same signal (mono, no sidechain): standalone processSample()
    // should track the graph path sample-for-sample.
    Dynamics::Compressor<float> standalone;
    standalone.setSampleRate (48000.0f);
    standalone.setThreshold (-18.0f);
    standalone.setRatio (4.0f);
    standalone.setKnee (0.0f);
    standalone.setAttackTime (0.005f);
    standalone.setReleaseTime (0.05f);

    float lastStandalone = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        lastStandalone = standalone.processSample (1.0f);
    }

    AudioGraph<float> graph;
    auto srcId = graph.emplace<ConstantNode<float>> (1.0f).id;
    auto compId = graph.emplace<Dynamics::Compressor<float>>().id;
    auto& comp = *graph.getNodeAs<Dynamics::Compressor<float>> (compId);
    comp.setThreshold (-18.0f);
    comp.setRatio (4.0f);
    comp.setKnee (0.0f);
    comp.setAttackTime (0.005f);
    comp.setReleaseTime (0.05f);
    ASSERT_TRUE (graph.connect (Port (srcId, 0), Port (compId, 0)).has_value());
    ASSERT_TRUE (graph.prepare (1, 1, 48000.0).has_value());

    float lastGraph = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        graph.process();
        lastGraph = graph.getNode (compId)->getOutputBuffer (0)->sample (0, 0);
    }

    EXPECT_NEAR (lastStandalone, lastGraph, 1e-5f);
}
