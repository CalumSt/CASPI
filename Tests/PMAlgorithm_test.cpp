/*
Algorithm Tests

[DONE] Initialises
    Has default sampleRate

[DONE] Can set frequency and sample rate
[DONE] Can turn on ADSR
[DONE] Can Render sine wave
[DONE] Can reset
[DONE] Can render Basic Cascade
[DONE] Can render Two Carriers
[DONE] Can switch between algorithms
 */



#include <gtest/gtest.h>
#include <utility>
#include "test_helpers.h"

// For testing, set private to public
// DO NOT DO THIS IN PRODUCTION CODE
#define private public
#include "Oscillators/caspi_PMOperator.h"
#include "Synthesizers/caspi_PMAlgorithm.h"

#include <Oscillators/caspi_BlepOscillator.h>

// Test params
constexpr auto frequency = 10.0;
constexpr auto sampleRate = 44100.0;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 3.0;
constexpr auto modDepth   = 0.5;
constexpr auto modFeedback = 3.0;
constexpr double newSampleRate = 22050.0;
using OP = CASPI::PM::Operator<double>;
using Cascade = CASPI::PM::Algorithms::TwoOperatorAlgs<double>;
using enum CASPI::PM::Algorithms::Algorithms;
using enum CASPI::PM::Algorithms::OpIndex;


// Common tests

TEST(TwoOperatorAlgsTests,Initialises_test)
{
    Cascade alg;
    EXPECT_EQ (alg.getSampleRate(),sampleRate);
    EXPECT_EQ(alg.numOperators,2);
}

TEST(TwoOperatorAlgsTests,setAlgorithm_test)
{
    Cascade alg;
    alg.setAlgorithm (BasicCascade);
    EXPECT_EQ (alg.currentAlg, BasicCascade);

    alg.setAlgorithm (TwoCarriers);
    EXPECT_EQ (alg.currentAlg, TwoCarriers);
}

TEST(TwoOperatorAlgsTests,setFrequency_test)
{
    Cascade alg;
    alg.setFrequency (frequency, sampleRate);
    EXPECT_EQ (alg.frequency, frequency);
    EXPECT_EQ (alg.sampleRate, sampleRate);
    EXPECT_EQ (alg.getFrequency(), frequency);
}

TEST(TwoOperatorAlgsTests,ADSR_test)
{
    Cascade alg;
    alg.enableADSR (OpA);
    EXPECT_TRUE (alg.OperatorA.envelopeEnabled);
    alg.disableADSR(OpA);
    EXPECT_FALSE (alg.OperatorA.envelopeEnabled);
    alg.enableADSR (OpB);
    EXPECT_TRUE (alg.OperatorB.envelopeEnabled);
    alg.disableADSR(OpB);
    EXPECT_FALSE (alg.OperatorB.envelopeEnabled);
    alg.enableADSR (All);
    EXPECT_TRUE (alg.OperatorA.envelopeEnabled);
    EXPECT_TRUE (alg.OperatorB.envelopeEnabled);
    alg.disableADSR(All);
    EXPECT_FALSE (alg.OperatorA.envelopeEnabled);
    EXPECT_FALSE (alg.OperatorB.envelopeEnabled);
}

TEST(TwoOperatorAlgsTests,BasicCascadeModulation_test)
{
    Cascade alg;
    alg.setAlgorithm (BasicCascade);
    alg.setFrequency (frequency, sampleRate);
    alg.setModulation (modIndex, modDepth);
    EXPECT_EQ (alg.frequency, frequency);
    EXPECT_EQ (alg.OperatorA.frequency, frequency * modIndex);
    EXPECT_EQ (alg.OperatorA.modIndex, modIndex);
    EXPECT_EQ (alg.OperatorA.modDepth, modDepth);
    EXPECT_EQ (alg.OperatorB.frequency, frequency);
    EXPECT_EQ (alg.getFrequency(), frequency);
}

TEST(TwoOperatorAlgsTests,BasicCascadeRender_test)
{
    Cascade alg;
    alg.setAlgorithm (BasicCascade);
    alg.setFrequency (frequency, sampleRate);
    alg.setModulation (modIndex, modDepth);
    alg.enableADSR (All);
    alg.setADSR(0.05, 0.05, 0.5, 0.05);

    std::vector<double> results (renderTime * sampleRate, 0);
    std::vector<double> times = results;

    using Sine = CASPI::BlepOscillator::Sine<double>;
    auto sine = CASPI::BlepOscillator::renderBlock<Sine, double> (frequency, sampleRate, renderTime * sampleRate);

    std::cout << results.size() << std::endl;
    std::cout << times.size() << std::endl;

    alg.noteOn();
    for (int i = 0; i < renderTime * sampleRate; i++)
    {
        results.at(i) = alg.render();
        times.at(i) = (double)(i / sampleRate);
        if (i != 0) { EXPECT_NE (results.at(i), sine.at(i)); }
        EXPECT_GT (results.at(i), -1.01);
        EXPECT_LT (results.at(i), 1.01);
    }
    saveToFile ("./GeneratedSignals/FM_BasicCascadeRender.csv", times, results);
}

TEST(TwoOperatorAlgsTests,TwoCarriersModulation_test)
{
    Cascade alg;
    alg.setAlgorithm (TwoCarriers);
    alg.setFrequency (frequency, sampleRate);
    alg.setModulation (modIndex, modDepth);
    EXPECT_EQ (alg.frequency, frequency);
    EXPECT_EQ (alg.OperatorA.frequency, alg.OperatorB.frequency);
    EXPECT_EQ (alg.OperatorA.frequency, frequency);
    EXPECT_EQ (alg.OperatorA.modIndex, 1.0);
    EXPECT_EQ (alg.OperatorA.modDepth, 1.0);
}

TEST(TwoOperatorAlgsTests,TwoCarriersRender_test)
{
    Cascade alg;
    Cascade test;
    test.setAlgorithm (BasicCascade);
    alg.setAlgorithm (TwoCarriers);
    alg.setFrequency (frequency, sampleRate);
    alg.setModulation (modIndex, modDepth);
    alg.setModFeedback (OpA, 20.0);

    std::vector<double> results (renderTime * sampleRate, 0);
    std::vector<double> times = results;

    std::cout << results.size() << std::endl;
    std::cout << times.size() << std::endl;

    alg.noteOn();
    for (int i = 0; i < renderTime * sampleRate; i++)
    {
        auto cascadeSignal = test.render();
        results.at(i) = alg.render();
        times.at(i) = (double)(i / sampleRate);
        if (i != 0) { EXPECT_NE (results.at(i), cascadeSignal); }
        EXPECT_GT (results.at(i), -1.01);
        EXPECT_LT (results.at(i), 1.01);
    }
    saveToFile ("./GeneratedSignals/FM_TwoCarriersRender.csv", times, results);
}

/* making sure no weirdness comes about from switching algs */
TEST(TwoOperatorAlgsTests, SwitchAlg_test)
{
    // If there are two algorithms, the output should always be the same.
    // so we set one up, change it to another alg, and make sure the output is the same

    Cascade alg1;
    Cascade alg2;

    alg1.setAlgorithm (BasicCascade);
    alg1.setFrequency (frequency, sampleRate);
    alg1.setModulation (modIndex, modDepth);

    alg1.setAlgorithm (TwoCarriers);
    alg2.setAlgorithm (TwoCarriers);
    alg2.setFrequency (frequency, sampleRate);
    EXPECT_EQ (alg1.OperatorA.frequency, alg1.OperatorB.frequency);
    EXPECT_EQ (alg1.OperatorA.frequency, alg2.OperatorA.frequency);
    EXPECT_EQ (alg1.frequency, alg1.OperatorB.frequency);
    EXPECT_EQ (alg1.OperatorA.modDepth, alg1.OperatorB.modDepth);
    EXPECT_EQ (alg1.OperatorA.modDepth, alg2.OperatorA.modDepth);

    for (int i = 0; i < renderTime * sampleRate; i++)
    {
        auto signal1 = alg1.render();
        auto signal2 = alg2.render();

        EXPECT_EQ (signal1, signal2);
    }

}

