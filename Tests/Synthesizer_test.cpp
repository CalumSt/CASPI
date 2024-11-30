#ifndef SYNTHESIZER_TEST_H
#define SYNTHESIZER_TEST_H
#include <gtest/gtest.h>
#include <Oscillators/caspi_BlepOscillator.h>

/// A synthesizer voice can hold any combination of an oscillator, an envelope, and a filter

TEST(SynthesizerTest, SynthesizerVoice) {
    EXPECT_EQ(true, true);
}

#endif // SYNTHESIZER_TEST_H