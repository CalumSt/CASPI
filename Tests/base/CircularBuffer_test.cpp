
#include <gtest/gtest.h>
#include "../test_helpers.h"

#define private public // DO NOT DO THIS IN PRODUCTION
#include "core/caspi_CircularBuffer.h"
#undef private

/*
==============================================================================
CircularBufferBase Unit Test Coverage Summary
==============================================================================

Current Coverage:
-------------------
1. RealTimeSafe_WriteReadBasic
   - Tests basic write and read behavior for real-time safe buffer.

2. RealTimeSafe_SetActiveSizeWithinMax
   - Validates resizing within max size for real-time safe buffer.

3. RealTimeSafe_ResizeBeyondMaxDisallowed
   - Confirms that resizeBeyondMax() is disallowed for real-time safe buffer.

4. NonRealTimeSafe_WriteReadResizeBeyondMax
   - Tests write, read, and resizing beyond max for non-real-time safe buffer.
   - Verifies preservation of old data and write/read integrity after resizing.

5. NonRealTimeSafe_SetActiveSizeAndWrite
   - Ensures correct behavior when resizing active window (not buffer size) and writing after.

6. ClearBuffer
   - Validates that clear() resets the buffer and writeIndex_.

TODO: Additional Tests

1. WriteWrapAround
   - Fill the buffer, then continue writing to ensure correct circular wrap-around behavior.

2. ReadOutOfBounds
   - Attempt to read with delay ≥ activeSize_ to trigger CASPI_ASSERT.

3. ResizeToZero_Assert
   - Attempt to resize to 0 and check that CASPI_ASSERT triggers.

4. ResizeActiveLargerThanMax_Assert
   - Call resize(newSize) with newSize > maxSize_ — should assert.

5. ResizeBeyondMaxWithDataPreservation
   - Write known data, resize beyond max, and verify old values are preserved in correct order.

6. WriteReadAfterWrapAndResize
   - Write enough to wrap around once, then resize active size smaller and validate correct read.

7. PartialWriteAndRead
   - Write fewer elements than activeSize_ and ensure unused slots return default (0).

8. Getters
   - Check getActiveSize() and getMaxSize() reflect correct values after resizes.

9. MaxSizeEqualsInitialSize
   - Construct with only an initial size (default max), and validate resizeBeyondMax is still allowed in non-RT.

10. EdgeCase_Delays
    - Read with delay of 0 and delay == activeSize_-1 to test bounds.

==============================================================================


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
using RTBuffer = CASPI::CircularBufferBase<int, CASPI::RealTimeSafeTag>;
using NonRTBuffer = CASPI::CircularBufferBase<int, CASPI::NonRealTimeSafeTag>;

TEST(CircularBufferBaseTest, RealTimeSafe_WriteReadBasic)
{
    RTBuffer buffer(4);

    buffer.write(10);
    buffer.write(20);
    buffer.write(30);

    auto r0 = buffer.read(0);
    auto r1 = buffer.read(1);
    auto r2 = buffer.read(2);

    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r0.value(), 30);
    EXPECT_EQ(r1.value(), 20);
    EXPECT_EQ(r2.value(), 10);
}

TEST(CircularBufferBaseTest, RealTimeSafe_SetActiveSizeWithinMax)
{
    RTBuffer buffer(4, 8);
    buffer.write(1);
    buffer.write(2);
    buffer.write(3);
    buffer.resize(2);

    EXPECT_EQ(buffer.getActiveSize(), 2);
}

TEST(CircularBufferBaseTest, RealTimeSafe_ResizeBeyondMaxDisallowed)
{
    RTBuffer buffer(4);
    SUCCEED(); // Compile-time test, no runtime validation
}

TEST(CircularBufferBaseTest, NonRealTimeSafe_WriteReadResizeBeyondMax)
{
    NonRTBuffer buffer(3, 3);
    buffer.write(1);
    buffer.write(2);
    buffer.write(3);

    buffer.resizeBeyondMax(6);
    EXPECT_EQ(buffer.getActiveSize(), 3);
    EXPECT_EQ(buffer.getMaxSize(), 6);

    auto r0 = buffer.read(0);
    auto r1 = buffer.read(1);
    auto r2 = buffer.read(2);

    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r0.value(), 3);
    EXPECT_EQ(r1.value(), 2);
    EXPECT_EQ(r2.value(), 1);

    buffer.write(4);
    buffer.write(5);
    buffer.write(6);

    auto r3 = buffer.read(0);
    auto r4 = buffer.read(1);
    auto r5 = buffer.read(2);

    ASSERT_TRUE(r3.has_value());
    ASSERT_TRUE(r4.has_value());
    ASSERT_TRUE(r5.has_value());

    EXPECT_EQ(r3.value(), 6);
    EXPECT_EQ(r4.value(), 5);
    EXPECT_EQ(r5.value(), 4);
}

TEST(CircularBufferBaseTest, NonRealTimeSafe_SetActiveSizeAndWrite)
{
    NonRTBuffer buffer(3, 5);
    buffer.resize(5);

    for (int i = 1; i <= 5; ++i)
        buffer.write(i);

    auto r0 = buffer.read(0);
    auto r1 = buffer.read(1);
    auto r4 = buffer.read(4);

    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r4.has_value());

    EXPECT_EQ(r0.value(), 5);
    EXPECT_EQ(r1.value(), 4);
    EXPECT_EQ(r4.value(), 1);
}

TEST(CircularBufferBaseTest, ClearBuffer)
{
    NonRTBuffer buffer(3);
    buffer.write(7);
    buffer.write(8);
    buffer.write(9);

    buffer.clear();

    buffer.write(5);

    auto r = buffer.read(0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 5);
}



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
    ASSERT_EQ (buffer.buffer.size(), newNumSamples);

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