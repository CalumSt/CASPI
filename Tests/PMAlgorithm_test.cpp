/*
Algorithm Tests

[DONE] Initialises
    Has default sampleRate

[DONE] Can set frequency and sample rate
[DONE] Can set modulation of modulator without affecting carrier
[DONE] Can set modulator to self-feedback
[DONE] Can turn on ADSR
[DONE] Can Render sine wave
[DONE] Can reset
 */



#include <gtest/gtest.h>
#include <utility>
#include "test_helpers.h"

// For testing, set private to public
// DO NOT DO THIS IN PRODUCTION CODE
#define private public
#include "Oscillators/caspi_PMOperator.h"
#include "Synthesizers/caspi_PMAlgorithm.h"


// Test params
constexpr auto frequency = 10.0;
constexpr auto sampleRate = 44100.0;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 0.5;
constexpr auto modDepth   = 0.5;
constexpr auto modFeedback = 3.0;
constexpr double newSampleRate = 22050.0;
using OP = CASPI::PM::Operator<double>;
using Cascade = CASPI::PM::Algorithms::BasicCascade<double>;
using enum CASPI::PM::Algorithms::BasicCascadeOpCodes;

TEST(BasicCascadeTests,Initialises_test)
{
    const Cascade alg;
    EXPECT_EQ (alg.sampleRate,sampleRate);
    EXPECT_EQ(alg.numOperators,2);
}

TEST(BasicCascadeTests, setFrequency_test)
{
    Cascade alg;
    alg.setFrequency (frequency,sampleRate);
    EXPECT_EQ (alg.Carrier.frequency,frequency);
    EXPECT_EQ (alg.Modulator.frequency,frequency);
    EXPECT_EQ (alg.Carrier.sampleRate,sampleRate);
    EXPECT_EQ (alg.Modulator.sampleRate,sampleRate);
}

TEST(BasicCascadeTests, setModulation_test)
{
    Cascade alg;
    alg.setFrequency (frequency,sampleRate);
    alg.setModulation (modIndex,modDepth);
    EXPECT_EQ (alg.Modulator.frequency, frequency*modIndex);
}

TEST(BasicCascadeTests, setModulationFeedback_test)
{
    Cascade alg;
    alg.setModulationFeedback (modFeedback);
    EXPECT_TRUE (alg.Modulator.isSelfModulating);
    EXPECT_EQ ( alg.Modulator.modFeedback, modFeedback);
}

TEST(BasicCascadeTests, enableADSR_test)
{
    Cascade alg;
    alg.enableADSR(Carrier);
    EXPECT_TRUE(alg.Carrier.envelopeEnabled);
    alg.enableADSR(Modulator);
    EXPECT_TRUE(alg.Carrier.envelopeEnabled);
    alg.disableADSR(Carrier);
    EXPECT_FALSE(alg.Carrier.envelopeEnabled);
    alg.disableADSR(Modulator);
    EXPECT_FALSE(alg.Carrier.envelopeEnabled);
}

TEST(BasicCascadeTests, render_test)
{
    Cascade alg;
    alg.setFrequency (frequency,sampleRate);
    alg.setModulation (modIndex,modDepth);
    alg.enableADSR(Carrier);
    alg.enableADSR(Modulator);
    alg.setADSR(Carrier,0.1,0.1,0.8,0.1);
    alg.setADSR(Modulator,0.1,0.1,0.8,0.1);
    alg.noteOn();

    OP testOsc;
    testOsc.setFrequency (frequency,sampleRate);
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = testOsc.render();
    }


    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = alg.render();
        samples.at(i) = sample;
        times.at(i) = static_cast<double>(i) / sampleRate;
        EXPECT_GE (sample,-1.0);
        EXPECT_LE(sample, 1.0);
        if (sample != 0.0)
        {
            EXPECT_NE(sample,baseSine.at(i));
        }


    }
    saveToFile ("./GeneratedSignals/FM_BasicCascadeRender.csv",times,samples);

}

TEST(BasicCascadeTests, reset_test)
{
    Cascade alg;
    alg.setFrequency (frequency,sampleRate);
    alg.setModulation (modIndex,modDepth);
    alg.enableADSR(Carrier);
    alg.enableADSR(Modulator);
    alg.setADSR(Carrier,0.1,0.1,0.8,0.1);
    alg.setADSR(Modulator,0.1,0.1,0.8,0.1);
    alg.setModulationFeedback (modFeedback);
    alg.noteOn();

    alg.reset();
    EXPECT_EQ (alg.sampleRate,sampleRate);
    EXPECT_EQ (alg.numOperators,2);
    EXPECT_EQ (alg.Carrier.frequency,0.0);
    EXPECT_EQ (alg.Modulator.frequency,0.0);
    EXPECT_EQ (alg.Carrier.isSelfModulating,false);
    EXPECT_EQ (alg.Modulator.isSelfModulating,false);
    EXPECT_EQ (alg.Carrier.output,0.0);
    EXPECT_EQ (alg.Modulator.output,0.0);
    EXPECT_EQ (alg.Modulator.modDepth,1.0);
    EXPECT_EQ (alg.Modulator.modIndex,1.0);
    EXPECT_EQ (alg.Modulator.modFeedback,0.0);
    EXPECT_EQ (alg.Modulator.phaseIncrement,0.0);
    EXPECT_EQ (alg.Carrier.phaseIncrement,0.0);
    EXPECT_EQ (alg.Carrier.Envelope.level,0.0);
    EXPECT_EQ (alg.Carrier.Envelope.level,0.0);
}
