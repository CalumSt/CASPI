#include <Utilities/caspi_Gain.h>
#include <gtest/gtest.h>

TEST(GainTests, GainRampDown_test)
{
    CASPI::Gain<float> Gain;
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
        auto currentGain = 0.5f - (static_cast<float>(i) * gainInc);
        if (currentGain < 0.0f) { currentGain = 0.0f; }
        EXPECT_NEAR(gain, currentGain, 0.001f);
        EXPECT_FALSE(Gain.isRampUp());
    }

    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
}

TEST(GainTests, GainRampUp_test)
{
    CASPI::Gain<float> Gain;
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
        auto currentGain = 0.5f + (static_cast<float>(i) * gainInc);
        if (currentGain > 1.0f) { currentGain = 1.0f; }
        EXPECT_NEAR(gain, currentGain, 0.001f);
        EXPECT_FALSE(Gain.isRampDown());
    }
    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_TRUE(Gain.isRampUp());
}

TEST(GainTests, GainEdgeCases_test) {
    // If targetGain < currentGain
    // Trying to set targetGain to below zero

}