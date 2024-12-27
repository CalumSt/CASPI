
#include <gtest/gtest.h>
#include "test_helpers.h"

#define private public // DO NOT DO THIS IN PRODUCTION
#include "Utilities/caspi_CircularBuffer_new.h"

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
[DONE] Can construct stereo buffer from existing data
[DONE] Can read from buffer with fractional delay
Can read from multichannel buffer with fractional delay
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

void printBuffer (const CASPI::CircularBuffer_new& buffer)
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
    CASPI::CircularBuffer_new buffer(numSamples);
    ASSERT_EQ (buffer.getNumSamples(), numSamples);
}

TEST (CircularBufferTests,readMonoBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    auto sample = buffer.read(100);
    ASSERT_EQ (sample,0.0);
}

TEST (CircularBufferTests,writeMonoBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    buffer.write(1.0);
    auto sample = buffer.read(numSamples+1);
    ASSERT_EQ (sample,1.0);
}

TEST (CircularBufferTests,resizeMonoBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    buffer.resize(newNumSamples);
    ASSERT_EQ (buffer.getNumSamples(), newNumSamples);
    ASSERT_EQ (buffer.buffer->size(), newNumSamples);

}

TEST (CircularBufferTests,resizeAndKeepExistingMonoBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); }
    buffer.resize(newNumSamples);
    // The first numSamples samples should be the same
    ASSERT_EQ (buffer.read(numSamples+1), 1.0);
    ASSERT_EQ (buffer.read(numSamples), 0.0);

}

TEST (CircularBufferTests,advanceWriteIndex_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    ASSERT_EQ (buffer.writeIndex, 0);
    buffer.write(1.0);
    ASSERT_EQ (buffer.writeIndex, 1);
    buffer.write(1.0);
    ASSERT_EQ (buffer.writeIndex, 2);
}

TEST (CircularBufferTests,writeIndexWrap_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    ASSERT_EQ (buffer.writeIndex, 0);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); } // write numSamples times
    ASSERT_EQ (buffer.writeIndex, 0);
}

TEST (CircularBufferTests,readWrap_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    buffer.write(1.0);
    auto sample = buffer.read(numSamples + 1);
    ASSERT_EQ (sample, 1.0);
}

TEST (CircularBufferTests,constructOnVector_test)
{
    const auto vector = std::vector<double> (numSamples, 1.0);
    const CASPI::CircularBuffer_new buffer(vector);
    ASSERT_EQ (buffer.getNumSamples(), numSamples);
    for (int i = 0; i < numSamples; i++) { ASSERT_EQ (buffer.read(i), 1.0); }
}

TEST (CircularBufferTests,getVectorData_test)
{
    const auto vector = std::vector<double> (numSamples, 1.0);
    const CASPI::CircularBuffer_new buffer(vector);
    auto newVector = buffer.getBufferAsVector();
    ASSERT_EQ (newVector.size(), numSamples);
    compareVectors (vector,newVector);
}

TEST (CircularBufferTests,clearBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    for (int i = 0; i < numSamples; i++) { buffer.write(1.0); } // write numSamples times
    buffer.clear();
    for (int i = 0; i < numSamples; i++) { ASSERT_EQ (buffer.read(i), 0.0); } // check each value is 0
}

TEST (CircularBufferTests,constructStereoBuffer_test)
{
    CASPI::MultichannelBuffer buffer(numSamples, numChannels);
    ASSERT_EQ (buffer.getNumSamples(), numSamples);
    ASSERT_EQ (buffer.getNumChannels(), numChannels);
}

TEST (CircularBufferTests,readStereoBuffer_test)
{
    CASPI::MultichannelBuffer buffer(numSamples, numChannels);
    auto sample = buffer.read(100);
    ASSERT_EQ (sample.size(), numChannels);
    ASSERT_EQ (sample[0],0.0);
    ASSERT_EQ (sample[1],0.0);

}

TEST (CircularBufferTests,writeStereoBuffer_test)
{
    CASPI::MultichannelBuffer buffer (numSamples, numChannels);
    const auto frame = std::vector<double> (numChannels, 1.0); // write 1.0 to each channel
    buffer.write (frame);
    const auto sample = buffer.read(numSamples+1);
    ASSERT_EQ (sample.size(), numChannels);
    ASSERT_EQ (sample.at(0),1.0);
    ASSERT_EQ (sample.at(1),1.0);

}

TEST (CircularBufferTests,clearStereoBuffer_test)
{
    CASPI::MultichannelBuffer buffer (numSamples, numChannels);
    const auto frame = std::vector<double> (numChannels, 1.0); // write 1.0 to each channel
    buffer.write (frame);
     auto sample = buffer.read(numSamples+1);
    ASSERT_EQ (sample.size(), numChannels);
    ASSERT_EQ (sample.at(0),1.0);
    ASSERT_EQ (sample.at(1),1.0);
    buffer.clear();
    sample = buffer.read(numSamples+1);
    ASSERT_EQ (sample.size(), numChannels);
    ASSERT_EQ (sample.at(0),0.0);
    ASSERT_EQ (sample.at(1),0.0);
}

TEST (CircularBufferTests,resizeStereoBuffer_test)
{
    CASPI::MultichannelBuffer buffer (numSamples, numChannels);
    buffer.resize (newNumSamples, numChannels);
    ASSERT_EQ (buffer.getNumSamples(), newNumSamples);
    ASSERT_EQ (buffer.getNumChannels(), numChannels);
    ASSERT_EQ (buffer.buffer.size(), numChannels);
}

TEST (CircularBufferTests, constructStereoBufferFromData_test)
{
    auto vector = std::vector<double> (numSamples, 1.0);
    CASPI::MultichannelBuffer buffer (vector, numChannels);
    auto expectedFrame = std::vector<double> (numChannels, 1.0);

    for (int i = 0; i < numSamples; i++)
    {
        auto frame = buffer.read (i);
        compareVectors (expectedFrame, frame);
    }
}

TEST (CircularBufferTests, linearInterpolation_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    buffer.write(1.0);
    buffer.write (2.0);
    const auto fractionalDelay = static_cast<double>(numSamples)+ 1.5;
    const auto sample = buffer.read(fractionalDelay,true);
    ASSERT_EQ (sample, 1.5);
}

TEST (CircularBufferTests, copyBuffer_test)
{
    CASPI::CircularBuffer_new buffer(numSamples);
    buffer.write (-1.0);
    buffer.write (2.0);

    const auto& newBuffer = buffer;

    ASSERT_EQ (newBuffer.read(2), -1.0);
    ASSERT_EQ (newBuffer.read(1), 2.0);

}

TEST (CircularBufferTests, stereoFractionalDelay_test)
{
    CASPI::MultichannelBuffer buffer(numSamples,numChannels);
    auto frame1 = std::vector<double> (numChannels, 1.0);
    auto frame2 = std::vector<double> (numChannels, 2.0);
    buffer.write (frame1);
    buffer.write (frame2);
    const auto fractionalDelay = static_cast<double>(numSamples)+ 1.5;
    const auto sample = buffer.read(fractionalDelay,true);
    ASSERT_EQ (sample.at(0), 1.5);
    ASSERT_EQ (sample.at(1), 1.5);

}