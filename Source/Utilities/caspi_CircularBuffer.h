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
#include "caspi_assert.h"

namespace CASPI
{
class CircularBuffer
/**
 * @class CircularBuffer
 * @brief A circular buffer for audio. Only contains one channel.
 */
{
public:
    explicit CircularBuffer (const size_t numSamples) noexcept : numSamples (numSamples)
    {
        buffer   = std::make_unique<std::vector<double>> (numSamples, 0.0);
        wrapMask = static_cast<int> (numSamples) - 1;
    }

    explicit CircularBuffer (const std::vector<double>& data) noexcept : numSamples (data.size())
    {
        buffer   = std::make_unique<std::vector<double>> (data);
        wrapMask = static_cast<int> (numSamples) - 1;
    }

    // Assignment operator - copy part of copy-swap
    CircularBuffer& operator= (CircularBuffer other) noexcept
    {
        if (this != &other)
        {
            swap(*this, other);
        }
        return *this;
    }

    // swap part of copy-swap idiom
    friend void swap (CircularBuffer& a, CircularBuffer& b) noexcept
    {
        using std::swap;
        swap (a.buffer, b.buffer);
        swap (a.numSamples, b.numSamples);
        swap (a.wrapMask, b.wrapMask);
        swap (a.writeIndex, b.writeIndex);
    }

    // Move constructor
    CircularBuffer (CircularBuffer&& other) noexcept
        : buffer (std::move (other.buffer)),
          numSamples (other.numSamples),
          wrapMask (other.wrapMask),
          writeIndex (other.writeIndex)
    {
        other.numSamples = 0;
        other.wrapMask   = 0;
        other.writeIndex = 0;
    }

    // Copy constructor
    CircularBuffer (const CircularBuffer& other) noexcept
        : buffer(std::make_unique<std::vector<double>>(*other.buffer)),
          numSamples(other.numSamples),
          wrapMask(other.wrapMask),
          writeIndex(other.writeIndex)
    {
    }


    [[nodiscard]] size_t getNumSamples() const { return numSamples; }

    [[nodiscard]] double read (const int delayInSamples) const
    {
        // The buffer is linearised.
        int readIndex  = writeIndex - delayInSamples;
        readIndex     &= wrapMask;
        return buffer->at (readIndex);
    }

    void write (const double value)
    {
        /*
         * Write to a specific channel. Can only assign 1 channel at a time.
         *
         * @param value The value to write.
         * @param channel The channel to write to (0-indexed).
         */
        buffer->at (writeIndex) = value;
        writeIndex++;
        writeIndex &= wrapMask;
    }

    void resize (const size_t _numSamples)
    {
        auto newBuffer = std::vector<double> (_numSamples, 0.0);

        if (_numSamples > numSamples)
        {
            for (int i = 0; i < numSamples; i++)
            {
                newBuffer.at (i) = buffer->at (i);
            }
        }
        else if (_numSamples <= numSamples)
        {
            for (int i = 0; i < _numSamples; i++)
            {
                newBuffer.at (i) = buffer->at (i);
            }
        }

        buffer     = std::make_unique<std::vector<double>> (newBuffer);
        wrapMask   = static_cast<int> (_numSamples) - 1;
        numSamples = _numSamples;
    }

    void clear()
    {
        buffer   = std::make_unique<std::vector<double>> (numSamples, 0.0);
        wrapMask = static_cast<int> (numSamples) - 1;
    }

    [[nodiscard]] std::vector<double> getBufferAsVector() const
    {
        // dereference the unique pointer
        return *buffer;
    }

    [[nodiscard]] double read (const double fractionalDelay, const bool interpolate = true) const
    {
        // Truncate and read the integer part of the delay
        const double y1 = read (static_cast<int> (fractionalDelay));
        // if no interpolation, return y1 as is
        if (! interpolate)
            return y1;
        // read sample before
        const double y2 = read (static_cast<int> (fractionalDelay) + 1);
        // interpolate
        const double fraction = fractionalDelay - static_cast<int> (fractionalDelay);

        return linearInterpolation (y1, y2, fraction);
    }

private:
    std::unique_ptr<std::vector<double>> buffer = nullptr;
    size_t numSamples                           = 0;
    size_t numChannels                          = 1;
    int wrapMask                                = 0;
    int writeIndex                              = 0;
    int readIndex                               = 0;

    static double linearInterpolation (const double y1, const double y2, const double fractional_X)
    {
        // check for invalid inputs
        if (fractional_X >= 1.0)
            return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (1.0 - fractional_X) * y1;
    }
};

class MultichannelBuffer
/**
 * @class MultichannelBuffer
 * @brief A circular buffer for holding multiple channels of data.
 */
{
public:
    explicit MultichannelBuffer (const size_t numSamples, const size_t numChannels) : numSamples (numSamples), numChannels (numChannels)
    {
        buffer.resize (numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i) = std::make_unique<CircularBuffer> (CircularBuffer (numSamples));
        }
    }

    explicit MultichannelBuffer (const std::vector<double>& data, const size_t numChannels) : numSamples (data.size()), numChannels (numChannels)
    {
        buffer.resize (numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i) = std::make_unique<CircularBuffer> (CircularBuffer (data));
        }
    }

    [[nodiscard]] size_t getNumSamples() const { return numSamples; }
    [[nodiscard]] size_t getNumChannels() const { return numChannels; }

    void write (const std::vector<double>& frame) const
    {
        CASPI_ASSERT (frame.size() == numChannels, "Frame size does not match number of channels to write.");

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i)->write (frame.at (i));
        }
    }

    [[nodiscard]] std::vector<double> read (const int delayInSamples) const
    {
        auto frame = std::vector<double> (numChannels, 0.0);
        for (int i = 0; i < numChannels; i++)
        {
            frame.at (i) = buffer.at (i)->read (delayInSamples);
        }
        return frame;
    }

    [[nodiscard]] std::vector<double> read (const double fractionalDelay, const bool interpolate = true) const
    {
        auto frame = std::vector<double> (numChannels, 0.0);
        for (int i = 0; i < numChannels; i++)
        {
            frame.at (i) = buffer.at (i)->read (fractionalDelay, interpolate);
        }
        return frame;
    }

    void resize (const size_t _numSamples, const size_t _numChannels)
    {
        numSamples  = _numSamples;
        numChannels = _numChannels;
        buffer.resize (numChannels);
        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i)->resize (_numSamples);
        }
    }

    void clear() const
    {
        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i)->clear();
        }
    }

private:
    std::vector<std::unique_ptr<CircularBuffer>> buffer;
    size_t numSamples  = 0;
    size_t numChannels = 2;
};
} // namespace CASPI

#endif //CASPI_CIRCULARBUFFER_NEW_H
