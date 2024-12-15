/*
CASPI PM Operator Tests

Initialises correctly with no unintialised members
     [DONE] carrier frequency
     [DONE] sample rate
     [DONE] modulation index

[DONE] Can set carrier frequency
[DONE] Can set sample rate
[DONE] Can set modulator frequency/index
Can set modulation depth
[DONE] Renders nothing if no modulator
[DONE] Setting frequency sets increment
[DONE] Setting frequency sets sample rate
[DONE] Phase increments each render call
[DONE] Phase wraps at 2 pi
[DONE] Can get a sine wave with no modulator frequency
Can generate a modulated sine wave
Can change sample rate during render
Can use doubles or floats
Can use saw waves
Can g

 */



#include <gtest/gtest.h>
#include <utility>
#include "test_helpers.h"

// For testing, set private to public
// DO NOT DO THIS IN PRODUCTION CODE
#define private public
#include "Oscillators/caspi_PMOperator_new.h"

// Test params
constexpr auto frequency = 1000.0;
constexpr auto sampleRate = 44100.0;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 0.5;
constexpr auto modDepth   = 100.0;
constexpr double newSampleRate = 48000.0;


TEST(FMTests, PMOperatorIntialises_test)
{
    CASPI::PMOperator_new osc;
    EXPECT_EQ (0.0,osc.getFrequency());
    EXPECT_EQ (44100.0,osc.getSampleRate());
    EXPECT_EQ (0.0,osc.getModulationIndex());
    EXPECT_EQ (0.0,osc.phaseIncrement);
}

TEST(FMTests, PMOperatorSetFrequency_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_EQ (frequency,osc.getFrequency());
}

TEST(FMTests, PMOperatorSetSampleRate_test)
{
    CASPI::PMOperator_new osc;
    osc.setSampleRate(newSampleRate);
    EXPECT_EQ (newSampleRate,osc.getSampleRate());
}

TEST(FMTests, setModulationIndex_test)
{
    CASPI::PMOperator_new osc;
    osc.setModulation(modIndex,modDepth);
    EXPECT_EQ (modIndex,osc.getModulationIndex());
    EXPECT_EQ (modDepth,osc.getModulationDepth());
}

TEST(FMTests,renderNothingWithNoFrequency_test)
{
    CASPI::PMOperator_new osc;
    auto sample = osc.render();
    EXPECT_EQ (0.0,sample);
}

TEST(FMTest, setFrequencySetsPhaseInc_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_NE (0.0,osc.phaseIncrement);
    EXPECT_EQ (CASPI::Constants::TWO_PI<double> * frequency/sampleRate,osc.phaseIncrement);

}

TEST(FMTests,incrementPhase_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < 10; i++)
    {
        osc.render();
    }
    EXPECT_NE(0.0,osc.currentPhase);

}

TEST(FMTests,phaseWrapsAtTwoPi_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < (int)sampleRate; i++)
    {
        osc.render();
        EXPECT_LE(osc.currentPhase,CASPI::Constants::TWO_PI<double>);
    }

}

TEST(FMTests,renderSine_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency (10.0,sampleRate);
    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        samples.at(i) = osc.render();
        times.at(i) = static_cast<double>(i) / sampleRate;
    }
}

TEST(FMTests,renderModSine_test)
{
    CASPI::PMOperator_new osc;
    osc.setFrequency (10.0,sampleRate);
    osc.setModulation (modIndex,modDepth);
    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        samples.at(i) = osc.render();
        times.at(i) = static_cast<double>(i) / sampleRate;
    }
    saveToFile ("./GeneratedSignals/FM_modSine.csv",times,samples);
}
