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
#include <vector>
#include <memory>

namespace CASPI
{
/**
 * @class CircularBuffer
 * @brief A circular buffer for audio. Only contains one channel.
 */
template <typename SampleType=double>
class CircularBuffer
{
public:
    explicit CircularBuffer (const size_t numSamples) noexcept : numSamples (numSamples)
    {
        buffer   = std::make_unique<std::vector<SampleType>> (numSamples, 0.0);
        wrapMask = static_cast<int> (numSamples) - 1;
    }

    explicit CircularBuffer (const std::vector<SampleType>& data) noexcept : numSamples (data.size())
    {
        buffer   = std::make_unique<std::vector<SampleType>> (data);
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

    /**
 	 * @brief Swaps the contents of two CircularBuffer objects.
 	 *
	 * @param a The first CircularBuffer object.
	 * @param b The second CircularBuffer object.
	 */
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
        : buffer(std::make_unique<std::vector<SampleType>>(*other.buffer)),
          numSamples(other.numSamples),
          wrapMask(other.wrapMask),
          writeIndex(other.writeIndex)
    {
    }
    /**
     * @brief Gets the buffer length.
     *
     * @return The number of samples.
     */
    [[nodiscard]] size_t getNumSamples() const { return numSamples; }


	/**
 	 * @brief Reads a sample from the buffer.
 	 *
	 * @param delayInSamples The delay in samples.
	 * @return The read sample.
	 */
    [[nodiscard]] SampleType read (const int delayInSamples) const
    {
        // The buffer is linearised.
        int _readIndex  = writeIndex - delayInSamples;
        _readIndex     &= wrapMask;
        return buffer->at (_readIndex);
    }

	/**
	 * @brief Reads a sample from the buffer with interpolation.
	 *
	 * @param fractionalDelay The fractional delay.
	 * @param interpolate Whether to interpolate or not.
	 * @return The read sample.
	 */
	[[nodiscard]] SampleType read (const SampleType fractionalDelay, const bool interpolate = true) const

    {
        // Truncate and read the integer part of the delay
        const SampleType y1 = read (static_cast<int> (fractionalDelay));
        // if no interpolation, return y1 as is
        if (! interpolate)
            return y1;
        // read sample before
        const SampleType y2 = read (static_cast<int> (fractionalDelay) + 1);
        // interpolate
        const SampleType fraction = fractionalDelay - static_cast<int> (fractionalDelay);

        return linearInterpolation (y1, y2, fraction);
    }

	/**
	 * @brief Writes a sample to the buffer.
	 *
	 * @param value The value to write.
	 */
    void write (const SampleType value)
    {
        buffer->at (writeIndex) = value;
        writeIndex++;
        writeIndex &= wrapMask;
    }

	/**
	 * @brief Resizes the buffer.
	 *
	 * @param _numSamples The new number of samples.
	 */
    void resize (const size_t _numSamples)
    {
        auto newBuffer = std::vector<SampleType> (_numSamples, 0.0);

        if (_numSamples > numSamples)
        {
            for (int i = 0; i < numSamples; i++)
            {
                newBuffer.at (i) = buffer->at (i);
            }
        }
        else
        {
            for (int i = 0; i < _numSamples; i++)
            {
                newBuffer.at (i) = buffer->at (i);
            }
        }

        buffer     = std::make_unique<std::vector<SampleType>> (newBuffer);
        wrapMask   = static_cast<int> (_numSamples) - 1;
        numSamples = _numSamples;
    }

	/**
 	 * @brief Clears the buffer.
 	 */
    void clear()
    {
        buffer   = std::make_unique<std::vector<SampleType>> (numSamples, 0.0);
        wrapMask = static_cast<int> (numSamples) - 1;
    }

	/**
	 * @brief Gets the buffer as a vector.
	 *
	 * @return The buffer as a vector.
	 */
    [[nodiscard]] std::vector<SampleType> getBufferAsVector() const
    {
        // dereference the unique pointer
        return *buffer;
    }

private:
    std::unique_ptr<std::vector<SampleType>> buffer = nullptr;
    size_t numSamples                           = 0;
    size_t numChannels                          = 1;
    int wrapMask                                = 0;
    int writeIndex                              = 0;
    int readIndex                               = 0;

    static SampleType linearInterpolation (const SampleType y1, const SampleType y2, const SampleType fractional_X)
    {
        // check for invalid inputs
        if (fractional_X >= 1.0)
            return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (1.0 - fractional_X) * y1;
    }
};

/**
 * @class MultichannelBuffer
 * @brief A circular buffer for holding multiple channels of data.
 */
template <typename SampleType=double>
class MultichannelBuffer
{
public:
	/**
     * @brief Constructs a new MultichannelBuffer object.
     *
     * @param numSamples The number of samples in the buffer.
     * @param numChannels The number of channels in the buffer.
     */
    explicit MultichannelBuffer (const size_t numSamples, const size_t numChannels) noexcept : numSamples (numSamples), numChannels (numChannels)
    {
        buffer.resize (numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i) = std::make_unique<CircularBuffer<SampleType>> (CircularBuffer<SampleType> (numSamples));
        }
    }

	/**
     * @brief Constructs a new MultichannelBuffer object from a vector of data.
     *
     * @param data The vector of data to initialize the buffer with.
     * @param numChannels The number of channels in the buffer.
     */
    explicit MultichannelBuffer (const std::vector<SampleType>& data, const size_t numChannels) noexcept : numSamples (data.size()), numChannels (numChannels)
    {
        buffer.resize (numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i) = std::make_unique<CircularBuffer<SampleType>> (CircularBuffer<SampleType> (data));
        }
    }


    /**
     * @brief Gets the number of samples in the buffer.
     *
     * @return The number of samples.
     */
    [[nodiscard]] size_t getNumSamples() const { return numSamples; }

    /**
     * @brief Gets the number of channels in the buffer.
     *
     * @return The number of channels.
     */
    [[nodiscard]] size_t getNumChannels() const { return numChannels; }

	/**
     * @brief Writes a frame of data to the buffer.
     *
     * @param frame The frame of data to write.
     */
    void write (const std::vector<SampleType>& frame) const
    {
        CASPI_ASSERT (frame.size() == numChannels, "Frame size does not match number of channels to write.");

        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i)->write (frame.at (i));
        }
    }

	/**
     * @brief Reads a frame of data from the buffer.
     *
     * @param delayInSamples The delay in samples.
     * @return The read frame of data.
     */
    [[nodiscard]] std::vector<SampleType> read (const int delayInSamples) const
    {
        auto frame = std::vector<SampleType> (numChannels, 0.0);
        for (int i = 0; i < numChannels; i++)
        {
            frame.at (i) = buffer.at (i)->read (delayInSamples);
        }
        return frame;
    }

    /**
	 * @brief Reads a frame of data from the buffer, linearly interpolating between samples.
	 *
	 * @param fractionalDelay The fractional delay in samples.
     * @param interpolate Whether to interpolate or not.
	 * @return The read frame of data.
	 */
    [[nodiscard]] std::vector<SampleType> read (const SampleType fractionalDelay, const bool interpolate = true) const
    {
        auto frame = std::vector<SampleType> (numChannels, 0.0);
        for (int i = 0; i < numChannels; i++)
        {
            frame.at (i) = buffer.at (i)->read (fractionalDelay, interpolate);
        }
        return frame;
    }

	/**
     * @brief Resizes the buffer.
     *
     * @param _numSamples The new number of samples.
     * @param _numChannels The new number of channels.
     */
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

	/**
     * @brief Clears the buffer.
     */
    void clear() const
    {
        for (int i = 0; i < numChannels; i++)
        {
            buffer.at (i)->clear();
        }
    }

private:
    std::vector<std::unique_ptr<CircularBuffer<SampleType>>> buffer;
    size_t numSamples  = 0;
    size_t numChannels = 2;
};
} // namespace CASPI

#endif //CASPI_CIRCULARBUFFER_NEW_H
