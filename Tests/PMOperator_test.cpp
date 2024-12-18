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
[DONE] Can generate a modulated sine wave
[DONE] Can reset operator
[DONE] Can render to a block
[DONE] Can use doubles or floats
Can generate documentation
Can use saw waves

 */



#include <gtest/gtest.h>
#include <utility>
#include "test_helpers.h"

// For testing, set private to public
// DO NOT DO THIS IN PRODUCTION CODE
#define private public
#include "Oscillators/caspi_PMOperator.h"

#include <Oscillators/caspi_BlepOscillator.h>

// Test params
constexpr auto frequency = 1000.0;
constexpr auto sampleRate = 44100.0;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 0.5;
constexpr auto modDepth   = 0.5;
constexpr double newSampleRate = 22050.0;


TEST(FMTests, PMOperatorIntialises_test)
{
    const CASPI::PMOperator<double> osc;
    EXPECT_EQ (0.0, osc.currentPhase);
    EXPECT_EQ (0.0, osc.currentModPhase);
    EXPECT_EQ (0.0, osc.frequency);
    EXPECT_EQ (0.0, osc.modIndex);
    EXPECT_EQ (0.0, osc.modDepth);
    EXPECT_EQ (0.0, osc.phaseIncrement);
    EXPECT_EQ (0.0, osc.modPhaseIncrement);
    EXPECT_EQ (44100.0, osc.sampleRate);
}

TEST(FMTests, PMOperatorSetFrequency_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_EQ (frequency,osc.getFrequency());
}

TEST(FMTests, PMOperatorSetSampleRate_test)
{
    CASPI::PMOperator<double> osc;
    osc.setSampleRate(newSampleRate);
    EXPECT_EQ (newSampleRate,osc.getSampleRate());
}

TEST(FMTests, setModulationIndex_test)
{
    CASPI::PMOperator<double> osc;
    osc.setModulation(modIndex,modDepth);
    EXPECT_EQ (modIndex,osc.getModulationIndex());
    EXPECT_EQ (modDepth,osc.getModulationDepth());
}

TEST(FMTests,renderNothingWithNoFrequency_test)
{
    CASPI::PMOperator<double> osc;
    auto sample = osc.render();
    EXPECT_EQ (0.0,sample);
}

TEST(FMTest, setFrequencySetsPhaseInc_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_NE (0.0,osc.phaseIncrement);
    EXPECT_EQ (CASPI::Constants::TWO_PI<double> * frequency/sampleRate,osc.phaseIncrement);

}

TEST(FMTests,incrementPhase_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < 10; i++)
    {
        osc.render();
    }
    EXPECT_NE(0.0,osc.currentPhase);

}

TEST(FMTests,phaseWrapsAtTwoPi_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < (int)sampleRate; i++)
    {
        osc.render();
        EXPECT_LE(osc.currentPhase,CASPI::Constants::TWO_PI<double>);
    }

}

TEST(FMTests,renderSine_test)
{
    CASPI::PMOperator<double> osc;
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
    CASPI::PMOperator<double> osc;
    osc.setFrequency (10.0,sampleRate);
    // render sine for test
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = osc.render();
    }

    osc.setModulation (modIndex,modDepth);
    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = osc.render();
        samples.at(i) = sample;
        times.at(i) = static_cast<double>(i) / sampleRate;
        EXPECT_GE (sample,-1.0);
        EXPECT_LE(sample, 1.0);
        if (sample != 0.0)
        {
            EXPECT_NE(sample,baseSine.at(i));
        }


    }
    saveToFile ("./GeneratedSignals/FM_modSine.csv",times,samples);
}

TEST(FMTests,reset_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency (10.0,sampleRate);
    osc.setModulation (modIndex,modDepth);

    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
       [[maybe_unused]] auto sample = osc.render();
    }


    osc.reset();

    EXPECT_EQ (0.0, osc.currentPhase);
    EXPECT_EQ (0.0, osc.currentModPhase);
    EXPECT_EQ (0.0, osc.frequency);
    EXPECT_EQ (0.0, osc.modIndex);
    EXPECT_EQ (0.0, osc.modDepth);
    EXPECT_EQ (0.0, osc.phaseIncrement);
    EXPECT_EQ (0.0, osc.modPhaseIncrement);
    EXPECT_EQ (44100.0, osc.sampleRate);

}

TEST(FMTests,renderBlock_test)
{
    CASPI::PMOperator<double> osc;
    osc.setFrequency (10.0,sampleRate);
    osc.setModulation (modIndex,modDepth);
    auto blockSamples = osc.renderBlock (512);
    osc.resetPhase();

    std::vector<double> samples(512,0.0);
    for (int i = 0; i < 512 ; i++)
    {
        auto sample = osc.render();
        samples.at(i) = sample;
        EXPECT_EQ (blockSamples.at(i), sample);
    }
}

TEST(FMTests,doubleOrFloat_test)
{
    CASPI::PMOperator<float> oscFloat;
    CASPI::PMOperator<float> oscDouble;

    oscFloat.setFrequency (10.0,sampleRate);
    oscFloat.setModulation (modIndex,modDepth);
    oscDouble.setFrequency (10.0,sampleRate);
    oscDouble.setModulation (modIndex,modDepth);

    for (int i = 0; i < 512 ; i++)
    {
        float sampleFloat = oscFloat.render();
        double sampleDouble = oscDouble.render();
        EXPECT_EQ (sampleFloat, sampleDouble);
    }
}
