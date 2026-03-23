#ifndef CASPI_CORE_H
#define CASPI_CORE_H
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
 * @file caspi_Core.h Defines common API base classes for algorithms.
 * @author CS Islay
 * @brief Defines common API base classes for algorithms.
 *        Producers, processors, modulators, and SampleRateAware are defined here.
 *        Producers are typically oscillators or other sound sources.
 *        Processors are typically filters or other audio effects.
 *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
 *        Additionally, Denormal handling is provided by feature/platform detection.
 *        Define CASPI_DISABLE_FLUSH_DENORMALS to disable manual flushing, it may save some CPU cycles by using a global setting.
 ************************************************************************/
#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "caspi_AudioBuffer.h"
#include <type_traits>

namespace CASPI
{
    namespace Core
    {
        namespace Traversal
        {
            // Per-sample: update state every sample
            struct PerSample
            {
                    template <typename Buf, typename F>
                    static void for_each (Buf& buf, F&& fn) noexcept
                    {
                        const std::size_t C  = buf.numChannels();
                        const std::size_t Fm = buf.numFrames();
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            for (std::size_t ch = 0; ch < C; ++ch)
                            {
                                fn (ch, f);
                            }
                        }
                    }
            };

            // Per-frame: update state once per frame, replicate across channels
            struct PerFrame
            {
                    template <typename Buf, typename F>
                    static void for_each (Buf& buf, F&& fn) noexcept
                    {
                        const std::size_t C  = buf.numChannels();
                        const std::size_t Fm = buf.numFrames();
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            fn (f, C); // “once per frame” callback
                        }
                    }
            };

            // Per-channel: update state once per channel, operate over all frames
            struct PerChannel
            {
                    template <typename Buf, typename F>
                    static void for_each (Buf& buf, F&& fn) noexcept
                    {
                        const std::size_t C  = buf.numChannels();
                        const std::size_t Fm = buf.numFrames();
                        for (std::size_t ch = 0; ch < C; ++ch)
                        {
                            fn (ch, Fm); // “once per channel” callback
                        }
                    }
            };
        } // namespace Traversal

    template <typename Policy>
    struct is_traversal_policy : std::false_type {};

    template <>
    struct is_traversal_policy<Core::Traversal::PerSample> : std::true_type {};

    template <>
    struct is_traversal_policy<Core::Traversal::PerFrame> : std::true_type {};

        template <>
        struct is_traversal_policy<Core::Traversal::PerChannel> : std::true_type
        {
        };

        /**
         * @class Producer
         * @brief Defines a common API for producers of audio data.
         *        Producers are typically oscillators or other sound sources.
         *        They can render a single sample or a buffer of samples.
         *
         * @tparam FloatType The floating-point type (float, double, long double)
         * @tparam Policy Traversal policy (PerSample, PerFrame, PerChannel)
         *
         * SIMD PROCESSING NOTES:
         * ----------------------
         * Producers generate samples without input data. For PerFrame and PerChannel policies,
         * renderSpan automatically uses SIMD when:
         * - Span is contiguous (checked by is_simd_eligible)
         * - FloatType supports vectorization (checked by SIMD::Strategy)
         *
         * SIMD is beneficial for producers because:
         * 1. Single state update generates multiple identical samples (broadcast pattern)
         * 2. No data dependencies between samples in a span
         * 3. Common for modulation sources (LFOs, envelopes) operating at control rate
         *
         * AUTOMATIC SIMD BEHAVIOR:
         * ------------------------
         * PerFrame policy:
         * - renderSample(channel, frame) called once per frame
         * - Result broadcast to all channels via SIMD fill
         * - Use for: frame-rate modulators, control signals
         *
         * PerChannel policy:
         * - renderSample(channel, frameOffset) called once per channel
         * - Result broadcast to all frames in channel via SIMD fill
         * - Use for: channel-rate modulators, DC sources
         *
         * PerSample policy:
         * - renderSample(channel, frame) called per sample
         * - No SIMD applied (sample-accurate generation)
         * - Use for: audio-rate oscillators, noise generators
         *
         * WHEN TO OVERRIDE renderSpan:
         * ----------------------------
         * Override for producers with vectorizable sample generation:
         *
         * template<typename Span>
         * void renderSpan(Span& span, size_t channel, size_t frameOffset) override
         * {
         *     renderSpanImpl(
         *         span, channel, frameOffset,
         *         typename std::integral_constant<bool, is_simd_eligible<Span>::value>{});
         * }
         *
         * private:
         *     template<typename Span>
         *     void renderSpanImpl(Span& span, size_t channel, size_t frameOffset, std::true_type)
         *     {
         *         // SIMD: generate multiple samples at once
         *         // Example: vectorized wavetable lookup, SIMD sin/cos
         *     }
         *
         *     template<typename Span>
         *     void renderSpanImpl(Span& span, size_t channel, size_t frameOffset, std::false_type)
         *     {
         *         // Scalar fallback
         *         for (size_t i = 0; i < span.size(); ++i)
         *             span[i] = renderSample(channel, frameOffset + i);
         *     }
         *
         * EXAMPLES OF SIMD-FRIENDLY PRODUCERS:
         * - Constant DC sources
         * - Control-rate LFOs
         * - Envelope generators (block-rate)
         * - Wavetable oscillators (with SIMD lookup)
         *
         * EXAMPLES REQUIRING PER-SAMPLE (DEFAULT):
         * - Band-limited oscillators (BLEP/BLAMP)
         * - Noise generators with per-sample random state
         * - Phase-modulated oscillators
         * - Any producer requiring sample-accurate phase updates
         */
        template <CASPI_FLOAT_TYPE FloatType = double, typename Policy = Traversal::PerSample>
        class Producer
        {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
                CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                                     "Producer base class requires a floating-point type (float, double, long double)");
#endif

                CASPI_STATIC_ASSERT (is_traversal_policy<Policy>::value,
                                     "Policy must be PerSample, PerFrame, or PerChannel");

            public:
                // ---- Hooks for derived types (override what you need) ---

                /**
                 * @brief Render a single sample (stateless variant)
                 * @return Generated sample value
                 */
                CASPI_NO_DISCARD virtual FloatType renderSample() CASPI_NON_BLOCKING
                {
                    return FloatType (0);
                }

                /**
                 * @brief Render a single sample with channel context
                 * @param channel Channel index
                 * @return Generated sample value
                 */
                CASPI_NO_DISCARD virtual FloatType renderSample (const std::size_t channel) CASPI_NON_BLOCKING
                {
                    (void) channel;
                    return renderSample();
                }

                /**
                 * @brief Render a single sample with full context
                 * @param channel Channel index
                 * @param frame Frame index
                 * @return Generated sample value
                 */
                CASPI_NO_DISCARD virtual FloatType renderSample (const std::size_t channel,
                                                                 const std::size_t frame) CASPI_NON_BLOCKING
                {
                    (void) frame;
                    return renderSample (channel);
                }

                /**
                 * @brief Called before rendering each block
                 * @param nFrames Number of frames in block
                 * @param nChannels Number of channels in block
                 *
                 * Use this to:
                 * - Update phase increments
                 * - Prepare wavetables
                 * - Cache coefficients
                 * - Prepare SIMD alignment
                 */
                virtual void prepareBlock (const std::size_t nFrames, const std::size_t nChannels)
                {
                    // Default: do nothing.
                    (void) nFrames;  (void) nChannels;
                }

                /**
                 * @brief Render a span of samples
                 * @param span View into buffer (frame or channel span)
                 * @param channel Channel index
                 * @param frameOffset Starting frame offset
                 *
                 * Default: SIMD broadcast for eligible spans (PerFrame/PerChannel policies).
                 * Override for advanced SIMD generation (vectorized wavetables, etc.)
                 */
                template <typename Span>
                void renderSpan (Span& span, const std::size_t channel, const std::size_t frameOffset = 0)
                {
                    renderSpanImpl (
                        span, channel, frameOffset, std::integral_constant<bool, is_simd_eligible<Span>::value> {});
                }

                // ---- Generic processing over AudioBuffer ----
                template <template <typename> class Layout>
                void render (AudioBuffer<FloatType, Layout>& buf) noexcept CASPI_NON_BLOCKING
                {
                    using P              = Policy;
                    const std::size_t C  = buf.numChannels();
                    const std::size_t Fm = buf.numFrames();

                    prepareBlock (Fm, C);

                    CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>)
                    {
                        // Per-sample: uses renderSample directly (sample-accurate)
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            for (std::size_t ch = 0; ch < C; ++ch)
                            {
                                buf.sample (ch, f) = renderSample (ch, f);
                            }
                        }
                    }
                    else CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>)
                    {
                        // Per-frame: render once, broadcast to all channels
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            auto frame = buf.frame_span (f);
                            renderSpan (frame, 0, f);
                        }
                    }
                    else
                    {
                        // PerChannel: render once, broadcast to all frames in channel
                        for (std::size_t ch = 0; ch < C; ++ch)
                        {
                            auto chan = buf.channel_span (ch);
                            renderSpan (chan, ch, 0);
                        }
                    }
                }

                virtual ~Producer() noexcept = default;

            private:
                // SIMD-friendly overload: broadcast single value to span
                template <typename Span,
                          typename = typename std::enable_if<is_simd_eligible<Span>::value>::type>
                void renderSpanImpl (Span& span, std::size_t channel, std::size_t frameOffset, std::true_type)
                {
                    const FloatType sample = renderSample (channel, frameOffset);
                    Core::fill (span, sample);
                }

                // Scalar fallback: fill span with repeated renderSample value
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

        /**
         * @class Processor
         * @brief Defines a common API for processors of audio data.
         *        Processors are typically filters or other audio effects.
         *        They can process a single sample or a buffer of samples.
         *
         * @tparam FloatType The floating-point type (float, double, long double)
         * @tparam Policy Traversal policy (PerSample, PerFrame, PerChannel)
         *
         * SIMD PROCESSING NOTES:
         * ----------------------
         * By default, processSpan uses scalar per-sample iteration. This is correct for:
         * - Stateful processors (filters, compressors, delays)
         * - Processors where each sample depends on previous samples
         * - Non-linear processing with sample-dependent state
         *
         * SIMD is NOT automatically applied because:
         * 1. Most audio processors are stateful and cannot vectorize trivially
         * 2. processSample(in, channel, frame) signature requires per-sample granularity
         * 3. Vectorizing state updates often requires algorithm redesign
         *
         * TO USE SIMD IN DERIVED CLASSES:
         * --------------------------------
         * Override processSpan for stateless or vectorizable operations:
         *
         * template<typename Span>
         * void processSpan(Span& span, size_t channel, size_t frameOffset) override
         * {
         *     processSpanImpl(
         *         span, channel, frameOffset,
         *         typename std::integral_constant<bool, is_simd_eligible<Span>::value>{});
         * }
         *
         * private:
         *     template<typename Span>
         *     void processSpanImpl(Span& span, size_t channel, size_t frameOffset, std::true_type)
         *     {
         *         // SIMD implementation using Core::SIMD operations
         *         // Example: gain, mix, simple arithmetic
         *     }
         *
         *     template<typename Span>
         *     void processSpanImpl(Span& span, size_t channel, size_t frameOffset, std::false_type)
         *     {
         *         // Scalar fallback
         *         for (auto& s : span)
         *             s = processSample(s, channel, frameOffset++);
         *     }
         *
         * EXAMPLES OF SIMD-FRIENDLY PROCESSORS:
         * - Gain/volume control
         * - DC offset removal
         * - Simple distortion (tanh, clip)
         * - Mixing/panning
         *
         * EXAMPLES REQUIRING PER-SAMPLE (NO SIMD):
         * - IIR filters (feedback state)
         * - Compressors (envelope followers)
         * - Delays (buffer indexing)
         * - Any processor with sample-dependent state transitions
         */
        template <CASPI_FLOAT_TYPE FloatType = double, typename Policy = Traversal::PerSample>
        class Processor
        {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
                CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                                     "Processor base class requires a floating-point type (float, double, long double)");
#endif

                CASPI_STATIC_ASSERT (is_traversal_policy<Policy>::value,
                                     "Policy must be PerSample, PerFrame, or PerChannel");

            public:
                // ---- Hooks for derived types (override what you need) ----

                /**
                 * @brief Process a single sample (stateless variant)
                 * @param in Input sample
                 * @return Processed sample
                 */
                CASPI_NO_DISCARD virtual FloatType processSample (FloatType in) CASPI_NON_BLOCKING
                {
                    return in;
                }

                /**
                 * @brief Process a single sample with channel context
                 * @param in Input sample
                 * @param channel Channel index
                 * @return Processed sample
                 */
                CASPI_NO_DISCARD virtual FloatType processSample (FloatType in, const std::size_t channel) CASPI_NON_BLOCKING
                {
                    (void) channel;
                    return processSample (in);
                }

                /**
                 * @brief Process a single sample with full context
                 * @param in Input sample
                 * @param channel Channel index
                 * @param frame Frame index
                 * @return Processed sample
                 */
                CASPI_NO_DISCARD virtual FloatType processSample (FloatType in, const std::size_t channel, const std::size_t frame) CASPI_NON_BLOCKING
                {
                    (void) frame;
                    return processSample (in, channel);
                }

                /**
                 * @brief Called before processing each block
                 * @param nFrames Number of frames in block
                 * @param nChannels Number of channels in block
                 *
                 * Use this to:
                 * - Resize per-channel state arrays
                 * - Update coefficients
                 * - Prepare SIMD alignment
                 */
                virtual void prepareBlock (const std::size_t nFrames, const std::size_t nChannels)CASPI_NON_BLOCKING
                {
                    // Default: do nothing
                    (void) nFrames;  (void) nChannels;
                }

                /**
                 * @brief Process a span of samples
                 * @param span View into buffer (frame or channel span)
                 * @param channel Channel index
                 * @param frameOffset Starting frame offset
                 *
                 * Default: scalar iteration calling processSample.
                 * Override for SIMD-friendly processors (gain, mix, etc.)
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

                // ---- Generic processing over AudioBuffer ----
                template <template <typename> class Layout>
                void process (AudioBuffer<FloatType, Layout>& buf) noexcept CASPI_NON_BLOCKING
                {
                    using P              = Policy;
                    const std::size_t C  = buf.numChannels();
                    const std::size_t Fm = buf.numFrames();

                    prepareBlock (Fm, C);

                    CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>)
                    {
                        // Per-sample: call processSample directly
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            for (std::size_t ch = 0; ch < C; ++ch)
                            {
                                buf.sample (ch, f) = processSample (buf.sample (ch, f), ch, f);
                            }
                        }
                    }
                    else CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>)
                    {
                        // Per-frame: call processSpan on each frame
                        for (std::size_t f = 0; f < Fm; ++f)
                        {
                            auto frame = buf.frame_span (f);
                            processSpan (frame, 0, f);
                        }
                    }
                    else
                    {
                        // PerChannel: call processSpan on each channel
                        for (std::size_t ch = 0; ch < C; ++ch)
                        {
                            auto chan = buf.channel_span (ch);
                            processSpan (chan, ch, 0);
                        }
                    }
                }

                virtual ~Processor() noexcept = default;
        };

        template <CASPI_FLOAT_TYPE FloatType = double>
        class SampleRateAware
        {
            public:
                virtual ~SampleRateAware() noexcept = default;

                SampleRateAware() = default;

                virtual void setSampleRate (FloatType newSampleRate)
                {
                    CASPI_ASSERT (newSampleRate > 0, "Sample rate must be greater than zero.");
                    this->sampleRate = newSampleRate;
                }

                virtual FloatType getSampleRate() const
                {
                    return sampleRate;
                }

                /**
                 * @brief Hook for derived classes to respond to sample rate changes
                 * @param rate New sample rate in Hz
                 */
                virtual void onSampleRateChanged(FloatType rate)
                {
                    (void)rate; // Default implementation does nothing, override in derived classes if needed
                }

            private:
                FloatType sampleRate = Constants::DEFAULT_SAMPLE_RATE<FloatType>;
        };

        template <typename T>
        struct is_producer : std::is_base_of<Producer<typename T::FloatType>, T>
        {
        };

        template <typename T>
        struct is_processor : std::is_base_of<Processor<typename T::FloatType>, T>
        {
        };

        template <typename T>
        struct is_sample_rate_aware : std::is_base_of<SampleRateAware<typename T::FloatType>, T>
        {
        };

#if defined(CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES)

        template <typename T>
        constexpr bool is_real_time_safe_v = is_real_time_safe<T>::value;

        template <typename T>
        constexpr bool is_non_real_time_safe_v = is_non_real_time_safe<T>::value;

        template <typename T>
        constexpr bool is_sample_rate_aware_v = is_sample_rate_aware<T>::value;

        template <typename T>
        constexpr bool is_producer_v = is_producer<T>::value;

        template <typename T>
        constexpr bool is_processor_v = is_processor<T>::value;

#endif // CASPI_CPP_17

        inline void configureFlushToZero (bool enable)
        {
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS)
            if (enable)
                _mm_setcsr (_mm_getcsr() | (1u << 15) | (1u << 6));
            else
                _mm_setcsr (_mm_getcsr() & ~((1u << 15) | (1u << 6)));
#elif defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
            if (enable)
                _mm_setcsr (_mm_getcsr() | (1u << 15));
            else
                _mm_setcsr (_mm_getcsr() & ~(1u << 15));
#else
            (void) enable;
#endif
        }

        template <typename FloatType>
        inline FloatType flushToZeroScalar (FloatType value,
                                            FloatType threshold = FloatType (1e-15))
        {
#if defined(CASPI_DISABLE_FLUSH_DENORMALS)
            return value;
#else
            return (value * static_cast<FloatType> (std::abs (value) >= threshold));
#endif
        }

        class ScopedFlushDenormals
        {
            public:
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO)

                ScopedFlushDenormals() noexcept
                    : mxcsr_ (_mm_getcsr())
                {
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS)
                    _mm_setcsr (mxcsr_ | (1u << 15) | (1u << 6)); // FZ | DAZ
#else
                    _mm_setcsr (mxcsr_ | (1u << 15)); // FZ only
#endif
                }

                ~ScopedFlushDenormals() noexcept
                {
                    _mm_setcsr (mxcsr_);
                }

#else
                ScopedFlushDenormals() noexcept = default;
                ~ScopedFlushDenormals()         = default;
#endif

                ScopedFlushDenormals (const ScopedFlushDenormals&)            = delete;
                ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

            private:
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
                unsigned int mxcsr_;
#endif
        };
    }; // namespace Core
} // namespace CASPI

#endif // CASPI_CORE_H
