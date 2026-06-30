#ifndef CASPI_NODE_H
#define CASPI_NODE_H
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
 * @file   core/caspi_Node.h
 * @author CS Islay
 * @brief  Base classes for all nodes in an AudioGraph.
 *
 * @details
 * Provides three base classes:
 *
 *   NodeBase<FloatType>
 *     Type-erased polymorphic base. AudioGraph holds these via unique_ptr.
 *     process() is the single virtual call per node per block.
 *
 *   AudioNode<Derived, FloatType>   : NodeBase<FloatType>
 *     CRTP base for audio-rate nodes (oscillators, filters, effects).
 *     Owns a ChannelMajorLayout output buffer.
 *     Hot path: process() [virtual, final] -> Derived::processImpl() [CRTP].
 *
 *   ControlNode<Derived, FloatType> : NodeBase<FloatType>
 *     CRTP base for control-rate nodes (envelopes, LFOs, mod matrices).
 *     Owns a flat vector of scalar output values.
 *     Hot path: process() [virtual, final] -> Derived::processImpl() [CRTP].
 *
 * ### CRTP contract for AudioNode<Derived, FloatType>
 *
 * Derived must provide:
 * @code
 *   void processImpl(AudioContext<FloatType>&) noexcept;
 * @endcode
 *
 * Derived may provide (optional):
 * @code
 *   void onPrepare(size_t numChannels, size_t numFrames, double sampleRate);
 * @endcode
 *
 * ### Output buffer semantics
 *
 * AudioNode::prepareToRender() clears outputBuffer to silence.
 * AudioNode::process() does NOT clear it before calling processImpl().
 * processImpl() is responsible for writing its output.
 * This allows feedback connections to read a previous block's output
 * (the pointer remains valid, content is from the last block).
 *
 * ### Multi-output nodes
 *
 * Override getOutputBuffer(port) in Derived to expose additional output
 * buffers. Default implementation returns &outputBuffer for port==0, nullptr
 * otherwise.
 *
 * ### Thread safety
 * - prepareToRender() : setup thread only. May allocate.
 * - process()         : audio thread only. noexcept. No allocation.
 * - All other methods : audio thread only unless noted.
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "caspi_AudioBuffer.h"

#include <cstddef>
#include <type_traits>
#include <vector>

namespace CASPI
{
    namespace Graph
    {

        /** @brief Unique identifier assigned by AudioGraph::addNode(). */
        using NodeId = std::size_t;

        /** @brief Sentinel value indicating an unassigned node. */
        static constexpr NodeId INVALID_NODE_ID = static_cast<NodeId> (-1);

        enum class NodeType
        {
            Audio, ///< Produces or transforms audio buffers (AudioNode).
            Control ///< Produces control-rate scalar signals (ControlNode).
        };

        template <typename FloatType>
        class AudioContext;

        template <typename FloatType>
        class AudioGraph;

        /*======================================================================
         * NodeBase<FloatType>
         *
         * Type-erased base class for all graph nodes.
         * AudioGraph holds std::unique_ptr<NodeBase<FloatType>>.
         * IDs are assigned by AudioGraph::addNode() via the friend relationship.
         *=======================================                  =============================*/
        template <typename FloatType>
        class NodeBase
        {
            public:
                using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;

                virtual ~NodeBase() noexcept = default;

                NodeBase (const NodeBase&)            = delete;
                NodeBase& operator= (const NodeBase&) = delete;
                NodeBase (NodeBase&&)                 = default;
                NodeBase& operator= (NodeBase&&)      = default;

                /**
                 * @brief Set the sample rate and notify derived classes.
                 * Stores the rate then calls onSampleRateChanged().
                 * Called automatically by prepareToRender().
                 * @param newRate  Sample rate in Hz. Must be > 0.
                 */
                virtual void setSampleRate (FloatType newRate)
                {
                    CASPI_ASSERT (newRate > FloatType (0), "Sample rate must be positive.");
                    sampleRate = newRate;
                    onSampleRateChanged (newRate);
                }

                /** @brief Return the current sample rate in Hz. */
                CASPI_NO_DISCARD virtual FloatType getSampleRate() const noexcept
                {
                    return sampleRate;
                }

                /**
                 * @brief Called when the sample rate changes. Override to recompute
                 * coefficients, phase increments, etc. Default: does nothing.
                 * @param rate  New sample rate in Hz.
                 */
                virtual void onSampleRateChanged (FloatType rate)
                {
                    (void) rate;
                }

                /**
                 * @brief Allocate and initialise state for the given block geometry.
                 *
                 * Called by AudioGraph::prepare(). May allocate. Not real-time safe.
                 * Implementations must call markPrepared() on success.
                 *
                 * @param numChannels  Number of audio channels.
                 * @param numFrames    Block size in frames.
                 * @param sampleRate   Sample rate in Hz.
                 */
                virtual void prepareToRender (std::size_t numChannels, std::size_t numFrames, double sampleRate) = 0;

                /**
                 * @brief Process one block of audio or control data.
                 *
                 * Called by AudioGraph::process() in topological order.
                 * Must be real-time safe: no allocation, no locks, noexcept.
                 *
                 * @param context  AudioContext providing access to connected input buffers.
                 */
                virtual void process (AudioContext<FloatType>& context) noexcept CASPI_NON_BLOCKING = 0;

                /**
                 * @brief Return the output audio buffer for the given port.
                 *
                 * Default: nullptr (not an audio output node).
                 * AudioNode<Derived, FloatType> overrides to return &outputBuffer for port 0.
                 *
                 * @param port  Zero-based output port index.
                 * @return      Pointer to the buffer, or nullptr if port is unused.
                 */
                virtual const BufferType* getOutputBuffer (std::size_t port) const noexcept
                {
                    (void) port;
                    return nullptr;
                }

                /**
                 * @brief Return a scalar control output for the given port.
                 *
                 * Default: FloatType(0) (not a control output node).
                 * ControlNode<Derived, FloatType> overrides to return controlOutputs[port].
                 *
                 * @param port  Zero-based output port index.
                 * @return      Scalar control value, or 0 if port is unused.
                 */
                virtual FloatType getControlOutput (std::size_t port) const noexcept
                {
                    (void) port;
                    return FloatType (0);
                }

                /**
                 * @brief Return a stable pointer into the control output for the given port.
                 * ControlNode overrides to return &controlOutputs[port].
                 * AudioNode returns nullptr.
                 * @param port  Zero-based output port index.
                 */
                virtual const FloatType* getControlValuePtr (std::size_t port) const noexcept
                {
                    (void) port;
                    return nullptr;
                }

                /** @brief NodeId assigned by AudioGraph::addNode(). */
                CASPI_NO_DISCARD NodeId getId() const noexcept
                {
                    return nodeId;
                }

                /** @brief Audio or Control node classification. */
                CASPI_NO_DISCARD NodeType getType() const noexcept
                {
                    return nodeType;
                }

                /** @brief Number of audio/control input ports consumed by this node. */
                CASPI_NO_DISCARD std::size_t getNumInputPorts() const noexcept
                {
                    return inputPortCount;
                }

                /** @brief Number of audio/control output ports produced by this node. */
                CASPI_NO_DISCARD std::size_t getNumOutputPorts() const noexcept
                {
                    return outputPortCount;
                }

                /** @brief True after a successful prepareToRender() call. */
                CASPI_NO_DISCARD bool isPrepared() const noexcept
                {
                    return prepared;
                }

            protected:
                NodeBase (NodeType type, std::size_t numIn, std::size_t numOut) noexcept
                    : nodeId (INVALID_NODE_ID)
                    , nodeType (type)
                    , inputPortCount (numIn)
                    , outputPortCount (numOut)
                    , prepared (false)
                    , sampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>)
                {
                }

                void markPrepared() noexcept
                {
                    prepared = true;
                }
                void markUnprepared() noexcept
                {
                    prepared = false;
                }

            private:
                // AudioGraph assigns IDs at addNode() time.
                template <typename F>
                friend class AudioGraph;

                void assignId (NodeId id) noexcept
                {
                    nodeId = id;
                }

                NodeId nodeId;
                NodeType nodeType;
                std::size_t inputPortCount;
                std::size_t outputPortCount;
                bool prepared;
                FloatType sampleRate;
        };

        /*======================================================================
         * AudioNode<Derived, FloatType>
         *
         * CRTP base for audio-rate nodes.
         *
         * Dispatch chain (per block):
         *   AudioGraph::process()
         *     -> NodeBase::process()  [virtual, 1 call per node per block]
         *     -> AudioNode::process() [final override]
         *     -> Derived::processImpl() [CRTP, compile-time resolved]
         *
         * Derived must provide:
         *   void processImpl(AudioContext<FloatType>&) noexcept;
         *
         * Derived may provide (optional overrides of defaults):
         *   void onPrepare(size_t numChannels, size_t numFrames, double sampleRate);
         *
         * Output buffer:
         *   outputBuffer is sized in prepareToRender() and cleared to silence.
         *   process() does NOT clear it before processImpl(). processImpl() writes
         *   the entire buffer (or leaves stale data, which is only safe if nothing
         *   reads it). This enables feedback reads of the previous block.
         *====================================================================*/
        template <typename Derived, CASPI_FLOAT_TYPE FloatType = double>
        class AudioNode : public NodeBase<FloatType>
        {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
                CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                                     "AudioNode requires a floating-point type (float, double, long double)");
#endif

            public:
                using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;

                /*------------------------------------------------------------------
                 * NodeBase overrides
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Resize outputBuffer and call Derived::onPrepare().
                 *
                 * Called once per AudioGraph::prepare(). The buffer is cleared to silence
                 * so feedback connections read zeros on the first block.
                 */
                void prepareToRender (std::size_t numChannels, std::size_t numFrames, double newSampleRate) override
                {
                    auto result = outputBuffer.resize (numChannels, numFrames);
                    CASPI_ASSERT (result.has_value(), "AudioNode: outputBuffer resize failed (out of memory)");
                    outputBuffer.clear();
                    this->setSampleRate (static_cast<FloatType> (newSampleRate));
                    getDerived().onPrepare (numChannels, numFrames, newSampleRate);
                    this->markPrepared();
                }

                /**
                 * @brief Dispatch to Derived::processImpl() via CRTP.
                 *
                 * One virtual call per block per node. The processImpl() call is
                 * resolved at compile time; no virtual dispatch for the inner work.
                 */
                void process (AudioContext<FloatType>& context) noexcept CASPI_NON_BLOCKING override final
                {
                    getDerived().processImpl (context);
                }

                /**
                 * @brief Return &outputBuffer for port 0, nullptr otherwise.
                 *
                 * Derived classes with multiple output buffers should override this.
                 */
                const BufferType* getOutputBuffer (std::size_t port) const noexcept override
                {
                    if (port == 0)
                    {
                        return &outputBuffer;
                    }
                    return nullptr;
                }

                /*------------------------------------------------------------------
                 * Default CRTP hooks - derived classes shadow these
                 *-----------------------------------------------------------------*/

                /** @brief Called by prepareToRender(). Override to set up per-node state. */
                void onPrepare (std::size_t, std::size_t, double)
                {
                }

                /**
                 * @brief Called by process() on the audio thread.
                 *
                 * Write output to this->outputBuffer. Read inputs via:
                 * @code
                 *   const auto* in = ctx.getInputBuffer(this->getId(), portIndex);
                 *   if (in != nullptr) { ... }
                 * @endcode
                 *
                 * Must be noexcept. No allocation. No locks.
                 */
                void processImpl (AudioContext<FloatType>&) noexcept CASPI_NON_BLOCKING
                {
                }

            protected:
                /**
                 * @param numInputPorts   Number of audio input connections this node accepts.
                 * @param numOutputPorts  Number of audio output connections this node exposes.
                 */
                explicit AudioNode (std::size_t numInputPorts = 0, std::size_t numOutputPorts = 1)
                    : NodeBase<FloatType> (NodeType::Audio, numInputPorts, numOutputPorts)
                {
                }

                Derived& getDerived() noexcept
                {
                    return static_cast<Derived&> (*this);
                }
                const Derived& getDerived() const noexcept
                {
                    return static_cast<const Derived&> (*this);
                }

                /** @brief Output buffer written by processImpl(). Accessible to downstream nodes. */
                BufferType outputBuffer;
        };

        /*======================================================================
         * ControlNode<Derived, FloatType>
         *
         * CRTP base for control-rate nodes (envelopes, LFOs, mod matrices).
         * Produces a flat vector of scalar FloatType values rather than an
         * audio buffer. Downstream nodes read these via AudioContext or direct
         * pointer access.
         *
         * Derived must provide:
         *   void processImpl(AudioContext<FloatType>&) noexcept;
         *   (write computed values into this->controlOutputs[])
         *
         * Derived may provide:
         *   void onPrepare(size_t numChannels, size_t numFrames, double sampleRate);
         *====================================================================*/
        template <typename Derived, typename FloatType = double>
        class ControlNode : public NodeBase<FloatType>
        {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
                CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                                     "ControlNode requires a floating-point type (float, double, long double)");
#endif

            public:
                void prepareToRender (std::size_t numChannels, std::size_t numFrames, double sampleRate) override
                {
                    this->setSampleRate (static_cast<FloatType> (sampleRate));
                    getDerived().onPrepare (numChannels, numFrames, sampleRate);
                    this->markPrepared();
                }

                void process (AudioContext<FloatType>& context) noexcept CASPI_NON_BLOCKING override final
                {
                    getDerived().processImpl (context);
                }

                /**
                 * @brief Return the scalar output at the given port.
                 * @return controlOutputs[port], or FloatType(0) if port >= numOutputs.
                 */
                const FloatType* getControlValuePtr (std::size_t port) const noexcept override
                {
                    return (port < controlOutputs.size()) ? &controlOutputs[port] : nullptr;
                }

                FloatType getControlOutput (std::size_t port) const noexcept override
                {
                    return (port < controlOutputs.size()) ? controlOutputs[port] : FloatType(0);
                }

                /*------------------------------------------------------------------
                 * Default CRTP hooks
                 *-----------------------------------------------------------------*/

                void onPrepare (std::size_t, std::size_t, double)
                {
                }

                void processImpl (AudioContext<FloatType>&) noexcept CASPI_NON_BLOCKING 
                {
                }

            protected:
                /**
                 * @param numControlOutputs  Number of scalar output values produced per block.
                 */
                explicit ControlNode (std::size_t numControlOutputs = 1)
                    : NodeBase<FloatType> (NodeType::Control, 0, numControlOutputs)
                {
                    controlOutputs.resize (numControlOutputs, FloatType (0));
                }

                Derived& getDerived() noexcept
                {
                    return static_cast<Derived&> (*this);
                }
                const Derived& getDerived() const noexcept
                {
                    return static_cast<const Derived&> (*this);
                }

                /** @brief Scalar outputs written by processImpl(). Indexed by port. */
                std::vector<FloatType> controlOutputs;
        };

    } // namespace Graph
} // namespace CASPI

#endif // CASPI_NODE_H