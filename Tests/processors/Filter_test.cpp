
#include <gtest/gtest.h>
#include "filters/caspi_SvfFilter.h"
#include "oscillators/caspi_BlepOscillator.h"

/// TODO: Implement better testing strategy. Use 3 sine waveforms, sum them, apply the filter, then FFT the result and check that the frequency has been removed.

// ===================================================================
// Behaviour to test: initialisation
TEST(SvfFilterTests,Constructor_test)
{
    caspi_SvfFilter<float> filter;
    const auto sample = filter.render(1.0f);
    EXPECT_EQ (sample, 0.0f);
}

// Behaviour to test: Filtering
TEST(SvfFilterTests, Filter_test)
{
    caspi_SvfFilter<float> filter;
    filter.reset();
    filter.setSampleRate (44100.0f);
    filter.updateCoefficients(1000.0f,0.707f);
    CASPI::BlepOscillator::Saw<float> osc;
    osc.setFrequency(1000.0f, 44100.0f);
    int numberOfSamples = 44100; // 1 second of samples
    for (int i = 0; i < numberOfSamples; i++) {
        const float oscSample = osc.renderSample();
        const float nextValue = filter.render(oscSample);
        EXPECT_GE(nextValue, -1.0f);
        EXPECT_LE(nextValue, 1.0f);
        if (i > 0) {
            EXPECT_NE (oscSample, nextValue);
        }
    }
}

// ===================================================================
// Behaviour to test: initialisation
TEST(LadderFilter,Constructor_test)
{
    caspi_SvfFilter<float> filter;
    const auto sample = filter.render(1.0f);
    EXPECT_EQ (sample, 0.0f);
}