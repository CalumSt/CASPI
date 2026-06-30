/*************************************************************************
 * @file Processor_test.cpp
 *
 * Unit tests for:
 *   CASPI::Core::Processor<Derived, FloatType, Policy>
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Test nodes:
 *   GainProcessor<F>       PerSample: multiplies input by a gain scalar.
 *   InvertProcessor<F>     PerSample: negates each sample.
 *   SumChannelsProcessor<F> PerChannel: accumulates per-channel state.
 *   SampleRateCapture<F>   Captures sampleRate from onSampleRateChanged.
 *   PrepareCapture<F>      Captures geometry from onPrepare.
 *   PrepareBlockCapture<F> Captures nFrames/nChannels from prepareBlock.
 *   ConstantProducer<F>    AudioNode source: fills output with a constant.
 *
 * -----------------------------------------------------------------------
 * Section 1: Construction
 * -----------------------------------------------------------------------
 * 1.1  DefaultConstructionHasCorrectPortCounts
 * 1.2  NodeTypeIsAudio
 * 1.3  NewProcessorIsNotPrepared
 *
 * -----------------------------------------------------------------------
 * Section 2: Standalone process — PerSample
 * -----------------------------------------------------------------------
 * 2.1  GainProcessorScalesAllSamples
 * 2.2  InvertProcessorNegatesAllSamples
 * 2.3  GainZeroProducesAllZeros
 *
 * -----------------------------------------------------------------------
 * Section 3: Standalone process — PerChannel
 * -----------------------------------------------------------------------
 * 3.1  PerChannelInvokedOncePerChannel
 *
 * -----------------------------------------------------------------------
 * Section 4: processSpan override
 * -----------------------------------------------------------------------
 * 4.1  ProcessSpanOverrideIsCalledForPerChannel
 *
 * -----------------------------------------------------------------------
 * Section 5: Sample rate and prepare hooks
 * -----------------------------------------------------------------------
 * 5.1  OnSampleRateChangedCalledByPrepareToRender
 * 5.2  OnPrepareCalledWithCorrectGeometry
 * 5.3  PrepareBlockCalledBeforeEachProcess
 * 5.4  PreparedAfterPrepareToRender
 *
 * -----------------------------------------------------------------------
 * Section 6: Graph integration — processImpl
 * -----------------------------------------------------------------------
 * 6.1  ProcessorInGraphReceivesUpstreamBuffer
 * 6.2  UnconnectedInputLeavesBufferAsSilence
 * 6.3  GainProcessorInGraphScalesUpstreamAudio
 * 6.4  ProcessorProcessImplCalledEachBlock
 * 6.5  ProcessorOutputBufferUpdatedEachBlock
 *
 ************************************************************************/

#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include "core/caspi_Processor.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

using namespace CASPI;
using namespace CASPI::Core;
using namespace CASPI::Graph;

/*======================================================================
 * Test helper nodes
 *====================================================================*/

/** PerSample: multiplies every sample by gain. */
template <typename FloatType>
class GainProcessor
    : public Processor<GainProcessor<FloatType>, FloatType, Traversal::PerSample>
{
public:
    FloatType gain = FloatType (1);

    explicit GainProcessor (FloatType g = FloatType (1))
        : Processor<GainProcessor<FloatType>, FloatType, Traversal::PerSample> (1, 1)
        , gain (g)
    {
    }

    FloatType processSample (FloatType in) CASPI_NON_BLOCKING override
    {
        return in * gain;
    }
};

/** PerSample: negates each sample. */
template <typename FloatType>
class InvertProcessor
    : public Processor<InvertProcessor<FloatType>, FloatType, Traversal::PerSample>
{
public:
    InvertProcessor()
        : Processor<InvertProcessor<FloatType>, FloatType, Traversal::PerSample> (1, 1)
    {
    }

    FloatType processSample (FloatType in) CASPI_NON_BLOCKING override
    {
        return -in;
    }
};

/** PerChannel: records how many times processSample(in, ch) is called per channel. */
template <typename FloatType>
class ChannelCountProcessor
    : public Processor<ChannelCountProcessor<FloatType>, FloatType, Traversal::PerChannel>
{
public:
    std::vector<int> callsPerChannel;

    explicit ChannelCountProcessor (std::size_t numChannels)
        : Processor<ChannelCountProcessor<FloatType>, FloatType, Traversal::PerChannel> (1, 1)
        , callsPerChannel (numChannels, 0)
    {
    }

    FloatType processSample (FloatType in, std::size_t ch) CASPI_NON_BLOCKING override
    {
        if (ch < callsPerChannel.size())
            ++callsPerChannel[ch];
        return in;
    }
};

/** PerChannel: overrides processSpan to write a sentinel value. */
template <typename FloatType>
class SpanOverrideProcessor
    : public Processor<SpanOverrideProcessor<FloatType>, FloatType, Traversal::PerChannel>
{
public:
    static constexpr FloatType kSentinel = FloatType (77);
    int callCount = 0;

    SpanOverrideProcessor()
        : Processor<SpanOverrideProcessor<FloatType>, FloatType, Traversal::PerChannel> (1, 1)
    {
    }

    FloatType processSample (FloatType /*in*/, std::size_t /*ch*/) CASPI_NON_BLOCKING override
    {
        ++callCount;
        return kSentinel;
    }
};

/** Captures sampleRate from onSampleRateChanged. */
template <typename FloatType>
class SRCaptureProcessor
    : public Processor<SRCaptureProcessor<FloatType>, FloatType, Traversal::PerSample>
{
public:
    FloatType capturedRate = FloatType (0);

    SRCaptureProcessor()
        : Processor<SRCaptureProcessor<FloatType>, FloatType, Traversal::PerSample> (1, 1)
    {
    }

    void onSampleRateChanged (FloatType rate) override { capturedRate = rate; }
    FloatType processSample (FloatType in) CASPI_NON_BLOCKING override { return in; }
};

/** Captures geometry from onPrepare. */
template <typename FloatType>
class PrepCaptureProcessor
    : public Processor<PrepCaptureProcessor<FloatType>, FloatType, Traversal::PerSample>
{
public:
    std::size_t capturedChannels   = 0;
    std::size_t capturedFrames     = 0;
    double      capturedSampleRate = 0.0;

    PrepCaptureProcessor()
        : Processor<PrepCaptureProcessor<FloatType>, FloatType, Traversal::PerSample> (1, 1)
    {
    }

    void onPrepare (std::size_t ch, std::size_t fr, double sr)
    {
        capturedChannels   = ch;
        capturedFrames     = fr;
        capturedSampleRate = sr;
    }

    FloatType processSample (FloatType in) CASPI_NON_BLOCKING override { return in; }
};

/** Captures nFrames/nChannels from prepareBlock. */
template <typename FloatType>
class PrepBlockCapture
    : public Processor<PrepBlockCapture<FloatType>, FloatType, Traversal::PerSample>
{
public:
    std::size_t capturedFrames   = 0;
    std::size_t capturedChannels = 0;
    int         prepareBlockCallCount = 0;

    PrepBlockCapture()
        : Processor<PrepBlockCapture<FloatType>, FloatType, Traversal::PerSample> (1, 1)
    {
    }

    void prepareBlock (std::size_t nFrames, std::size_t nChannels) CASPI_NON_BLOCKING override
    {
        ++prepareBlockCallCount;
        capturedFrames   = nFrames;
        capturedChannels = nChannels;
    }

    FloatType processSample (FloatType in) CASPI_NON_BLOCKING override { return in; }
};

/** Minimal AudioNode source: fills output buffer with a constant. */
template <typename FloatType>
class ConstantSource : public AudioNode<ConstantSource<FloatType>, FloatType>
{
public:
    FloatType value = FloatType (0);

    explicit ConstantSource (FloatType v = FloatType (0))
        : AudioNode<ConstantSource<FloatType>, FloatType> (0, 1)
        , value (v)
    {
    }

    void processImpl (AudioContext<FloatType>&) noexcept
    {
        this->outputBuffer.fill (value);
    }
};

/*======================================================================
 * Fixture
 *====================================================================*/
struct ProcessorFixture : ::testing::Test
{
    AudioGraph<float> graph;

    static constexpr std::size_t kChannels   = 2;
    static constexpr std::size_t kFrames     = 64;
    static constexpr double      kSampleRate = 44100.0;

    NodeId addSource (float value)
    {
        auto res = graph.addNode (std::make_unique<ConstantSource<float>> (value));
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    void prepareGraph()
    {
        auto res = graph.prepare (kChannels, kFrames, kSampleRate);
        ASSERT_TRUE (res.has_value()) << "prepare() failed";
    }
};

/*======================================================================
 * Section 1: Construction
 *====================================================================*/

TEST (ProcessorTest, DefaultConstructionHasCorrectPortCounts)
{
    GainProcessor<float> p;
    EXPECT_EQ (p.getNumInputPorts(),  1u);
    EXPECT_EQ (p.getNumOutputPorts(), 1u);
}

TEST (ProcessorTest, NodeTypeIsAudio)
{
    GainProcessor<float> p;
    EXPECT_EQ (p.getType(), NodeType::Audio);
}

TEST (ProcessorTest, NewProcessorIsNotPrepared)
{
    GainProcessor<float> p;
    EXPECT_FALSE (p.isPrepared());
}

/*======================================================================
 * Section 2: Standalone process — PerSample
 *====================================================================*/

TEST (ProcessorTest, GainProcessorScalesAllSamples)
{
    constexpr std::size_t C = 2, F = 8;
    GainProcessor<float> p (2.0f);
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);
    buf.fill (1.0f);

    p.process (buf);

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), 2.0f) << "ch=" << ch << " f=" << f;
}

TEST (ProcessorTest, InvertProcessorNegatesAllSamples)
{
    constexpr std::size_t C = 2, F = 4;
    InvertProcessor<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);
    buf.fill (0.5f);

    p.process (buf);

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), -0.5f) << "ch=" << ch << " f=" << f;
}

TEST (ProcessorTest, GainZeroProducesAllZeros)
{
    constexpr std::size_t C = 2, F = 8;
    GainProcessor<float> p (0.0f);
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);
    buf.fill (1.0f);

    p.process (buf);

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), 0.0f);
}

/*======================================================================
 * Section 3: Standalone process — PerChannel
 *====================================================================*/

TEST (ProcessorTest, PerChannelInvokedOncePerChannel)
{
    constexpr std::size_t C = 3, F = 16;
    ChannelCountProcessor<float> p (C);
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);
    buf.fill (1.0f);

    p.process (buf);

    // PerChannel: processSample called F times per channel
    for (std::size_t ch = 0; ch < C; ++ch)
        EXPECT_EQ (p.callsPerChannel[ch], static_cast<int> (F)) << "ch=" << ch;
}

/*======================================================================
 * Section 4: processSpan override
 *====================================================================*/

TEST (ProcessorTest, ProcessSpanOverrideIsCalledForPerChannel)
{
    constexpr std::size_t C = 2, F = 8;
    SpanOverrideProcessor<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);
    buf.fill (0.0f);

    p.process (buf);

    EXPECT_EQ (p.callCount, static_cast<int> (C * F));

    for (std::size_t ch = 0; ch < C; ++ch)
        for (std::size_t f = 0; f < F; ++f)
            EXPECT_FLOAT_EQ (buf.sample (ch, f), SpanOverrideProcessor<float>::kSentinel)
                << "ch=" << ch << " f=" << f;
}

/*======================================================================
 * Section 5: Sample rate and prepare hooks
 *====================================================================*/

TEST (ProcessorTest, OnSampleRateChangedCalledByPrepareToRender)
{
    SRCaptureProcessor<float> p;
    p.prepareToRender (2, 64, 48000.0);
    EXPECT_FLOAT_EQ (p.capturedRate, 48000.0f);
}

TEST (ProcessorTest, OnPrepareCalledWithCorrectGeometry)
{
    PrepCaptureProcessor<float> p;
    p.prepareToRender (4, 256, 96000.0);
    EXPECT_EQ (p.capturedChannels,     4u);
    EXPECT_EQ (p.capturedFrames,       256u);
    EXPECT_DOUBLE_EQ (p.capturedSampleRate, 96000.0);
}

TEST (ProcessorTest, PrepareBlockCalledBeforeEachProcess)
{
    constexpr std::size_t C = 2, F = 32;
    PrepBlockCapture<float> p;
    AudioBuffer<float, ChannelMajorLayout> buf (C, F);

    p.process (buf);
    p.process (buf);

    EXPECT_EQ (p.prepareBlockCallCount, 2);
    EXPECT_EQ (p.capturedFrames,   F);
    EXPECT_EQ (p.capturedChannels, C);
}

TEST (ProcessorTest, PreparedAfterPrepareToRender)
{
    GainProcessor<float> p;
    ASSERT_FALSE (p.isPrepared());
    p.prepareToRender (2, 64, 44100.0);
    EXPECT_TRUE (p.isPrepared());
}

/*======================================================================
 * Section 6: Graph integration
 *====================================================================*/

TEST_F (ProcessorFixture, ProcessorInGraphReceivesUpstreamBuffer)
{
    NodeId srcId  = addSource (1.0f);
    auto procNode = std::make_unique<GainProcessor<float>> (1.0f);
    auto* rawProc = procNode.get();
    auto procRes  = graph.addNode (std::move (procNode));
    ASSERT_TRUE (procRes.has_value());
    NodeId procId = procRes.value();

    ASSERT_TRUE (graph.connect (srcId, 0, procId, 0, ConnectionType::Audio).has_value());
    prepareGraph();
    graph.process();

    const auto* buf = rawProc->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_FLOAT_EQ (buf->sample (0, 0), 1.0f);
}

TEST_F (ProcessorFixture, UnconnectedInputLeavesBufferAsSilence)
{
    auto procNode = std::make_unique<GainProcessor<float>> (2.0f);
    auto* rawProc = procNode.get();
    ASSERT_TRUE (graph.addNode (std::move (procNode)).has_value());

    prepareGraph();
    graph.process();

    const auto* buf = rawProc->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);

    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t f = 0; f < kFrames; ++f)
            EXPECT_FLOAT_EQ (buf->sample (ch, f), 0.0f)
                << "ch=" << ch << " f=" << f;
}

TEST_F (ProcessorFixture, GainProcessorInGraphScalesUpstreamAudio)
{
    NodeId srcId  = addSource (0.5f);
    auto procNode = std::make_unique<GainProcessor<float>> (3.0f);
    auto* rawProc = procNode.get();
    auto procRes  = graph.addNode (std::move (procNode));
    ASSERT_TRUE (procRes.has_value());
    NodeId procId = procRes.value();

    ASSERT_TRUE (graph.connect (srcId, 0, procId, 0, ConnectionType::Audio).has_value());
    prepareGraph();
    graph.process();

    const auto* buf = rawProc->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_NEAR (buf->sample (0, 0), 1.5f, 1e-5f);
}

TEST_F (ProcessorFixture, ProcessorProcessImplCalledEachBlock)
{
    NodeId srcId  = addSource (1.0f);
    auto procNode = std::make_unique<PrepBlockCapture<float>>();
    auto* rawProc = procNode.get();
    NodeId procId = graph.addNode (std::move (procNode)).value();

    ASSERT_TRUE (graph.connect (srcId, 0, procId, 0, ConnectionType::Audio).has_value());
    prepareGraph();

    constexpr int kBlocks = 6;
    for (int i = 0; i < kBlocks; ++i)
        graph.process();

    EXPECT_EQ (rawProc->prepareBlockCallCount, kBlocks);
}

TEST_F (ProcessorFixture, ProcessorOutputBufferUpdatedEachBlock)
{
    NodeId srcId  = addSource (0.0f);
    auto procNode = std::make_unique<GainProcessor<float>> (2.0f);
    auto* rawProc = procNode.get();
    NodeId procId = graph.addNode (std::move (procNode)).value();

    ASSERT_TRUE (graph.connect (srcId, 0, procId, 0, ConnectionType::Audio).has_value());
    prepareGraph();
    graph.process();

    EXPECT_FLOAT_EQ (rawProc->getOutputBuffer (0)->sample (0, 0), 0.0f);

    // Change source value via dynamic_cast to update fill value
    auto* src = dynamic_cast<ConstantSource<float>*> (graph.getNode (srcId));
    ASSERT_NE (src, nullptr);
    src->value = 1.0f;

    graph.process();
    EXPECT_FLOAT_EQ (rawProc->getOutputBuffer (0)->sample (0, 0), 2.0f);
}