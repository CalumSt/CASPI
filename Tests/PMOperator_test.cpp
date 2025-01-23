/*
CASPI PM Operator Tests

Initialises
    Frequency
    Sample Rate
    Mod Index
    Mod Depth
    Mod Feedback
    Self-feedback

[DONE] Setters/Getters
[DONE] Can set carrier frequency
[DONE] Can set sample rate
[DONE] Can set modulator frequency/index
[DONE] Can set modulation depth
[DONE] Renders nothing if no modulator
[DONE] Setting frequency sets increment
[DONE] Setting frequency sets sample rate
[DONE] Phase increments each render call
[DONE] Phase wraps at 2 pi
[DONE] Can get a sine wave with no modulator frequency
[DONE] Can set ADSR parameters
[DONE] Can render modulated sine
[DONE] Can render modulated sine with feedback
[DONE] Can enable ADSR
[DONE] Applies ADSR to output
[DONE] Can render simple OP-MOD cascade with feedback and ADSR

Algorithm Tests

Initialises


 */

#include <gtest/gtest.h>
#include <utility>
#include "test_helpers.h"

// For testing, set private to public
// DO NOT DO THIS IN PRODUCTION CODE
#define private public
#include "Oscillators/caspi_PMOperator.h"

// Test params
constexpr auto frequency = 10.0;
constexpr auto sampleRate = 44100.0;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 0.5;
constexpr auto modDepth   = 0.5;
constexpr auto modFeedback = 3.0;
constexpr double newSampleRate = 22050.0;
using OP = CASPI::PM::Operator<double>;

TEST(FMTests, PMOperatorIntialises_test)
{
    const OP osc;
    EXPECT_EQ (0.0, osc.currentPhase);
    EXPECT_EQ (0.0, osc.frequency);
    EXPECT_EQ (1.0, osc.modIndex);
    EXPECT_EQ (1.0, osc.modDepth);
    EXPECT_EQ (0.0, osc.phaseIncrement);
    EXPECT_EQ (44100.0, osc.sampleRate);
    EXPECT_EQ (0.0, osc.modFeedback);
    EXPECT_TRUE(true);
}

TEST(FMTests, PMOperatorSetFrequency_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_EQ (frequency,osc.getFrequency());
    EXPECT_EQ (frequency,osc.frequency);
    EXPECT_EQ (sampleRate, osc.sampleRate);
}

TEST(FMTests, PMOperatorSetSampleRate_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setSampleRate(newSampleRate);
    EXPECT_EQ (newSampleRate,osc.getSampleRate());
}

TEST(FMTests, setModulationIndex_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setFrequency (frequency, sampleRate);
    osc.setModulation(modIndex,modDepth);
    EXPECT_EQ (modIndex,osc.getModulationIndex());
    EXPECT_EQ (modDepth,osc.getModulationDepth());
}

TEST(FMTests, setModFeedback_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setModFeedback(modFeedback);
    EXPECT_EQ(osc.modFeedback,modFeedback);
}

TEST(FMTests,renderNothingWithNoFrequency_test)
{
    CASPI::PM::Operator<double> osc;
    auto sample = osc.render();
    EXPECT_EQ (0.0,sample);
}

TEST(FMTest, setFrequencySetsPhaseInc_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_NE (0.0,osc.phaseIncrement);
    EXPECT_EQ (CASPI::Constants::TWO_PI<double> * frequency/sampleRate,osc.phaseIncrement);

}

TEST(FMTests,incrementPhase_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < 10; i++)
    {
        osc.render();
    }
    EXPECT_NE(0.0,osc.currentPhase);

}

TEST(FMTests,phaseWrapsAtTwoPi_test)
{
    CASPI::PM::Operator<double> osc;
    osc.setFrequency (frequency,sampleRate);
    for (int i = 0; i < (int)sampleRate; i++)
    {
        osc.render();
        EXPECT_LE(osc.currentPhase,CASPI::Constants::TWO_PI<double>);
    }

}

TEST(FMTests,renderSine_test)
{
    CASPI::PM::Operator<double> osc;
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
    OP Carrier;
    OP Modulator;
    Carrier.setFrequency (frequency,sampleRate);
    Modulator.setFrequency (frequency,sampleRate);
    // render sine for test
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = Carrier.render();
    }

    Modulator.setModulation (modIndex,modDepth);
    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = Carrier.render(Modulator.render());
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
    CASPI::PM::Operator<double> osc;
    osc.setFrequency (10.0,sampleRate);
    osc.setModulation (modIndex,modDepth);

    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
       [[maybe_unused]] auto sample = osc.render();
    }


    osc.reset();

    EXPECT_EQ (0.0, osc.currentPhase);
    EXPECT_EQ (0.0, osc.frequency);
    EXPECT_EQ (1.0, osc.modIndex);
    EXPECT_EQ (1.0, osc.modDepth);
    EXPECT_EQ (0.0, osc.phaseIncrement);
    EXPECT_EQ (44100.0, osc.sampleRate);

}

TEST(FMTests,doubleOrFloat_test)
{
    CASPI::PM::Operator<float> oscFloat;
    CASPI::PM::Operator<double> oscDouble;

    oscFloat.setFrequency (10.0,sampleRate);
    oscFloat.setModulation (modIndex,modDepth);
    oscDouble.setFrequency (10.0,sampleRate);
    oscDouble.setModulation (modIndex,modDepth);

    for (int i = 0; i < 512 ; i++)
    {
        float sampleFloat = oscFloat.render();
        double sampleDouble = oscDouble.render();
        EXPECT_NEAR (sampleFloat, sampleDouble,1e-5);
    }
}

TEST(FMTests,renderModSineWithFeedback_test)
{
    OP Carrier;
    OP Modulator;
    Carrier.setFrequency (frequency,sampleRate);
    Modulator.setFrequency (frequency,sampleRate);
    Modulator.setModulation (modIndex,modDepth,modFeedback);
    // render sine for test
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = Carrier.render();
    }
    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = Carrier.render(Modulator.render());
        samples.at(i) = sample;
        times.at(i) = static_cast<double>(i) / sampleRate;
        EXPECT_GE (sample,-1.0);
        EXPECT_LE(sample, 1.0);
        if (sample != 0.0)
        {
            EXPECT_NE(sample,baseSine.at(i));
        }


    }
    saveToFile ("./GeneratedSignals/FM_modSineFeedback.csv",times,samples);
}

TEST(FMTests, enableADSR_test)
{
    OP Carrier;
    Carrier.setFrequency (frequency,sampleRate);
    Carrier.enableEnvelope();
    EXPECT_TRUE(Carrier.envelopeEnabled);
}

TEST(FMTests, ADSRRender_test)
{
    OP Carrier;
    Carrier.setFrequency (frequency,sampleRate);
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = Carrier.render();
    }

    Carrier.enableEnvelope();
    EXPECT_TRUE(Carrier.envelopeEnabled);

    Carrier.setADSR(0.2,0.2,0.8,0.5);
    Carrier.noteOn();

    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = Carrier.render();
        samples.at(i) = sample;
        times.at(i) = static_cast<double>(i) / sampleRate;
        EXPECT_GE (sample,-1.0);
        EXPECT_LE(sample, 1.0);
        if (sample != 0.0)
        {
            EXPECT_NE(sample,baseSine.at(i));
        }


    }
    saveToFile ("./GeneratedSignals/FM_sineADSR.csv",times,samples);
}

TEST(FMTests, FullRender_test)
{
    OP Carrier;
    OP Modulator;
    Carrier.setFrequency (frequency,sampleRate);
    Modulator.setFrequency (frequency,sampleRate);
    Modulator.setModulation (modIndex,modDepth,modFeedback);
    auto baseSine = std::vector<double>(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        baseSine.at(i) = Carrier.render();
    }

    Carrier.enableEnvelope();
    Modulator.enableEnvelope();

    Carrier.setADSR(0.2,0.2,0.8,0.5);
    Modulator.setADSR (0.2,0.2,0.8,0.1);
    Carrier.noteOn();
    Modulator.noteOn();

    auto times = std::vector<double>(static_cast<int> (sampleRate),0.0);
    std::vector<double> samples(static_cast<int> (sampleRate),0.0);
    for (int i = 0; i < static_cast<int> (sampleRate) ; i++)
    {
        auto sample = Carrier.render(Modulator.render());
        samples.at(i) = sample;
        times.at(i) = static_cast<double>(i) / sampleRate;
        EXPECT_GE (sample,-1.0);
        EXPECT_LE(sample, 1.0);
        if (sample != 0.0)
        {
            EXPECT_NE(sample,baseSine.at(i));
        }


    }
    saveToFile ("./GeneratedSignals/FM_modSineADSR.csv",times,samples);
}