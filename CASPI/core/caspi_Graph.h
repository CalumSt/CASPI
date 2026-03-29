#ifndef CASPI_GRAPH_H
#define CASPI_GRAPH_H
/*************************************************************************
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
 * @details
 * ### Overview
 *
 * AudioGraph<FloatType> owns a set of NodeBase<FloatType> instances and a set
 * of Connections between their ports. On each call to process(), nodes are
 * visited in topological order and each node's process() is called once.
 *
 * ### Thread safety model
 *
 *   addNode / removeNode / connect / disconnect  - setup thread only
 *   prepare()                                    - setup thread only, may allocate
 *   process()                                    - audio thread only, noexcept, no allocation
 *
 * Mutations after prepare() require a fresh prepare() before the next process().
 * The graph asserts isPrepared() at the entry of process().
 *
 * For dynamic topology changes on a live audio thread, enqueue mutations
 * and apply them between blocks (stop -> mutate -> prepare -> restart).
 *
 * ### Connection types
 *
 * Every connection has a ConnectionType:
 *
 *   ConnectionType::Audio
 *     Source must be NodeType::Audio. Carries a BufferType* resolved at
 *     prepare() time. Destination reads via AudioContext::getAudioInput().
 *
 *   ConnectionType::Control
 *     Source must be NodeType::Control. Carries a const FloatType* resolved
 *     at prepare() time (pointer into ControlNode::controlOutputs[srcPort]).
 *     Destination reads via AudioContext::getControlInput(). O(1) deref,
 *     no virtual call, no scan.
 *
 * AudioGraph::connect() validates ConnectionType against the source node's
 * NodeType and returns GraphError::TypeMismatch on disagreement.
 *
 * ### isFeedback and connection identity
 *
 * isFeedback is part of the connection key. Two connections between the same
 * (srcNode, srcPort, dstNode, dstPort) may coexist if one is normal and the
 * other is feedback. disconnect() requires the isFeedback flag to identify
 * which connection to remove.
 *
 * A feedback Audio connection reads the source node's previous-block buffer
 * (one block of latency). A feedback Control connection reads the source
 * node's previous-block control value by the same mechanism.
 *
 * ### Topological sort
 *
 * Kahn's algorithm (BFS) over non-feedback edges. O(V + E).
 * Nodes with no non-feedback predecessors are roots.
 *
 * If a non-feedback cycle is detected, prepare() returns GraphError::CycleDetected.
 * Mark the back-edge as isFeedback=true in connect() to break the cycle.
 *
 * ### Real-time path
 *
 * process() is noexcept and performs no heap allocation. All pointer resolution
 * is done at prepare() time and cached in:
 *   - cachedAudioLinks:   flat array of (dstNode, dstPort, BufferType*).
 *   - cachedControlLinks: flat array of (dstNode, dstPort, const FloatType*).
 *   - sortedNodePtrs:     flat array of raw NodeBase* in execution order.
 *
 * AudioContext is constructed each process() call with reserved capacity for
 * both caches — no allocation occurs in the push_back loops.
 *
 * ### Connecting existing Producers / Processors
 *
 * Use composition via an adapter:
 * @code
 *   template <typename ProducerT, typename FloatType = double>
 *   class ProducerNodeAdapter
 *       : public AudioNode<ProducerNodeAdapter<ProducerT, FloatType>, FloatType>
 *   {
 *   public:
 *       ProducerT producer;
 *       void onPrepare(size_t, size_t, double sr) { ... }
 *       void processImpl(AudioContext<FloatType>&) noexcept {
 *           producer.render(this->outputBuffer);
 *       }
 *   };
 * @endcode
 ************************************************************************/

#include "caspi_Expected.h"
#include "core/caspi_Node.h"

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace CASPI
{
    namespace Graph
    {

        /*======================================================================
         * ConnectionType
         *====================================================================*/

        /**
         * @brief Discriminates audio-buffer connections from control-scalar connections.
         *
         * AudioGraph::connect() validates this against the source node's NodeType:
         * - ConnectionType::Audio   requires the source to be NodeType::Audio.
         * - ConnectionType::Control requires the source to be NodeType::Control.
         *
         * Mismatches return GraphError::TypeMismatch from connect().
         */
        enum class ConnectionType
        {
            Audio, ///< Carries a BufferType*; read via AudioContext::getAudioInput().
            Control ///< Carries a const FloatType*; read via AudioContext::getControlInput().
        };

        /*======================================================================
         * Connection
         *====================================================================*/

        /**
         * @brief Directed edge between two node ports.
         *
         * ### Connection identity
         * The full key is (sourceNode, sourcePort, destinationNode, destinationPort,
         * connectionType, isFeedback). All six fields must match for a connection to
         * be considered a duplicate. In particular, a normal and a feedback connection
         * between the same port pair are distinct and may coexist. disconnect() requires
         * all six fields (connectionType and isFeedback included) to target the correct edge.
         *
         * ### isFeedback semantics
         * - false (default): normal dataflow edge; included in topological sort.
         *   Destination reads source's current-block output.
         * - true: excluded from topological sort; destination reads source's
         *   previous-block output (one block of latency). Use to break cycles.
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
                 * @brief If true, excluded from the topological sort.
                 *
                 * The destination reads the source's previous-block output.
                 * One block of latency. Use to break cycles.
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
            InvalidNodeId, ///< NodeId does not exist in this graph.
            InvalidPort, ///< Port index out of range for the specified node.
            DuplicateConnection, ///< Identical connection (all six key fields) already present.
            ConnectionNotFound, ///< disconnect() target does not exist.
            NodeNotFound, ///< removeNode() target does not exist.
            CycleDetected, ///< Non-feedback cycle; mark the back-edge isFeedback=true.
            TypeMismatch, ///< ConnectionType does not match the source node's NodeType.
            NotPrepared, ///< process() called before prepare() or after a mutation.
            NullNode ///< addNode() called with nullptr.
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
         * ### Audio inputs
         * Populated from ConnectionType::Audio connections. Queried via
         * getAudioInput(dstNode, dstPort). Returns nullptr if no audio connection
         * exists for that port. Linear scan: O(numAudioConnections).
         *
         * ### Control inputs
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
                 * For feedback Audio connections the buffer contains the previous block's
                 * output (not yet overwritten in the current block).
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
                        if (entry.destinationNode == destinationNode && entry.destinationPort == destinationPort)
                            return entry.buffer;
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
                 * For feedback Control connections the value is from the previous block.
                 *
                 * Linear scan to find the entry: O(numControlConnections), then a pointer
                 * dereference. Real-time safe.
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
                        if (entry.destinationNode == destinationNode && entry.destinationPort == destinationPort)
                            return *entry.valuePtr;
                    }
                    return FloatType (0);
                }

                /*------------------------------------------------------------------
                 * Block geometry accessors
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Number of audio channels for this block.
                 * @return Channel count.
                 */
                CASPI_NO_DISCARD std::size_t getNumChannels() const noexcept
                {
                    return numChannels;
                }

                /**
                 * @brief Block size in frames.
                 * @return Frame count.
                 */
                CASPI_NO_DISCARD std::size_t getNumFrames() const noexcept
                {
                    return numFrames;
                }

                /**
                 * @brief Sample rate in Hz.
                 * @return Sample rate.
                 */
                CASPI_NO_DISCARD double getSampleRate() const noexcept
                {
                    return sampleRate;
                }

            private:
                /** @brief AudioGraph builds and populates the context each process() call. */
                template <typename F>
                friend class AudioGraph;

                /**
                 * @brief Resolved audio input: maps (dstNode, dstPort) to an upstream BufferType*.
                 *
                 * Built from ConnectionType::Audio entries in cachedAudioLinks.
                 */
                struct ResolvedAudioInput
                {
                        /** @brief Destination node that reads this buffer. */
                        NodeId destinationNode;

                        /** @brief Zero-based destination input port index. */
                        std::size_t destinationPort;

                        /** @brief Non-owning pointer into the upstream node's outputBuffer. */
                        const BufferType* buffer;
                };

                /**
                 * @brief Resolved control input: maps (dstNode, dstPort) to a const FloatType*.
                 *
                 * Built from ConnectionType::Control entries in cachedControlLinks.
                 * The pointer points into ControlNode::controlOutputs[srcPort] and is
                 * stable for the lifetime of the prepare() session.
                 */
                struct ResolvedControlInput
                {
                        /** @brief Destination node that reads this control value. */
                        NodeId destinationNode;

                        /** @brief Zero-based destination input port index. */
                        std::size_t destinationPort;

                        /** @brief Stable pointer into the upstream ControlNode's output value. */
                        const FloatType* valuePtr;
                };

                /**
                 * @brief Construct an AudioContext for a block with the given geometry.
                 *
                 * Pre-reserves both input vectors to prevent heap allocation during
                 * the subsequent addAudioInput() / addControlInput() calls.
                 *
                 * @param numChannels         Number of audio channels.
                 * @param numFrames           Block size in frames.
                 * @param sampleRate          Sample rate in Hz.
                 * @param numAudioLinks       Capacity hint for audio resolved inputs.
                 * @param numControlLinks     Capacity hint for control resolved inputs.
                 */
                AudioContext (std::size_t numChannels,
                              std::size_t numFrames,
                              double sampleRate,
                              std::size_t numAudioLinks,
                              std::size_t numControlLinks)
                    : numChannels (numChannels)
                    , numFrames (numFrames)
                    , sampleRate (sampleRate)
                {
                    resolvedAudioInputs.reserve (numAudioLinks);
                    resolvedControlInputs.reserve (numControlLinks);
                }

                /**
                 * @brief Register a resolved audio (destination, port) -> buffer mapping.
                 *
                 * Does not allocate if the vector was reserved with sufficient capacity.
                 *
                 * @param dst     Destination NodeId.
                 * @param dstPort Destination port index.
                 * @param buf     Non-owning pointer to the upstream audio buffer.
                 */
                void addAudioInput (NodeId dst, std::size_t dstPort, const BufferType* buf)
                {
                    resolvedAudioInputs.push_back ({ dst, dstPort, buf });
                }

                /**
                 * @brief Register a resolved control (destination, port) -> value pointer mapping.
                 *
                 * Does not allocate if the vector was reserved with sufficient capacity.
                 *
                 * @param dst      Destination NodeId.
                 * @param dstPort  Destination port index.
                 * @param valuePtr Stable pointer into the upstream ControlNode's output.
                 */
                void addControlInput (NodeId dst, std::size_t dstPort, const FloatType* valuePtr)
                {
                    resolvedControlInputs.push_back ({ dst, dstPort, valuePtr });
                }

                /** @brief Resolved audio buffer mappings. Pre-reserved; no allocation during process(). */
                std::vector<ResolvedAudioInput> resolvedAudioInputs;

                /** @brief Resolved control pointer mappings. Pre-reserved; no allocation during process(). */
                std::vector<ResolvedControlInput> resolvedControlInputs;

                /** @brief Number of audio channels for this block. */
                std::size_t numChannels;

                /** @brief Block size in frames. */
                std::size_t numFrames;

                /** @brief Sample rate in Hz. */
                double sampleRate;
        };

        /*======================================================================
         * AudioGraph<FloatType>
         *====================================================================*/

        /**
         * @brief Directed acyclic graph of NodeBase<FloatType> instances.
         *
         * Owns all nodes. Evaluates them in topological order each process() call.
         * Not copyable.
         *
         * @tparam FloatType  Floating-point sample type (float, double, long double). Default: double.
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

                /*==================================================================
                 * Setup API — setup thread only; not real-time safe.
                 *================================================================*/

                /**
                 * @brief Add a node to the graph, taking ownership.
                 *
                 * Assigns a NodeId and returns it. NodeIds are monotonically increasing
                 * from 0 and are never reused. Invalidates the current prepare() result.
                 *
                 * @param node  Non-null unique_ptr to any NodeBase<FloatType> subclass.
                 * @return      Assigned NodeId on success.
                 * @return      GraphError::NullNode if node is nullptr.
                 */
                CASPI_NO_DISCARD expected<NodeId, GraphError> addNode (std::unique_ptr<NodeType_t> node)
                    CASPI_ALLOCATING
                {
                    if (! node) return make_unexpected<NodeId> (GraphError::NullNode);

                    const NodeId id = nextId++;
                    node->assignId (id);
                    nodes[id]     = std::move (node);
                    graphPrepared = false;
                    return make_expected<NodeId, GraphError> (id);
                }

                /**
                 * @brief Remove a node and all connections involving it.
                 *
                 * The node is destroyed immediately. All connections referencing
                 * the removed NodeId are erased. Invalidates the current prepare().
                 *
                 * @param id  NodeId to remove.
                 * @return    void on success.
                 * @return    GraphError::NodeNotFound if id does not exist.
                 */
                CASPI_NO_DISCARD expected<void, GraphError> removeNode (NodeId id)
                {
                    auto it = nodes.find (id);
                    if (it == nodes.end()) return make_unexpected (GraphError::NodeNotFound);

                    connections.erase (std::remove_if (connections.begin(),
                                                       connections.end(),
                                                       [id] (const Connection& c)
                                                       { return c.sourceNode == id || c.destinationNode == id; }),
                                       connections.end());

                    nodes.erase (it);
                    graphPrepared = false;
                    return {};
                }

                /**
                 * @brief Add a typed, directed connection from a source output port to a
                 *        destination input port.
                 *
                 * ### Validation
                 * - srcNode and dstNode must exist.
                 * - srcPort < source node's output port count.
                 * - dstPort < destination node's input port count.
                 * - connectionType must match the source node's NodeType
                 *   (Audio source -> ConnectionType::Audio,
                 *    Control source -> ConnectionType::Control).
                 * - The full six-field key (srcNode, srcPort, dstNode, dstPort,
                 *   connectionType, isFeedback) must be unique. A normal and a
                 *   feedback connection between the same port pair are distinct
                 *   and may coexist.
                 *
                 * Cycle detection is deferred to prepare(). connect() does not walk
                 * the graph. Non-feedback cycles detected at prepare() return
                 * GraphError::CycleDetected.
                 *
                 * Invalidates the current prepare() result.
                 *
                 * @param srcNode        Source NodeId.
                 * @param srcPort        Zero-based source output port index.
                 * @param dstNode        Destination NodeId.
                 * @param dstPort        Zero-based destination input port index.
                 * @param connectionType Audio or Control routing type.
                 * @param isFeedback     If true, excluded from sort; reads previous block.
                 * @return               void on success.
                 * @return               GraphError::InvalidNodeId   — unknown node.
                 * @return               GraphError::InvalidPort     — port out of range.
                 * @return               GraphError::TypeMismatch    — connectionType vs NodeType.
                 * @return               GraphError::DuplicateConnection — key already present.
                 */
                CASPI_NO_DISCARD expected<void, GraphError> connect (NodeId srcNode,
                                                                     std::size_t srcPort,
                                                                     NodeId dstNode,
                                                                     std::size_t dstPort,
                                                                     ConnectionType connectionType,
                                                                     bool isFeedback = false) CASPI_ALLOCATING
                {
                    auto srcIt = nodes.find (srcNode);
                    auto dstIt = nodes.find (dstNode);

                    if (srcIt == nodes.end() || dstIt == nodes.end())
                        return make_unexpected (GraphError::InvalidNodeId);

                    if (srcPort >= srcIt->second->getNumOutputPorts()) return make_unexpected (GraphError::InvalidPort);

                    if (dstPort >= dstIt->second->getNumInputPorts()) return make_unexpected (GraphError::InvalidPort);

                    // Validate ConnectionType matches source NodeType.
                    const NodeType srcNodeType = srcIt->second->getType();
                    if (connectionType == ConnectionType::Audio && srcNodeType != NodeType::Audio)
                        return make_unexpected (GraphError::TypeMismatch);
                    if (connectionType == ConnectionType::Control && srcNodeType != NodeType::Control)
                        return make_unexpected (GraphError::TypeMismatch);

                    // All six fields form the connection key.
                    for (const auto& c : connections)
                    {
                        if (c.sourceNode == srcNode && c.sourcePort == srcPort && c.destinationNode == dstNode
                            && c.destinationPort == dstPort && c.connectionType == connectionType
                            && c.isFeedback == isFeedback)
                            return make_unexpected (GraphError::DuplicateConnection);
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

                /**
                 * @brief Remove a specific connection identified by its full six-field key.
                 *
                 * All six fields (srcNode, srcPort, dstNode, dstPort, connectionType,
                 * isFeedback) must match to locate the connection. Invalidates prepare().
                 *
                 * @param srcNode        Source NodeId.
                 * @param srcPort        Zero-based source output port index.
                 * @param dstNode        Destination NodeId.
                 * @param dstPort        Zero-based destination input port index.
                 * @param connectionType Audio or Control routing type of the target connection.
                 * @param isFeedback     Feedback flag of the target connection.
                 * @return               void on success.
                 * @return               GraphError::ConnectionNotFound if no matching connection exists.
                 */
                CASPI_NO_DISCARD expected<void, GraphError> disconnect (NodeId srcNode,
                                                                        std::size_t srcPort,
                                                                        NodeId dstNode,
                                                                        std::size_t dstPort,
                                                                        ConnectionType connectionType,
                                                                        bool isFeedback = false)
                {
                    auto it = std::find_if (connections.begin(),
                                            connections.end(),
                                            [&] (const Connection& c)
                                            {
                                                return c.sourceNode == srcNode && c.sourcePort == srcPort
                                                       && c.destinationNode == dstNode && c.destinationPort == dstPort
                                                       && c.connectionType == connectionType
                                                       && c.isFeedback == isFeedback;
                                            });

                    if (it == connections.end()) return make_unexpected (GraphError::ConnectionNotFound);

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
                 * Resolves Audio connections -> BufferType* cache (cachedAudioLinks).
                 * Resolves Control connections -> const FloatType* cache (cachedControlLinks).
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
                CASPI_NO_DISCARD expected<void, GraphError> prepare (std::size_t numChannels,
                                                                     std::size_t numFrames,
                                                                     double sampleRate) CASPI_ALLOCATING
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

                    // Build sorted raw-pointer array — eliminates map lookups on audio thread.
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

                    // Resolve connections into typed caches.
                    cachedAudioLinks.clear();
                    cachedControlLinks.clear();

                    for (const auto& conn : connections)
                    {
                        auto srcIt = nodes.find (conn.sourceNode);
                        if (srcIt == nodes.end()) continue;

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
                        else // ConnectionType::Control
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
                        ctx.addAudioInput (link.destinationNode, link.destinationPort, link.buffer);

                    for (const auto& link : cachedControlLinks)
                        ctx.addControlInput (link.destinationNode, link.destinationPort, link.valuePtr);

                    for (NodeType_t* node : sortedNodePtrs)
                        node->process (ctx);
                }

                /*==================================================================
                 * Observers
                 *================================================================*/

                /**
                 * @brief Number of nodes currently in the graph.
                 * @return Node count.
                 */
                CASPI_NO_DISCARD std::size_t getNumNodes() const noexcept
                {
                    return nodes.size();
                }

                /**
                 * @brief Number of connections currently in the graph (all types combined).
                 * @return Connection count.
                 */
                CASPI_NO_DISCARD std::size_t getNumConnections() const noexcept
                {
                    return connections.size();
                }

                /**
                 * @brief True if prepare() has been called since the last topology change.
                 *
                 * process() asserts this. Reset by addNode(), removeNode(), connect(),
                 * disconnect(), and at the start of prepare() itself.
                 *
                 * @return Prepared state.
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
                 * Topological sort — Kahn's algorithm, non-feedback edges only.
                 *
                 * Writes the sorted NodeId sequence into sortedOrder.
                 * Applies the feedback ordering pass after BFS.
                 *
                 * Complexity: O((V + E) log V) due to std::map.
                 * Feedback pass: O(F^2 * N) worst case. Setup path only.
                 *================================================================*/

                /**
                 * @brief Run Kahn's BFS sort and the feedback ordering pass.
                 *
                 * @return void on success.
                 * @return GraphError::CycleDetected if a non-feedback cycle exists.
                 */
                CASPI_NO_DISCARD expected<void, GraphError> topologicalSort() CASPI_ALLOCATING
                {
                    sortedOrder.clear();
                    sortedOrder.reserve (nodes.size());

                    std::map<NodeId, int> inDegree;
                    std::map<NodeId, std::vector<NodeId>> adjacency;

                    for (const auto& kv : nodes)
                    {
                        inDegree[kv.first]  = 0;
                        adjacency[kv.first] = {};
                    }

                    for (const auto& conn : connections)
                    {
                        if (! conn.isFeedback)
                        {
                            adjacency[conn.sourceNode].push_back (conn.destinationNode);
                            inDegree[conn.destinationNode]++;
                        }
                    }

                    // Seed queue with root nodes. std::map gives ascending NodeId order.
                    std::queue<NodeId> q;
                    for (const auto& kv : inDegree)
                        if (kv.second == 0) q.push (kv.first);

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
                                    if (--degIt->second == 0) q.push (neighbour);
                            }
                        }
                    }

                    if (sortedOrder.size() != nodes.size())
                    {
                        sortedOrder.clear();
                        return make_unexpected (GraphError::CycleDetected);
                    }

                    // Feedback ordering pass.
                    //
                    // For each feedback connection (feedSrc -> feedDst), feedDst must
                    // appear BEFORE feedSrc so that feedDst reads feedSrc's previous-block
                    // buffer. For each violation, move feedSrc to immediately after feedDst.
                    // Repeat until stable.
                    //
                    // Complexity: O(F * N) per outer iteration, O(F^2 * N) worst case.
                    // Setup-path only; not a real-time concern.
                    bool changed = true;
                    while (changed)
                    {
                        changed = false;
                        for (const auto& conn : connections)
                        {
                            if (! conn.isFeedback) continue;

                            const NodeId feedSrc = conn.sourceNode;
                            const NodeId feedDst = conn.destinationNode;

                            auto itSrc = std::find (sortedOrder.begin(), sortedOrder.end(), feedSrc);
                            auto itDst = std::find (sortedOrder.begin(), sortedOrder.end(), feedDst);

                            if (itSrc == sortedOrder.end() || itDst == sortedOrder.end()) continue;

                            if (itSrc <= itDst)
                            {
                                // Violation: move feedSrc to just after feedDst.
                                // Re-find feedDst after erasing feedSrc (iterator invalidation).
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

                /**
                 * @brief All connections in insertion order.
                 *
                 * Insertion order determines Kahn's BFS tie-breaking for equal-priority nodes.
                 */
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
                 * Non-owning; nodes are owned by the nodes map. Invalidated by
                 * any structural change to the nodes map.
                 */
                std::vector<NodeType_t*> sortedNodePtrs;

                /**
                 * @brief Cached audio link: maps (dstNode, dstPort) to a BufferType*.
                 *
                 * Built from ConnectionType::Audio entries at prepare() time.
                 * Buffer pointers are stable for the lifetime of the prepare() session.
                 */
                struct CachedAudioLink
                {
                        /** @brief Destination NodeId. */
                        NodeId destinationNode;

                        /** @brief Zero-based destination input port index. */
                        std::size_t destinationPort;

                        /** @brief Non-owning pointer into the upstream AudioNode's outputBuffer. */
                        const BufferType* buffer;
                };

                /** @brief Flat cache of resolved audio input links. Built by prepare(). */
                std::vector<CachedAudioLink> cachedAudioLinks;

                /**
                 * @brief Cached control link: maps (dstNode, dstPort) to a const FloatType*.
                 *
                 * Built from ConnectionType::Control entries at prepare() time.
                 * Pointers are into ControlNode::controlOutputs[] and are stable for
                 * the lifetime of the prepare() session (controlOutputs never resizes).
                 */
                struct CachedControlLink
                {
                        /** @brief Destination NodeId. */
                        NodeId destinationNode;

                        /** @brief Zero-based destination input port index. */
                        std::size_t destinationPort;

                        /** @brief Stable pointer into the upstream ControlNode's output value. */
                        const FloatType* valuePtr;
                };

                /** @brief Flat cache of resolved control input links. Built by prepare(). */
                std::vector<CachedControlLink> cachedControlLinks;

                /** @brief Monotonically increasing ID counter. Never reset or reused. */
                NodeId nextId = 0;

                /**
                 * @brief True after a successful prepare() call, false after any mutation.
                 *
                 * process() asserts this. Set false by addNode(), removeNode(), connect(),
                 * disconnect(), and at the start of prepare() itself.
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