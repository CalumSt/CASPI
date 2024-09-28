#ifndef OSCILLATOR_TEST_H
#define OSCILLATOR_TEST_H

#include <gtest/gtest.h>
#include "Oscillators/caspi_Oscillators.h"

TEST(OscillatorTests, test) {
    EXPECT_TRUE(true);
}

TEST(OscillatorTests, SineSetFrequency_test) {
    caspi_BlepOscillator<float>::Sine osc;
    osc.setFrequency(1000.0f,44100.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
}

TEST(OscillatorTests, SawSetFrequency_test) {
    caspi_BlepOscillator<float>::Saw osc;
    osc.setFrequency(1000.0f,44100.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
}

TEST(OscillatorTests, SquareSetFrequency_test) {
    caspi_BlepOscillator<float>::Square osc;
    osc.setFrequency(1000.0f,44100.0f);
    EXPECT_NE(osc.phase.increment, 0.0f);
}

#endif //OSCILLATOR_TEST_H
