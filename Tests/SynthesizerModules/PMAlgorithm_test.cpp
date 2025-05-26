/*
Algorithm Tests

[DONE] Initialises
    Has default sampleRate

[DONE] Can set frequency and sample rate
[DONE] Can turn on ADSR
[DONE] Can Render sine wave
[DONE] Can reset
[DONE] Can render Basic Cascade
[DONE] Can switch between algorithms
 */



#include <gtest/gtest.h>
#include <utility>
#include "../test_helpers.h"

// For testing, set private to public
#define private public
#include "Oscillators/caspi_PMOperator.h"
#include "Synthesizers/caspi_PMAlgorithm.h"

#include <Oscillators/caspi_BlepOscillator.h>


using OpIndex = CASPI::PM::OpIndex;

enum class TwoOperatorAlgorithms : int
{
    Series,
    Parallel
};

// Set up our test implementations, gibe it a float type, how many operators, and the Algorithm enum
class TwoOperatorTestImplementation final : public CASPI::PM::Algorithm<double, 2, TwoOperatorAlgorithms>
{
public:

    double render() noexcept override
    {
        auto out = CASPI::Constants::zero<double>;
        auto modSignal = CASPI::Constants::zero<double>;
        switch (currentAlgorithm)
        {
            case TwoOperatorAlgorithms::Series:
                modSignal = operators.at(std::to_underlying(OpIndex::OpA)).render();
            out       = operators.at(std::to_underlying(OpIndex::OpB)).render(modSignal);
            break;
            case TwoOperatorAlgorithms::Parallel:
                modSignal = operators.at(std::to_underlying(OpIndex::OpA)).render();
            out       = operators.at(std::to_underlying(OpIndex::OpB)).render();
            out       = (out + modSignal) / 2;
            break;
            default:
                break;


        }
        return out;
    }
};

// Test params
constexpr auto frequency = 10.0;
constexpr auto sampleRate = 44100.0;
const std::vector<double> sampleRates = { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr auto renderTime = 1;
constexpr auto modIndex   = 3.0;
constexpr auto modDepth   = 0.5;
constexpr auto modFeedback = 3.0;
constexpr double newSampleRate = 22050.0;
using OP = CASPI::PM::Operator<double>;


// Common tests

TEST(TwoOperatorAlgsTests,Initialises_test)
{
    const TwoOperatorTestImplementation alg;
    EXPECT_EQ (alg.getSampleRate(),sampleRate);
    EXPECT_EQ(alg.numOperators,2);
}

TEST(TwoOperatorAlgsTests,setAlgorithm_test)
{
    TwoOperatorTestImplementation alg;
    alg.setAlgorithm (TwoOperatorAlgorithms::Series);
    EXPECT_EQ (alg.currentAlgorithm, TwoOperatorAlgorithms::Series);

    alg.setAlgorithm (TwoOperatorAlgorithms::Parallel);
    EXPECT_EQ (alg.currentAlgorithm, TwoOperatorAlgorithms::Parallel);
}

TEST(TwoOperatorAlgsTests,setFrequency_test)
{
    TwoOperatorTestImplementation alg;
    for (const auto fs : sampleRates)
    {
        alg.setFrequency (frequency, fs);
        EXPECT_EQ (alg.frequency, frequency);
        EXPECT_EQ (alg.sampleRate, fs);
        EXPECT_EQ (alg.getFrequency(), frequency);
        EXPECT_EQ (alg.getSampleRate(),fs);
    }
}

TEST(TwoOperatorAlgsTests,ADSR_test)
{
    using enum CASPI::PM::OpIndex;
    TwoOperatorTestImplementation alg;
    alg.enableADSR (OpA);
    EXPECT_TRUE (alg.operators.at(std::to_underlying(OpA)).envelopeEnabled);
    alg.disableADSR(OpA);
    EXPECT_FALSE (alg.operators.at(std::to_underlying(OpA)).envelopeEnabled);
    alg.enableADSR (OpB);
    EXPECT_TRUE (alg.operators.at(std::to_underlying(OpB)).envelopeEnabled);
    alg.disableADSR(OpB);
    EXPECT_FALSE (alg.operators.at(std::to_underlying(OpB)).envelopeEnabled);
    alg.enableADSR ();
    EXPECT_TRUE (alg.operators.at(std::to_underlying(OpA)).envelopeEnabled);
    EXPECT_TRUE (alg.operators.at(std::to_underlying(OpB)).envelopeEnabled);
    alg.disableADSR();
    EXPECT_FALSE (alg.operators.at(std::to_underlying(OpA)).envelopeEnabled);
    EXPECT_FALSE (alg.operators.at(std::to_underlying(OpB)).envelopeEnabled);
}

TEST(TwoOperatorAlgsTests,SeriesModulation_test)
{
    TwoOperatorTestImplementation alg;
    alg.setAlgorithm (TwoOperatorAlgorithms::Series);
    for (const auto fs : sampleRates)
    {
        alg.setFrequency (frequency, fs);
        alg.setModulation (CASPI::PM::OpIndex::OpA,modIndex, modDepth, modFeedback);
        alg.setModulation (CASPI::PM::OpIndex::OpB,modIndex, modDepth, modFeedback);
        EXPECT_EQ (alg.frequency, frequency);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modFrequency, frequency * modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modIndex, modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modDepth, modDepth);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modFeedback, modFeedback);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).frequency, frequency);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modFrequency, frequency * modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modIndex, modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modDepth, modDepth);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modFeedback, modFeedback);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).frequency, frequency);
        EXPECT_EQ (alg.getFrequency(), frequency);
    }

}

TEST(TwoOperatorAlgsTests,BasicCascadeRender_test)
{
    TwoOperatorTestImplementation alg;
    alg.setAlgorithm (TwoOperatorAlgorithms::Series);
    for (const auto fs : sampleRates)
    {
        alg.setFrequency (frequency, fs);
        alg.setModulation (OpIndex::OpA, modIndex, modDepth, modFeedback);
        alg.enableADSR (OpIndex::All);
        alg.setADSR(0.05, 0.05, 0.5, 0.05);

        std::vector<double> results (renderTime * fs, 0);
        std::vector<double> times = results;

        using Sine = CASPI::BlepOscillator::Sine<double>;
        auto sine = CASPI::BlepOscillator::renderBlock<Sine, double> (frequency, fs, renderTime * fs);

        alg.noteOn();
        for (int i = 0; i < renderTime * fs; i++)
        {
            results.at(i) = alg.render();
            times.at(i) = i / fs;
            if (i != 0) { EXPECT_NE (results.at(i), sine.at(i)); }
            EXPECT_GT (results.at(i), -1.01);
            EXPECT_LT (results.at(i), 1.01);
        }
        if (fs == 44100.0)
        {
            saveToFile ("./GeneratedSignals/FM_BasicCascadeRender.csv", times, results);
        }
    }

}

TEST(TwoOperatorAlgsTests,ParallelCarriers_test)
{
    TwoOperatorTestImplementation alg;
    alg.setAlgorithm (TwoOperatorAlgorithms::Parallel);
    for (const auto fs : sampleRates)
    {
        alg.setFrequency (frequency, fs);
        alg.setModulation (CASPI::PM::OpIndex::OpA,modIndex, modDepth, modFeedback);
        alg.setModulation (CASPI::PM::OpIndex::OpB,modIndex, modDepth, modFeedback);
        EXPECT_EQ (alg.frequency, frequency);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modFrequency, frequency * modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modIndex, modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modDepth, modDepth);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).modFeedback, modFeedback);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpA)).frequency, frequency);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modFrequency, frequency * modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modIndex, modIndex);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modDepth, modDepth);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).modFeedback, modFeedback);
        EXPECT_EQ (alg.operators.at(std::to_underlying(OpIndex::OpB)).frequency, frequency);
        EXPECT_EQ (alg.getFrequency(), frequency);
    }

}

/* making sure no weirdness comes about from switching algs */
TEST(TwoOperatorAlgsTests, SwitchAlg_test)
{
    // If there are two algorithms, the output should always be the same.
    // so we set one up, change it to another alg, and make sure the output is the same

    TwoOperatorTestImplementation alg1;
    TwoOperatorTestImplementation alg2;

    alg1.setAlgorithm (TwoOperatorAlgorithms::Series);
    alg2.setAlgorithm (TwoOperatorAlgorithms::Parallel);
    for (const auto fs : sampleRates)
    {
        alg1.setFrequency (frequency, fs);
        alg2.setFrequency (frequency, fs);
        alg1.setModulation (OpIndex::OpA,modIndex, modDepth, modFeedback);
        alg1.setModulation (OpIndex::OpB, modIndex, modDepth, modFeedback);
        alg2.setModulation (OpIndex::OpA, modIndex, modDepth, modFeedback);
        alg2.setModulation (OpIndex::OpB, modIndex, modDepth, modFeedback);
        alg1.setAlgorithm (TwoOperatorAlgorithms::Parallel);

        EXPECT_EQ (alg1.operators.at(std::to_underlying(OpIndex::OpA)).frequency, alg1.operators.at(std::to_underlying(OpIndex::OpB)).frequency);
        EXPECT_EQ (alg1.operators.at(std::to_underlying(OpIndex::OpA)).frequency, alg2.operators.at(std::to_underlying(OpIndex::OpA)).frequency);
        EXPECT_EQ (alg1.frequency, alg1.operators.at(std::to_underlying(OpIndex::OpB)).frequency);
        EXPECT_EQ (alg1.operators.at(std::to_underlying(OpIndex::OpA)).modDepth, alg1.operators.at(std::to_underlying(OpIndex::OpB)).modDepth);
        EXPECT_EQ (alg1.operators.at(std::to_underlying(OpIndex::OpA)).modDepth, alg2.operators.at(std::to_underlying(OpIndex::OpA)).modDepth);

        for (int i = 0; i < renderTime * sampleRate; i++)
        {
            auto signal1 = alg1.render();
            auto signal2 = alg2.render();

            EXPECT_EQ (signal1, signal2);
        }
    }
}

#undef private
