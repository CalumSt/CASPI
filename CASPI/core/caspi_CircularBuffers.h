/************************************************************************
 .d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888


* @file caspi_CircularBuffers.h
* @author CS Islay
* @brief A DelayLine and FIFO class for audio processing.
*
************************************************************************/

#ifndef CASPI_CIRCULARBUFFERS_H
#define CASPI_CIRCULARBUFFERS_H

#include "caspi_AudioBuffer.h"

namespace CASPI
{

    //================================================================================
    // Interpolation Policies
    //================================================================================

    template <typename T>
    struct LinearInterpolation
    {
        static T apply (T a, T b, double frac)
        {
            return static_cast<T> ((1.0 - frac) * a + frac * b);
        }
    };

    template <typename T>
    struct NearestNeighbour
    {
        static T apply (T a, T b, double frac)
        {
            return (frac < 0.5) ? a : b;
        }
    };


    template <typename SampleType>
    using AudioFrame = std::vector<SampleType>;
    /**
     * @class DelayLine
     * @brief A delay line for audio processing, built on top of the AudioBuffer class.
     * @tparam SampleType
     */
    template <typename SampleType>
    class DelayLine
    {
    public:
        DelayLine(size_t channels, size_t delayFrames)
            : buffer_(channels, delayFrames), channels_(channels), delayFrames_(delayFrames), writePos_(0)
        {
            buffer_.clear();
        }

        void write(const AudioFrame<SampleType>& input)
        {
            for (size_t ch = 0; ch < channels_; ++ch)
            {
                buffer_.setSample(ch, writePos_, input[ch]);
            }
            writePos_ = (writePos_ + 1) % delayFrames_;
        }

        AudioFrame<SampleType> read(const std::size_t delay = 0) const
        {
            std::vector<SampleType> output(channels_);
            size_t readPos = (writePos_ + delayFrames_ - delay - 1) % delayFrames_;
            for (size_t ch = 0; ch < channels_; ++ch)
            {
                output[ch] = buffer_.sample(ch, readPos);
            }
            return output;
        }

    private:
        AudioBuffer<SampleType> buffer_;
        size_t channels_;
        size_t delayFrames_;
        size_t writePos_;
    };

    template <typename SampleType, template <typename> class Layout = InterleavedLayout>
class CircularAudioBuffer : public AudioBuffer<SampleType, Layout>
    {
    public:
        CircularAudioBuffer(size_t channels = 0, size_t frames = 0)
            : AudioBuffer<SampleType, Layout>(channels, frames),
              writePos_(0), readPos_(0) {}

        void write(const SampleType& value)
        {
            this->sample(0, writePos_) = value; // mono example
            writePos_ = (writePos_ + 1) % this->numFrames();
        }

        SampleType read()
        {
            auto val = this->sample(0, readPos_);
            readPos_ = (readPos_ + 1) % this->numFrames();
            return val;
        }

    private:
        size_t writePos_;
        size_t readPos_;
    };
}
#endif //CASPI_CIRCULARBUFFERS_H
