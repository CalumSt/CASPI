#ifndef UTILITIES_TEST_H
#define UTILITIES_TEST_H

#include <gtest/gtest.h>
#include <Utilities/caspi_utilities.h>

// Stub test to make sure the project compiles
TEST(UtilitiesTests, test) {
    EXPECT_TRUE(true);
}

// =======================================================================
// Circular Buffer Tests
// =======================================================================

caspi_CircularBuffer<float> CircularBuffer;


TEST(BufferTest, WriteAndReadBuffer_test) {
    CircularBuffer.createCircularBuffer(4000);
    CircularBuffer.writeBuffer(0.1f);
    CircularBuffer.writeBuffer(0.2f);
    CircularBuffer.writeBuffer(0.3f);
    auto s = CircularBuffer.readBuffer(3);
    EXPECT_EQ(s, 0.1f);
}

TEST(BufferTest, ReadBufferOutOfBounds_test) {
    CircularBuffer.clear();
    CircularBuffer.createCircularBuffer(2);
    CircularBuffer.writeBuffer(0.1f);
    CircularBuffer.writeBuffer(0.2f);
    auto s = CircularBuffer.readBuffer(3);
    EXPECT_EQ(s, 0.2f);
}

TEST(BufferTest,ClearBuffer_test) {
    CircularBuffer.clear();
    CircularBuffer.createCircularBuffer(2);
    CircularBuffer.writeBuffer(0.1f);
    CircularBuffer.writeBuffer(0.2f);
    auto s = CircularBuffer.readBuffer(1);
    EXPECT_EQ(s, 0.0f);
}

TEST(BufferTest, FractionalDelay_test) {
    CircularBuffer.clear();
    CircularBuffer.createCircularBuffer(2);
    CircularBuffer.writeBuffer(0.1f);
    CircularBuffer.writeBuffer(0.2f);
    auto s = CircularBuffer.readBuffer(0.5f);
    EXPECT_EQ (s, 0.15f);
}


#endif //UTILITIES_TEST_H
