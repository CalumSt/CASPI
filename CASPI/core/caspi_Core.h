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
#include "caspi_CircularBuffer.h"
#include <cstddef>
#include <type_traits>

#define CASPI_COMMON_PRODUCER_LOGIC                                             \
public:                                                                         \
    virtual FloatType render() = 0;                                             \
                                                                                \
    void render (FloatType* buffer, const std::size_t n)                        \
    {                                                                           \
        for (std::size_t i = 0; i < n; ++i)                                     \
        {                                                                       \
            buffer[i] = render();                                               \
        }                                                                       \
    }                                                                           \
                                                                                \
    template <typename OutputIt>                                                \
    void render (OutputIt begin, OutputIt end)                                  \
    {                                                                           \
        auto n = std::distance (begin, end);                                    \
        render (&(*begin), n);                                                  \
    }                                                                           \
                                                                                \
    void render (CASPI::CircularBuffer<FloatType>& buffer, const std::size_t n) \
    {                                                                           \
        for (std::size_t i = 0; i < n; ++i)                                     \
        {                                                                       \
            buffer.write (render());                                            \
        }                                                                       \
    }                                                                           \
                                                                                \
    virtual ~Producer() = default;

#define CASPI_COMMON_PROCESSOR_LOGIC                                                     \
public:                                                                                  \
    virtual FloatType process (FloatType input) = 0;                                     \
                                                                                         \
    void process (FloatType* buffer, const std::size_t n)                                \
    {                                                                                    \
        for (std::size_t i = 0; i < n; ++i)                                              \
        {                                                                                \
            buffer[i] = process (buffer[i]);                                             \
        }                                                                                \
    }                                                                                    \
                                                                                         \
    void process (FloatType* in, FloatType* out, const std::size_t n)                    \
    {                                                                                    \
        for (std::size_t i = 0; i < n; ++i)                                              \
        {                                                                                \
            out[i] = process (in[i]);                                                    \
        }                                                                                \
    }                                                                                    \
                                                                                         \
    template <typename OutputIt>                                                         \
    void process (OutputIt begin, OutputIt end)                                          \
    {                                                                                    \
        auto n = std::distance (begin, end);                                             \
        process (&(*begin), n);                                                          \
    }                                                                                    \
                                                                                         \
    virtual void process (CASPI::CircularBuffer<FloatType>& buffer, const std::size_t n) \
    {                                                                                    \
        for (std::size_t i = 0; i < n; ++i)                                              \
        {                                                                                \
            buffer.write (process (buffer.read()));                                      \
        }                                                                                \
    }                                                                                    \
    virtual ~Processor() = default;

#define CASPI_COMMON_MODULATOR_LOGIC                                       \
public:                                                                    \
    virtual FloatType modulate()                                      = 0; \
    virtual void modulate (FloatType* buffer, std::size_t numSamples) = 0; \
    virtual ~Modulator()                                              = default;

#define CASPI_COMMON_SAMPLE_RATE_AWARE_LOGIC                                     \
public:                                                                          \
    void setSampleRate (FloatType sampleRate)                                    \
    {                                                                            \
        CASPI_ASSERT (sampleRate > 0, "Sample rate must be greater than zero."); \
        this->sampleRate = sampleRate;                                           \
    }                                                                            \
    FloatType getSampleRate() const                                              \
    {                                                                            \
        return sampleRate;                                                       \
    }                                                                            \
                                                                                 \
private:                                                                         \
    FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;

namespace CASPI::Core {
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
    template <FloatingPoint FloatType>
    class Producer
    {
        public:
            CASPI_COMMON_PRODUCER_LOGIC
    };

    /**
        * @class Processor
        * @brief Defines a common API for processors of audio data.
        *        Process are typically filters or other audio effects.
        *        They can process a single sample or a buffer of samples.
        *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
        */
    template <FloatingPoint FloatType>
    class Processor
    {
        public:
            CASPI_COMMON_PROCESSOR_LOGIC
    };

    template <FloatingPoint FloatType>
    class Modulator
    {
        public:
            CASPI_COMMON_MODULATOR_LOGIC
    };

    template <FloatingPoint FloatType>
    class SampleRateAware
    {
            CASPI_COMMON_SAMPLE_RATE_AWARE_LOGIC
    };

#else
    template<typename FloatType = double>
    class Producer {
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "Producer base class requires a floating-point type (float, double, long double)")
        ;

        CASPI_COMMON_PRODUCER_LOGIC
    };

    template<typename FloatType = double>
    class Processor {
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "Processor base class requires a floating-point type (float, double, long double)")
        ;

        CASPI_COMMON_PROCESSOR_LOGIC
    };

    template<typename FloatType = double>
    class Modulator {
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "Modulator requires a floating-point type");

        CASPI_COMMON_MODULATOR_LOGIC
    };

    template<typename FloatType = double>
    class SampleRateAware {
        CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                            "SampleRateAware requires a floating-point type");

        CASPI_COMMON_SAMPLE_RATE_AWARE_LOGIC
    };

    template<typename T>
    struct is_sample_rate_aware : std::false_type {
    };

#endif // CASPI_FEATURES_HAS_CONCEPTS

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

    [[maybe_unused]] inline void configureFlushToZero(bool enable) {
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
