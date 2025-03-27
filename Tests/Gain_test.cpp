

#include <gtest/gtest.h>
#define private public
#include <Gain/caspi_Gain.h>
/*
 * [DONE] Can initialise gain object
 * [DONE] Can set gain levels
 * [DONE] Can set ramp duration
 * [DONE] Can reset gain
 * [DONE] Can ramp gain up
 * [DONE] Can ramp gain down
 * [DONE] Can set gain with Decibels
 * Can use non-linear ramps
 * Can get fractional gain
 */

constexpr auto sampleRate = 44100.0;
constexpr auto rampTime   = 0.5;
constexpr auto targetGain = 0.5;

// derived
constexpr auto numberOfSamples = static_cast<int>(sampleRate * rampTime);

TEST(GainTests, constructor_test)
{
    CASPI::Gain<double> Gain;
    EXPECT_EQ (Gain.gain, 0.0);
    EXPECT_EQ (Gain.gainIncrement, 0.0);
    EXPECT_FALSE (Gain.isRampUp());
    EXPECT_FALSE(Gain.isRampDown());
}

TEST(GainTests, setters_test)
{
    CASPI::Gain<double> Gain;
    Gain.setGain (targetGain,sampleRate);
    EXPECT_EQ (Gain.targetGain,targetGain);
    EXPECT_TRUE(Gain.isRampUp());
    EXPECT_EQ ( Gain.getGain(),0.0);
    Gain.setGainRampDuration (rampTime,sampleRate);
    EXPECT_EQ ( Gain.rampDuration_s,rampTime);
}

TEST(GainTests, reset_test)
{
    CASPI::Gain<double> Gain;
    Gain.setGain (targetGain,sampleRate);
    Gain.reset();
    EXPECT_EQ (Gain.gain,0.0);
    EXPECT_EQ (Gain.targetGain,0.0);
}


TEST(GainTests, GainRampUp_test)
{
    CASPI::Gain<double> Gain;
    Gain.reset();
    EXPECT_FALSE(Gain.isRampDown());
    EXPECT_FALSE(Gain.isRampUp());
    Gain.setGainRampDuration(rampTime,sampleRate);
    Gain.setGain (targetGain,sampleRate);
    // After 0.25s, expect gain to be at half target gain.
    // After 0.5s, expect gain to be a target gain.
    auto testSample = 1.0;
    for (int i = 0; i < numberOfSamples / 2; i++)
    {
        testSample = 1.0;
        Gain.apply(testSample);
    }

    EXPECT_NEAR(Gain.getGain(),targetGain/2.0,1e-4);
    EXPECT_NEAR(Gain.getGain(),testSample,1e-4);

    for (int i = 0; i < numberOfSamples / 2; i++)
    {
        testSample = 1.0;
        Gain.apply(testSample);
    }

    EXPECT_EQ(Gain.getGain(),targetGain);
    EXPECT_EQ(Gain.getGain(),testSample);
}

TEST(GainTests, GainRampDown_test)
{
    CASPI::Gain<double> Gain;
    Gain.reset();
    Gain.setGain (targetGain,sampleRate,true); // set current gain forcibly.
    EXPECT_EQ (Gain.gain,targetGain);
    Gain.setGain (0.0,sampleRate);
    Gain.setGainRampDuration(rampTime,sampleRate);
    // After 0.25s, expect gain to be at half target gain.
    auto testSample = 1.0;
    for (int i = 0; i < numberOfSamples / 2; i++)
    {
        testSample = 1.0;
        Gain.apply(testSample);
    }

    EXPECT_NEAR(Gain.getGain(),targetGain/2.0,1e-4);
    EXPECT_NEAR(Gain.getGain(),testSample,1e-4);
    // After 0.5s, expect gain to be at 0.
    for (int i = 0; i < numberOfSamples / 2; i++)
    {
        testSample = 1.0;
        Gain.apply(testSample);
    }

    EXPECT_EQ(Gain.getGain(),0.0);
    EXPECT_EQ(Gain.getGain(),testSample);
}

TEST(GainTests,GainDecibels_test)
{
    CASPI::Gain<double> Gain;
    Gain.reset();
    Gain.setGain_db(0.0,sampleRate);
    EXPECT_EQ (Gain.targetGain,1.0);
    Gain.setGain_db (-100.0,sampleRate);
    EXPECT_EQ (Gain.targetGain,0.0);
    Gain.setGain_db (-10.0,sampleRate);
    EXPECT_NEAR (Gain.targetGain,0.316227766,1e-4);
}