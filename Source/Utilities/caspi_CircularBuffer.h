#ifndef CASPI_CIRCULARBUFFER_H
#define CASPI_CIRCULARBUFFER_H
#include <cmath>
#include <memory>

template <typename T>
class CircularBuffer
{

    std::unique_ptr<T[]> buffer = nullptr;

    unsigned int writeIndex = 0;

    unsigned int bufferLength = 0;

    unsigned int wrapMask = 0;

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

    T readBuffer(int delayInSamples)
        {
        // subtract to make read index
        int readIndex = writeIndex - delayInSamples;
        // autowrap index
        readIndex &= wrapMask;
        // read buffer
        return buffer[readIndex];
        }

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


    template<typename dataType, typename fractionalType>
    auto linearInterpolation(const dataType y1, const dataType y2, const fractionalType fractional_X)
        {
        // check for invalid inputs
        if (fractional_X >= 1.0) return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (1.0 - fractional_X) * y1;
        }


    void writeBuffer(T input)
    {
        /// write and increment index counter
        buffer[writeIndex++] = input;
        /// wrap if index > buffer length - 1
        writeIndex &= wrapMask;
    }

    void clear() { memset(&buffer[0], 0, bufferLength * sizeof(T)); }
};

#endif //CASPI_CIRCULARBUFFER_H
