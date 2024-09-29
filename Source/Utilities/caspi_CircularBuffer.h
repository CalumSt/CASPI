/**
* @file caspi_CircularBuffer.h
 * @author CS Islay
 * @brief A template class implementing a circular buffer data structure.
 *
 * This class provides a circular buffer implementation, allowing for efficient
 * storage and retrieval of data in a ring buffer structure.
 * The intention is for it to be used with audio data, so has linear interpolation
 * It's very C-like in its implementation, so should definitely be modernised.
 */

#ifndef CASPI_CIRCULARBUFFER_H
#define CASPI_CIRCULARBUFFER_H
#include <cmath>
#include <memory>

/**
 * @class caspi_CircularBuffer
 * @brief A template class implementing a circular buffer data structure, allowing for efficient
 * storage and retrieval of data in a ring buffer structure.
 *
 * @tparam FloatType The type of data to be stored in the buffer.
 */


template <typename FloatType>
class caspi_CircularBuffer
{
public:
    /**
     * @brief Creates a new circular buffer with the specified length.
     *
     * The buffer length will be rounded up to the nearest power of 2.
     *
     * @param _bufferLength The desired length of the buffer.
     */
    void createCircularBuffer(unsigned int _bufferLength) {
        // reset to top
        writeIndex = 0;

        // find nearest power of 2 for buffer, save as buffer length
        bufferLength = static_cast<unsigned int>(pow(2, ceil(log(_bufferLength) / log(2))));

        // save for wrapping mask
        wrapMask = bufferLength - 1;

        // create new buffer
        buffer.reset(new FloatType[bufferLength]);

        // flush buffer
        clear();
    }

    /**
     * @brief Reads data from the buffer with the specified delay.
     *
     * The delay is specified in samples, and the function will return the data
     * that was written to the buffer the specified number of samples ago.
     *
     * @param delayInSamples The delay in samples.
     * @return The data read from the buffer.
     */
    FloatType readBuffer(const int delayInSamples)
        {
        // subtract to make read index
        int readIndex = writeIndex - delayInSamples;
        // autowrap index
        readIndex &= wrapMask;
        // read buffer
        return buffer[readIndex];
        }

    /**
      * @brief Reads data from the buffer with the specified fractional delay.
      *
      * The delay is specified in fractional samples, and the function will return
      * the interpolated data that was written to the buffer the specified number
      * of fractional samples ago.
      *
      * @param delayInFractionalSamples The delay in fractional samples.
      * @param interpolate Whether to perform interpolation (default: true).
      * @return The interpolated data read from the buffer.
      */

    FloatType readBuffer(FloatType delayInFractionalSamples, const bool interpolate = true)
        {
        // Truncate and read the integer part of the delay
        FloatType y1 = readBuffer(static_cast<int>(delayInFractionalSamples));
        // if no interpolation, return y1 as is
        if (!interpolate) return y1;
        // read sample before
        FloatType y2 = readBuffer(static_cast<int>(delayInFractionalSamples) + 1);
        // interpolate
        double fraction = delayInFractionalSamples - static_cast<int>(delayInFractionalSamples);

        return linearInterpolation(y1, y2, fraction);
        }

    /**
     * @brief Performs linear interpolation between two values.
     *
     * @param y1 The first value.
     * @param y2 The second value.
     * @param fractional_X The fractional value between 0 and 1.
     * @return The interpolated value.
     */

    auto linearInterpolation(const FloatType y1, const FloatType y2, const FloatType fractional_X)
        {
        auto one = static_cast<FloatType>(1.0);
        // check for invalid inputs
        if (fractional_X >= one) return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (one - fractional_X) * y1;
        }

    /**
     * @brief Writes data to the buffer.
     *
     * @param input The data to write to the buffer.
     */
    void writeBuffer(FloatType input)
    {
        /// write and increment index counter
        buffer[writeIndex++] = input;
        /// wrap if index > buffer length - 1
        writeIndex &= wrapMask;
    }

    /**
     * @brief Clears the buffer by setting all elements to zero.
     */
    void clear() { memset(&buffer[0], 0, bufferLength * sizeof(FloatType)); }


private:
    std::unique_ptr<FloatType[]> buffer = nullptr;
    unsigned int writeIndex = 0;
    unsigned int bufferLength = 0;
    unsigned int wrapMask = 0;

};

#endif //CASPI_CIRCULARBUFFER_H
