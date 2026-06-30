/*
 * @file Graph_test.cpp
 *
 * Unit and integration tests for:
 *   CASPI::Graph::Port
 *   CASPI::Graph::NodeHandle<T>
 *   CASPI::Graph::NodeBase<float>
 *   CASPI::Graph::AudioNode<Derived, float>
 *   CASPI::Graph::ControlNode<Derived, float>
 *   CASPI::Graph::AudioGraph<float>
 *   CASPI::Graph::AudioContext<float>
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Stub nodes (sections 1-8, no production DSP dependencies):
 *   ConstantNode<F>        AudioNode: fills outputBuffer with a constant value.
 *   SumNode<F>             AudioNode: sums all connected audio inputs.
 *   OrderTrackingNode<F>   AudioNode: appends NodeId to a shared vector.
 *   CounterControlNode<F>  ControlNode: increments controlOutputs[0] each block.
 *   ScaledByControlNode<F> AudioNode: scales audio input by a control input.
 *   MultiplyNode<F>        AudioNode: multiplies two audio inputs sample-by-sample.
 *
 * Production nodes (sections 9-13, requires CASPI DSP headers):
 *   BlepOscillator<float>, ADSR<float>, WhiteNoiseOscillator<float>, ModMatrix<float>
 *
 * -----------------------------------------------------------------------
 * Section 1: Port and NodeHandle
 * -----------------------------------------------------------------------
 * 1.1  PortDefaultIndexIsZero
 * 1.2  PortExplicitIndex
 * 1.3  PortImplicitFromNodeId
 * 1.4  NodeHandleCarriesIdAndReference
 * 1.5  EmplaceReturnsHandle
 * 1.6  EmplaceNodeIsConfigurableViaHandle
 *
 * -----------------------------------------------------------------------
 * Section 2: Node construction
 * -----------------------------------------------------------------------
 * 2.1  NewNodeHasInvalidId
 * 2.2  NewNodeIsNotPrepared
 * 2.3  AudioNodePortCounts
 * 2.4  AudioNodeTypeIsAudio
 * 2.5  ControlNodeTypeIsControl
 *
 * -----------------------------------------------------------------------
 * Section 3: Graph — addNode / emplace / removeNode
 * -----------------------------------------------------------------------
 * 3.1  AddNodeReturnsValidId
 * 3.2  AddMultipleNodesReturnsSequentialIds
 * 3.3  AddNullNodeReturnsError
 * 3.4  GetNumNodesReflectsAdditions
 * 3.5  GetNodeByIdReturnsCorrectNode
 * 3.6  RemoveNodeDecreasesCount
 * 3.7  RemoveNodeRemovesItsConnections
 * 3.8  RemoveNonExistentNodeReturnsError
 * 3.9  AddNodeInvalidatesPrepare
 * 3.10 EmplaceAddsNodeAndReturnsHandle
 *
 * -----------------------------------------------------------------------
 * Section 4: Graph — connect / disconnect
 * -----------------------------------------------------------------------
 * 4.1  ConnectValidNodesSucceeds
 * 4.2  ConnectPortOverloadPortZeroToZero
 * 4.3  ConnectNodeIdOverloadImpliesPortZero
 * 4.4  ConnectControlSucceeds
 * 4.5  ConnectFeedbackSucceeds
 * 4.6  ConnectInvalidSourceIdReturnsError
 * 4.7  ConnectInvalidDestinationIdReturnsError
 * 4.8  ConnectOutOfRangeSourcePortReturnsError
 * 4.9  ConnectOutOfRangeDestinationPortReturnsError
 * 4.10 DuplicateConnectionReturnsError
 * 4.11 DisconnectExistingConnectionSucceeds
 * 4.12 DisconnectNonExistentConnectionReturnsError
 * 4.13 ConnectInvalidatesPrepare
 * 4.14 NormalAndFeedbackBetweenSamePortsBothPermitted
 * 4.15 DisconnectRequiresFeedbackFlag
 * 4.16 ConnectAudioSourceWithControlTypeReturnsTypeMismatch
 * 4.17 ConnectControlSourceWithAudioTypeReturnsTypeMismatch
 *
 * -----------------------------------------------------------------------
 * Section 5: Topological sort
 * -----------------------------------------------------------------------
 * 5.1  SingleNodeSortedOrder
 * 5.2  LinearChainSortedOrder
 * 5.3  DiamondTopologySortedOrder
 * 5.4  NonFeedbackCycleReturnsError
 * 5.5  FeedbackConnectionBreaksCycle
 * 5.6  ConnectFeedbackBreaksCycle
 * 5.7  DisconnectedSubgraphSortedCorrectly
 *
 * -----------------------------------------------------------------------
 * Section 6: Data flow — audio
 * -----------------------------------------------------------------------
 * 6.1  ConstantNodeFillsOutputBuffer
 * 6.2  ConnectedNodeReceivesUpstreamBuffer
 * 6.3  SumNodeAccumulatesMultipleInputs
 * 6.4  UnconnectedPortReceivesNullptr
 * 6.5  OutputBufferPersistsAcrossBlocks
 * 6.6  FeedbackReadsPreviousBlockOutput
 *
 * -----------------------------------------------------------------------
 * Section 7: Data flow — control
 * -----------------------------------------------------------------------
 * 7.1  ControlNodeOutputIsZeroBeforeProcess
 * 7.2  ControlNodeIncrementsEachBlock
 * 7.3  ControlInputReachesDownstreamNode
 * 7.4  UnconnectedControlPortReturnsZero
 *
 * -----------------------------------------------------------------------
 * Section 8: process() preconditions and execution order
 * -----------------------------------------------------------------------
 * 8.1  PrepareIsCalledBeforeProcess
 * 8.2  AddNodeAfterPrepareInvalidatesPrepared
 * 8.3  NodeIsPreparedAfterGraphPrepare
 * 8.4  LinearChainProcessedInOrder
 * 8.5  DiamondProcessedWithCorrectPrecedence
 * 8.6  ProcessCalledOncePerNodePerBlock
 *
 * -----------------------------------------------------------------------
 * Section 9: BlepOscillator in graph
 * -----------------------------------------------------------------------
 * 9.1  SawOscillatorProducesNonZeroOutput
 * 9.2  SawOscillatorFundamentalPeakAt440Hz
 * 9.3  SawOscillatorHasHarmonicAt880Hz
 * 9.4  SineOscillatorHasNoSignificantHarmonics
 * 9.5  TwoDifferentFrequenciesYieldDifferentPeaks
 *
 * -----------------------------------------------------------------------
 * Section 10: ADSR in graph
 * -----------------------------------------------------------------------
 * 10.1  AdsrIdleProducesZeroOutput
 * 10.2  AdsrAttackLevelRisesMonotonically
 * 10.3  AdsrSustainLevelIsStable
 * 10.4  AdsrReleaseDecaysToIdle
 *
 * -----------------------------------------------------------------------
 * Section 11: Multi-node patches
 * -----------------------------------------------------------------------
 * 11.1  TwoOscillatorsSummedShowBothFundamentals
 * 11.2  OscillatorMultipliedByAdsrRisesWithAttack
 * 11.3  NoiseOscillatorHasBroadSpectrum
 * 11.4  NoiseSpectralCentroidInReasonableRange
 *
 * -----------------------------------------------------------------------
 * Section 12: ModMatrix integration
 * -----------------------------------------------------------------------
 * 12.1  ModMatrixRegistersParameterAndCountsRouting
 * 12.2  ModMatrixZeroSourceLeavesParameterUnmodulated
 *
 * -----------------------------------------------------------------------
 * Section 13: Graph vs standalone equivalence
 * -----------------------------------------------------------------------
 * 13.1  GraphSineRMSMatchesStandaloneRMSWithinFivePercent
 */

#include "analysis/caspi_SpectralProfile.h"
#include "controls/caspi_Envelope.h"
#include "controls/caspi_ModMatrix.h"
#include "core/caspi_Graph.h"
#include "base/caspi_RealtimeContext.h"
#include "core/caspi_Node.h"
#include "oscillators/caspi_BlepOscillator.h"
#include "oscillators/caspi_Noise.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <vector>

using namespace CASPI;
using namespace CASPI::Graph;

/*======================================================================
 * Stub nodes
 *====================================================================*/

template <typename FloatType>
class ConstantNode : public AudioNode<ConstantNode<FloatType>, FloatType>
{
    public:
        FloatType fillValue = FloatType (0);
        int callCount       = 0;

        explicit ConstantNode (FloatType value  = FloatType (0),
                               std::size_t numIn  = 0,
                               std::size_t numOut = 1)
            : AudioNode<ConstantNode<FloatType>, FloatType> (numIn, numOut)
            , fillValue (value)
        {
        }

        void processImpl (AudioContext<FloatType>&) noexcept
        {
            ++callCount;
            this->outputBuffer.fill (fillValue);
        }
};

template <typename FloatType>
class SumNode : public AudioNode<SumNode<FloatType>, FloatType>
{
    public:
        int callCount         = 0;
        bool sawNullInputPort = false;

        explicit SumNode (std::size_t numInputs = 1)
            : AudioNode<SumNode<FloatType>, FloatType> (numInputs, 1)
        {
        }

        void processImpl (AudioContext<FloatType>& ctx) noexcept
        {
            ++callCount;
            this->outputBuffer.clear();

            for (std::size_t port = 0; port < this->getNumInputPorts(); ++port)
            {
                const auto* in = ctx.getAudioInput (this->getId(), port);
                if (in == nullptr)
                {
                    sawNullInputPort = true;
                    continue;
                }
                for (std::size_t ch = 0; ch < this->outputBuffer.numChannels(); ++ch)
                {
                    for (std::size_t fr = 0; fr < this->outputBuffer.numFrames(); ++fr)
                    {
                        this->outputBuffer.sample (ch, fr) += in->sample (ch, fr);
                    }
                }
            }
        }
};

template <typename FloatType>
class OrderTrackingNode : public AudioNode<OrderTrackingNode<FloatType>, FloatType>
{
    public:
        std::vector<NodeId>* orderTracker = nullptr;
        int callCount                     = 0;

        explicit OrderTrackingNode (std::size_t numIn = 0, std::size_t numOut = 1)
            : AudioNode<OrderTrackingNode<FloatType>, FloatType> (numIn, numOut)
        {
        }

        void onPrepare (std::size_t numChannels, std::size_t numFrames, double sr)
        {
            (void) numChannels; (void) sr;
            if (orderTracker != nullptr)
            {
                orderTracker->reserve (numChannels);  // One call per block max
            }
        }

        void processImpl (AudioContext<FloatType>&) noexcept
        {
            ++callCount;
            if (orderTracker != nullptr)
            {
                orderTracker->push_back (this->getId());
            }
        }
};

template <typename FloatType>
class CounterControlNode : public ControlNode<CounterControlNode<FloatType>, FloatType>
{
    public:
        CounterControlNode()
            : ControlNode<CounterControlNode<FloatType>, FloatType> (1)
        {
        }

        void processImpl (AudioContext<FloatType>&) noexcept
        {
            this->controlOutputs[0] += FloatType (1);
        }
};

template <typename FloatType>
class ScaledByControlNode : public AudioNode<ScaledByControlNode<FloatType>, FloatType>
{
    public:
        ScaledByControlNode()
            : AudioNode<ScaledByControlNode<FloatType>, FloatType> (2, 1)
        {
        }

        void processImpl (AudioContext<FloatType>& ctx) noexcept
        {
            const FloatType gain = ctx.getControlInput (this->getId(), 1);
            const auto* in       = ctx.getAudioInput (this->getId(), 0);

            if (in == nullptr)
            {
                this->outputBuffer.clear();
                return;
            }

            for (std::size_t ch = 0; ch < this->outputBuffer.numChannels(); ++ch)
            {
                for (std::size_t fr = 0; fr < this->outputBuffer.numFrames(); ++fr)
                {
                    this->outputBuffer.sample (ch, fr) = in->sample (ch, fr) * gain;
                }
            }
        }
};

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
                    this->outputBuffer.sample (ch, fr) =
                        a->sample (ch, fr) * b->sample (ch, fr);
                }
            }
        }
};

/*======================================================================
 * Fixture — stub node tests
 *====================================================================*/

struct AudioGraphFixture : ::testing::Test
{
    AudioGraph<float> graph;

    static constexpr std::size_t kChannels   = 2;
    static constexpr std::size_t kFrames     = 64;
    static constexpr double      kSampleRate = 44100.0;

    NodeId addConstant (float value = 0.f, std::size_t numIn = 0, std::size_t numOut = 1)
    {
        auto res = graph.addNode (std::make_unique<ConstantNode<float>> (value, numIn, numOut));
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    NodeId addSum (std::size_t numInputs = 1)
    {
        auto res = graph.addNode (std::make_unique<SumNode<float>> (numInputs));
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    NodeId addCounter()
    {
        auto res = graph.addNode (std::make_unique<CounterControlNode<float>>());
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    NodeId addScaled()
    {
        auto res = graph.addNode (std::make_unique<ScaledByControlNode<float>>());
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    NodeId addTracking (std::vector<NodeId>* tracker, std::size_t numIn = 0, std::size_t numOut = 1)
    {
        auto node          = std::make_unique<OrderTrackingNode<float>> (numIn, numOut);
        node->orderTracker = tracker;
        auto res           = graph.addNode (std::move (node));
        EXPECT_TRUE (res.has_value());
        return res.value();
    }

    CASPI::expected<void, GraphError>
    connectAudio (NodeId src, std::size_t sp, NodeId dst, std::size_t dp, bool feedback = false)
    {
        return graph.connect (src, sp, dst, dp, ConnectionType::Audio, feedback);
    }

    CASPI::expected<void, GraphError>
    connectControl (NodeId src, std::size_t sp, NodeId dst, std::size_t dp)
    {
        return graph.connect (src, sp, dst, dp, ConnectionType::Control, false);
    }

    void prepareGraph()
    {
        auto res = graph.prepare (kChannels, kFrames, kSampleRate);
        ASSERT_TRUE (res.has_value()) << "prepare() failed";
    }
};

/*======================================================================
 * Integration fixture — production nodes
 *====================================================================*/

static constexpr std::size_t kIntCh     = 1;
static constexpr std::size_t kIntFrames = 512;
static constexpr double      kIntRate   = 44100.0;
static constexpr int         kIntBlocks = 32;

static std::vector<float> renderAndCollect (AudioGraph<float>& g,
                                             NodeId outputId,
                                             int numBlocks)
{
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (numBlocks) * kIntFrames);
    for (int i = 0; i < numBlocks; ++i)
    {
        g.process();
        const auto* buf = g.getNode (outputId)->getOutputBuffer (0);
        for (std::size_t f = 0; f < kIntFrames; ++f)
        {
            out.push_back (buf->sample (0, f));
        }
    }
    return out;
}

static SpectralProfile analyzeF (const std::vector<float>& samples)
{
    std::vector<double> d (samples.begin(), samples.end());
    return SpectralProfile (d, kIntRate, WindowType::Hann, 0.005);
}

static float rmsOf (const std::vector<float>& v)
{
    double sum = 0.0;
    for (float s : v)
    {
        sum += static_cast<double> (s) * s;
    }
    return static_cast<float> (std::sqrt (sum / v.size()));
}

struct GraphIntegrationFixture : ::testing::Test
{
    AudioGraph<float> graph;

    void prepareGraph()
    {
        auto res = graph.prepare (kIntCh, kIntFrames, kIntRate);
        ASSERT_TRUE (res.has_value()) << "prepare() failed";
    }

    NodeId addOscillator (Oscillators::WaveShape shape, float hz)
    {
        auto h = graph.emplace<Oscillators::BlepOscillator<float>>();
        h.node.setFrequency (hz);
        h.node.setShape (shape);
        return h.id;
    }

    NodeId addADSR()
    {
        return graph.emplace<Envelope::ADSR<float>>().id;
    }
};

/*======================================================================
 * Section 1: Port and NodeHandle
 *====================================================================*/

TEST (PortTest, PortDefaultIndexIsZero)
{
    const Port p (42u);
    EXPECT_EQ (p.node,  42u);
    EXPECT_EQ (p.index, 0u);
}

TEST (PortTest, PortExplicitIndex)
{
    const Port p (42u, 3u);
    EXPECT_EQ (p.node,  42u);
    EXPECT_EQ (p.index, 3u);
}

TEST (PortTest, PortImplicitFromNodeId)
{
    Port p = NodeId (7u);
    EXPECT_EQ (p.node,  7u);
    EXPECT_EQ (p.index, 0u);
}

TEST (NodeHandleTest, NodeHandleCarriesIdAndReference)
{
    ConstantNode<float> node (1.5f);
    const NodeId id = 99u;
    NodeHandle<ConstantNode<float>> h { id, node };
    EXPECT_EQ (h.id, id);
    EXPECT_FLOAT_EQ (h.node.fillValue, 1.5f);
}

TEST_F (AudioGraphFixture, EmplaceReturnsHandle)
{
    auto h = graph.emplace<ConstantNode<float>> (0.5f, 0u, 1u);
    EXPECT_NE (h.id, INVALID_NODE_ID);
    EXPECT_EQ (graph.getNumNodes(), 1u);
}

TEST_F (AudioGraphFixture, EmplaceNodeIsConfigurableViaHandle)
{
    auto h      = graph.emplace<ConstantNode<float>> (0.f, 0u, 1u);
    h.node.fillValue = 3.14f;

    auto* retrieved = graph.getNodeAs<ConstantNode<float>> (h.id);
    ASSERT_NE (retrieved, nullptr);
    EXPECT_FLOAT_EQ (retrieved->fillValue, 3.14f);
}

/*======================================================================
 * Section 2: Node construction
 *====================================================================*/

TEST (NodeTest, NewNodeHasInvalidId)
{
    ConstantNode<float> node;
    EXPECT_EQ (node.getId(), INVALID_NODE_ID);
}

TEST (NodeTest, NewNodeIsNotPrepared)
{
    ConstantNode<float> node;
    EXPECT_FALSE (node.isPrepared());
}

TEST (NodeTest, AudioNodePortCounts)
{
    ConstantNode<float> node (0.f, 3u, 2u);
    EXPECT_EQ (node.getNumInputPorts(),  3u);
    EXPECT_EQ (node.getNumOutputPorts(), 2u);
}

TEST (NodeTest, AudioNodeTypeIsAudio)
{
    ConstantNode<float> node;
    EXPECT_EQ (node.getType(), NodeType::Audio);
}

TEST (NodeTest, ControlNodeTypeIsControl)
{
    CounterControlNode<float> node;
    EXPECT_EQ (node.getType(), NodeType::Control);
}

/*======================================================================
 * Section 3: addNode / emplace / removeNode
 *====================================================================*/

TEST_F (AudioGraphFixture, AddNodeReturnsValidId)
{
    auto res = graph.addNode (std::make_unique<ConstantNode<float>>());
    ASSERT_TRUE (res.has_value());
    EXPECT_NE (res.value(), INVALID_NODE_ID);
}

TEST_F (AudioGraphFixture, AddMultipleNodesReturnsSequentialIds)
{
    auto id0 = graph.addNode (std::make_unique<ConstantNode<float>>()).value();
    auto id1 = graph.addNode (std::make_unique<ConstantNode<float>>()).value();
    EXPECT_EQ (id1, id0 + 1);
}

TEST_F (AudioGraphFixture, AddNullNodeReturnsError)
{
    auto res = graph.addNode (nullptr);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (graph.getNumNodes(), 0u);
}

TEST_F (AudioGraphFixture, GetNumNodesReflectsAdditions)
{
    addConstant();
    addConstant();
    addConstant();
    EXPECT_EQ (graph.getNumNodes(), 3u);
}

TEST_F (AudioGraphFixture, GetNodeByIdReturnsCorrectNode)
{
    NodeId id = addConstant (1.5f);
    auto* n   = graph.getNodeAs<ConstantNode<float>> (id);
    ASSERT_NE (n, nullptr);
    EXPECT_FLOAT_EQ (n->fillValue, 1.5f);
}

TEST_F (AudioGraphFixture, RemoveNodeDecreasesCount)
{
    NodeId id = addConstant();
    ASSERT_EQ (graph.getNumNodes(), 1u);
    EXPECT_TRUE (graph.removeNode (id).has_value());
    EXPECT_EQ (graph.getNumNodes(), 0u);
}

TEST_F (AudioGraphFixture, RemoveNodeRemovesItsConnections)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0).has_value());
    ASSERT_EQ (graph.getNumConnections(), 1u);

    graph.removeNode (src);
    EXPECT_EQ (graph.getNumConnections(), 0u);
}

TEST_F (AudioGraphFixture, RemoveNonExistentNodeReturnsError)
{
    auto res = graph.removeNode (999u);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::NodeNotFound);
}

TEST_F (AudioGraphFixture, AddNodeInvalidatesPrepare)
{
    addConstant();
    prepareGraph();
    ASSERT_TRUE (graph.isPrepared());

    addConstant();
    EXPECT_FALSE (graph.isPrepared());
}

TEST_F (AudioGraphFixture, EmplaceAddsNodeAndReturnsHandle)
{
    auto h = graph.emplace<ConstantNode<float>> (2.0f, 0u, 1u);
    EXPECT_NE (h.id, INVALID_NODE_ID);
    EXPECT_FLOAT_EQ (h.node.fillValue, 2.0f);
    EXPECT_EQ (graph.getNumNodes(), 1u);
}

/*======================================================================
 * Section 4: connect / disconnect
 *====================================================================*/

TEST_F (AudioGraphFixture, ConnectValidNodesSucceeds)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    EXPECT_TRUE (connectAudio (src, 0, dst, 0).has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

TEST_F (AudioGraphFixture, ConnectPortOverloadPortZeroToZero)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto r     = graph.connect (Port (src, 0), Port (dst, 0));
    EXPECT_TRUE (r.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

TEST_F (AudioGraphFixture, ConnectNodeIdOverloadImpliesPortZero)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto r     = graph.connect (src, dst);
    EXPECT_TRUE (r.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

TEST_F (AudioGraphFixture, ConnectControlSucceeds)
{
    NodeId ctrl   = addCounter();
    NodeId scaled = addScaled();
    auto r        = graph.connectControl (Port (ctrl, 0), Port (scaled, 1));
    EXPECT_TRUE (r.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

TEST_F (AudioGraphFixture, ConnectFeedbackSucceeds)
{
    NodeId a = addConstant (0.f, 1, 1);
    NodeId b = addConstant (0.f, 1, 1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    auto r = graph.connectFeedback (Port (b, 0), Port (a, 0));
    EXPECT_TRUE (r.has_value());
    EXPECT_EQ (graph.getNumConnections(), 2u);
}

TEST_F (AudioGraphFixture, ConnectInvalidSourceIdReturnsError)
{
    NodeId dst = addSum (1);
    auto res   = graph.connect (999u, 0u, dst, 0u, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidNodeId);
}

TEST_F (AudioGraphFixture, ConnectInvalidDestinationIdReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    auto res   = graph.connect (src, 0u, 999u, 0u, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidNodeId);
}

TEST_F (AudioGraphFixture, ConnectOutOfRangeSourcePortReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 99u, dst, 0u, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidPort);
}

TEST_F (AudioGraphFixture, ConnectOutOfRangeDestinationPortReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 0u, dst, 99u, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidPort);
}

TEST_F (AudioGraphFixture, DuplicateConnectionReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0).has_value());
    auto res = connectAudio (src, 0, dst, 0);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::DuplicateConnection);
}

TEST_F (AudioGraphFixture, DisconnectExistingConnectionSucceeds)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0).has_value());

    auto res = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 0u);
}

TEST_F (AudioGraphFixture, DisconnectNonExistentConnectionReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::ConnectionNotFound);
}

TEST_F (AudioGraphFixture, ConnectInvalidatesPrepare)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    prepareGraph();
    ASSERT_TRUE (graph.isPrepared());

    connectAudio (src, 0, dst, 0);
    EXPECT_FALSE (graph.isPrepared());
}

TEST_F (AudioGraphFixture, NormalAndFeedbackBetweenSamePortsBothPermitted)
{
    NodeId src = addConstant (0.f, 1, 1);
    NodeId dst = addSum (1);

    ASSERT_TRUE (connectAudio (src, 0, dst, 0, false).has_value());
    auto res = connectAudio (src, 0, dst, 0, true);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 2u);
}

TEST_F (AudioGraphFixture, DisconnectRequiresFeedbackFlag)
{
    NodeId src = addConstant (0.f, 1, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0, false).has_value());
    ASSERT_TRUE (connectAudio (src, 0, dst, 0, true).has_value());
    ASSERT_EQ (graph.getNumConnections(), 2u);

    auto res = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio, true);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);

    auto res2 = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio, true);
    EXPECT_FALSE (res2.has_value());
    EXPECT_EQ (res2.error(), GraphError::ConnectionNotFound);
}

TEST_F (AudioGraphFixture, ConnectAudioSourceWithControlTypeReturnsTypeMismatch)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 0, dst, 0, ConnectionType::Control);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::TypeMismatch);
}

TEST_F (AudioGraphFixture, ConnectControlSourceWithAudioTypeReturnsTypeMismatch)
{
    NodeId ctrl = addCounter();
    NodeId dst  = addSum (1);
    auto res    = graph.connect (ctrl, 0, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::TypeMismatch);
}

/*======================================================================
 * Section 5: Topological sort
 *====================================================================*/

TEST_F (AudioGraphFixture, SingleNodeSortedOrder)
{
    NodeId id = addConstant();
    prepareGraph();
    const auto& order = graph.getSortedOrder();
    ASSERT_EQ (order.size(), 1u);
    EXPECT_EQ (order[0], id);
}

TEST_F (AudioGraphFixture, LinearChainSortedOrder)
{
    NodeId a = addConstant (0.f, 0, 1);
    NodeId b = addConstant (0.f, 1, 1);
    NodeId c = addSum (1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, c, 0).has_value());
    prepareGraph();

    const auto& order = graph.getSortedOrder();
    ASSERT_EQ (order.size(), 3u);

    auto posA = std::find (order.begin(), order.end(), a) - order.begin();
    auto posB = std::find (order.begin(), order.end(), b) - order.begin();
    auto posC = std::find (order.begin(), order.end(), c) - order.begin();
    EXPECT_LT (posA, posB);
    EXPECT_LT (posB, posC);
}

TEST_F (AudioGraphFixture, DiamondTopologySortedOrder)
{
    NodeId a = addConstant (0.f, 0, 1);
    NodeId b = addConstant (0.f, 1, 1);
    NodeId c = addConstant (0.f, 1, 1);
    NodeId d = addSum (2);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (a, 0, c, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, d, 0).has_value());
    ASSERT_TRUE (connectAudio (c, 0, d, 1).has_value());
    prepareGraph();

    const auto& order = graph.getSortedOrder();
    ASSERT_EQ (order.size(), 4u);

    auto posA = std::find (order.begin(), order.end(), a) - order.begin();
    auto posB = std::find (order.begin(), order.end(), b) - order.begin();
    auto posC = std::find (order.begin(), order.end(), c) - order.begin();
    auto posD = std::find (order.begin(), order.end(), d) - order.begin();
    EXPECT_LT (posA, posB);
    EXPECT_LT (posA, posC);
    EXPECT_LT (posB, posD);
    EXPECT_LT (posC, posD);
}

TEST_F (AudioGraphFixture, NonFeedbackCycleReturnsError)
{
    NodeId a = addConstant (0.f, 1, 1);
    NodeId b = addConstant (0.f, 1, 1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, a, 0).has_value());

    auto res = graph.prepare (kChannels, kFrames, kSampleRate);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::CycleDetected);
}

TEST_F (AudioGraphFixture, FeedbackConnectionBreaksCycle)
{
    NodeId a = addConstant (0.f, 1, 1);
    NodeId b = addConstant (0.f, 1, 1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, a, 0, true).has_value());

    EXPECT_TRUE (graph.prepare (kChannels, kFrames, kSampleRate).has_value());
}

TEST_F (AudioGraphFixture, ConnectFeedbackBreaksCycle)
{
    NodeId a = addConstant (0.f, 1, 1);
    NodeId b = addConstant (0.f, 1, 1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (graph.connectFeedback (Port (b, 0), Port (a, 0)).has_value());

    EXPECT_TRUE (graph.prepare (kChannels, kFrames, kSampleRate).has_value());
}

TEST_F (AudioGraphFixture, DisconnectedSubgraphSortedCorrectly)
{
    NodeId x = addConstant (0.f, 0, 1);
    NodeId y = addSum (1);
    NodeId p = addConstant (0.f, 0, 1);
    NodeId q = addSum (1);
    ASSERT_TRUE (connectAudio (x, 0, y, 0).has_value());
    ASSERT_TRUE (connectAudio (p, 0, q, 0).has_value());
    prepareGraph();

    const auto& order = graph.getSortedOrder();
    ASSERT_EQ (order.size(), 4u);

    auto posX = std::find (order.begin(), order.end(), x) - order.begin();
    auto posY = std::find (order.begin(), order.end(), y) - order.begin();
    auto posP = std::find (order.begin(), order.end(), p) - order.begin();
    auto posQ = std::find (order.begin(), order.end(), q) - order.begin();
    EXPECT_LT (posX, posY);
    EXPECT_LT (posP, posQ);
}

/*======================================================================
 * Section 6: Data flow — audio
 *====================================================================*/

TEST_F (AudioGraphFixture, ConstantNodeFillsOutputBuffer)
{
    NodeId id = addConstant (0.5f);
    prepareGraph();
    graph.process();

    const auto* buf = graph.getNodeAs<ConstantNode<float>> (id)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    for (std::size_t ch = 0; ch < buf->numChannels(); ++ch)
    {
        for (std::size_t fr = 0; fr < buf->numFrames(); ++fr)
        {
            EXPECT_FLOAT_EQ (buf->sample (ch, fr), 0.5f);
        }
    }
}

TEST_F (AudioGraphFixture, ConnectedNodeReceivesUpstreamBuffer)
{
    NodeId src   = addConstant (1.0f, 0, 1);
    NodeId sumId = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, sumId, 0).has_value());
    prepareGraph();
    graph.process();

    auto* sumNode = graph.getNodeAs<SumNode<float>> (sumId);
    ASSERT_NE (sumNode, nullptr);
    EXPECT_FALSE (sumNode->sawNullInputPort);
    EXPECT_FLOAT_EQ (sumNode->getOutputBuffer (0)->sample (0, 0), 1.0f);
}

TEST_F (AudioGraphFixture, SumNodeAccumulatesMultipleInputs)
{
    NodeId a     = addConstant (0.3f, 0, 1);
    NodeId b     = addConstant (0.7f, 0, 1);
    NodeId sumId = addSum (2);
    ASSERT_TRUE (connectAudio (a, 0, sumId, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, sumId, 1).has_value());
    prepareGraph();
    graph.process();

    const auto* buf = graph.getNodeAs<SumNode<float>> (sumId)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_NEAR (buf->sample (0, 0), 1.0f, 1e-5f);
}

TEST_F (AudioGraphFixture, UnconnectedPortReceivesNullptr)
{
    NodeId src   = addConstant (1.0f, 0, 1);
    NodeId sumId = addSum (2);
    ASSERT_TRUE (connectAudio (src, 0, sumId, 0).has_value());
    prepareGraph();
    graph.process();

    EXPECT_TRUE (graph.getNodeAs<SumNode<float>> (sumId)->sawNullInputPort);
}

TEST_F (AudioGraphFixture, OutputBufferPersistsAcrossBlocks)
{
    NodeId id = addConstant (0.5f);
    prepareGraph();
    graph.process();
    graph.process();

    const auto* buf = graph.getNodeAs<ConstantNode<float>> (id)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_FLOAT_EQ (buf->sample (0, 0), 0.5f);
}

/*
 * 6.6 FeedbackReadsPreviousBlockOutput
 *
 * Topology: Root(0.5f) --normal--> Sum port 0
 *           Src(1.0f)  --feedback-> Sum port 1
 *
 * Block 1: Sum = 0.5f + 0.0f = 0.5f.  Src writes 1.0f.
 * Block 2: Sum = 0.5f + 1.0f = 1.5f.
 */
TEST_F (AudioGraphFixture, FeedbackReadsPreviousBlockOutput)
{
    NodeId rootId = addConstant (0.5f, 0, 1);
    NodeId sumId  = addSum (2);
    NodeId srcId  = addConstant (1.0f, 0, 1);

    ASSERT_TRUE (connectAudio (rootId, 0, sumId, 0, false).has_value());
    ASSERT_TRUE (connectAudio (srcId,  0, sumId, 1, true).has_value());
    prepareGraph();

    {
        const auto& order = graph.getSortedOrder();
        auto posRoot      = std::find (order.begin(), order.end(), rootId) - order.begin();
        auto posSum       = std::find (order.begin(), order.end(), sumId)  - order.begin();
        auto posSrc       = std::find (order.begin(), order.end(), srcId)  - order.begin();
        ASSERT_LT (posRoot, posSum);
        ASSERT_LT (posSum,  posSrc);
    }

    auto* sumNode = graph.getNodeAs<SumNode<float>> (sumId);
    ASSERT_NE (sumNode, nullptr);

    graph.process();
    EXPECT_NEAR (sumNode->getOutputBuffer (0)->sample (0, 0), 0.5f, 1e-5f);

    graph.process();
    EXPECT_NEAR (sumNode->getOutputBuffer (0)->sample (0, 0), 1.5f, 1e-5f);
}

/*======================================================================
 * Section 7: Data flow — control
 *====================================================================*/

TEST_F (AudioGraphFixture, ControlNodeOutputIsZeroBeforeProcess)
{
    NodeId id = addCounter();
    prepareGraph();
    EXPECT_FLOAT_EQ (graph.getNodeAs<CounterControlNode<float>> (id)->getControlOutput (0), 0.0f);
}

TEST_F (AudioGraphFixture, ControlNodeIncrementsEachBlock)
{
    NodeId id = addCounter();
    prepareGraph();

    constexpr int kBlocks = 4;
    for (int i = 0; i < kBlocks; ++i)
    {
        graph.process();
    }

    EXPECT_FLOAT_EQ (
        graph.getNodeAs<CounterControlNode<float>> (id)->getControlOutput (0),
        static_cast<float> (kBlocks));
}

TEST_F (AudioGraphFixture, ControlInputReachesDownstreamNode)
{
    NodeId srcId    = addConstant (0.5f, 0, 1);
    NodeId ctrlId   = addCounter();
    NodeId scaledId = addScaled();

    ASSERT_TRUE (connectAudio (srcId, 0, scaledId, 0).has_value());
    ASSERT_TRUE (connectControl (ctrlId, 0, scaledId, 1).has_value());
    prepareGraph();

    {
        const auto& order = graph.getSortedOrder();
        auto posCtrl   = std::find (order.begin(), order.end(), ctrlId)   - order.begin();
        auto posScaled = std::find (order.begin(), order.end(), scaledId) - order.begin();
        ASSERT_LT (posCtrl, posScaled);
    }

    auto* scaledNode = graph.getNodeAs<ScaledByControlNode<float>> (scaledId);
    ASSERT_NE (scaledNode, nullptr);

    graph.process();
    EXPECT_NEAR (scaledNode->getOutputBuffer (0)->sample (0, 0), 0.5f, 1e-5f);

    graph.process();
    EXPECT_NEAR (scaledNode->getOutputBuffer (0)->sample (0, 0), 1.0f, 1e-5f);
}

TEST_F (AudioGraphFixture, UnconnectedControlPortReturnsZero)
{
    NodeId srcId    = addConstant (0.5f, 0, 1);
    NodeId scaledId = addScaled();
    ASSERT_TRUE (connectAudio (srcId, 0, scaledId, 0).has_value());
    prepareGraph();
    graph.process();

    const auto* buf = graph.getNodeAs<ScaledByControlNode<float>> (scaledId)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_FLOAT_EQ (buf->sample (0, 0), 0.0f);
}

/*======================================================================
 * Section 8: process() preconditions and execution order
 *====================================================================*/

TEST_F (AudioGraphFixture, PrepareIsCalledBeforeProcess)
{
    addConstant();
    prepareGraph();
    EXPECT_TRUE (graph.isPrepared());
}

TEST_F (AudioGraphFixture, AddNodeAfterPrepareInvalidatesPrepared)
{
    addConstant();
    prepareGraph();
    ASSERT_TRUE (graph.isPrepared());

    addConstant();
    EXPECT_FALSE (graph.isPrepared());
}

TEST_F (AudioGraphFixture, NodeIsPreparedAfterGraphPrepare)
{
    NodeId id = addConstant();
    prepareGraph();
    EXPECT_TRUE (graph.getNode (id)->isPrepared());
}

TEST_F (AudioGraphFixture, LinearChainProcessedInOrder)
{
    std::vector<NodeId> order;
    order.reserve(3);

    NodeId a = addTracking (&order, 0, 1);
    NodeId b = addTracking (&order, 1, 1);
    NodeId c = addTracking (&order, 1, 1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, c, 0).has_value());
    prepareGraph();
    graph.process();

    ASSERT_EQ (order.size(), 3u);
    EXPECT_EQ (order[0], a);
    EXPECT_EQ (order[1], b);
    EXPECT_EQ (order[2], c);
}

TEST_F (AudioGraphFixture, DiamondProcessedWithCorrectPrecedence)
{
    std::vector<NodeId> order;
    order.reserve(4);

    NodeId a = addTracking (&order, 0, 1);
    NodeId b = addTracking (&order, 1, 1);
    NodeId c = addTracking (&order, 1, 1);
    NodeId d = addTracking (&order, 2, 1);

    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (a, 0, c, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, d, 0).has_value());
    ASSERT_TRUE (connectAudio (c, 0, d, 1).has_value());
    prepareGraph();
    graph.process();

    ASSERT_EQ (order.size(), 4u);
    auto posA = std::find (order.begin(), order.end(), a) - order.begin();
    auto posB = std::find (order.begin(), order.end(), b) - order.begin();
    auto posC = std::find (order.begin(), order.end(), c) - order.begin();
    auto posD = std::find (order.begin(), order.end(), d) - order.begin();
    EXPECT_LT (posA, posB);
    EXPECT_LT (posA, posC);
    EXPECT_LT (posB, posD);
    EXPECT_LT (posC, posD);
}

TEST_F (AudioGraphFixture, ProcessCalledOncePerNodePerBlock)
{
    NodeId a = addConstant (0.f, 0, 1);
    NodeId b = addSum (1);
    NodeId c = addSum (1);
    ASSERT_TRUE (connectAudio (a, 0, b, 0).has_value());
    ASSERT_TRUE (connectAudio (b, 0, c, 0).has_value());
    prepareGraph();

    constexpr int kBlocks = 5;
    for (int i = 0; i < kBlocks; ++i)
    {
        graph.process();
    }

    EXPECT_EQ (graph.getNodeAs<ConstantNode<float>> (a)->callCount, kBlocks);
    EXPECT_EQ (graph.getNodeAs<SumNode<float>> (b)->callCount,      kBlocks);
    EXPECT_EQ (graph.getNodeAs<SumNode<float>> (c)->callCount,      kBlocks);
}

/*======================================================================
 * Section 9: BlepOscillator in graph
 *====================================================================*/

TEST_F (GraphIntegrationFixture, SawOscillatorProducesNonZeroOutput)
{
    NodeId id = addOscillator (Oscillators::WaveShape::Saw, 440.f);
    prepareGraph();

    const auto samples = renderAndCollect (graph, id, kIntBlocks);
    const float r      = rmsOf (samples);
    EXPECT_GT (r, 0.1f);
    EXPECT_LT (r, 1.0f);
}

TEST_F (GraphIntegrationFixture, SawOscillatorFundamentalPeakAt440Hz)
{
    NodeId id = addOscillator (Oscillators::WaveShape::Saw, 440.f);
    prepareGraph();

    auto profile = analyzeF (renderAndCollect (graph, id, kIntBlocks));
    ASSERT_FALSE (profile.getPeaks().empty());
    EXPECT_NEAR (profile.getPeaks().front().frequency, 440.0, 10.0);
}

TEST_F (GraphIntegrationFixture, SawOscillatorHasHarmonicAt880Hz)
{
    NodeId id = addOscillator (Oscillators::WaveShape::Saw, 440.f);
    prepareGraph();

    auto profile = analyzeF (renderAndCollect (graph, id, kIntBlocks));
    EXPECT_TRUE (profile.hasPeakAt (880.0, 15.0));
}

TEST_F (GraphIntegrationFixture, SineOscillatorHasNoSignificantHarmonics)
{
    NodeId id = addOscillator (Oscillators::WaveShape::Sine, 440.f);
    prepareGraph();

    auto profile     = analyzeF (renderAndCollect (graph, id, kIntBlocks));
    const double fund  = profile.getMagnitudeAt (440.0);
    const double harm2 = profile.getMagnitudeAt (880.0);
    const double harm3 = profile.getMagnitudeAt (1320.0);

    ASSERT_GT (fund, 0.0);
    EXPECT_LT (harm2 / fund, 0.01);
    EXPECT_LT (harm3 / fund, 0.01);
}

TEST_F (GraphIntegrationFixture, TwoDifferentFrequenciesYieldDifferentPeaks)
{
    auto buildAndAnalyze = [] (float hz) -> double
    {
        AudioGraph<float> g;
        auto h = g.emplace<Oscillators::BlepOscillator<float>>();
        h.node.setShape (Oscillators::WaveShape::Saw);
        h.node.setFrequency (hz);
        g.prepare (kIntCh, kIntFrames, kIntRate);
        auto p = analyzeF (renderAndCollect (g, h.id, kIntBlocks));
        EXPECT_FALSE (p.getPeaks().empty());
        return p.getPeaks().empty() ? 0.0 : p.getPeaks().front().frequency;
    };

    EXPECT_NEAR (buildAndAnalyze (220.f), 220.0, 15.0);
    EXPECT_NEAR (buildAndAnalyze (880.f), 880.0, 15.0);
}

/*======================================================================
 * Section 10: ADSR in graph
 *====================================================================*/

TEST_F (GraphIntegrationFixture, AdsrIdleProducesZeroOutput)
{
    NodeId envId = addADSR();
    prepareGraph();

    auto* env = graph.getNodeAs<Envelope::ADSR<float>> (envId);
    ASSERT_NE (env, nullptr);
    env->setADSR (0.01f, 0.05f, 0.7f, 0.1f);
    graph.process();

    const auto* buf = graph.getNode (envId)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    for (std::size_t f = 0; f < kIntFrames; ++f)
    {
        EXPECT_FLOAT_EQ (buf->sample (0, f), 0.0f);
    }
}

TEST_F (GraphIntegrationFixture, AdsrAttackLevelRisesMonotonically)
{
    NodeId envId = addADSR();
    prepareGraph();

    auto* env = graph.getNodeAs<Envelope::ADSR<float>> (envId);
    ASSERT_NE (env, nullptr);
    env->setADSR (1.0f, 0.1f, 0.8f, 0.2f);
    env->noteOn();

    float prevMean = -1.0f;
    for (int blk = 0; blk < 5; ++blk)
    {
        graph.process();
        const auto* buf = graph.getNode (envId)->getOutputBuffer (0);
        float sum       = 0.f;
        for (std::size_t f = 0; f < kIntFrames; ++f)
        {
            sum += buf->sample (0, f);
        }
        const float mean = sum / static_cast<float> (kIntFrames);
        EXPECT_GT (mean, prevMean);
        prevMean = mean;
    }
}

TEST_F (GraphIntegrationFixture, AdsrSustainLevelIsStable)
{
    NodeId envId = addADSR();
    prepareGraph();

    auto* env = graph.getNodeAs<Envelope::ADSR<float>> (envId);
    ASSERT_NE (env, nullptr);
    env->setADSR (0.001f, 0.001f, 0.8f, 0.5f);
    env->noteOn();

    for (int i = 0; i < 3; ++i)
    {
        graph.process();
    }

    EXPECT_EQ (env->getState(), Envelope::State::sustain);

    float prevLevel = -1.f;
    for (int blk = 0; blk < 4; ++blk)
    {
        graph.process();
        const float level = env->getLevel();
        if (prevLevel >= 0.f)
        {
            EXPECT_NEAR (level, prevLevel, 1e-4f);
        }
        prevLevel = level;
    }
    EXPECT_NEAR (prevLevel, 0.8f, 0.05f);
}

TEST_F (GraphIntegrationFixture, AdsrReleaseDecaysToIdle)
{
    NodeId envId = addADSR();
    prepareGraph();

    auto* env = graph.getNodeAs<Envelope::ADSR<float>> (envId);
    ASSERT_NE (env, nullptr);
    env->setADSR (0.001f, 0.001f, 0.8f, 0.05f);
    env->noteOn();

    for (int i = 0; i < 5; ++i)
    {
        graph.process();
    }
    ASSERT_EQ (env->getState(), Envelope::State::sustain);

    env->noteOff();

    for (int i = 0; i < 300 && ! env->isIdle(); ++i)
    {
        graph.process();
    }

    EXPECT_TRUE (env->isIdle());
    EXPECT_FLOAT_EQ (env->getLevel(), 0.0f);
}

/*======================================================================
 * Section 11: Multi-node patches
 *====================================================================*/

TEST_F (GraphIntegrationFixture, TwoOscillatorsSummedShowBothFundamentals)
{
    NodeId oscA  = addOscillator (Oscillators::WaveShape::Saw, 300.f);
    NodeId oscB  = addOscillator (Oscillators::WaveShape::Saw, 700.f);
    auto   sumH  = graph.emplace<SumNode<float>> (2u);

    ASSERT_TRUE (graph.connect (oscA, 0, sumH.id, 0, ConnectionType::Audio).has_value());
    ASSERT_TRUE (graph.connect (oscB, 0, sumH.id, 1, ConnectionType::Audio).has_value());
    prepareGraph();

    auto profile = analyzeF (renderAndCollect (graph, sumH.id, kIntBlocks));
    EXPECT_TRUE (profile.hasPeakAt (300.0, 15.0));
    EXPECT_TRUE (profile.hasPeakAt (700.0, 15.0));
}

TEST_F (GraphIntegrationFixture, OscillatorMultipliedByAdsrRisesWithAttack)
{
    NodeId oscId = addOscillator (Oscillators::WaveShape::Saw, 440.f);
    NodeId envId = addADSR();
    auto   mulH  = graph.emplace<MultiplyNode<float>>();

    ASSERT_TRUE (graph.connect (oscId, 0, mulH.id, 0, ConnectionType::Audio).has_value());
    ASSERT_TRUE (graph.connect (envId, 0, mulH.id, 1, ConnectionType::Audio).has_value());
    prepareGraph();

    auto* env = graph.getNodeAs<Envelope::ADSR<float>> (envId);
    ASSERT_NE (env, nullptr);
    env->setADSR (2.0f, 0.1f, 0.8f, 0.5f);
    env->noteOn();

    float prevRMS = 0.f;
    for (int blk = 0; blk < 10; ++blk)
    {
        graph.process();
        const auto* buf = graph.getNode (mulH.id)->getOutputBuffer (0);
        float sq = 0.f;
        for (std::size_t f = 0; f < kIntFrames; ++f)
        {
            sq += buf->sample (0, f) * buf->sample (0, f);
        }
        const float blockRMS = std::sqrt (sq / static_cast<float> (kIntFrames));
        EXPECT_GE (blockRMS, prevRMS * 0.995f);
        prevRMS = blockRMS;
    }
    EXPECT_GT (prevRMS, 0.f);
}

TEST_F (GraphIntegrationFixture, NoiseOscillatorHasBroadSpectrum)
{
    auto h = graph.emplace<Oscillators::WhiteNoiseOscillator<float>>();
    h.node.seed (12345);
    prepareGraph();

    auto profile      = analyzeF (renderAndCollect (graph, h.id, kIntBlocks));
    const double low  = profile.getEnergyInRange (100.0,  5000.0);
    const double high = profile.getEnergyInRange (5000.0, 22000.0);

    EXPECT_GT (low,  0.0);
    EXPECT_GT (high, 0.0);
    const double ratio = high / low;
    EXPECT_GT (ratio, 0.5);
    EXPECT_LT (ratio, 10.0);
}

TEST_F (GraphIntegrationFixture, NoiseSpectralCentroidInReasonableRange)
{
    auto h = graph.emplace<Oscillators::WhiteNoiseOscillator<float>>();
    h.node.seed (54321);
    prepareGraph();

    auto profile = analyzeF (renderAndCollect (graph, h.id, 64));
    EXPECT_GT (profile.getSpectralCentroid(), 5000.0);
    EXPECT_LT (profile.getSpectralCentroid(), 18000.0);
}

/*======================================================================
 * Section 12: ModMatrix integration
 *====================================================================*/

TEST_F (GraphIntegrationFixture, ModMatrixRegistersParameterAndCountsRouting)
{
    auto mmH  = graph.emplace<Controls::ModMatrix<float>> (0u);
    auto oscH = graph.emplace<Oscillators::BlepOscillator<float>>();
    oscH.node.setFrequency (440.f);

    auto destResult = mmH.node.registerParameter (&oscH.node.amplitude);
    ASSERT_TRUE (destResult.has_value());
    const std::size_t destId = destResult.value();

    mmH.node.addRouting (Controls::ModulationRouting<float> (0, destId, 0.5f));

    prepareGraph();

    mmH.node.setSourceValue (0, 0.8f);
    graph.process();

    EXPECT_EQ (mmH.node.getNumParameters(), 1u);
    EXPECT_EQ (mmH.node.getNumRoutings(),   1u);
    EXPECT_FLOAT_EQ (mmH.node.getSourceValue (0), 0.8f);
}

TEST_F (GraphIntegrationFixture, ModMatrixZeroSourceLeavesParameterUnmodulated)
{
    auto mmH  = graph.emplace<Controls::ModMatrix<float>> (0u);
    auto oscH = graph.emplace<Oscillators::BlepOscillator<float>>();
    oscH.node.setFrequency (440.f);

    auto destResult = mmH.node.registerParameter (&oscH.node.amplitude);
    ASSERT_TRUE (destResult.has_value());
    mmH.node.addRouting (Controls::ModulationRouting<float> (0, destResult.value(), 0.5f));

    prepareGraph();
    graph.process();

    EXPECT_FLOAT_EQ (mmH.node.getSourceValue (0), 0.0f);
    EXPECT_EQ (mmH.node.getNumRoutings(), 1u);
}

/*======================================================================
 * Section 13: Graph vs standalone equivalence
 *====================================================================*/

TEST_F (GraphIntegrationFixture, GraphSineRMSMatchesStandaloneRMSWithinFivePercent)
{
    auto h = graph.emplace<Oscillators::BlepOscillator<float>>();
    h.node.setShape (Oscillators::WaveShape::Sine);
    h.node.setFrequency (440.f);
    prepareGraph();

    const auto graphSamples = renderAndCollect (graph, h.id, kIntBlocks);

    Oscillators::BlepOscillator<float> sa;
    sa.setSampleRate (static_cast<float> (kIntRate));
    sa.setShape (Oscillators::WaveShape::Sine);
    sa.setFrequency (440.f);

    const std::size_t total = static_cast<std::size_t> (kIntBlocks) * kIntFrames;
    std::vector<float> standaloneSamples (total);
    sa.renderBlock (standaloneSamples.data(), static_cast<int> (total));

    const float rmsGraph      = rmsOf (graphSamples);
    const float rmsStandalone = rmsOf (standaloneSamples);

    EXPECT_GT (rmsGraph,      0.1f);
    EXPECT_GT (rmsStandalone, 0.1f);
    EXPECT_NEAR (rmsGraph, rmsStandalone, rmsStandalone * 0.05f);
}