#ifndef CASPI_PROCESSOR_H
#define CASPI_PROCESSOR_H
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
 * @file   core/caspi_Processor.h
 * @author CS Islay
 * @brief  Processor<Derived,F,Policy> — CRTP base for audio-rate processors.
 *
 * @details
 * ### Inheritance
 *
 *   NodeBase<F>
 *   └── AudioNode<Derived, F>
 *       └── Processor<Derived, F, Policy>
 *
 * ### Dual-mode operation
 *
 * **Graph mode** (AudioGraph owns the node):
 *   processImpl() reads the upstream audio buffer from AudioContext port 0,
 *   copies it into this->outputBuffer, then calls process(outputBuffer)
 *   in-place. Unconnected input → outputBuffer left as silence.
 *   Input port 0 must be connected with ConnectionType::Audio.
 *
 * **Standalone mode** (no graph):
 *   Call process(buf) directly. Identical to the original API.
 *
 * ### CRTP contract
 *
 * Derived must override at least one processSample() overload. All three
 * default to chaining up:
 * @code
 *   processSample(in, ch, fr) -> processSample(in, ch) -> processSample(in)
 * @endcode
 *
 * Derived may additionally override:
 * - prepareBlock(nFrames, nChannels) — resize per-channel state, cache coefficients
 * - onPrepare(numChannels, numFrames, sampleRate) — AudioNode hook
 * - onSampleRateChanged(rate) — NodeBase hook
 * - processImpl(ctx) — full graph override (e.g. sidechain, multi-input)
 * - processSpan(span, ch, offset) — custom SIMD span processing
 *
 * ### SIMD notes
 *
 * processSpan() defaults to a scalar loop calling processSample() per element.
 * This is correct for any stateful processor with feedback (IIR filters,
 * compressors, delays). Override processSpan() for stateless or vectorisable
 * operations (gain, DC removal, soft clip):
 * @code
 *   template<typename Span>
 *   void processSpan(Span& span, size_t ch, size_t offset) {
 *       Core::scale(span, gainValue);  // SIMD path in Core
 *   }
 * @endcode
 *
 * ### Multiple inputs (sidechain, stereo, etc.)
 *
 * Override processImpl() and read additional ports via ctx.getAudioInput():
 * @code
 *   void processImpl(AudioContext<FloatType>& ctx) noexcept {
 *       const auto* sidechain = ctx.getAudioInput(this->getId(), 1);
 *       // ... use sidechain alongside the main input on port 0
 *       Processor::processImpl(ctx);  // handles port 0 copy + process()
 *   }
 * @endcode
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Features.h"
#include "caspi_AudioBuffer.h"
#include "core/caspi_Node.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Producer.h"   // for Traversal policies and is_traversal_policy

#include <type_traits>

namespace CASPI
{
namespace Core
{
/**
 * @class Processor
 * @brief CRTP base for audio-rate processors (filters, effects, etc.).
 *
 * Inherits AudioNode<Derived,F>, which inherits NodeBase<F>.
 *
 * @tparam Derived    Concrete subclass (CRTP).
 * @tparam FloatType  float or double.
 * @tparam Policy     Traversal::PerSample (default), PerFrame, or PerChannel.
 *
 * @code
 *   // Minimal gain processor
 *   class GainProcessor : public CASPI::Core::Processor<GainProcessor, float>
 *   {
 *   public:
 *       GainProcessor() : Processor(1, 1) {}
 *       float gain = 1.f;
 *
 *       float processSample(float in) noexcept override { return in * gain; }
 *   };
 * @endcode
 */
template <typename Derived,
          typename FloatType = double,
          typename Policy    = Traversal::PerSample>
class Processor : public Graph::AudioNode<Derived, FloatType>
{
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                         "Processor requires a floating-point type (float, double, long double)");
#endif
    CASPI_STATIC_ASSERT (is_traversal_policy<Policy>::value,
                         "Policy must be PerSample, PerFrame, or PerChannel");

public:
    /** @brief Process one sample with no context. Override for stateless processors. */
    CASPI_NO_DISCARD virtual FloatType processSample (FloatType in) CASPI_NON_BLOCKING
    {
        return in;
    }

    /** @brief Process one sample with channel context. Default: delegates to processSample(in). */
    CASPI_NO_DISCARD virtual FloatType processSample (FloatType   in,
                                                      std::size_t channel) CASPI_NON_BLOCKING
    {
        (void) channel;
        return processSample (in);
    }

    /** @brief Process one sample with full context. Default: delegates to processSample(in, ch). */
    CASPI_NO_DISCARD virtual FloatType processSample (FloatType   in,
                                                      std::size_t channel,
                                                      std::size_t frame) CASPI_NON_BLOCKING
    {
        (void) frame;
        return processSample (in, channel);
    }

    /**
     * @brief Called once per block before the traversal loop.
     *
     * Override to resize per-channel state arrays, update coefficients, or
     * prepare SIMD alignment. Default: does nothing.
     *
     * @param nFrames    Block size in frames.
     * @param nChannels  Number of channels.
     */
    virtual void prepareBlock (std::size_t nFrames, std::size_t nChannels) CASPI_NON_BLOCKING
    {
        (void) nFrames; (void) nChannels;
    }

    /**
     * @brief Process @p buf in-place using the Policy traversal.
     *
     * @code
     *   // Standalone
     *   processor.process(myBuffer);
     *
     *   // Graph: processImpl() calls this on outputBuffer after copying input.
     * @endcode
     */
    template <template <typename> class Layout>
    void process (AudioBuffer<FloatType, Layout>& buf) noexcept CASPI_NON_BLOCKING
    {
        using P              = Policy;
        const std::size_t C  = buf.numChannels();
        const std::size_t Fm = buf.numFrames();

        prepareBlock (Fm, C);

        CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>)
        {
            for (std::size_t f = 0; f < Fm; ++f)
                for (std::size_t ch = 0; ch < C; ++ch)
                    buf.sample (ch, f) = processSample (buf.sample (ch, f), ch, f);
        }
        else CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>)
        {
            for (std::size_t f = 0; f < Fm; ++f)
            {
                auto frame = buf.frame_span (f);
                processSpan (frame, 0, f);
            }
        }
        else
        {
            for (std::size_t ch = 0; ch < C; ++ch)
            {
                auto chan = buf.channel_span (ch);
                processSpan (chan, ch, 0);
            }
        }
    }

    /**
     * @brief Called by AudioNode::process() each block.
     *
     * Reads the audio buffer from AudioContext input port 0, copies it
     * into this->outputBuffer, then calls process(outputBuffer) in-place.
     *
     * If port 0 is not connected, outputBuffer retains its previous content
     * (cleared to silence at prepare() time, so the first block is silent).
     *
     * Override to:
     * - Read a sidechain from port 1: `ctx.getAudioInput(this->getId(), 1)`
     * - Implement zero-copy processing
     * - Read control inputs: `ctx.getControlInput(this->getId(), port)`
     *
     * @param ctx  AudioContext providing upstream buffer and control pointers.
     */
    void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
    {
        const auto* inBuf = ctx.getAudioInput (this->getId(), 0);

        if (inBuf != nullptr)
        {
            const std::size_t C  = this->outputBuffer.numChannels();
            const std::size_t Fm = this->outputBuffer.numFrames();

            for (std::size_t ch = 0; ch < C; ++ch)
            {
                for (std::size_t fr = 0; fr < Fm; ++fr)
                {
                    this->outputBuffer.sample (ch, fr) = inBuf->sample (ch, fr);
                }
            }
        }

        this->process (this->outputBuffer);
    }

    /**
     * @brief Process a contiguous or strided span in-place.
     *
     * Default: scalar loop calling processSample() per element.
     * Correct for all stateful processors (IIR filters, compressors, delays).
     *
     * Override for stateless or vectorisable operations:
     * @code
     *   template<typename Span>
     *   void processSpan(Span& span, size_t ch, size_t offset) noexcept {
     *       Core::scale(span, gainValue);
     *   }
     * @endcode
     *
     * @param span         View into the buffer (frame or channel span).
     * @param channel      Channel index.
     * @param frameOffset  Starting frame index within the block.
     */
    template <typename Span>
    void processSpan (Span& span, std::size_t channel, std::size_t frameOffset = 0) CASPI_NON_BLOCKING
    {
        std::size_t frame = frameOffset;
        for (auto& s : span)
        {
            s = processSample (s, channel, frame);
            ++frame;
        }
    }

protected:
    /**
     * @param numInputPorts   Audio inputs accepted. Default 1 (the signal to process).
     * @param numOutputPorts  Audio outputs produced. Default 1.
     */
    explicit Processor (std::size_t numInputPorts  = 1,
                        std::size_t numOutputPorts = 1)
        : Graph::AudioNode<Derived, FloatType> (numInputPorts, numOutputPorts)
    {
    }
};


template <typename T>
struct is_processor : std::is_base_of<Graph::AudioNode<T, typename T::FloatType>, T> {};

#if defined(CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES)
template <typename T>
constexpr bool is_processor_v = is_processor<T>::value;
#endif

} // namespace Core
} // namespace CASPI

#endif // CASPI_PROCESSOR_H