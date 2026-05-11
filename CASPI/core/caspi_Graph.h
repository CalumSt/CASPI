#ifndef CASPI_GRAPH_H
#define CASPI_GRAPH_H

/*
 *  .d8888b.                             d8b
 * d88P  Y88b                            Y8P
 * 888    888
 * 888         8888b.  .d8888b  88888b.  888
 * 888            "88b 88K      888 "88b 888
 * 888    888 .d888888 "Y8888b. 888  888 888
 * Y88b  d88P 888  888      X88 888 d88P 888
 *  "Y8888P"  "Y888888  88888P' 88888P"  888
 *                              888
 *                              888
 *                              888
 *
 * @file   core/caspi_Graph.h
 * @author CS Islay
 * @brief  Directed acyclic graph of audio and control nodes.
 *
 * OVERVIEW
 *
 * AudioGraph<FloatType> owns a set of NodeBase<FloatType> instances and a
 * set of Connections between their ports. On each call to process(), nodes
 * are visited in topological order and each node's process() is called once.
 *
 * THREAD SAFETY
 *
 *   addNode / emplace / removeNode / connect / disconnect  setup thread only
 *   prepare()                                              setup thread only, may allocate
 *   process()                                              audio thread only, noexcept, no allocation
 *
 * Mutations after prepare() require a fresh prepare() before the next
 * process(). The graph asserts isPrepared() at the entry of process().
 *
 * CONNECTION TYPES
 *
 *   ConnectionType::Audio
 *     Source must be NodeType::Audio. Carries a BufferType* resolved at
 *     prepare() time. Destination reads via AudioContext::getAudioInput().
 *
 *   ConnectionType::Control
 *     Source must be NodeType::Control. Carries a const FloatType* resolved
 *     at prepare() time. Destination reads via AudioContext::getControlInput().
 *     O(1) dereference, no virtual call, no scan.
 *
 * PORT TYPE
 *
 * Port{nodeId, portIndex} is a lightweight value type used by the ergonomic
 * connect overloads. Port(nodeId) defaults the port index to 0, covering the
 * common single-port case:
 *
 *   graph.connect(oscId, filterId);              // port 0 -> port 0
 *   graph.connect({oscId, 0}, {filterId, 0});    // explicit
 *
 * EMPLACE / NODE HANDLE
 *
 * emplace<T>(args...) constructs a node in-place, adds it to the graph, and
 * returns a NodeHandle<T>. The handle carries the NodeId and a typed non-owning
 * reference valid for the lifetime of the node in the graph:
 *
 *   auto [id, osc] = graph.emplace<BlepOscillator<float>>();
 *   osc.setFrequency(440.f);
 *   osc.setShape(WaveShape::Saw);
 *
 * addNode(unique_ptr) is retained for cases where the node is constructed
 * externally (e.g. Python bindings, factory patterns).
 *
 * FEEDBACK
 *
 * connectFeedback() is the explicit API for back-edges. It is self-documenting
 * and removes the positional isFeedback=true argument from connect():
 *
 *   graph.connectFeedback(delayOutputId, delayInputId);
 *
 * A feedback Audio connection reads the source node's previous-block buffer
 * (one block of latency). A feedback Control connection reads the previous-
 * block control value by the same mechanism.
 *
 * TOPOLOGICAL SORT
 *
 * Kahn's algorithm (BFS) over non-feedback edges. O(V + E).
 * Nodes with no non-feedback predecessors are roots.
 *
 * If a non-feedback cycle is detected, prepare() returns GraphError::CycleDetected.
 * Use connectFeedback() (or the isFeedback=true overload) to break cycles.
 *
 * REAL-TIME PATH
 *
 * process() is noexcept and performs no heap allocation. All pointer resolution
 * is done at prepare() time and cached in:
 *   cachedAudioLinks   flat array of (dstNode, dstPort, BufferType*)
 *   cachedControlLinks flat array of (dstNode, dstPort, const FloatType*)
 *   sortedNodePtrs     flat array of raw NodeBase* in execution order
 *
 * AudioContext is constructed each process() call with reserved capacity for
 * both caches — no allocation occurs in the push_back loops.
 */

#include "base/caspi_Compatibility.h"
#include "caspi_Expected.h"
#include "core/caspi_Node.h"

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace CASPI
{
namespace Graph
{

    /*======================================================================
     * Port
     *====================================================================*/

    /**
     * @brief Lightweight value type identifying a node output or input port.
     *
     * Constructed from a NodeId alone (port defaults to 0) or from an explicit
     * {nodeId, portIndex} pair. Used by the ergonomic connect() overloads to
     * eliminate positional port arguments.
     *
     * @code
     *   graph.connect(oscId, filterId);              // Port(oscId,0) -> Port(filterId,0)
     *   graph.connect({oscId, 1}, {filterId, 2});    // explicit ports
     * @endcode
     */
    struct Port
    {
        /** @brief Node this port belongs to. */
        NodeId node = INVALID_NODE_ID;

        /** @brief Zero-based port index on the node. */
        std::size_t index = 0;

        /** @brief Construct from a NodeId alone; port index defaults to 0. */
        /* implicit */ Port (NodeId n) : node (n), index (0) {}  // NOLINT(google-explicit-constructor)

        /** @brief Construct from an explicit node and port index. */
        Port (NodeId n, std::size_t i) : node (n), index (i) {}
    };

    /*======================================================================
     * NodeHandle<T>
     *====================================================================*/

    /**
     * @brief Typed, non-owning handle returned by AudioGraph::emplace<T>().
     *
     * Carries both the NodeId (stable, survives the node's lifetime for logging
     * and connection purposes) and a typed raw reference to the node itself.
     * The reference is valid only while the node remains in the graph.
     *
     * Structured-binding compatible on C++17:
     * @code
     *   auto [id, osc] = graph.emplace<BlepOscillator<float>>();
     *   osc.setFrequency(440.f);
     * @endcode
     *
     * On C++11/14 access via .id and .node:
     * @code
     *   NodeHandle<BlepOscillator<float>> h = graph.emplace<BlepOscillator<float>>();
     *   h.node.setFrequency(440.f);
     *   NodeId oscId = h.id;
     * @endcode
     *
     * @tparam T  Concrete node type (must inherit NodeBase<FloatType>).
     */
    template <typename T>
    struct NodeHandle
    {
        /** @brief NodeId assigned by addNode(). Stable. */
        NodeId id;

        /** @brief Non-owning reference to the node. Valid while node is in graph. */
        T& node;
    };

    /*======================================================================
     * ConnectionType
     *====================================================================*/

    /**
     * @brief Discriminates audio-buffer connections from control-scalar connections.
     *
     * AudioGraph::connect() validates this against the source node's NodeType:
     *   ConnectionType::Audio   requires the source to be NodeType::Audio.
     *   ConnectionType::Control requires the source to be NodeType::Control.
     *
     * Mismatches return GraphError::TypeMismatch from connect().
     */
    enum class ConnectionType
    {
        Audio,   ///< Carries a BufferType*; read via AudioContext::getAudioInput().
        Control  ///< Carries a const FloatType*; read via AudioContext::getControlInput().
    };

    /*======================================================================
     * Connection
     *====================================================================*/

    /**
     * @brief Directed edge between two node ports.
     *
     * CONNECTION IDENTITY
     *
     * The full key is (sourceNode, sourcePort, destinationNode, destinationPort,
     * connectionType, isFeedback). All six fields must match for a connection to
     * be considered a duplicate. A normal and a feedback connection between the
     * same port pair are distinct and may coexist.
     *
     * FEEDBACK SEMANTICS
     *
     * isFeedback == false  Normal dataflow edge; included in topological sort.
     *                      Destination reads source's current-block output.
     *
     * isFeedback == true   Excluded from topological sort. Destination reads
     *                      source's previous-block output (one block of latency).
     *                      Use connectFeedback() or the isFeedback overload of
     *                      connect() to create these edges.
     */
    struct Connection
    {
        /** @brief NodeId of the upstream (source) node. */
        NodeId sourceNode = INVALID_NODE_ID;

        /** @brief Zero-based output port index on the source node. */
        std::size_t sourcePort = 0;

        /** @brief NodeId of the downstream (destination) node. */
        NodeId destinationNode = INVALID_NODE_ID;

        /** @brief Zero-based input port index on the destination node. */
        std::size_t destinationPort = 0;

        /** @brief Audio or Control routing type. Must match the source node's NodeType. */
        ConnectionType connectionType = ConnectionType::Audio;

        /**
         * @brief If true, excluded from topological sort; reads previous-block output.
         *
         * Use connectFeedback() at the call site rather than setting this flag
         * directly — the intent is clearer and the argument is not positional.
         */
        bool isFeedback = false;
    };

    /*======================================================================
     * GraphError
     *====================================================================*/

    /**
     * @brief Error codes returned by AudioGraph setup and preparation methods.
     */
    enum class GraphError
    {
        InvalidNodeId,       ///< NodeId does not exist in this graph.
        InvalidPort,         ///< Port index out of range for the specified node.
        DuplicateConnection, ///< Identical connection (all six key fields) already present.
        ConnectionNotFound,  ///< disconnect() target does not exist.
        NodeNotFound,        ///< removeNode() target does not exist.
        CycleDetected,       ///< Non-feedback cycle; use connectFeedback() for back-edges.
        TypeMismatch,        ///< ConnectionType does not match the source node's NodeType.
        NotPrepared,         ///< process() called before prepare() or after a mutation.
        NullNode             ///< addNode() called with nullptr.
    };

    /*======================================================================
     * AudioContext<FloatType>
     *====================================================================*/

    /**
     * @brief Per-block context passed to each node's processImpl().
     *
     * Provides typed access to upstream audio buffers and control scalars,
     * both resolved at prepare() time. Pointers remain stable for the
     * lifetime of a prepare() session.
     *
     * AudioContext is constructed each process() call with reserved capacity
     * equal to the number of cached links — push_back never allocates.
     *
     * AUDIO INPUTS
     *
     * Populated from ConnectionType::Audio connections. Queried via
     * getAudioInput(dstNode, dstPort). Returns nullptr if no audio connection
     * exists for that port. Linear scan: O(numAudioConnections).
     *
     * CONTROL INPUTS
     *
     * Populated from ConnectionType::Control connections. Queried via
     * getControlInput(dstNode, dstPort). Returns FloatType(0) if no control
     * connection exists for that port (unconnected control ports are silent).
     * Linear scan: O(numControlConnections), then a single pointer dereference.
     *
     * @tparam FloatType  Floating-point sample type matching the owning AudioGraph.
     */
    template <typename FloatType>
    class AudioContext
    {
        public:
            /** @brief Audio buffer type used throughout this context. */
            using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;

            /*------------------------------------------------------------------
             * Audio input query (audio thread)
             *-----------------------------------------------------------------*/

            /**
             * @brief Return the audio buffer connected to (destinationNode, destinationPort).
             *
             * Returns nullptr if no Audio connection targets this (node, port) pair.
             * For feedback connections the buffer contains the previous block's output.
             *
             * Linear scan: O(numAudioConnections). Real-time safe.
             *
             * @param destinationNode  NodeId of the querying node.
             * @param destinationPort  Zero-based input port index.
             * @return                 Pointer to upstream audio buffer, or nullptr.
             */
            CASPI_NO_DISCARD const BufferType* getAudioInput (NodeId destinationNode,
                                                              std::size_t destinationPort) const noexcept
                CASPI_NON_BLOCKING
            {
                for (const auto& entry : resolvedAudioInputs)
                {
                    if (entry.destinationNode == destinationNode
                        && entry.destinationPort == destinationPort)
                    {
                        return entry.buffer;
                    }
                }
                return nullptr;
            }

            /*------------------------------------------------------------------
             * Control input query (audio thread)
             *-----------------------------------------------------------------*/

            /**
             * @brief Return the control scalar connected to (destinationNode, destinationPort).
             *
             * Returns FloatType(0) if no Control connection targets this (node, port) pair.
             * For feedback connections the value is from the previous block.
             *
             * Linear scan: O(numControlConnections), then one pointer dereference.
             * Real-time safe.
             *
             * @param destinationNode  NodeId of the querying node.
             * @param destinationPort  Zero-based input port index.
             * @return                 Current control scalar, or FloatType(0) if unconnected.
             */
            CASPI_NO_DISCARD FloatType getControlInput (NodeId destinationNode,
                                                        std::size_t destinationPort) const noexcept
                CASPI_NON_BLOCKING
            {
                for (const auto& entry : resolvedControlInputs)
                {
                    if (entry.destinationNode == destinationNode
                        && entry.destinationPort == destinationPort)
                    {
                        return *entry.valuePtr;
                    }
                }
                return FloatType (0);
            }

            /*------------------------------------------------------------------
             * Block geometry accessors
             *-----------------------------------------------------------------*/

            /** @brief Number of audio channels for this block. */
            CASPI_NO_DISCARD std::size_t getNumChannels() const noexcept { return numChannels; }

            /** @brief Block size in frames. */
            CASPI_NO_DISCARD std::size_t getNumFrames() const noexcept { return numFrames; }

            /** @brief Sample rate in Hz. */
            CASPI_NO_DISCARD double getSampleRate() const noexcept { return sampleRate; }

        private:
            /** @brief AudioGraph builds and populates the context each process() call. */
            template <typename F>
            friend class AudioGraph;

            /** @brief Resolved audio input: maps (dstNode, dstPort) to an upstream BufferType*. */
            struct ResolvedAudioInput
            {
                NodeId destinationNode;
                std::size_t destinationPort;
                const BufferType* buffer;
            };

            /** @brief Resolved control input: maps (dstNode, dstPort) to a const FloatType*. */
            struct ResolvedControlInput
            {
                NodeId destinationNode;
                std::size_t destinationPort;
                const FloatType* valuePtr;
            };

            /**
             * @brief Construct an AudioContext for a block with the given geometry.
             *
             * Pre-reserves both input vectors to prevent heap allocation during
             * the subsequent addAudioInput() / addControlInput() calls.
             */
            AudioContext (std::size_t numChannelsIn,
                          std::size_t numFramesIn,
                          double sampleRateIn,
                          std::size_t numAudioLinks,
                          std::size_t numControlLinks)
                : numChannels (numChannelsIn)
                , numFrames (numFramesIn)
                , sampleRate (sampleRateIn)
            {
                resolvedAudioInputs.reserve (numAudioLinks);
                resolvedControlInputs.reserve (numControlLinks);
            }

            void addAudioInput (NodeId dst, std::size_t dstPort, const BufferType* buf)
            {
                resolvedAudioInputs.push_back ({ dst, dstPort, buf });
            }

            void addControlInput (NodeId dst, std::size_t dstPort, const FloatType* valuePtr)
            {
                resolvedControlInputs.push_back ({ dst, dstPort, valuePtr });
            }

            std::vector<ResolvedAudioInput>   resolvedAudioInputs;
            std::vector<ResolvedControlInput>  resolvedControlInputs;
            std::size_t numChannels;
            std::size_t numFrames;
            double      sampleRate;
    };

    /*======================================================================
     * AudioGraph<FloatType>
     *====================================================================*/

    /**
     * @brief Directed acyclic graph of NodeBase<FloatType> instances.
     *
     * Owns all nodes. Evaluates them in topological order each process() call.
     * Not copyable. Move-constructible.
     *
     * @tparam FloatType  Floating-point sample type (float, double). Default: double.
     */
    template <typename FloatType = double>
    class AudioGraph
    {
        public:
            /** @brief Polymorphic node base type held by this graph. */
            using NodeType_t = NodeBase<FloatType>;

            /** @brief Audio buffer type used throughout this graph. */
            using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;

            AudioGraph()  = default;
            ~AudioGraph() = default;

            AudioGraph (const AudioGraph&)            = delete;
            AudioGraph& operator= (const AudioGraph&) = delete;

            AudioGraph (AudioGraph&&)            = default;
            AudioGraph& operator= (AudioGraph&&) = default;

            /*==================================================================
             * Node addition — setup thread only
             *================================================================*/

            /**
             * @brief Construct a node in-place, add it to the graph, and return a typed handle.
             *
             * This is the preferred API for C++ callers. The handle carries both the
             * NodeId and a typed non-owning reference, eliminating the need for a
             * subsequent getNodeAs() call:
             *
             * @code
             *   auto [id, osc] = graph.emplace<BlepOscillator<float>>();
             *   osc.setFrequency(440.f);
             * @endcode
             *
             * Invalidates the current prepare() result.
             *
             * @tparam T       Concrete node type. Must inherit NodeBase<FloatType>.
             * @tparam Args    Constructor argument types for T.
             * @param  args    Constructor arguments forwarded to T.
             * @return         NodeHandle<T> with .id and .node members.
             */
            template <typename T, typename... Args>
            NodeHandle<T> emplace (Args&&... args) CASPI_ALLOCATING
            {
                auto node   = CASPI::make_unique<T> (std::forward<Args> (args)...);
                T* raw      = node.get();
                const NodeId id = addNode (std::move (node)).value();
                return NodeHandle<T> { id, *raw };
            }

            /**
             * @brief Add a node to the graph, taking ownership.
             *
             * Assigns a NodeId and returns it wrapped in expected<>. NodeIds are
             * monotonically increasing from 0 and are never reused.
             *
             * Prefer emplace<T>() for C++ callers. Use addNode() when the node is
             * constructed externally (factory patterns, Python bindings).
             *
             * Invalidates the current prepare() result.
             *
             * @param node  Non-null unique_ptr to any NodeBase<FloatType> subclass.
             * @return      Assigned NodeId on success.
             * @return      GraphError::NullNode if node is nullptr.
             */
            CASPI_NO_DISCARD expected<NodeId, GraphError>
            addNode (std::unique_ptr<NodeType_t> node) CASPI_ALLOCATING
            {
                if (! node)
                {
                    return make_unexpected<NodeId> (GraphError::NullNode);
                }

                const NodeId id = nextId++;
                node->assignId (id);
                nodes[id]     = std::move (node);
                graphPrepared = false;
                return make_expected<NodeId, GraphError> (id);
            }

            /**
             * @brief Remove a node and all connections involving it.
             *
             * The node is destroyed immediately. All connections referencing the
             * removed NodeId are erased. Invalidates the current prepare().
             *
             * @param id  NodeId to remove.
             * @return    void on success.
             * @return    GraphError::NodeNotFound if id does not exist.
             */
            CASPI_NO_DISCARD expected<void, GraphError> removeNode (NodeId id)
            {
                auto it = nodes.find (id);
                if (it == nodes.end())
                {
                    return make_unexpected (GraphError::NodeNotFound);
                }

                connections.erase (
                    std::remove_if (connections.begin(), connections.end(),
                                    [id] (const Connection& c)
                                    { return c.sourceNode == id || c.destinationNode == id; }),
                    connections.end());

                nodes.erase (it);
                graphPrepared = false;
                return {};
            }

            /*==================================================================
             * Connection — setup thread only
             *================================================================*/

            /**
             * @brief Connect two nodes audio port-to-port (port 0 to port 0).
             *
             * Convenience overload for the common single-port case. Equivalent to
             * connect(Port{src, 0}, Port{dst, 0}).
             *
             * @param src  Source NodeId (must be NodeType::Audio).
             * @param dst  Destination NodeId.
             * @return     void on success, or a GraphError.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            connect (NodeId src, NodeId dst) CASPI_ALLOCATING
            {
                return connectImpl (src, 0, dst, 0, ConnectionType::Audio, false);
            }

            /**
             * @brief Connect two ports with explicit source and destination Port values.
             *
             * Port{nodeId} defaults the index to 0; Port{nodeId, n} is explicit:
             *
             * @code
             *   graph.connect(oscId, filterId);              // 0 -> 0
             *   graph.connect({oscId, 0}, {filterId, 0});    // explicit
             *   graph.connect({mixId, 1}, {outId, 0});       // non-zero source port
             * @endcode
             *
             * The ConnectionType is inferred from the source node's NodeType and must
             * be Audio. Use connectControl() for control-rate connections.
             *
             * @param src  Source port (AudioNode).
             * @param dst  Destination port.
             * @return     void on success, or a GraphError.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            connect (Port src, Port dst) CASPI_ALLOCATING
            {
                return connectImpl (src.node, src.index, dst.node, dst.index,
                                    ConnectionType::Audio, false);
            }

            /**
             * @brief Connect a ControlNode output to a destination input port.
             *
             * Explicit control-rate connection. The source must be NodeType::Control.
             *
             * @code
             *   graph.connectControl(lfoId, {filterId, 1});  // LFO -> filter cutoff port
             * @endcode
             *
             * @param src  Source port (ControlNode).
             * @param dst  Destination port.
             * @return     void on success, or a GraphError.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            connectControl (Port src, Port dst) CASPI_ALLOCATING
            {
                return connectImpl (src.node, src.index, dst.node, dst.index,
                                    ConnectionType::Control, false);
            }

            /**
             * @brief Connect a feedback (back-edge) between two nodes.
             *
             * The destination reads the source's previous-block output (one block
             * of latency). Feedback connections are excluded from the topological sort
             * and are used to break cycles.
             *
             * Prefer this over the isFeedback=true overload of connect() — the intent
             * is self-documenting at the call site.
             *
             * @code
             *   graph.connectFeedback(delayOutputId, delayInputId);
             * @endcode
             *
             * @param src  Source port.
             * @param dst  Destination port.
             * @return     void on success, or a GraphError.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            connectFeedback (Port src, Port dst) CASPI_ALLOCATING
            {
                return connectImpl (src.node, src.index, dst.node, dst.index,
                                    ConnectionType::Audio, true);
            }

            /**
             * @brief Full-form connect with all six parameters explicit.
             *
             * Retained for completeness and for cases (such as Python bindings) where
             * the simpler overloads are inconvenient. Prefer the Port-based overloads
             * in C++ code.
             *
             * @param srcNode        Source NodeId.
             * @param srcPort        Zero-based source output port index.
             * @param dstNode        Destination NodeId.
             * @param dstPort        Zero-based destination input port index.
             * @param connectionType Audio or Control routing type.
             * @param isFeedback     If true, excluded from sort; reads previous block.
             * @return               void on success, or a GraphError.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            connect (NodeId srcNode,
                     std::size_t srcPort,
                     NodeId dstNode,
                     std::size_t dstPort,
                     ConnectionType connectionType,
                     bool isFeedback = false) CASPI_ALLOCATING
            {
                return connectImpl (srcNode, srcPort, dstNode, dstPort, connectionType, isFeedback);
            }

            /**
             * @brief Remove a connection identified by its full six-field key.
             *
             * All six fields must match to locate the connection. Invalidates prepare().
             *
             * @param srcNode        Source NodeId.
             * @param srcPort        Zero-based source output port index.
             * @param dstNode        Destination NodeId.
             * @param dstPort        Zero-based destination input port index.
             * @param connectionType Routing type of the target connection.
             * @param isFeedback     Feedback flag of the target connection.
             * @return               void on success.
             * @return               GraphError::ConnectionNotFound if no match.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            disconnect (NodeId srcNode,
                        std::size_t srcPort,
                        NodeId dstNode,
                        std::size_t dstPort,
                        ConnectionType connectionType,
                        bool isFeedback = false)
            {
                auto it = std::find_if (connections.begin(), connections.end(),
                                        [&] (const Connection& c)
                                        {
                                            return c.sourceNode      == srcNode
                                                && c.sourcePort      == srcPort
                                                && c.destinationNode == dstNode
                                                && c.destinationPort == dstPort
                                                && c.connectionType  == connectionType
                                                && c.isFeedback      == isFeedback;
                                        });

                if (it == connections.end())
                {
                    return make_unexpected (GraphError::ConnectionNotFound);
                }

                connections.erase (it);
                graphPrepared = false;
                return {};
            }

            /*==================================================================
             * Preparation — setup thread, may allocate
             *================================================================*/

            /**
             * @brief Sort nodes and prepare them for the given block geometry.
             *
             * Must be called after any topology change and before process().
             * Performs Kahn's BFS on non-feedback edges: O(V + E).
             * Calls prepareToRender() on each node in topological order.
             * Resolves Audio and Control connections into flat pointer caches.
             * Builds sortedNodePtrs for O(1) dispatch during process().
             *
             * On success isPrepared() returns true. On failure the caches and
             * sortedNodePtrs are cleared and isPrepared() returns false.
             *
             * @param numChannels  Number of audio channels.
             * @param numFrames    Block size in frames.
             * @param sampleRate   Sample rate in Hz.
             * @return             void on success.
             * @return             GraphError::CycleDetected if a non-feedback cycle exists.
             */
            CASPI_NO_DISCARD expected<void, GraphError>
            prepare (std::size_t numChannels, std::size_t numFrames, double sampleRate) CASPI_ALLOCATING
            {
                graphPrepared = false;

                blockChannels   = numChannels;
                blockFrames     = numFrames;
                blockSampleRate = sampleRate;

                auto sortResult = topologicalSort();
                if (sortResult.has_error())
                {
                    return make_unexpected (sortResult.error());
                }

                sortedNodePtrs.clear();
                sortedNodePtrs.reserve (sortedOrder.size());

                for (NodeId id : sortedOrder)
                {
                    auto nodeIt = nodes.find (id);
                    if (nodeIt != nodes.end())
                    {
                        nodeIt->second->prepareToRender (numChannels, numFrames, sampleRate);
                        sortedNodePtrs.push_back (nodeIt->second.get());
                    }
                }

                cachedAudioLinks.clear();
                cachedControlLinks.clear();

                for (const auto& conn : connections)
                {
                    auto srcIt = nodes.find (conn.sourceNode);
                    if (srcIt == nodes.end())
                    {
                        continue;
                    }

                    if (conn.connectionType == ConnectionType::Audio)
                    {
                        const BufferType* buf = srcIt->second->getOutputBuffer (conn.sourcePort);
                        if (buf != nullptr)
                        {
                            CachedAudioLink link;
                            link.destinationNode = conn.destinationNode;
                            link.destinationPort = conn.destinationPort;
                            link.buffer          = buf;
                            cachedAudioLinks.push_back (link);
                        }
                    }
                    else
                    {
                        const FloatType* ptr = srcIt->second->getControlValuePtr (conn.sourcePort);
                        if (ptr != nullptr)
                        {
                            CachedControlLink link;
                            link.destinationNode = conn.destinationNode;
                            link.destinationPort = conn.destinationPort;
                            link.valuePtr        = ptr;
                            cachedControlLinks.push_back (link);
                        }
                    }
                }

                graphPrepared = true;
                return {};
            }

            /*==================================================================
             * Audio thread — noexcept, no allocation
             *================================================================*/

            /**
             * @brief Process one block through all nodes in topological order.
             *
             * Constructs an AudioContext pre-reserved for both audio and control
             * caches (no allocation), populates it, then dispatches to each node
             * via sortedNodePtrs (no map lookup). Real-time safe: noexcept, no
             * allocation, no system calls.
             *
             * Asserts isPrepared(). Call prepare() before the first process() and
             * after any topology change.
             */
            void process() noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (graphPrepared, "AudioGraph::process() called without prepare()");

                AudioContext<FloatType> ctx (blockChannels,
                                             blockFrames,
                                             blockSampleRate,
                                             cachedAudioLinks.size(),
                                             cachedControlLinks.size());

                for (const auto& link : cachedAudioLinks)
                {
                    ctx.addAudioInput (link.destinationNode, link.destinationPort, link.buffer);
                }

                for (const auto& link : cachedControlLinks)
                {
                    ctx.addControlInput (link.destinationNode, link.destinationPort, link.valuePtr);
                }

                for (NodeType_t* node : sortedNodePtrs)
                {
                    node->process (ctx);
                }
            }

            /*==================================================================
             * Observers
             *================================================================*/

            /** @brief Number of nodes currently in the graph. */
            CASPI_NO_DISCARD std::size_t getNumNodes() const noexcept
            {
                return nodes.size();
            }

            /** @brief Number of connections currently in the graph (all types combined). */
            CASPI_NO_DISCARD std::size_t getNumConnections() const noexcept
            {
                return connections.size();
            }

            /**
             * @brief True if prepare() has been called since the last topology change.
             *
             * process() asserts this. Reset by addNode(), emplace(), removeNode(),
             * connect(), disconnect(), and at the start of prepare() itself.
             */
            CASPI_NO_DISCARD bool isPrepared() const noexcept
            {
                return graphPrepared;
            }

            /**
             * @brief The topological execution order produced by the last prepare() call.
             *
             * Empty if prepare() has not been called or failed.
             *
             * @return Const reference to the sorted NodeId vector.
             */
            CASPI_NO_DISCARD const std::vector<NodeId>& getSortedOrder() const noexcept
            {
                return sortedOrder;
            }

            /**
             * @brief Return a raw non-owning pointer to the node with the given ID, or nullptr.
             *
             * Not for audio-thread use. Invalidated by removeNode() or graph destruction.
             *
             * @param id  NodeId to look up.
             * @return    Raw pointer to the node, or nullptr.
             */
            CASPI_NO_DISCARD NodeType_t* getNode (NodeId id) const noexcept
            {
                auto it = nodes.find (id);
                return (it != nodes.end()) ? it->second.get() : nullptr;
            }

            /**
             * @brief Type-safe node accessor via dynamic_cast.
             *
             * Requires RTTI. Returns nullptr if the node does not exist or the cast fails.
             * Not for audio-thread use. Invalidated by removeNode() or graph destruction.
             *
             * @tparam NodeT  Concrete node type to cast to.
             * @param  id     NodeId to look up.
             * @return        Pointer to NodeT, or nullptr.
             */
            template <typename NodeT>
            CASPI_NO_DISCARD NodeT* getNodeAs (NodeId id) const noexcept
            {
                return dynamic_cast<NodeT*> (getNode (id));
            }

        private:
            /*==================================================================
             * connectImpl — shared implementation for all connect() overloads
             *================================================================*/

            CASPI_NO_DISCARD expected<void, GraphError>
            connectImpl (NodeId srcNode,
                         std::size_t srcPort,
                         NodeId dstNode,
                         std::size_t dstPort,
                         ConnectionType connectionType,
                         bool isFeedback) CASPI_ALLOCATING
            {
                auto srcIt = nodes.find (srcNode);
                auto dstIt = nodes.find (dstNode);

                if (srcIt == nodes.end() || dstIt == nodes.end())
                {
                    return make_unexpected (GraphError::InvalidNodeId);
                }

                if (srcPort >= srcIt->second->getNumOutputPorts())
                {
                    return make_unexpected (GraphError::InvalidPort);
                }

                if (dstPort >= dstIt->second->getNumInputPorts())
                {
                    return make_unexpected (GraphError::InvalidPort);
                }

                const NodeType srcNodeType = srcIt->second->getType();
                if (connectionType == ConnectionType::Audio && srcNodeType != NodeType::Audio)
                {
                    return make_unexpected (GraphError::TypeMismatch);
                }
                if (connectionType == ConnectionType::Control && srcNodeType != NodeType::Control)
                {
                    return make_unexpected (GraphError::TypeMismatch);
                }

                for (const auto& c : connections)
                {
                    if (c.sourceNode      == srcNode
                        && c.sourcePort      == srcPort
                        && c.destinationNode == dstNode
                        && c.destinationPort == dstPort
                        && c.connectionType  == connectionType
                        && c.isFeedback      == isFeedback)
                    {
                        return make_unexpected (GraphError::DuplicateConnection);
                    }
                }

                Connection conn;
                conn.sourceNode      = srcNode;
                conn.sourcePort      = srcPort;
                conn.destinationNode = dstNode;
                conn.destinationPort = dstPort;
                conn.connectionType  = connectionType;
                conn.isFeedback      = isFeedback;
                connections.push_back (conn);
                graphPrepared = false;
                return {};
            }

            /*==================================================================
             * Topological sort — Kahn's algorithm, non-feedback edges only
             *
             * Writes the sorted NodeId sequence into sortedOrder.
             * Applies the feedback ordering pass after BFS.
             *
             * Complexity: O((V + E) log V) due to std::map.
             * Feedback pass: O(F^2 * N) worst case. Setup path only.
             *================================================================*/

            CASPI_NO_DISCARD expected<void, GraphError> topologicalSort() CASPI_ALLOCATING
            {
                sortedOrder.clear();
                sortedOrder.reserve (nodes.size());

                std::map<NodeId, int> inDegree;
                std::map<NodeId, std::vector<NodeId>> adjacency;

                for (const auto& kv : nodes)
                {
                    inDegree[kv.first]  = 0;
                    adjacency[kv.first] = std::vector<NodeId>();
                }

                for (const auto& conn : connections)
                {
                    if (! conn.isFeedback)
                    {
                        adjacency[conn.sourceNode].push_back (conn.destinationNode);
                        inDegree[conn.destinationNode]++;
                    }
                }

                std::queue<NodeId> q;
                for (const auto& kv : inDegree)
                {
                    if (kv.second == 0)
                    {
                        q.push (kv.first);
                    }
                }

                while (! q.empty())
                {
                    const NodeId current = q.front();
                    q.pop();
                    sortedOrder.push_back (current);

                    auto adjIt = adjacency.find (current);
                    if (adjIt != adjacency.end())
                    {
                        for (NodeId neighbour : adjIt->second)
                        {
                            auto degIt = inDegree.find (neighbour);
                            if (degIt != inDegree.end())
                            {
                                if (--degIt->second == 0)
                                {
                                    q.push (neighbour);
                                }
                            }
                        }
                    }
                }

                if (sortedOrder.size() != nodes.size())
                {
                    sortedOrder.clear();
                    return make_unexpected (GraphError::CycleDetected);
                }

                /*
                 * Feedback ordering pass.
                 *
                 * For each feedback connection (feedSrc -> feedDst), feedDst must
                 * appear BEFORE feedSrc so that feedDst reads feedSrc's previous-block
                 * buffer. For each violation, move feedSrc to immediately after feedDst.
                 * Repeat until stable.
                 *
                 * Complexity: O(F * N) per outer iteration, O(F^2 * N) worst case.
                 * Setup-path only.
                 */
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (const auto& conn : connections)
                    {
                        if (! conn.isFeedback)
                        {
                            continue;
                        }

                        const NodeId feedSrc = conn.sourceNode;
                        const NodeId feedDst = conn.destinationNode;

                        auto itSrc = std::find (sortedOrder.begin(), sortedOrder.end(), feedSrc);
                        auto itDst = std::find (sortedOrder.begin(), sortedOrder.end(), feedDst);

                        if (itSrc == sortedOrder.end() || itDst == sortedOrder.end())
                        {
                            continue;
                        }

                        if (itSrc <= itDst)
                        {
                            const NodeId srcId = *itSrc;
                            sortedOrder.erase (itSrc);
                            auto newDst = std::find (sortedOrder.begin(), sortedOrder.end(), feedDst);
                            sortedOrder.insert (newDst + 1, srcId);
                            changed = true;
                        }
                    }
                }

                return {};
            }

            /*==================================================================
             * Data members
             *================================================================*/

            /**
             * @brief Node ownership map: NodeId -> unique_ptr<NodeBase>.
             *
             * std::map provides pointer stability required by the cached link
             * arrays (which store raw pointers into node output buffers).
             * Not accessed on the audio thread (see sortedNodePtrs).
             */
            std::map<NodeId, std::unique_ptr<NodeType_t>> nodes;

            /** @brief All connections in insertion order. */
            std::vector<Connection> connections;

            /**
             * @brief Topological execution order produced by prepare().
             *
             * Contains NodeIds in the order process() calls them.
             * Empty if prepare() has not been called or failed.
             */
            std::vector<NodeId> sortedOrder;

            /**
             * @brief Raw node pointers in topological order, built by prepare().
             *
             * Eliminates std::map lookups from the audio-thread hot path.
             * Non-owning; nodes are owned by the nodes map.
             */
            std::vector<NodeType_t*> sortedNodePtrs;

            /** @brief Cached audio link: maps (dstNode, dstPort) to a BufferType*. */
            struct CachedAudioLink
            {
                NodeId destinationNode;
                std::size_t destinationPort;
                const BufferType* buffer;
            };

            std::vector<CachedAudioLink> cachedAudioLinks;

            /** @brief Cached control link: maps (dstNode, dstPort) to a const FloatType*. */
            struct CachedControlLink
            {
                NodeId destinationNode;
                std::size_t destinationPort;
                const FloatType* valuePtr;
            };

            std::vector<CachedControlLink> cachedControlLinks;

            /** @brief Monotonically increasing ID counter. Never reset or reused. */
            NodeId nextId = 0;

            /**
             * @brief True after a successful prepare() call, false after any mutation.
             *
             * process() asserts this. Set false by addNode(), emplace(), removeNode(),
             * connect(), disconnect(), and at the start of prepare() itself.
             */
            bool graphPrepared = false;

            /** @brief Number of audio channels for the current prepare() session. */
            std::size_t blockChannels = 0;

            /** @brief Block size in frames for the current prepare() session. */
            std::size_t blockFrames = 0;

            /** @brief Sample rate in Hz for the current prepare() session. */
            double blockSampleRate = 0.0;
    };

} // namespace Graph
} // namespace CASPI

#endif // CASPI_GRAPH_H