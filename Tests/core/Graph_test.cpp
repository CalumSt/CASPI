/*************************************************************************
 * @file Graph_test.cpp
 *
 * Unit tests for:
 *   CASPI::Graph::NodeBase<float>
 *   CASPI::Graph::AudioNode<Derived, float>
 *   CASPI::Graph::ControlNode<Derived, float>
 *   CASPI::Graph::AudioGraph<float>
 *   CASPI::Graph::AudioContext<float>
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Test nodes:
 *   ConstantNode<F>       AudioNode: fills outputBuffer with a constant value.
 *   SumNode<F>            AudioNode: sums all connected audio inputs.
 *   OrderTrackingNode<F>  AudioNode: appends NodeId to a shared vector.
 *   CounterControlNode<F> ControlNode: increments controlOutputs[0] each block.
 *   ScaledByControlNode<F> AudioNode: scales input audio by a control input.
 *
 * -----------------------------------------------------------------------
 * Section 1: Node construction
 * -----------------------------------------------------------------------
 * 1.1  NewNodeHasInvalidId
 * 1.2  NewNodeIsNotPrepared
 * 1.3  AudioNodePortCounts
 * 1.4  AudioNodeTypeIsAudio
 * 1.5  ControlNodeTypeIsControl
 *
 * -----------------------------------------------------------------------
 * Section 2: Graph — addNode / removeNode
 * -----------------------------------------------------------------------
 * 2.1  AddNodeReturnsValidId
 * 2.2  AddMultipleNodesReturnsSequentialIds
 * 2.3  AddNullNodeReturnsError
 * 2.4  GetNumNodesReflectsAdditions
 * 2.5  GetNodeByIdReturnsCorrectNode
 * 2.6  RemoveNodeDecreasesCount
 * 2.7  RemoveNodeRemovesItsConnections
 * 2.8  RemoveNonExistentNodeReturnsError
 * 2.9  AddNodeInvalidatesPrepare
 *
 * -----------------------------------------------------------------------
 * Section 3: Graph — connect / disconnect
 * -----------------------------------------------------------------------
 * 3.1  ConnectValidNodesSucceeds
 * 3.2  ConnectInvalidSourceIdReturnsError
 * 3.3  ConnectInvalidDestinationIdReturnsError
 * 3.4  ConnectOutOfRangeSourcePortReturnsError
 * 3.5  ConnectOutOfRangeDestinationPortReturnsError
 * 3.6  DuplicateConnectionReturnsError
 * 3.7  DisconnectExistingConnectionSucceeds
 * 3.8  DisconnectNonExistentConnectionReturnsError
 * 3.9  ConnectInvalidatesPrepare
 * 3.10 NormalAndFeedbackBetweenSamePortsBothPermitted
 *      A normal and a feedback connection between the same port pair are distinct
 *      (isFeedback is part of the key) and may coexist. Both should succeed.
 * 3.11 DisconnectRequiresFeedbackFlag
 *      With both a normal and a feedback connection present, disconnect with
 *      isFeedback=true removes only the feedback edge; the normal edge remains.
 * 3.12 ConnectAudioSourceWithControlTypeReturnsTypeMismatch
 *      Connecting an AudioNode source with ConnectionType::Control returns TypeMismatch.
 * 3.13 ConnectControlSourceWithAudioTypeReturnsTypeMismatch
 *      Connecting a ControlNode source with ConnectionType::Audio returns TypeMismatch.
 * 3.14 ConnectControlNodeSucceeds
 *      Connecting a ControlNode source with ConnectionType::Control succeeds.
 *
 * -----------------------------------------------------------------------
 * Section 4: Topological sort
 * -----------------------------------------------------------------------
 * 4.1  SingleNodeSortedOrder
 * 4.2  LinearChainSortedOrder
 * 4.3  DiamondTopologySortedOrder
 * 4.4  NonFeedbackCycleReturnsError
 * 4.5  FeedbackConnectionBreaksCycle
 * 4.6  DisconnectedSubgraphSortedCorrectly
 *
 * -----------------------------------------------------------------------
 * Section 5: Data flow — audio
 * -----------------------------------------------------------------------
 * 5.1  ConstantNodeFillsOutputBuffer
 * 5.2  ConnectedNodeReceivesUpstreamBuffer
 * 5.3  SumNodeAccumulatesMultipleInputs
 * 5.4  UnconnectedPortReceivesNullptr
 * 5.5  OutputBufferPersistsAcrossBlocks
 * 5.6  FeedbackReadsPreviousBlockOutput
 *
 * -----------------------------------------------------------------------
 * Section 6: Data flow — control
 * -----------------------------------------------------------------------
 * 6.1  ControlNodeOutputIsZeroBeforeProcess
 *      CounterControlNode starts at 0. getControlOutput(0) == 0.
 * 6.2  ControlNodeIncrementsEachBlock
 *      After N process() calls, getControlOutput(0) == N.
 * 6.3  ControlInputReachesDownstreamNode
 *      CounterControlNode -> ScaledByControlNode (audio in + control gain).
 *      Block 1: gain=1, audio=0.5f, output=0.5f.
 *      Block 2: gain=2, audio=0.5f, output=1.0f.
 * 6.4  UnconnectedControlPortReturnsZero
 *      ScaledByControlNode with no control connection: getControlInput returns 0.
 *      All output samples == 0 (0.5f * gain(0) = 0).
 *
 * -----------------------------------------------------------------------
 * Section 7: process() preconditions
 * -----------------------------------------------------------------------
 * 7.1  PrepareIsCalledBeforeProcess
 * 7.2  AddNodeAfterPrepareInvalidatesPrepared
 * 7.3  NodeIsPreparedAfterGraphPrepare
 *
 * -----------------------------------------------------------------------
 * Section 8: Execution order
 * -----------------------------------------------------------------------
 * 8.1  LinearChainProcessedInOrder
 * 8.2  DiamondProcessedWithCorrectPrecedence
 * 8.3  ProcessCalledOncePerNodePerBlock
 *
 ************************************************************************/

#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

using namespace CASPI::Graph;

/*======================================================================
 * Test helper nodes
 *====================================================================*/

/** AudioNode: fills outputBuffer with a constant value each block. */
template <typename FloatType>
class ConstantNode : public AudioNode<ConstantNode<FloatType>, FloatType>
{
    public:
        FloatType fillValue = FloatType (0);
        int callCount       = 0;

        explicit ConstantNode (FloatType value = FloatType (0), std::size_t numIn = 0, std::size_t numOut = 1)
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

/**
 * AudioNode: sums all connected audio inputs sample-by-sample.
 * Sets sawNullInputPort=true if any declared audio port has no connection.
 */
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
                    for (std::size_t fr = 0; fr < this->outputBuffer.numFrames(); ++fr)
                        this->outputBuffer.sample (ch, fr) += in->sample (ch, fr);
            }
        }
};

/** AudioNode: appends own NodeId to a shared vector on each process() call. */
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

        void processImpl (AudioContext<FloatType>&) noexcept
        {
            ++callCount;
            if (orderTracker != nullptr) orderTracker->push_back (this->getId());
        }
};

/** ControlNode: increments controlOutputs[0] by 1 each block. */
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

/**
 * AudioNode: multiplies a single audio input (port 0, Audio connection) by a
 * control scalar (port 1, Control connection). Exposes a second input port so
 * the total port count is 2, matching the usual port-count validation path.
 *
 * processImpl reads:
 *   audio input  via ctx.getAudioInput(id, 0)
 *   control gain via ctx.getControlInput(id, 1)
 *
 * Output = audio * gain. If the audio input is unconnected, output is 0.
 * If the control port is unconnected, ctx.getControlInput returns 0, so output
 * is also 0 (tests 6.4 depends on this).
 */
template <typename FloatType>
class ScaledByControlNode : public AudioNode<ScaledByControlNode<FloatType>, FloatType>
{
    public:
        ScaledByControlNode()
            : AudioNode<ScaledByControlNode<FloatType>, FloatType> (/*numInputPorts=*/2, /*numOutputPorts=*/1)
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
                for (std::size_t fr = 0; fr < this->outputBuffer.numFrames(); ++fr)
                    this->outputBuffer.sample (ch, fr) = in->sample (ch, fr) * gain;
        }
};

/*======================================================================
 * Fixture
 *
 * Fresh AudioGraph<float>, 2 channels, 64 frames, 44100 Hz.
 *====================================================================*/
struct AudioGraphFixture : ::testing::Test
{
        AudioGraph<float> graph;

        static constexpr std::size_t kChannels = 2;
        static constexpr std::size_t kFrames   = 64;
        static constexpr double kSampleRate    = 44100.0;

        NodeId addConstant (float value = 0.0f, std::size_t numIn = 0, std::size_t numOut = 1)
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

        // Helpers: connect with explicit type
        CASPI::expected<void, GraphError> connectAudio (NodeId src,
                                                        std::size_t sp,
                                                        NodeId dst,
                                                        std::size_t dp,
                                                        bool feedback = false)
        {
            return graph.connect (src, sp, dst, dp, ConnectionType::Audio, feedback);
        }

        CASPI::expected<void, GraphError> connectControl (NodeId src,
                                                          std::size_t sp,
                                                          NodeId dst,
                                                          std::size_t dp,
                                                          bool feedback = false)
        {
            return graph.connect (src, sp, dst, dp, ConnectionType::Control, feedback);
        }

        void prepareGraph()
        {
            auto res = graph.prepare (kChannels, kFrames, kSampleRate);
            ASSERT_TRUE (res.has_value()) << "prepare() failed";
        }
};

/*======================================================================
 * Section 1: Node construction
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
    ConstantNode<float> node (0.f, 3, 2);
    EXPECT_EQ (node.getNumInputPorts(), 3u);
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
 * Section 2: Graph — addNode / removeNode
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

/*======================================================================
 * Section 3: Graph — connect / disconnect
 *====================================================================*/

TEST_F (AudioGraphFixture, ConnectValidNodesSucceeds)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    EXPECT_TRUE (connectAudio (src, 0, dst, 0).has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

TEST_F (AudioGraphFixture, ConnectInvalidSourceIdReturnsError)
{
    NodeId dst = addSum (1);
    auto res   = graph.connect (999u, 0, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidNodeId);
}

TEST_F (AudioGraphFixture, ConnectInvalidDestinationIdReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    auto res   = graph.connect (src, 0, 999u, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidNodeId);
}

TEST_F (AudioGraphFixture, ConnectOutOfRangeSourcePortReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 99, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidPort);
}

TEST_F (AudioGraphFixture, ConnectOutOfRangeDestinationPortReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 0, dst, 99, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::InvalidPort);
}

// 3.6 — duplicate requires all six fields to match
TEST_F (AudioGraphFixture, DuplicateConnectionReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0).has_value());
    auto res = connectAudio (src, 0, dst, 0); // identical
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::DuplicateConnection);
}

// 3.7
TEST_F (AudioGraphFixture, DisconnectExistingConnectionSucceeds)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0).has_value());

    auto res = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 0u);
}

// 3.8
TEST_F (AudioGraphFixture, DisconnectNonExistentConnectionReturnsError)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    auto res   = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::ConnectionNotFound);
}

// 3.9
TEST_F (AudioGraphFixture, ConnectInvalidatesPrepare)
{
    NodeId src = addConstant (0.f, 0, 1);
    NodeId dst = addSum (1);
    prepareGraph();
    ASSERT_TRUE (graph.isPrepared());

    connectAudio (src, 0, dst, 0);
    EXPECT_FALSE (graph.isPrepared());
}

// 3.10 — normal and feedback between the same ports are distinct keys and may coexist
TEST_F (AudioGraphFixture, NormalAndFeedbackBetweenSamePortsBothPermitted)
{
    NodeId src = addConstant (0.f, 1, 1); // 1 input port so feedback is accepted
    NodeId dst = addSum (1);

    ASSERT_TRUE (connectAudio (src, 0, dst, 0, /*isFeedback=*/false).has_value());
    auto res = connectAudio (src, 0, dst, 0, /*isFeedback=*/true);
    EXPECT_TRUE (res.has_value()) << "Normal and feedback between same ports must both be accepted";
    EXPECT_EQ (graph.getNumConnections(), 2u);
}

// 3.11 — disconnect with isFeedback=true removes only the feedback edge
TEST_F (AudioGraphFixture, DisconnectRequiresFeedbackFlag)
{
    NodeId src = addConstant (0.f, 1, 1);
    NodeId dst = addSum (1);
    ASSERT_TRUE (connectAudio (src, 0, dst, 0, /*isFeedback=*/false).has_value());
    ASSERT_TRUE (connectAudio (src, 0, dst, 0, /*isFeedback=*/true).has_value());
    ASSERT_EQ (graph.getNumConnections(), 2u);

    // Remove only the feedback edge.
    auto res = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio, /*isFeedback=*/true);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u); // normal edge remains

    // Attempting to remove the feedback edge again should fail.
    auto res2 = graph.disconnect (src, 0, dst, 0, ConnectionType::Audio, /*isFeedback=*/true);
    EXPECT_FALSE (res2.has_value());
    EXPECT_EQ (res2.error(), GraphError::ConnectionNotFound);
}

// 3.12
TEST_F (AudioGraphFixture, ConnectAudioSourceWithControlTypeReturnsTypeMismatch)
{
    NodeId src = addConstant (0.f, 0, 1); // NodeType::Audio
    NodeId dst = addSum (1);
    auto res   = graph.connect (src, 0, dst, 0, ConnectionType::Control);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::TypeMismatch);
}

// 3.13
TEST_F (AudioGraphFixture, ConnectControlSourceWithAudioTypeReturnsTypeMismatch)
{
    NodeId ctrl = addCounter(); // NodeType::Control
    NodeId dst  = addSum (1);
    auto res    = graph.connect (ctrl, 0, dst, 0, ConnectionType::Audio);
    EXPECT_FALSE (res.has_value());
    EXPECT_EQ (res.error(), GraphError::TypeMismatch);
}

// 3.14
TEST_F (AudioGraphFixture, ConnectControlNodeSucceeds)
{
    NodeId ctrl   = addCounter();
    NodeId scaled = addScaled();
    auto res      = connectControl (ctrl, 0, scaled, 1);
    EXPECT_TRUE (res.has_value());
    EXPECT_EQ (graph.getNumConnections(), 1u);
}

/*======================================================================
 * Section 4: Topological sort
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
    ASSERT_TRUE (connectAudio (b, 0, a, 0, /*isFeedback=*/true).has_value());

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
 * Section 5: Data flow — audio
 *====================================================================*/

TEST_F (AudioGraphFixture, ConstantNodeFillsOutputBuffer)
{
    NodeId id = addConstant (0.5f);
    prepareGraph();
    graph.process();

    const auto* buf = graph.getNodeAs<ConstantNode<float>> (id)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    for (std::size_t ch = 0; ch < buf->numChannels(); ++ch)
        for (std::size_t fr = 0; fr < buf->numFrames(); ++fr)
            EXPECT_FLOAT_EQ (buf->sample (ch, fr), 0.5f) << "ch=" << ch << " fr=" << fr;
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
 * 5.6 FeedbackReadsPreviousBlockOutput
 *
 * Topology (NodeId order: Root=0, Sum=1, Src=2):
 *   Root (0.5f) --normal-->   Sum port 0
 *   Src  (1.0f) --feedback--> Sum port 1
 *
 * Kahn over non-feedback edges: Root and Src are seeds (in-degree 0).
 * std::map seeds in ascending NodeId order: Root=0 first, then Src=2.
 * Processing Root decrements Sum's in-degree to 0, enqueuing Sum before Src.
 * Kahn order: [Root, Src, Sum].
 *
 * Feedback ordering pass: Src (feedSrc) must be after Sum (feedDst).
 * pos(Src)=1 < pos(Sum)=2 is a violation. Move Src to after Sum.
 * Final order: [Root, Sum, Src].
 *
 * Block 1: Sum = Root(0.5f) + Src_prev(0.0f) = 0.5f. Src writes 1.0f.
 * Block 2: Sum = Root(0.5f) + Src_prev(1.0f) = 1.5f.
 */
TEST_F (AudioGraphFixture, FeedbackReadsPreviousBlockOutput)
{
    NodeId rootId = addConstant (0.5f, 0, 1);
    NodeId sumId  = addSum (2);
    NodeId srcId  = addConstant (1.0f, 0, 1);

    ASSERT_TRUE (connectAudio (rootId, 0, sumId, 0, /*isFeedback=*/false).has_value());
    ASSERT_TRUE (connectAudio (srcId, 0, sumId, 1, /*isFeedback=*/true).has_value());
    prepareGraph();

    {
        const auto& order = graph.getSortedOrder();
        auto posRoot      = std::find (order.begin(), order.end(), rootId) - order.begin();
        auto posSum       = std::find (order.begin(), order.end(), sumId) - order.begin();
        auto posSrc       = std::find (order.begin(), order.end(), srcId) - order.begin();
        ASSERT_LT (posRoot, posSum) << "Root must precede Sum (normal edge)";
        ASSERT_LT (posSum, posSrc) << "Feedback pass must place Sum before Src";
    }

    auto* sumNode = graph.getNodeAs<SumNode<float>> (sumId);
    ASSERT_NE (sumNode, nullptr);

    graph.process();
    EXPECT_NEAR (sumNode->getOutputBuffer (0)->sample (0, 0), 0.5f, 1e-5f) << "Block 1: Root(0.5f) + Src_prev(0.0f)";

    graph.process();
    EXPECT_NEAR (sumNode->getOutputBuffer (0)->sample (0, 0), 1.5f, 1e-5f) << "Block 2: Root(0.5f) + Src_prev(1.0f)";
}

/*======================================================================
 * Section 6: Data flow — control
 *====================================================================*/

// 6.1
TEST_F (AudioGraphFixture, ControlNodeOutputIsZeroBeforeProcess)
{
    NodeId id = addCounter();
    prepareGraph();
    EXPECT_FLOAT_EQ (graph.getNodeAs<CounterControlNode<float>> (id)->getControlOutput (0), 0.0f);
}

// 6.2
TEST_F (AudioGraphFixture, ControlNodeIncrementsEachBlock)
{
    NodeId id = addCounter();
    prepareGraph();

    constexpr int kBlocks = 4;
    for (int i = 0; i < kBlocks; ++i)
        graph.process();

    EXPECT_FLOAT_EQ (graph.getNodeAs<CounterControlNode<float>> (id)->getControlOutput (0),
                     static_cast<float> (kBlocks));
}

/*
 * 6.3 ControlInputReachesDownstreamNode
 *
 * Topology:
 *   Src  (ConstantNode, 0.5f)  --Audio-->   Scaled port 0
 *   Ctrl (CounterControlNode)  --Control--> Scaled port 1
 *
 * Ctrl increments by 1 each block. Scaled multiplies audio by the control gain.
 *
 * Block 1: Ctrl runs first (in-degree 0, lower NodeId). Ctrl output = 1.
 *          Scaled reads audio=0.5f, gain=1 -> output = 0.5f.
 * Block 2: Ctrl output = 2.
 *          Scaled reads audio=0.5f, gain=2 -> output = 1.0f.
 */
TEST_F (AudioGraphFixture, ControlInputReachesDownstreamNode)
{
    NodeId srcId    = addConstant (0.5f, 0, 1);
    NodeId ctrlId   = addCounter();
    NodeId scaledId = addScaled();

    ASSERT_TRUE (connectAudio (srcId, 0, scaledId, 0).has_value());
    ASSERT_TRUE (connectControl (ctrlId, 0, scaledId, 1).has_value());
    prepareGraph();

    // Verify Ctrl appears before Scaled in the sorted order.
    {
        const auto& order = graph.getSortedOrder();
        auto posCtrl      = std::find (order.begin(), order.end(), ctrlId) - order.begin();
        auto posScaled    = std::find (order.begin(), order.end(), scaledId) - order.begin();
        ASSERT_LT (posCtrl, posScaled) << "ControlNode must process before its consumer";
    }

    auto* scaledNode = graph.getNodeAs<ScaledByControlNode<float>> (scaledId);
    ASSERT_NE (scaledNode, nullptr);

    graph.process();
    EXPECT_NEAR (scaledNode->getOutputBuffer (0)->sample (0, 0), 0.5f, 1e-5f) << "Block 1: 0.5f * gain(1)";

    graph.process();
    EXPECT_NEAR (scaledNode->getOutputBuffer (0)->sample (0, 0), 1.0f, 1e-5f) << "Block 2: 0.5f * gain(2)";
}

// 6.4 — no control connection: getControlInput returns 0, so output is 0
TEST_F (AudioGraphFixture, UnconnectedControlPortReturnsZero)
{
    NodeId srcId    = addConstant (0.5f, 0, 1);
    NodeId scaledId = addScaled();
    ASSERT_TRUE (connectAudio (srcId, 0, scaledId, 0).has_value()); // audio only
    prepareGraph();
    graph.process();

    const auto* buf = graph.getNodeAs<ScaledByControlNode<float>> (scaledId)->getOutputBuffer (0);
    ASSERT_NE (buf, nullptr);
    EXPECT_FLOAT_EQ (buf->sample (0, 0), 0.0f) << "Unconnected control port returns 0; 0.5f * 0 = 0";
}

/*======================================================================
 * Section 7: process() preconditions
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

/*======================================================================
 * Section 8: Execution order
 *====================================================================*/

TEST_F (AudioGraphFixture, LinearChainProcessedInOrder)
{
    std::vector<NodeId> order;

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
        graph.process();

    EXPECT_EQ (graph.getNodeAs<ConstantNode<float>> (a)->callCount, kBlocks);
    EXPECT_EQ (graph.getNodeAs<SumNode<float>> (b)->callCount, kBlocks);
    EXPECT_EQ (graph.getNodeAs<SumNode<float>> (c)->callCount, kBlocks);
}