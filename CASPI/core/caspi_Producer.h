#ifndef CASPI_PRODUCER_H
#define CASPI_PRODUCER_H
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
 * @file   core/caspi_Producer.h
 * @author CS Islay
 * @brief  Producer<Derived,F,Policy> — CRTP base for audio-rate sources.
 *
 * @details
 * ### Inheritance
 *
 *   NodeBase<F>
 *   └── AudioNode<Derived, F>
 *       └── Producer<Derived, F, Policy>
 *
 * Producer inherits AudioNode, which inherits NodeBase. Every Producer is
 * therefore a graph node with sample-rate awareness and an output buffer.
 *
 * ### Traversal policies
 *
 *   Traversal::PerSample   renderSample(ch, fr) called every sample (default)
 *   Traversal::PerFrame    renderSample(0, fr) called once per frame;
 *                          result broadcast to all channels
 *   Traversal::PerChannel  renderSample(ch, 0) called once per channel;
 *                          result broadcast to all frames in that channel
 *
 * ### Dual-mode operation
 *
 * **Graph mode** (AudioGraph owns the node):
 *   AudioGraph calls process(ctx) → AudioNode dispatches → processImpl(ctx)
 *   → render(this->outputBuffer). Derived classes override renderSample();
 *   no other change needed for graph participation.
 *
 * **Standalone mode** (no graph):
 *   Call render(buf) directly. Identical to the original API.
 *
 * ### CRTP contract
 *
 * Derived must override at least one renderSample() overload. All three
 * default to chaining up to the zero-argument version:
 * @code
 *   renderSample(ch, fr) -> renderSample(ch) -> renderSample()
 * @endcode
 *
 * Derived may additionally override:
 * - prepareBlock(nFrames, nChannels) — cache block-level state
 * - onPrepare(numChannels, numFrames, sampleRate) — AudioNode hook
 * - onSampleRateChanged(rate) — NodeBase hook
 * - processImpl(ctx) — full graph override (read ctx inputs before rendering)
 * - renderSpan(span, ch, offset) — custom SIMD span rendering
 *
 * ### SIMD notes
 *
 * For PerFrame and PerChannel policies, renderSpan() broadcasts the single
 * renderSample() result across the span using Core::fill(), which selects
 * SIMD or scalar based on is_simd_eligible<Span>. Override renderSpan()
 * to provide a vectorised generation path (e.g. wavetable lookup, SIMD sin).
 *
 * PerSample policy: no SIMD applied automatically (no data independence
 * guarantee). Override processImpl() with a hand-vectorised loop if needed.
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Features.h"
#include "caspi_AudioBuffer.h"
#include "core/caspi_Node.h"

#include <type_traits>

namespace CASPI
{
namespace Core
{

/**
 * @class Producer
 * @brief CRTP base for audio-rate sources (oscillators, noise, etc.).
 *
 * Inherits AudioNode<Derived,F>, which inherits NodeBase<F>.
 *
 * @tparam Derived    Concrete subclass (CRTP).
 * @tparam FloatType  float or double.
 * @tparam Policy     Traversal::PerSample (default), PerFrame, or PerChannel.
 *
 * @code
 *   // Minimal derived class
 *   class MySine : public CASPI::Core::Producer<MySine, float, Traversal::PerFrame>
 *   {
 *   public:
 *       MySine() : Producer(0, 1) {}
 *
 *       float renderSample() noexcept override {
 *           float s = std::sin(phase);
 *           phase += increment;
 *           return s;
 *       }
 *       void onSampleRateChanged(float sr) override { increment = freq / sr; }
 *   private:
 *       float phase = 0.f, increment = 0.f, freq = 440.f;
 *   };
 * @endcode
 */
template <typename Derived,
          typename FloatType = double,
          typename Policy    = Traversal::PerSample>
class Producer : public Graph::AudioNode<Derived, FloatType>
{
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                         "Producer requires a floating-point type (float, double, long double)");
#endif
    CASPI_STATIC_ASSERT (is_traversal_policy<Policy>::value,
                         "Policy must be PerSample, PerFrame, or PerChannel");

public:

    /** @brief Render one sample with no context. Override for stateless sources. */
    CASPI_NO_DISCARD virtual FloatType renderSample() CASPI_NON_BLOCKING
    {
        return FloatType (0);
    }

    /** @brief Render one sample with channel context. Default: delegates to renderSample(). */
    CASPI_NO_DISCARD virtual FloatType renderSample (std::size_t channel) CASPI_NON_BLOCKING
    {
        (void) channel;
        return renderSample();
    }

    /** @brief Render one sample with full context. Default: delegates to renderSample(ch). */
    CASPI_NO_DISCARD virtual FloatType renderSample (std::size_t channel,
                                                     std::size_t frame) CASPI_NON_BLOCKING
    {
        (void) frame;
        return renderSample (channel);
    }

    /**
     * @brief Called once per block before the traversal loop.
     *
     * Override to cache coefficients, update phase increments, or prepare
     * SIMD alignment. Default: does nothing.
     *
     * @param nFrames    Block size in frames.
     * @param nChannels  Number of channels.
     */
    virtual void prepareBlock (std::size_t nFrames, std::size_t nChannels)
    {
        (void) nFrames; (void) nChannels;
    }

    /**
     * @brief Render a full block into @p buf using the Policy traversal.
     *
     * @code
     *   // Standalone
     *   osc.render(myBuffer);
     *
     *   // Graph: processImpl() calls this automatically.
     * @endcode
     */
    template <template <typename> class Layout>
    void render (AudioBuffer<FloatType, Layout>& buf) noexcept CASPI_NON_BLOCKING
    {
        using P              = Policy;
        const std::size_t C  = buf.numChannels();
        const std::size_t Fm = buf.numFrames();

        prepareBlock (Fm, C);

        CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>)
        {
            for (std::size_t f = 0; f < Fm; ++f)
                for (std::size_t ch = 0; ch < C; ++ch)
                    buf.sample (ch, f) = renderSample (ch, f);
        }
        else CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>)
        {
            for (std::size_t f = 0; f < Fm; ++f)
            {
                auto frame = buf.frame_span (f);
                renderSpan (frame, 0, f);
            }
        }
        else
        {
            for (std::size_t ch = 0; ch < C; ++ch)
            {
                auto chan = buf.channel_span (ch);
                renderSpan (chan, ch, 0);
            }
        }
    }


    /**
     * @brief Called by AudioNode::process() each block.
     *
     * Default: calls render(this->outputBuffer). Override to read upstream
     * inputs from @p ctx before or after rendering.
     *
     * @param ctx  AudioContext providing upstream buffer and control pointers.
     */
    void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
    {
        (void) ctx;
        this->render (this->outputBuffer);
    }

    /**
     * @brief Render a span of samples (called by render() for PerFrame/PerChannel).
     *
     * Default: broadcasts renderSample(ch, frameOffset) across the span
     * using Core::fill() (SIMD-eligible for contiguous spans).
     *
     * Override for vectorised generation (e.g. SIMD sin, wavetable lookup).
     *
     * @param span         View into the output buffer.
     * @param channel      Channel index.
     * @param frameOffset  Starting frame index.
     */
    template <typename Span>
    void renderSpan (Span& span, std::size_t channel, std::size_t frameOffset = 0)
    {
        renderSpanImpl (span, channel, frameOffset,
                        std::integral_constant<bool, is_simd_eligible<Span>::value> {});
    }

protected:
    /**
     * @param numInputPorts   Audio inputs accepted. Default 0 (source has no audio input).
     * @param numOutputPorts  Audio outputs produced. Default 1.
     */
    explicit Producer (std::size_t numInputPorts  = 0,
                       std::size_t numOutputPorts = 1)
        : Graph::AudioNode<Derived, FloatType> (numInputPorts, numOutputPorts)
    {
    }

private:
    template <typename Span,
              typename = typename std::enable_if<is_simd_eligible<Span>::value>::type>
    void renderSpanImpl (Span& span, std::size_t channel, std::size_t frameOffset, std::true_type)
    {
        const FloatType sample = renderSample (channel, frameOffset);
        Core::fill (span, sample);
    }

    template <typename Span>
    void renderSpanImpl (Span& span, std::size_t channel, std::size_t frameOffset, std::false_type)
    {
        const FloatType sample = renderSample (channel, frameOffset);
        for (auto& s : span)
        {
            s = sample;
        }
    }
};

template <typename T>
struct is_producer : std::is_base_of<Graph::AudioNode<T, typename T::FloatType>, T> {};

#if defined(CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES)
template <typename T>
constexpr bool is_producer_v = is_producer<T>::value;
#endif

} // namespace Core
} // namespace CASPI

#endif // CASPI_PRODUCER_H