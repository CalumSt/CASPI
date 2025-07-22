
#include <gtest/gtest.h>
#include "../test_helpers.h"

#define private public // DO NOT DO THIS IN PRODUCTION
#include "core/caspi_CircularBuffer.h"

/*
TESTS
[DONE] Can construct a circular buffer of given samples
[DONE] Can read from mono circular buffer
[DONE] Can write to mono circular buffer
[DONE] Can resize a mono circular buffer
[DONE] Can resize a mono circular buffer and keep what's in the buffer
[DONE] Can construct a circular buffer of given channels
[DONE] Writing to circular buffer advances write index
[DONE] Reading from circular buffer wraps
[DONE] Can clear buffer
[DONE] Can create stereo buffer
[DONE] Can read from stereo buffer
[DONE] Can write to stereo buffer
[DONE] Can resize stereo buffer
[DONE] Can get the vector data from a mono buffer
[DONE] Can construct mono buffer from existing data (vector or plain array)
[DONE] Can copy buffer to new buffer
Can FFT on a buffer
Can store floats and doubles
Can store other types (ints, structures, classes)
Can be written to by multiple threads in a FIFO-style
 */

constexpr int numSamples    = 512;
constexpr int numChannels   = 2;
constexpr int newNumSamples = 1024;
constexpr int newNumChannels = 3;

template <typename T>
void printBuffer (const CASPI::CircularBuffer<T>& buffer)
{
    std::cout << "START BUFFER\n" << std::endl;
    for (int i = 0; i < buffer.getNumSamples(); i++)
    {
        std::cout << buffer.read(i) << std::endl;
    }
    std::cout << "END BUFFER\n" << std::endl;
}

TEST (CircularBufferTests,constructBufferOfGivenSize_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    ASSERT_EQ (buffer.getNumSamples(), numSamples);
}

TEST (CircularBufferTests,readMonoBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    auto sample = buffer.read(100);
    ASSERT_EQ (sample,0.0);
}

TEST (CircularBufferTests,writeMonoBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    buffer.write(1.0);
    auto sample = buffer.read(numSamples+1);
    ASSERT_EQ (sample,1.0);
}

TEST (CircularBufferTests,resizeMonoBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    buffer.resize(newNumSamples);
    ASSERT_EQ (buffer.getNumSamples(), newNumSamples);
    ASSERT_EQ (buffer.buffer->size(), newNumSamples);

}

TEST (CircularBufferTests,resizeAndKeepExistingMonoBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); }
    buffer.resize(newNumSamples);
    // The first numSamples samples should be the same
    ASSERT_EQ (buffer.read(numSamples+1), 1.0);
    ASSERT_EQ (buffer.read(numSamples), 0.0);

}

TEST (CircularBufferTests,advanceWriteIndex_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    ASSERT_EQ (buffer.writeIndex, 0);
    buffer.write(1.0);
    ASSERT_EQ (buffer.writeIndex, 1);
    buffer.write(1.0);
    ASSERT_EQ (buffer.writeIndex, 2);
}

TEST (CircularBufferTests,writeIndexWrap_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    ASSERT_EQ (buffer.writeIndex, 0);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); } // write numSamples times
    ASSERT_EQ (buffer.writeIndex, 0);
}

TEST (CircularBufferTests,readWrap_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    buffer.write(1.0);
    auto sample = buffer.read(numSamples + 1);
    ASSERT_EQ (sample, 1.0);
}

TEST (CircularBufferTests,constructOnVector_test)
{
    const auto vector = std::vector<double> (numSamples, 1.0);
    const CASPI::CircularBuffer buffer(vector);
    ASSERT_EQ (buffer.getNumSamples(), numSamples);
    for (int i = 0; i < numSamples; i++) { ASSERT_EQ (buffer.read(i), 1.0); }
}

TEST (CircularBufferTests,getVectorData_test)
{
    const auto vector = std::vector<double> (numSamples, 1.0);
    const CASPI::CircularBuffer buffer(vector);
    auto newVector = buffer.getBufferAsVector();
    ASSERT_EQ (newVector.size(), numSamples);
    compareVectors (vector,newVector);
}

TEST (CircularBufferTests,clearBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); } // write numSamples times
    buffer.clear();
    for (int i = 0; i < numSamples; i++) { ASSERT_EQ (buffer.read(i), 0.0); } // check each value is 0
}

TEST (CircularBufferTests, linearInterpolation_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    buffer.write(1.0);
    buffer.write (2.0);
    const auto fractionalDelay = static_cast<double>(numSamples)+ 1.5;
    const auto sample = buffer.read(fractionalDelay,true);
    ASSERT_EQ (sample, 1.5);
}

TEST (CircularBufferTests, copyBuffer_test)
{
    CASPI::CircularBuffer buffer(numSamples);
    buffer.write (-1.0);
    buffer.write (2.0);

    const auto& newBuffer = buffer;

    ASSERT_EQ (newBuffer.read(2), -1.0);
    ASSERT_EQ (newBuffer.read(1), 2.0);

}

#undef private