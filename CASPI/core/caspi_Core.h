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
#include "caspi_AudioBuffer.h"
#include <cstddef>
#include <type_traits>

namespace CASPI::Core {
    namespace Traversal {
        // Per-sample: update state every sample
        struct PerSample {
            template<typename Buf, typename F>
            CASPI_NON_BLOCKING static void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t f = 0; f < Fm; ++f)
                {
                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        fn(ch, f);
                    }
                }
            }
        };

        // Per-frame: update state once per frame, replicate across channels
        struct PerFrame {
            template<typename Buf, typename F>
            CASPI_NON_BLOCKING static void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t f = 0; f < Fm; ++f)
                {
                    fn(f, C); // “once per frame” callback
                }
            }
        };

        // Per-channel: update state once per channel, operate over all frames
        struct PerChannel
        {
            template<typename Buf, typename F>
            CASPI_NON_BLOCKING static void for_each(Buf &buf, F &&fn) noexcept {
                const std::size_t C = buf.numChannels();
                const std::size_t Fm = buf.numFrames();
                for (std::size_t ch = 0; ch < C; ++ch)
                {
                    fn(ch, Fm); // “once per channel” callback
                }
            }
        };
    } // namespace Traversal

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

    public:
        // ---- Hooks for derived types (override what you need) ----
        // 1) Per-sample hook
        virtual FloatType renderSample() { return FloatType(0); }

        // 2) Per-frame hook (called ONCE per frame)
        //    Default calls renderSample() once, meaning state updates once/frame.
        virtual FloatType renderFrame() { return renderSample(); }

        // 3) Per-channel hook (called ONCE per channel)
        //    Default fills via renderSample() across all frames of that channel.
        virtual void renderChannel(FloatType *chData, std::size_t nFrames) {
            for (std::size_t f = 0; f < nFrames; ++f)
                chData[f] = renderSample();
        }

        // ---- Generic rendering into AudioBuffer ----
        template<template <typename> class Layout>
        CASPI_NON_BLOCKING void render(AudioBuffer<FloatType, Layout> &buf) noexcept {
            using P = Policy;
            // Per-sample
            if (std::is_same<P, Traversal::PerSample>::value) {
                P::for_each(buf, [this, &buf](std::size_t ch, std::size_t f) {
                    buf.sample(ch, f) = this->renderSample();
                });
            }
            // Per-frame
            else if (std::is_same<P, Traversal::PerFrame>::value) {
                P::for_each(buf, [this, &buf](std::size_t f, std::size_t C) {
                    const FloatType v = this->renderFrame(); // state advances once
                    for (std::size_t ch = 0; ch < C; ++ch) buf.sample(ch, f) = v;
                });
            }
            // Per-channel
            else {
                // PerChannel
                P::for_each(buf, [this, &buf](std::size_t ch, std::size_t nFrames) {
                    this->renderChannel(buf.channelData(ch), nFrames);
                });
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
        virtual FloatType processSample(FloatType x) { return x; }

        // Called once per frame with a contiguous “frame” copy (size = nChannels).
        // Default: per-sample processing and write-back.
        virtual void processFrame(FloatType *frame, std::size_t nChannels) {
            for (std::size_t ch = 0; ch < nChannels; ++ch)
                frame[ch] = processSample(frame[ch]);
        }

        // Called once per channel on the full channel buffer (nFrames).
        // Default: per-sample processing and write-back.
        virtual void processChannel(FloatType *channel, std::size_t nFrames) {
            for (std::size_t f = 0; f < nFrames; ++f)
                channel[f] = processSample(channel[f]);
        }

        // ---- Generic processing over AudioBuffer ----
        template<template <typename> class Layout>
        CASPI_NON_BLOCKING void process(AudioBuffer<FloatType, Layout> &buf) noexcept {
            using P = Policy;
            const std::size_t C = buf.numChannels();
            const std::size_t Fm = buf.numFrames();

            if (std::is_same<P, Traversal::PerSample>::value) {
                // state per sample
                for (std::size_t f = 0; f < Fm; ++f)
                    for (std::size_t ch = 0; ch < C; ++ch)
                        buf.sample(ch, f) = processSample(buf.sample(ch, f));
            } else if (std::is_same<P, Traversal::PerFrame>::value) {
                // state per frame; gather frame into a small stack array, process once, scatter back
                for (std::size_t f = 0; f < Fm; ++f) {
                    // small fixed-size scratch: if C is large, use vector (rare in audio)
                    std::vector<FloatType> frame(C);
                    for (std::size_t ch = 0; ch < C; ++ch)
                        frame[ch] = buf.sample(ch, f);
                    processFrame(frame.data(), C);
                    for (std::size_t ch = 0; ch < C; ++ch)
                        buf.sample(ch, f) = frame[ch];
                }
            } else {
                // PerChannel
                for (std::size_t ch = 0; ch < C; ++ch) {
                    processChannel(buf.channelData(ch), Fm);
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

        void setSampleRate(FloatType sampleRate) {
            CASPI_ASSERT(sampleRate > 0, "Sample rate must be greater than zero.");
            this->sampleRate = sampleRate;
        }

        FloatType getSampleRate() const {
            return sampleRate;
        }

    private:
        FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
    };

#if defined(CASPI_FEATURES_HAS_TYPE_TRAITS)
    template <typename T>
    struct is_producer : std::is_base_of<Producer<typename T::FloatType>, T>
    {
    };

    template <typename T>
    struct is_processor : std::is_base_of<Processor<typename T::FloatType>, T>
    {
    };

    template <typename T>
    struct is_modulator : std::is_base_of<Modulator<typename T::FloatType>, T>
    {
    };

    template <typename T>
    struct is_sample_rate_aware : std::is_base_of<SampleRateAware<typename T::FloatType>, T>
    {
    };
#endif // CASPI_FEATURES_HAS_TYPE_TRAITS

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

#endif //CASPI_BASE_H
