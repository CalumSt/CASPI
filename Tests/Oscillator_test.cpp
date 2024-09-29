#ifndef OSCILLATOR_TEST_H
#define OSCILLATOR_TEST_H

#include <gtest/gtest.h>
#include "Oscillators/caspi_Oscillators.h"

// Test params
constexpr auto frequency = 1000.0f;
constexpr auto sampleRate = 44100.0f;
constexpr auto renderTime = 1;

// derived
constexpr auto samplesToRender = renderTime * static_cast<int>(sampleRate);
constexpr auto phaseIncrement = frequency / sampleRate;

TEST(OscillatorTests, test) {
    EXPECT_TRUE(true);
}

TEST(OscillatorTests, SineSetFrequency_test) {
    caspi_BlepOscillator<float>::Sine osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_GT(osc.phase.increment, -1.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
    EXPECT_LT(osc.phase.increment, 1.0f);
    EXPECT_EQ(osc.phase.increment, phaseIncrement);
}

TEST(OscillatorTests, SawSetFrequency_test) {
    caspi_BlepOscillator<float>::Saw osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_GT(osc.phase.increment, -1.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
    EXPECT_LT(osc.phase.increment, 1.0f);
    EXPECT_EQ(osc.phase.increment, phaseIncrement);
}

TEST(OscillatorTests, SquareSetFrequency_test) {
    caspi_BlepOscillator<float>::Square osc;
    osc.setFrequency(frequency,sampleRate);
    EXPECT_GT(osc.phase.increment, -1.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
    EXPECT_LT(osc.phase.increment, 1.0f);
    EXPECT_EQ(osc.phase.increment, phaseIncrement);
}

// Don't need to test triangle as it is based on square

//================================================================
// Render tests

// test individual sample
TEST(OscillatorTests, SineGetNextSample_test) {
    caspi_BlepOscillator<float>::Sine osc;
    osc.setFrequency(frequency,sampleRate);
    const auto s = osc.getNextSample();
    EXPECT_GT(s, -1.0f);
    EXPECT_LT(s, 1.0f);
    EXPECT_NEAR(s, 0.0f,0.01f);
}

// test whole waveform
TEST(OscillatorTests, SineRenderWaveform_test) {

    caspi_BlepOscillator<float>::Sine osc;
    osc.setFrequency(frequency,sampleRate);
    auto currentPhase = 0.0f;
    EXPECT_EQ(phaseIncrement,osc.phase.increment);
    while (currentPhase >= static_cast<float>(CASPI::TWO_PI)) { currentPhase -= static_cast<float>(CASPI::TWO_PI); }
    for (int i = 0;i < samplesToRender; i++) {
        auto s = osc.getNextSample();
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
        EXPECT_NEAR(s, std::sin(CASPI::TWO_PI * -currentPhase),0.15f);
        currentPhase += phaseIncrement;

    }
}



#endif //OSCILLATOR_TEST_H
