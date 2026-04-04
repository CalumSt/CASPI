/*************************************************************************
 * @file Producer_test.cpp
 *
 * Unit tests for:
 *   CASPI::Core::Producer<Derived, FloatType, Policy>
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Test nodes:
 *   SineProducer<F>       PerFrame: produces sin(phase), advances phase.
 *   CountingProducer<F>   PerSample: returns ++counter for each (ch, fr) call.
 *   ChannelProducer<F>    PerChannel: returns channel index as FloatType.
 *   SampleRateCapture<F>  PerSample: captures sampleRate from onSampleRateChanged.
 *   PrepareCapture<F>     PerSample: captures numChannels/numFrames from onPrepare.
 *   SpanOverrideProducer<F> PerFrame: overrides renderSpan to write known pattern.
 *
 * -----------------------------------------------------------------------
 * Section 1: Construction
 * -----------------------------------------------------------------------
 * 1.1  DefaultConstructionHasCorrectPortCounts
 * 1.2  NodeTypeIsAudio
 * 1.3  NewProducerIsNotPrepared
 *
 * -----------------------------------------------------------------------
 * Section 2: Standalone render — PerSample
 * -----------------------------------------------------------------------
 * 2.1  PerSampleCallsRenderSampleForEveryChannelAndFrame
 * 2.2  PerSampleChannelAndFrameIndicesCorrect
 *
 * -----------------------------------------------------------------------
 * Section 3: Standalone render — PerFrame
 * -----------------------------------------------------------------------
 * 3.1  PerFrameCallsRenderSampleOncePerFrame
 * 3.2  PerFrameBroadcastsToAllChannels
 *
 * -----------------------------------------------------------------------
 * Section 4: Standalone render — PerChannel
 * -----------------------------------------------------------------------
 * 4.1  PerChannelCallsRenderSampleOncePerChannel
 * 4.2  PerChannelBroadcastsToAllFrames
 *
 * -----------------------------------------------------------------------
 * Section 5: renderSpan override
 * -----------------------------------------------------------------------
 * 5.1  RenderSpanOverrideIsCalledForPerFrame
 *
 * -----------------------------------------------------------------------
 * Section 6: Sample rate and prepare hooks
 * -----------------------------------------------------------------------
 * 6.1  OnSampleRateChangedCalledByPrepareToRender
 * 6.2  OnPrepareCalledWithCorrectGeometry
 * 6.3  PreparedAfterPrepareToRender
 *
 * -----------------------------------------------------------------------
 * Section 7: Graph integration
 * -----------------------------------------------------------------------
 * 7.1  ProducerInGraphFillsOutputBuffer
 * 7.2  ProducerProcessImplCalledEachBlock
 * 7.3  DownstreamNodeReceivesProducerOutput
 *
 ************************************************************************/

#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include "core/caspi_Producer.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

using namespace CASPI;
using namespace CASPI::Core;
using namespace CASPI::Graph;

/*======================================================================
 * Test helper nodes
 *====================================================================*/

/** PerSample: records every (ch, fr) pair passed to renderSample(ch, fr). */
template <typename FloatType>
class CountingProducer
    : public Producer<CountingProducer<FloatType>, FloatType, Traversal::PerSample>
{
public:
    struct Call { std::size_t ch; std::size_t fr; };

    std::vector<Call> calls;
    FloatType returnValue = FloatType (1);

    explicit CountingProducer (std::size_t numOut = 1)
        : Producer<CountingProducer<FloatType>, FloatType, Traversal::PerSample> (0, numOut)
    {
    }

    FloatType renderSample (std::size_t ch, std::size_t fr) CASPI_NON_BLOCKING override
    {
        calls.push_back ({ ch, fr });
        return returnValue;
    }
};

/** PerFrame: returns sin(phase) and advances phase each frame. */
template <typename FloatType>
class SineProducer
    : public Producer<SineProducer<FloatType>, FloatType, Traversal::PerFrame>
{
public:
    FloatType phase     = FloatType (0);
    FloatType increment = FloatType (0.1);
    int renderCallCount = 0;

    SineProducer()
        : Producer<SineProducer<FloatType>, FloatType, Traversal::PerFrame> (0, 1)
    {
    }

    FloatType renderSample() CASPI_NON_BLOCKING override
    {
        ++renderCallCount;
        FloatType s = static_cast<FloatType> (std::sin (static_cast<double> (phase)));
        phase += increment;
        return s;
    }
};

/** PerChannel: returns channel index cast to FloatType. */
template <typename FloatType>
class ChannelProducer
    : public Producer<ChannelProducer<FloatType>, FloatType, Traversal::PerChannel>
{
public:
    ChannelProducer()
        : Producer<ChannelProducer<FloatType>, FloatType, Traversal::PerChannel> (0, 1)
    {
    }

    FloatType renderSample (std::size_t ch) CASPI_NON_BLOCKING override
    {
        return static_cast<FloatType> (ch);
    }
};

/** Captures sampleRate from onSampleRateChanged. */
template <typename FloatType>
class SampleRateCapture
    : public Producer<SampleRateCapture<FloatType>, FloatType, Traversal::PerSample>
{
public:
    FloatType capturedRate = FloatType (0);

    SampleRateCapture()
        : Producer<SampleRateCapture<FloatType>, FloatType, Traversal::PerSample> (0, 1)
    {
    }

    void onSampleRateChanged (FloatType rate) override
    {
        capturedRate = rate;
    }

    FloatType renderSample() CASPI_NON_BLOCKING override { return FloatType (0); }
};

/** Captures geometry from onPrepare. */
template <typename FloatType>
class PrepareCapture
    : public Producer<PrepareCapture<FloatType>, FloatType, Traversal::PerSample>
{
public:
    std::size_t capturedChannels  = 0;
    std::size_t capturedFrames    = 0;
    double      capturedSampleRate = 0.0;

    PrepareCapture()
        : Producer<PrepareCapture<FloatType>, FloatType, Traversal::PerSample> (0, 1)
    {
    }

    void onPrepare (std::size_t ch, std::size_t fr, double sr)
    {
        capturedChannels   = ch;
        capturedFrames     = fr;
        capturedSampleRate = sr;
    }

    FloatType renderSample() CASPI_NON_BLOCKING override { return FloatType (0); }
};

/** PerFrame: overrides renderSpan to write a known sentinel into the span. */
template <typename FloatType>
class SpanOverrideProducer
    : public Producer<SpanOverrideProducer<FloatType>, FloatType, Traversal::PerFrame>
{
public:
    static constexpr FloatType kSentinel = FloatType (99);
    int renderCallCount = 0;

    SpanOverrideProducer()
        : Producer<SpanOverrideProducer<FloatType>, FloatType, Traversal::PerFrame> (0, 1)
    {
    }

    FloatType renderSample() CASPI_NON_BLOCKING
    {
        ++renderCallCount;
        return kSentinel; // Should not be called when renderSpan is overridden.
    }
};

/** Minimal AudioNode sink: captures the upstream output buffer pointer. */
template <typename FloatType>
class CaptureNode : public AudioNode<CaptureNode<FloatType>, FloatType>
{
public:
    const AudioBuffer<FloatType, ChannelMajorLayout>* capturedInput = nullptr;
    int callCount = 0;

    explicit CaptureNode()
        : AudioNode<CaptureNode<FloatType>, FloatType> (1, 0)
    {
    }

    void processImpl (AudioContext<FloatType>& ctx) noexcept
    {
        ++callCount;
        capturedInput = ctx.getAudioInput (this->getId(), 0);
    }
};

/*======================================================================
 * Fixture
 *====================================================================*/
struct ProducerFixture : ::testing::Test
{
    AudioGraph<float> graph;

    static constexpr std::size_t kChannels   = 2;
    static constexpr std::size_t kFrames     = 64;
    static constexpr double      kSampleRate = 44100.0;

    void prepareGraph()
    {
        auto res = graph.prepare (kChannels, kFrames, kSampleRate);
        ASSERT_TRUE (res.has_value()) << "prepare() failed";
    }
};

/*======================================================================
 * Section 1: Construction
 *====================================================================*/

TEST (ProducerTest, DefaultConstructionHasCorrectPortCounts)
{
    SineProducer<float> p;
    EXPECT_EQ (p.getNumInputPorts(),  0u);
    EXPECT_EQ (p.getNumOutputPorts(), 1u);
}

TEST (ProducerTest, NodeTypeIsAudio)
{
    SineProducer<float> p;
    EXPECT_EQ (p.getType(), NodeType::Audio);
}

TEST (ProducerTest, NewProducerIsNotPrepared)
{
    SineProducer<float> p;
    EXPECT_FALSE (p.isPrepared());
}

/*======================================================================
 * Section 2: Standalone render — PerSample
 *====================================================================*/

TEST (ProducerTest, PerSampleCallsRenderSampleForEveryChannelAndFrame)
{
    constexpr std::size_t C = 2, F = 8;
    CountingProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    EXPECT_EQ (p.calls.size(), C * F);
}

TEST (ProducerTest, PerSampleChannelAndFrameIndicesCorrect)
{
    constexpr std::size_t C = 2, F = 4;
    CountingProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    // render() iterates: for f in frames, for ch in channels
    std::size_t idx = 0;
    for (std::size_t f = 0; f < F; ++f)
    {
        for (std::size_t ch = 0; ch < C; ++ch, ++idx)
        {
            EXPECT_EQ (p.calls[idx].fr, f)  << "idx=" << idx;
            EXPECT_EQ (p.calls[idx].ch, ch) << "idx=" << idx;
        }
    }
}

/*======================================================================
 * Section 3: Standalone render — PerFrame
 *====================================================================*/

TEST (ProducerTest, PerFrameCallsRenderSampleOncePerFrame)
{
    constexpr std::size_t C = 3, F = 16;
    SineProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    EXPECT_EQ (p.renderCallCount, static_cast<int> (F));
}

TEST (ProducerTest, PerFrameBroadcastsToAllChannels)
{
    constexpr std::size_t C = 3, F = 8;
    SineProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    for (std::size_t f = 0; f < F; ++f)
    {
        float ref = buf.sample (0, f);
        for (std::size_t ch = 1; ch < C; ++ch)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), ref)
                << "frame=" << f << " ch=" << ch;
    }
}

/*======================================================================
 * Section 4: Standalone render — PerChannel
 *====================================================================*/

TEST (ProducerTest, PerChannelCallsRenderSampleOncePerChannel)
{
    // ChannelProducer returns channel index; verify each channel's frames
    // are all equal to that channel index.
    constexpr std::size_t C = 4, F = 16;
    ChannelProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), static_cast<float> (ch))
                << "ch=" << ch << " f=" << f;
}

TEST (ProducerTest, PerChannelBroadcastsToAllFrames)
{
    constexpr std::size_t C = 2, F = 32;
    ChannelProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    for (std::size_t ch = 0; ch < C; ++ch)
    {
        float ref = buf.sample (ch, 0);
        for (std::size_t f = 1; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), ref)
                << "ch=" << ch << " f=" << f;
    }
}

/*======================================================================
 * Section 5: renderSpan override
 *====================================================================*/

TEST (ProducerTest, RenderSpanOverrideIsCalledForPerFrame)
{
    constexpr std::size_t C = 2, F = 8;
    SpanOverrideProducer<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.render (buf);

    EXPECT_EQ (p.renderCallCount, static_cast<int> (F));

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), SpanOverrideProducer<float>::kSentinel)
                << "ch=" << ch << " f=" << f;
}

/*======================================================================
 * Section 6: Sample rate and prepare hooks
 *====================================================================*/

TEST (ProducerTest, OnSampleRateChangedCalledByPrepareToRender)
{
    SampleRateCapture<float> p;
    p.prepareToRender (2, 64, 48000.0);
    EXPECT_FLOAT_EQ (p.capturedRate, 48000.0f);
}

TEST (ProducerTest, OnPrepareCalledWithCorrectGeometry)
{
    PrepareCapture<float> p;
    p.prepareToRender (3, 128, 96000.0);
    EXPECT_EQ (p.capturedChannels,   3u);
    EXPECT_EQ (p.capturedFrames,     128u);
    EXPECT_DOUBLE_EQ (p.capturedSampleRate, 96000.0);
}

TEST (ProducerTest, PreparedAfterPrepareToRender)
{
    SineProducer<float> p;
    ASSERT_FALSE (p.isPrepared());
    p.prepareToRender (2, 64, 44100.0);
    EXPECT_TRUE (p.isPrepared());
}

/*======================================================================
 * Section 7: Graph integration
 *====================================================================*/

TEST_F (ProducerFixture, ProducerInGraphFillsOutputBuffer)
{
    auto node = std::make_unique<CountingProducer<float>>();
    node->returnValue = 0.75f;
    auto res = graph.addNode (std::move (node));
    ASSERT_TRUE (res.has_value());
    NodeId id = res.value();

    prepareGraph();
    graph.process();

    const auto* buf = graph.getNode (id)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);

    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t f = 0; f < kFrames; ++f)
            EXPECT_FLOAT_EQ (buf->sample (ch, f), 0.75f)
                << "ch=" << ch << " f=" << f;
}

TEST_F (ProducerFixture, ProducerProcessImplCalledEachBlock)
{
    auto node = std::make_unique<SineProducer<float>>();
    auto* raw = node.get();
    ASSERT_TRUE (graph.addNode (std::move (node)).has_value());

    prepareGraph();

    constexpr int kBlocks = 5;
    for (int i = 0; i < kBlocks; ++i)
        graph.process();

    // PerFrame: kBlocks * kFrames renderSample() calls
    EXPECT_EQ (raw->renderCallCount, static_cast<int> (kBlocks * kFrames));
}

TEST_F (ProducerFixture, DownstreamNodeReceivesProducerOutput)
{
    auto prod = std::make_unique<CountingProducer<float>>();
    prod->returnValue = 0.5f;
    auto prodRes = graph.addNode (std::move (prod));
    ASSERT_TRUE (prodRes.has_value());
    NodeId prodId = prodRes.value();

    auto sink    = std::make_unique<CaptureNode<float>>();
    auto* rawSink = sink.get();
    auto sinkRes = graph.addNode (std::move (sink));
    ASSERT_TRUE (sinkRes.has_value());
    NodeId sinkId = sinkRes.value();

    ASSERT_TRUE (graph.connect (prodId, 0, sinkId, 0, ConnectionType::Audio).has_value());

    prepareGraph();
    graph.process();

    ASSERT_EQ (rawSink->callCount, 1);
    ASSERT_NE (rawSink->capturedInput, nullptr);
    EXPECT_FLOAT_EQ (rawSink->capturedInput->sample (0, 0), 0.5f);
}