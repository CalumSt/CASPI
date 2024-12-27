#ifndef OSCILLATOR_TEST_H
#define OSCILLATOR_TEST_H

#include <gtest/gtest.h>

#include "Oscillators/caspi_BlepOscillator.h"
#include "Oscillators/caspi_PMOperator.h"
#include "test_helpers.h"
#include <fstream>
#include <utility>

// Test params
constexpr auto frequency  = 1000.0f;
constexpr auto sampleRate = 44100.0f;
constexpr auto renderTime = 1;
constexpr auto modIndex   = 0.8f;

// derived
constexpr auto samplesToRender = renderTime * static_cast<int> (sampleRate);
constexpr auto phaseIncrement  = frequency / sampleRate;

TEST (OscillatorTests, test)
{
    EXPECT_TRUE (true);
}

TEST (OscillatorTests, SineSetFrequency_test)
{
    CASPI::BlepOscillator::Sine<float> osc;
    osc.setFrequency (frequency, sampleRate);
    EXPECT_GT (osc.phase.increment, -1.0f);
    EXPECT_NE (osc.phase.increment, 0.0f);
    EXPECT_LT (osc.phase.increment, 1.0f);
    EXPECT_NEAR (osc.phase.increment, CASPI::Constants::TWO_PI<float> * phaseIncrement, 0.001f);
}

TEST (OscillatorTests, SawSetFrequency_test)
{
    CASPI::BlepOscillator::Saw<float> osc;
    osc.setFrequency (frequency, sampleRate);
    EXPECT_GT (osc.phase.increment, -1.0f);
    EXPECT_NE (osc.phase.increment, 0.0f);
    EXPECT_LT (osc.phase.increment, 1.0f);
    EXPECT_EQ (osc.phase.increment, phaseIncrement);
}

TEST (OscillatorTests, SquareSetFrequency_test)
{
    CASPI::BlepOscillator::Square<float> osc;
    osc.setFrequency (frequency, sampleRate);
    EXPECT_GT (osc.phase.increment, -1.0f);
    EXPECT_NE (osc.phase.increment, 0.0f);
    EXPECT_LT (osc.phase.increment, 1.0f);
    EXPECT_EQ (osc.phase.increment, phaseIncrement);
}

// Don't need to test triangle as it is based on square

// Render tests

// test individual sample
TEST (OscillatorTests, SineGetNextSample_test)
{
    CASPI::BlepOscillator::Sine<float> osc;
    osc.setFrequency (frequency, sampleRate);
    const auto s = osc.getNextSample();
    EXPECT_GE (s, -1.0f);
    EXPECT_LE (s, 1.0f);
    EXPECT_NEAR (s, 0.0f, 0.01f);
}

// test whole waveform
/// TODO: replace with better testing strategy: Have pre-generated expected waveforms and try to get them to match
TEST (OscillatorTests, SineRenderWaveform_test)
{
    const auto internal_pi                    = CASPI::Constants::TWO_PI<float>;
    constexpr auto testInternalPhaseIncrement = internal_pi * phaseIncrement;
    CASPI::BlepOscillator::Sine<float> osc;
    osc.setFrequency (frequency, sampleRate);
    EXPECT_NEAR (testInternalPhaseIncrement, osc.phase.increment, 0.001f);

    for (int i = 0; i < samplesToRender; i++)
    {
        auto s = osc.getNextSample();
        EXPECT_GE (s, -1.0f);
        EXPECT_LE (s, 1.0f);
    }
}

TEST (OscillatorTests, SawRenderWaveform_test)
{
    const auto internal_pi = CASPI::Constants::TWO_PI<float>;
    CASPI::BlepOscillator::Saw<float> osc;
    osc.setFrequency (frequency, sampleRate);
    auto currentPhase = 0.0f;
    while (currentPhase >= internal_pi)
    {
        currentPhase -= internal_pi;
    }
    for (int i = 0; i < samplesToRender; i++)
    {
        auto s = osc.getNextSample();
        EXPECT_GE (s, -1.0f);
        EXPECT_LE (s, 1.0f);
        currentPhase += phaseIncrement;
    }
}

TEST (OscillatorTests, SquareRenderWaveform_test)
{
    const auto internal_pi = CASPI::Constants::TWO_PI<float>;
    CASPI::BlepOscillator::Square<float> osc;
    osc.setFrequency (frequency, sampleRate);
    auto currentPhase = 0.0f;
    while (currentPhase >= internal_pi)
    {
        currentPhase -= internal_pi;
    }
    for (int i = 0; i < samplesToRender; i++)
    {
        auto s = osc.getNextSample();
        EXPECT_GE (s, -1.0f);
        EXPECT_LE (s, 1.0f);
        currentPhase += phaseIncrement;
    }
}

TEST (OscillatorTests, TriangleRenderWaveform_test)
{
    const auto internal_pi = CASPI::Constants::TWO_PI<float>;
    CASPI::BlepOscillator::Triangle<float> osc;
    osc.setFrequency (frequency, sampleRate);
    auto currentPhase = 0.0f;
    while (currentPhase >= internal_pi)
    {
        currentPhase -= internal_pi;
    }
    for (int i = 0; i < samplesToRender; i++)
    {
        auto s = osc.getNextSample();
        EXPECT_GE (s, -1.0f);
        EXPECT_LE (s, 1.0f);
        currentPhase += phaseIncrement;
    }
}

TEST (OscillatorTests, renderToBlock_test)
{
    CASPI::BlepOscillator::Sine<float> osc;
    osc.setFrequency (frequency, sampleRate);
    auto test = std::vector<float> (1024);
    for (int i = 0; i < 1024; i++)
    {
        auto s = osc.getNextSample();
        test.push_back (s);
    }
    osc.resetPhase();
    // This API is ugly but will work for now
    auto output = CASPI::BlepOscillator::renderBlock<CASPI::BlepOscillator::Sine<float>, float> (frequency, sampleRate, 1024);
    for (int i = 0; i < 1024; i++)
    {
        EXPECT_EQ (output[i], test[i]);
    }
}

#endif //OSCILLATOR_TEST_H
