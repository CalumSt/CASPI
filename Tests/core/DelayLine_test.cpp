//
// Created by calum on 14/08/2025.
//
#include <gtest/gtest.h>
#include "core/caspi_CircularBuffers.h"

TEST(LinearInterpolationTest, ReturnsAWhenFracZero) {
    float a = 0.5f;
    float b = 1.5f;
    double frac = 0.0;
    EXPECT_FLOAT_EQ(CASPI::LinearInterpolation<float>::apply(a, b, frac), 0.5f);
}

TEST(LinearInterpolationTest, ReturnsBWhenFracOne) {
    float a = 0.5f;
    float b = 1.5f;
    double frac = 1.0;
    EXPECT_FLOAT_EQ(CASPI::LinearInterpolation<float>::apply(a, b, frac), 1.5f);
}

TEST(LinearInterpolationTest, ReturnsInterpolatedValue) {
    float a = 0.5f;
    float b = 1.5f;
    double frac = 0.75;
    EXPECT_FLOAT_EQ(CASPI::LinearInterpolation<float>::apply(a, b, frac), 1.25f);
}

TEST(DelayLineTest, WriteAndReadZeroDelay)
{
    // This test checks that writing a single frame and reading it back with zero delay
    // returns exactly the same samples.
    CASPI::DelayLine<float> delay(2, 4); // 2 channels, 4 frames delay
    CASPI::AudioFrame<float> input = {1.0f, 2.0f};

    delay.write(input);
    auto output = delay.read(0); // zero delay

    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 2.0f);
}

TEST(DelayLineTest, ReadDelayedSample) {
    // This test verifies that reading a frame after a specific delay
    // returns the correct sample previously written at that delay.
    CASPI::DelayLine<float> delay(1, 3); // 1 channel, 3 frames delay

    delay.write({0.1f}); // frame 0
    delay.write({0.2f}); // frame 1
    delay.write({0.3f}); // frame 2

    // Reading with delay=2 should give the first frame
    auto output = delay.read(2);
    EXPECT_FLOAT_EQ(output[0], 0.1f);
}

TEST(DISABLED_DelayLineTest, CircularBufferWraps) {
    // This test ensures that the delay line behaves circularly:
    // once the buffer is full, writing new frames overwrites the oldest frames.
    CASPI::DelayLine<float> delay(1, 3); // 1 channel, 3 frames delay

    delay.write({1.0f}); // frame 0
    delay.write({2.0f}); // frame 1
    delay.write({3.0f}); // frame 2
    delay.write({4.0f}); // frame 3 -> should overwrite frame 0

    // Reading the oldest frame (delay=3) should now return frame 1
    auto output = delay.read(3);
    EXPECT_FLOAT_EQ(output[0], 2.0f);
}

TEST(DelayLineTest, MultiChannelDelay) {
    // This test checks that the delay line correctly handles multiple channels,
    // writing and reading per-channel samples independently.
    CASPI::DelayLine<float> delay(2, 2); // 2 channels, 2 frames delay

    delay.write({1.0f, 10.0f});
    delay.write({2.0f, 20.0f});

    auto output = delay.read(1); // read first frame
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 10.0f);
}