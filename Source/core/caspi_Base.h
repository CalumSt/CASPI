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
* @file caspi_Base.h Defines common API base classes for algorithms.
* @author CS Islay
* @brief Defines common API base classes for algorithms.
*        Producers, processors, modulators, and SampleRateAware are defined here.
*        Producers are typically oscillators or other sound sources.
*        Processors are typically filters or other audio effects.
*        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
************************************************************************/
#include <cstddef>
#include <type_traits>
#include "caspi_Features.h"
#include "caspi_Assert.h"
#include "caspi_CircularBuffer.h"

namespace CASPI::Base
{
    #if defined(CASPI_FEATURES_HAS_CONCEPTS) && !defined(CASPI_FEATURES_DISABLE_CONCEPTS)

        template<typename T>
        concept FloatingPoint = std::is_floating_point_v<T>;

        /**
        * @class Producer
        * @brief Defines a common API for producers of audio data.
        *        Producers are typically oscillators or other sound sources.
        *        They can render a single sample or a buffer of samples.
        *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
        */
        template<FloatingPoint FloatType>
        class Producer 
        {
        public:
            virtual FloatType render() = 0;
            
            virtual void render(FloatType* buffer, const std::size_t n) = 0;
            
            // Render to a generic output range (e.g. vector, array, span)
            template <typename OutputIt>
            void render(OutputIt begin, OutputIt end) 
            {
                auto n = std::distance(begin, end);
                renderToBuffer(&(*begin), n);  // call internal pointer version
            }
            
            virtual ~Producer() = default;
            
        };

        /**
        * @class Processor
        * @brief Defines a common API for processors of audio data.
        *        Process are typically filters or other audio effects.
        *        They can process a single sample or a buffer of samples.
        *        If C++20 concepts are available, they will be used. Define CASPI_FEATURES_DISABLE_CONCEPTS to disable.
        */
        template<FloatingPoint FloatType>
        class Processor {
        public:
            virtual FloatType process() = 0;
            virtual void render(FloatType* buffer, const std::size_t n) = 0;
            
            // Render to a generic output range (e.g. vector, array, span)
            template <typename OutputIt>
            void render(OutputIt begin, OutputIt end) 
            {
                auto n = std::distance(begin, end);
                renderToBuffer(&(*begin), n);  // call internal pointer version
            }
            virtual ~Processor() = default;
        };

        template<FloatingPoint SampleType>
        class Modulator
        {
        public:
            virtual SampleType modulate() = 0;
            virtual void modulate(SampleType* buffer, std::size_t numSamples) = 0;
            virtual ~Modulator() = default;
        };

        template<FloatingPoint SampleType>
        class SampleRateAware
        {
        public:
            virtual void setSampleRate(SampleType sampleRate) = 0;
            virtual SampleType getSampleRate() const = 0;
            virtual ~SampleRateAware() = default;
        private:
            SampleType sampleRate = 0;
        };

    #else
        template <typename FloatType = double>
        class Producer
        {
            CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                      "Producer base class requires a floating-point type (float, double, long double)");
        public:
            virtual FloatType render() = 0;
            virtual void render(FloatType* buffer, const std::size_t n) = 0;
            
            // Render to a generic output range (e.g. vector, array, span)
            template <typename OutputIt>
            void render(OutputIt begin, OutputIt end) 
            {
                auto n = std::distance(begin, end);
                renderToBuffer(&(*begin), n);  // call internal pointer version
            }
            virtual ~Producer() = default;
        };

        template<typename FloatType = double>
        class Processor
        {
            CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                  "Processor base class requires a floating-point type (float, double, long double)");
        public:
            virtual FloatType process() = 0;
            virtual void render(FloatType* buffer, const std::size_t n) = 0;
            
            // Render to a generic output range (e.g. vector, array, span)
            template <typename OutputIt>
            void render(OutputIt begin, OutputIt end) 
            {
                auto n = std::distance(begin, end);
                renderToBuffer(&(*begin), n);  // call internal pointer version
            }
            virtual ~Processor() = default;
        };

        template<typename FloatType = double>
        class Modulator
        {
            CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                          "Modulator requires a floating-point type");
        public:
            virtual FloatType modulate() = 0;
            virtual void modulate(FloatType* buffer, std::size_t numSamples) = 0;
            virtual ~Modulator() = default;
        };

        template<typename FloatType = double>
        class SampleRateAware
        {
            CASPI_STATIC_ASSERT(std::is_floating_point<FloatType>::value,
                          "SampleRateAware requires a floating-point type");
        public:
            virtual void setSampleRate(FloatType sampleRate) = 0;
            virtual FloatType getSampleRate() const = 0;
            virtual ~SampleRateAware() = default;

        private:
            FloatType sampleRate = 0;
        };

    #endif // CASPI_FEATURES_HAS_CONCEPTS
}

#endif //CASPI_BASE_H
