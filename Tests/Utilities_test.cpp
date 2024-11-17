#include <gtest/gtest-spi.h>
#ifndef UTILITIES_TEST_H
#define UTILITIES_TEST_H

#include <gtest/gtest.h>
#include <Utilities/caspi_Constants.h>
#include <Utilities/caspi_Assert.h>
#include <Utilities/caspi_Gain.h>
#include <Utilities/caspi_CircularBuffer.h>
// Stub test to make sure the project compiles
TEST(UtilitiesTests, test) {
    EXPECT_TRUE(true);
}

TEST(UtilitiesTests, assert_test) {
    EXPECT_NO_THROW(CASPI_ASSERT(true,"If this has failed, sorry."));
    EXPECT_DEATH(CASPI_ASSERT(false,"This is supposed to fail."), "Assertion failed.");
}

TEST(UtilitiesTests, ConstantsTest) {
    double expected = 3.14159265358979323846;
    auto test = CASPI::Constants::PI<double>;
    EXPECT_EQ(test,expected);
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


TEST(GainTests, GainRampDown_test)
{
    Gain<float> Gain;
    Gain.reset();
    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
    EXPECT_EQ(1.0f, Gain.getGain());
    Gain.setGain(0.5f);
    EXPECT_EQ(0.5f, Gain.getGain());
    Gain.gainRampDown(0.0f,0.5f, 44100.0f);
    int numberOfSamples = 22050;
    auto gainInc    = 0.5f / 22050.0f;
    EXPECT_TRUE(Gain.isRampDown());
    for (int i = 0; i < numberOfSamples; i++)
    {
        auto gain = Gain.getGain();
        auto currentGain = 0.5f - (i * gainInc);
        if (currentGain < 0.0f) { currentGain = 0.0f; }
        EXPECT_NEAR(gain, currentGain, 0.001f);
        EXPECT_FALSE(Gain.isRampUp());
    }

    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
}

TEST(GainTests, GainRampUp_test)
{
    Gain<float> Gain;
    Gain.reset();
    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
    EXPECT_EQ(1.0f, Gain.getGain());
    Gain.setGain(0.5f);
    EXPECT_EQ(0.5f, Gain.getGain());
    Gain.gainRampUp(1.0f,0.5f, 44100.0f);
    int numberOfSamples = 22050;
    auto gainInc    = 0.5f / 22050.0f;
    EXPECT_TRUE(Gain.isRampUp());

    for (int i = 0; i < numberOfSamples; i++)
    {
        auto gain = Gain.getGain();
        auto currentGain = 0.5f + (i * gainInc);
        if (currentGain > 1.0f) { currentGain = 1.0f; }
        EXPECT_NEAR(gain, currentGain, 0.001f);
        EXPECT_FALSE(Gain.isRampDown());
    }

    numberOfSamples = 10;
    for (int i = 0; i < numberOfSamples; i++)
    {
        auto gain = Gain.getGain();
    }
    auto gain = Gain.getGain();
    EXPECT_EQ(gain, 1.0f);
    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
}

TEST(GainTests, GainEdgeCases_test) {
    // If targetGain < currentGain
    // Trying to set targetGain to below zero

}


#endif //UTILITIES_TEST_H
