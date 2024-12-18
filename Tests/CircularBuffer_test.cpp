//
// Created by calum on 18/12/2024.
//

#include <gtest/gtest.h>
#include "Utilities/caspi_CircularBuffer.h"
#include "test_helpers.h"

/*
TESTS
Can construct a circular buffer


 */

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