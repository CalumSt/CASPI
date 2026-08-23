/*
 * @file Mixer_test.cpp
 *
 * Unit tests for CASPI::Mixer<FloatType> — the generic N-input audio summing
 * node used to combine several graph-native FM carriers (or any other audio
 * nodes) into one output, replacing FMGraphDSP's internal output-operator
 * mixing when a voice is built as a plain Graph::AudioGraph.
 */

#include "core/caspi_Mixer.h"
#include "core/caspi_Graph.h"
#include <gtest/gtest.h>

using namespace CASPI;
using namespace CASPI::Graph;

namespace
{
    template <typename F>
    class ConstantNode : public AudioNode<ConstantNode<F>, F>
    {
        public:
            explicit ConstantNode (F v) : value (v) {}

            void processImpl (AudioContext<F>&) noexcept
            {
                auto& buf = this->outputBuffer;
                for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
                    for (std::size_t fr = 0; fr < buf.numFrames(); ++fr)
                        buf.sample (ch, fr) = value;
            }

        private:
            F value;
    };

    constexpr std::size_t kChannels = 1;
    constexpr std::size_t kFrames   = 16;
    constexpr double kRate          = 44100.0;
}

TEST (MixerTest, SumsTwoConstantInputs)
{
    AudioGraph<float> graph;

    auto [aId, a] = graph.emplace<ConstantNode<float>> (0.3f);
    auto [bId, b] = graph.emplace<ConstantNode<float>> (0.5f);
    auto [mixId, mix] = graph.emplace<Mixer<float>> (2);
    mix.setAutoScale (false);

    ASSERT_TRUE (graph.connect (Port (aId), Port (mixId, 0)).has_value());
    ASSERT_TRUE (graph.connect (Port (bId), Port (mixId, 1)).has_value());

    ASSERT_TRUE (graph.prepare (kChannels, kFrames, kRate).has_value());
    graph.process();

    const auto* out = graph.getNode (mixId)->getOutputBuffer (0);
    ASSERT_NE (out, nullptr);
    for (std::size_t fr = 0; fr < kFrames; ++fr)
        EXPECT_NEAR (out->sample (0, fr), 0.8f, 1e-6f);
}

TEST (MixerTest, AutoScaleDividesByDeclaredPortCount)
{
    AudioGraph<float> graph;

    auto [aId, a] = graph.emplace<ConstantNode<float>> (1.0f);
    auto [bId, b] = graph.emplace<ConstantNode<float>> (1.0f);
    auto [mixId, mix] = graph.emplace<Mixer<float>> (2);
    // autoScale defaults to true.

    ASSERT_TRUE (graph.connect (Port (aId), Port (mixId, 0)).has_value());
    ASSERT_TRUE (graph.connect (Port (bId), Port (mixId, 1)).has_value());

    ASSERT_TRUE (graph.prepare (kChannels, kFrames, kRate).has_value());
    graph.process();

    const auto* out = graph.getNode (mixId)->getOutputBuffer (0);
    ASSERT_NE (out, nullptr);
    for (std::size_t fr = 0; fr < kFrames; ++fr)
        EXPECT_NEAR (out->sample (0, fr), 1.0f, 1e-6f); // (1+1)/2
}

TEST (MixerTest, UnconnectedPortContributesSilenceNotError)
{
    AudioGraph<float> graph;

    auto [aId, a] = graph.emplace<ConstantNode<float>> (1.0f);
    auto [mixId, mix] = graph.emplace<Mixer<float>> (2); // port 1 left unconnected
    mix.setAutoScale (false);

    ASSERT_TRUE (graph.connect (Port (aId), Port (mixId, 0)).has_value());

    ASSERT_TRUE (graph.prepare (kChannels, kFrames, kRate).has_value());
    graph.process();

    const auto* out = graph.getNode (mixId)->getOutputBuffer (0);
    ASSERT_NE (out, nullptr);
    for (std::size_t fr = 0; fr < kFrames; ++fr)
        EXPECT_NEAR (out->sample (0, fr), 1.0f, 1e-6f);
}

TEST (MixerTest, OutputGainAppliedAfterSum)
{
    AudioGraph<float> graph;

    auto [aId, a] = graph.emplace<ConstantNode<float>> (1.0f);
    auto [mixId, mix] = graph.emplace<Mixer<float>> (1);
    mix.setAutoScale (false);
    mix.setOutputGain (0.25f);

    ASSERT_TRUE (graph.connect (Port (aId), Port (mixId, 0)).has_value());

    ASSERT_TRUE (graph.prepare (kChannels, kFrames, kRate).has_value());
    graph.process();

    const auto* out = graph.getNode (mixId)->getOutputBuffer (0);
    ASSERT_NE (out, nullptr);
    for (std::size_t fr = 0; fr < kFrames; ++fr)
        EXPECT_NEAR (out->sample (0, fr), 0.25f, 1e-6f);
}

TEST (MixerTest, GetterReflectSetters)
{
    Mixer<float> mix (3);
    mix.setAutoScale (false);
    mix.setOutputGain (0.5f);

    EXPECT_FALSE (mix.getAutoScale());
    EXPECT_FLOAT_EQ (mix.getOutputGain(), 0.5f);
}
