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


* @file caspi_CircularBuffer.h
* @author CS Islay
* @brief A collection of useful buffers for audio.
*
************************************************************************/

#ifndef CASPI_CIRCULARBUFFER_NEW_H
#define CASPI_CIRCULARBUFFER_NEW_H
#include "caspi_Assert.h"
#include <memory>
#include <vector>

namespace CASPI
{
/**
 * @class CircularBuffer
 * @brief A circular buffer for audio. Only contains one channel.
 */
template <typename SampleType = double>
class CircularBuffer
{
public:
    explicit CircularBuffer (size_t numSamples) noexcept
        : buffer (numSamples, static_cast<SampleType> (0)),
          numSamples (numSamples) {}

    explicit CircularBuffer (const std::vector<SampleType>& data) noexcept
        : buffer (data), numSamples (data.size()) {}

    CircularBuffer& operator= (CircularBuffer other) noexcept
    {
        if (this != &other)
        {
            swap (*this, other);
        }
        return *this;
    }

    friend void swap (CircularBuffer& a, CircularBuffer& b) noexcept
    {
        using std::swap;
        swap (a.buffer, b.buffer);
        swap (a.numSamples, b.numSamples);
        swap (a.writeIndex, b.writeIndex);
    }

    CircularBuffer (CircularBuffer&& other) noexcept      = default;
    CircularBuffer (const CircularBuffer& other) noexcept = default;

    [[nodiscard]] size_t getNumSamples() const { return numSamples; }

    [[nodiscard]] SampleType read (int delayInSamples) const
    {
        int readIndex = writeIndex - delayInSamples;
        if (readIndex < 0)
            readIndex += static_cast<int> (numSamples);
        return buffer[readIndex % numSamples];
    }

    [[nodiscard]] SampleType read (SampleType fractionalDelay, bool interpolate = true) const
    {
        const int index1    = static_cast<int> (fractionalDelay);
        const int index2    = index1 + 1;
        const SampleType y1 = read (index1);
        if (! interpolate)
            return y1;
        const SampleType y2   = read (index2);
        const SampleType frac = fractionalDelay - static_cast<int> (fractionalDelay);
        return linearInterpolation (y1, y2, frac);
    }

    void write (SampleType value)
    {
        buffer[writeIndex] = value;
        writeIndex         = (writeIndex + 1) % numSamples;
    }

    void resize (size_t newSize)
    {
        std::vector<SampleType> newBuffer (newSize, static_cast<SampleType> (0));
        size_t minSize = std::min (newSize, numSamples);
        for (size_t i = 0; i < minSize; ++i)
        {
            newBuffer[i] = buffer[i];
        }
        buffer      = std::move (newBuffer);
        numSamples  = newSize;
        writeIndex %= numSamples;
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), static_cast<SampleType> (0));
        writeIndex = 0;
    }

    [[nodiscard]] std::vector<SampleType> getBufferAsVector() const
    {
        return buffer;
    }

    [[nodiscard]] SampleType* ptr()
    {
        return buffer.data();
    }

    [[nodiscard]] const SampleType* ptr() const
    {
        return buffer.data();
    }

private:
    std::vector<SampleType> buffer;
    size_t numSamples = 0;
    size_t writeIndex = 0;

    static SampleType linearInterpolation (SampleType y1, SampleType y2, SampleType frac)
    {
        if (frac >= 1.0)
            return y2;
        return frac * y2 + (1.0 - frac) * y1;
    }
};

} // namespace CASPI

#endif //CASPI_CIRCULARBUFFER_NEW_H
