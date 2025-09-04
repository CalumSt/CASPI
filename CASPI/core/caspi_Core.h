#ifndef CASPI_BASE_H
#define CASPI_BASE_H
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
#include "base/caspi_Traits.h"
#include "caspi_AudioBuffer.h"
#include <cstddef>
#include <type_traits>

namespace CASPI::Core {
    namespace Traversal {
        // Per-sample: update state every sample
        struct PerSample {
            template<typename Buf, typename F>
            static CASPI_NON_BLOCKING void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t f = 0; f < Fm; ++f) {
                    for (std::size_t ch = 0; ch < C; ++ch) {
                        fn(ch, f);
                    }
                }
            }
        };

        // Per-frame: update state once per frame, replicate across channels
        struct PerFrame {
            template<typename Buf, typename F>
            static CASPI_NON_BLOCKING void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t f = 0; f < Fm; ++f) {
                    fn(f, C); // “once per frame” callback
                }
            }
        };

        // Per-channel: update state once per channel, operate over all frames
        struct PerChannel {
            template<typename Buf, typename F>
            static CASPI_NON_BLOCKING void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t ch = 0; ch < C; ++ch) {
                    fn(ch, Fm); // “once per channel” callback
                }
            }
        };
    } // namespace Traversal

    template<typename Policy>
    struct is_traversal_policy : std::false_type {
    };

    template<>
    struct is_traversal_policy<Core::Traversal::PerSample> : std::true_type {
    };

    template<>
    struct is_traversal_policy<Core::Traversal::PerFrame> : std::true_type {
    };

    template<>
    struct is_traversal_policy<Core::Traversal::PerChannel> : std::true_type {
    };

#if defined(CASPI_FEATURES_HAS_CONCEPTS) && ! defined(CASPI_FEATURES_DISABLE_CONCEPTS)

    template <typename T>
    concept FloatingPoint = std::is_floating_point_v<T>;

    /**
     * @class Producer
     * @brief Defines a common API for producers of audio data.
     *        Producers are typically oscillators or other sound sources.
     *        They can render a single sample or a buffer of samples.
     *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
     */
    template <FloatingPoint FloatType, typename Policy = Traversal::PerSample>
#else
    template<typename FloatType = double, typename Policy = Traversal::PerSample>
#endif
    class Producer {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "Producer base class requires a floating-point type (float, double, long double)")
        ;
#endif
        CASPI_STATIC_ASSERT(is_traversal_policy<Policy>::value,
                            "Policy must be PerSample, PerFrame, or PerChannel");

    public:
        // ---- Hooks for derived types (override what you need) ---
        CASPI_NO_DISCARD virtual FloatType renderSample() {
            return FloatType(0);
        }

        CASPI_NO_DISCARD virtual FloatType renderSample(const std::size_t channel) {
            (void) channel;
            return renderSample();
        }

        CASPI_NO_DISCARD virtual FloatType renderSample(const std::size_t channel,
                                                        const std::size_t frame)
        {
            (void) frame;
            return renderSample(channel);
        }

        virtual void prepareBlock(const std::size_t nFrames, const std::size_t nChannels) {
            // Default: do nothing.
        }

        template<typename Span>
        void renderSpan(Span span, const std::size_t channel, const std::size_t frameOffset = 0) {
            std::size_t frame = frameOffset;
            for (auto &s: span) {
                s = renderSample(channel, frame);
                ++frame;
            }
        }

        // ---- Generic processing over AudioBuffer ----
        template<template <typename> class Layout>
        CASPI_NON_BLOCKING
        void render(AudioBuffer<FloatType, Layout> &buf) noexcept {
            using P = Policy;
            const std::size_t C = buf.numChannels();
            const std::size_t Fm = buf.numFrames();

            prepareBlock(Fm, C);

            CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>) {
                // Per-sample still uses renderSample directly (per-sample granularity)
                for (std::size_t f = 0; f < Fm; ++f) {
                    for (std::size_t ch = 0; ch < C; ++ch) {
                        buf.sample(ch, f) = renderSample(ch, f);
                    }
                }
            } else
                CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>) {
                    // Use renderSpan per frame; frame_span may be contiguous or strided
                    for (std::size_t f = 0; f < Fm; ++f) {
                        auto frame = buf.frame_span(f);
                        renderSpan(frame, 0, f); // channel = 0 for frame; frameOffset = f
                    }
                } else {
                    // PerChannel
                    for (std::size_t ch = 0; ch < C; ++ch) {
                        auto chan = buf.channel_span(ch);
                        renderSpan(chan, ch, 0); // channel = ch; frameOffset = 0
                    }
                }
        }

        virtual ~Producer() noexcept = default;
    };

    /**
     * @class Processor
     * @brief Defines a common API for processors of audio data.
     *        Process are typically filters or other audio effects.
     *        They can process a single sample or a buffer of samples.
     *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
     */
#if defined(CASPI_FEATURES_HAS_CONCEPTS) && ! defined(CASPI_FEATURES_DISABLE_CONCEPTS)
    template <FloatingPoint FloatType, typename Policy = Traversal::PerSample>
#else
    template<typename FloatType = double, typename Policy = Traversal::PerSample>
#endif
    class Processor {
#if ! defined(CASPI_FEATURES_HAS_CONCEPTS) || defined(CASPI_FEATURES_DISABLE_CONCEPTS)
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "Producer base class requires a floating-point type (float, double, long double)")
        ;
#endif

    public:
        // ---- Hooks for derived types (override what you need) ----

        CASPI_NO_DISCARD virtual FloatType processSample(FloatType in) {
            return in;
        }

        CASPI_NO_DISCARD virtual FloatType processSample(FloatType in, const std::size_t channel) {
            (void) channel;
            return processSample(in);
        }

        CASPI_NO_DISCARD virtual FloatType processSample(FloatType in, const std::size_t channel,
                                                         const std::size_t frame) {
            (void) frame;
            return processSample(in, channel);
        }

        virtual void prepareBlock(const std::size_t nFrames, const std::size_t nChannels) {
            // Default: do nothing.
        }

        template<typename Span>
        void processSpan(Span span, std::size_t channel, std::size_t frameOffset = 0) {
            std::size_t frame = frameOffset;
            for (auto &s: span) {
                s = processSample(s, channel, frame);
                ++frame;
            }
        }

        // ---- Generic processing over AudioBuffer ----
        template<template <typename> class Layout>
        CASPI_NON_BLOCKING
        void process(AudioBuffer<FloatType, Layout> &buf) noexcept {
            using P = Policy;
            const std::size_t C = buf.numChannels();
            const std::size_t Fm = buf.numFrames();

            prepareBlock(Fm, C);

            CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerSample>) {
                // Per-sample: call processSample directly
                for (std::size_t f = 0; f < Fm; ++f) {
                    for (std::size_t ch = 0; ch < C; ++ch) {
                        buf.sample(ch, f) = processSample(buf.sample(ch, f), ch, f);
                    }
                }
            } else
                CASPI_CPP17_IF_CONSTEXPR (std::is_same_v<P, Traversal::PerFrame>) {
                    // Per-frame: call processSpan on each frame
                    for (std::size_t f = 0; f < Fm; ++f) {
                        auto frame = buf.frame_span(f);
                        processSpan(frame, 0, f); // channel = 0 for frame; frameOffset = f
                    }
                } else {
                    // PerChannel
                    for (std::size_t ch = 0; ch < C; ++ch) {
                        auto chan = buf.channel_span(ch);
                        processSpan(chan, ch, 0); // channel = ch; frameOffset = 0
                    }
                }
        }

        virtual ~Processor() noexcept = default;
    };

#if defined(CASPI_FEATURES_HAS_CONCEPTS) && ! defined(CASPI_FEATURES_DISABLE_CONCEPTS)
    template <FloatingPoint FloatType>
#else
    template<typename FloatType = double>
#endif
    class Modulator {
    public:
        virtual FloatType modulate() = 0;

        virtual void modulate(FloatType *buffer, std::size_t numSamples) = 0;

        virtual ~Modulator() = default;
    };

#if defined(CASPI_FEATURES_HAS_CONCEPTS) && ! defined(CASPI_FEATURES_DISABLE_CONCEPTS)
    template <FloatingPoint FloatType>
#else
    template<typename FloatType = double>
#endif
    class SampleRateAware {
    public:
        ~SampleRateAware() noexcept = default;

        SampleRateAware() = default;

        void setSampleRate(FloatType newSampleRate) {
            CASPI_ASSERT(newSampleRate > 0, "Sample rate must be greater than zero.");
            this->sampleRate = newSampleRate;
        }

        FloatType getSampleRate() const {
            return sampleRate;
        }

    private:
        FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
    };

    template<typename T>
    struct is_producer : std::is_base_of<Producer<typename T::FloatType>, T> {
    };

    template<typename T>
    struct is_processor : std::is_base_of<Processor<typename T::FloatType>, T> {
    };

    template<typename T>
    struct is_modulator : std::is_base_of<Modulator<typename T::FloatType>, T> {
    };

    template<typename T>
    struct is_sample_rate_aware : std::is_base_of<SampleRateAware<typename T::FloatType>, T> {
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

    template <typename T>
    constexpr bool is_modulator_v = is_modulator<T>::value;

#endif // CASPI_CPP_17

    template<typename FloatType>
    inline FloatType flushToZero(FloatType value, FloatType threshold = FloatType(1e-15)) {
        // Compute mask: 1 if abs(value) >= threshold, else 0
        // Use std::abs to handle negative values
#if ! defined(CASPI_DISABLE_FLUSH_DENORMALS)
        auto mask = static_cast<FloatType>(std::abs(value) >= threshold);
        return value * mask;
#else
        return value;
#endif
    }

    [[maybe_unused]]
    inline void configureFlushToZero(bool enable) {
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS)
        if (enable)
        {
            _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
            _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
        }
        else
        {
            _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_OFF);
            _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_OFF);
        }

#elif defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
        if (enable)
            _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        else
            _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);

#else
        // Fallback for unsupported platforms
        (void) enable;
#endif
    }

    class ScopedFlushDenormals {
    public:
        ScopedFlushDenormals() {
#if ! defined(CASPI_DISABLE_FLUSH_DENORMALS)
            configureFlushToZero(true);
#endif
        }

        ~ScopedFlushDenormals() {
#if ! defined(CASPI_DISABLE_FLUSH_DENORMALS)
            configureFlushToZero(false);
#endif
        }
    };
} // namespace CASPI::Core

#endif // CASPI_BASE_H
