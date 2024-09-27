/**
* @file caspi_CircularBuffer.h
 * @author CS Islay
 * @brief A template class implementing a circular buffer data structure.
 *
 * This class provides a circular buffer implementation, allowing for efficient
 * storage and retrieval of data in a ring buffer structure.
 */

#ifndef CASPI_CIRCULARBUFFER_H
#define CASPI_CIRCULARBUFFER_H
#include <cmath>
#include <memory>

/**
 * @class CircularBuffer
 * @brief A template class implementing a circular buffer data structure.
 *
 * This class provides a circular buffer implementation, allowing for efficient
 * storage and retrieval of data in a ring buffer structure.
 *
 * @tparam T The type of data to be stored in the buffer.
 */


template <typename T>
class CircularBuffer
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
        buffer.reset(new T[bufferLength]);

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
    T readBuffer(int delayInSamples)
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
    template <typename FractionalDelayType>
    T readBuffer(FractionalDelayType delayInFractionalSamples, const bool interpolate = true)
        {
        // Truncate and read the integer part of the delay
        T y1 = readBuffer(static_cast<int>(delayInFractionalSamples));
        // if no interpolation, return y1 as is
        if (!interpolate) return y1;
        // read sample before
        T y2 = readBuffer(static_cast<int>(delayInFractionalSamples) + 1);
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
    template<typename dataType, typename fractionalType>
    auto linearInterpolation(const dataType y1, const dataType y2, const fractionalType fractional_X)
        {
        // check for invalid inputs
        if (fractional_X >= 1.0) return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (1.0 - fractional_X) * y1;
        }

    /**
     * @brief Writes data to the buffer.
     *
     * @param input The data to write to the buffer.
     */
    void writeBuffer(T input)
    {
        /// write and increment index counter
        buffer[writeIndex++] = input;
        /// wrap if index > buffer length - 1
        writeIndex &= wrapMask;
    }

    /**
     * @brief Clears the buffer by setting all elements to zero.
     */
    void clear() { memset(&buffer[0], 0, bufferLength * sizeof(T)); }

private:
    std::unique_ptr<T[]> buffer = nullptr;
    unsigned int writeIndex = 0;
    unsigned int bufferLength = 0;
    unsigned int wrapMask = 0;

};

#endif //CASPI_CIRCULARBUFFER_H
