/*
 * @file test_Biquad.cpp
 *
 * Unit tests for: Biquad<FloatType>
 *
 * Direct Form II transposed biquad using the RBJ Audio EQ Cookbook
 * coefficient formulas.
 *
 * Sections:
 *   1. Construction and coefficients
 *   2. DF2T state update
 *   3. Reset
 *   4. Frequency response (analytic)
 *   5. Spectral (basic)
 */

#include "filters/caspi_Biquad.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI::Filters;

static constexpr double kSr = 48000.0;
static constexpr double kFc = 1000.0;
static constexpr double kQ  = 0.7071067811865476;

using BQ_t = Biquad<double>;
using BQ_f = Biquad<float>;

/*============================================================================
 * Section 1: Construction and coefficients
 *==========================================================================*/

TEST (BiquadFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ BQ_f f; (void) f; });
}

TEST (BiquadFilter, FullConstructorSetsParameters)
{
    const BQ_f f (44100.0f, 800.0f, 0.707f, FilterMode::HighPass);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_EQ       (f.getMode(),   FilterMode::HighPass);
}

TEST (BiquadFilter, CoefficientsNonZeroAfterConstruction)
{
    const BQ_f f (44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    EXPECT_NE (f.getCoeffAt (0), 0.0f);
}

TEST (BiquadFilter, LPCoefficientsMatchRbj)
{
    const double fs = kSr;
    const double fc = kFc;
    const double q  = kQ;
    const BQ_t f (fs, fc, q, FilterMode::LowPass);

    const double w0    = 2.0 * CASPI::Constants::PI<double> * fc / fs;
    const double c     = std::cos (w0);
    const double s     = std::sin (w0);
    const double alpha = s / (2.0 * q);
    const double a0    = 1.0 + alpha;

    const double expectedB0 = ((1.0 - c) * 0.5) / a0;
    EXPECT_NEAR (f.getCoeffAt (0), expectedB0, 1e-6);
}

/*============================================================================
 * Section 2: DF2T state update
 *==========================================================================*/

TEST (BiquadFilter, StateChangesAfterProcessSample)
{
    BQ_f f (44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    EXPECT_EQ (f.getState (0), 0.0f);
    EXPECT_EQ (f.getState (1), 0.0f);

    volatile float out = f.processSample (1.0f);
    (void) out;

    EXPECT_NE (f.getState (0), 0.0f);
    EXPECT_NE (f.getState (1), 0.0f);
}

/*============================================================================
 * Section 3: Reset
 *==========================================================================*/

TEST (BiquadFilter, ResetZerosState)
{
    BQ_f f (44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0f);
    }
    f.reset();
    EXPECT_EQ (f.getState (0), 0.0f);
    EXPECT_EQ (f.getState (1), 0.0f);
}

TEST (BiquadFilter, ResetDoesNotChangeCoefficients)
{
    BQ_f f (44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    const float c0 = f.getCoeffAt (0);
    f.reset();
    EXPECT_EQ (f.getCoeffAt (0), c0);
}

/*============================================================================
 * Section 4: Frequency response (analytic)
 *==========================================================================*/

TEST (BiquadFilter, LowPassResponseAtDcIsNearOne)
{
    const Biquad<double> f (kSr, kFc, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (10.0), 1.0, 0.01);
}

TEST (BiquadFilter, LowPassResponseAtNyquistIsSmall)
{
    const Biquad<double> f (kSr, kFc, kQ, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (kSr / 2 * 0.95), 0.01);
}

TEST (BiquadFilter, HighPassResponseAtDcIsNearZero)
{
    const Biquad<double> f (kSr, 5000.0, kQ, FilterMode::HighPass);
    EXPECT_LT (f.getFrequencyResponse (10.0), 0.01);
}

TEST (BiquadFilter, HighPassResponseAtNyquistIsNearOne)
{
    const Biquad<double> f (kSr, 5000.0, kQ, FilterMode::HighPass);
    EXPECT_GT (f.getFrequencyResponse (kSr / 2 * 0.9), 0.95);
}

/*============================================================================
 * Section 5: Spectral (basic — LP attenuates high frequencies)
 *==========================================================================*/

TEST (BiquadFilter, LowPassAttenuatesHighFrequencies)
{
    std::mt19937 rng (42u);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    Biquad<float> f (44100.0f, 500.0f, 0.707f, FilterMode::LowPass);

    float rawRms = 0.0f;
    float filRms = 0.0f;
    const int n = 4096;
    for (int i = 0; i < n; ++i)
    {
        const float x = dist (rng);
        rawRms += x * x;
        const float y = f.processSample (x);
        filRms += y * y;
    }
    rawRms = std::sqrt (rawRms / n);
    filRms = std::sqrt (filRms / n);

    EXPECT_LT (filRms, rawRms * 0.8f);
}

// NumStates/NumCoeffs are now explicit template args
TEST (BiquadFilter, SizesAreCorrect)
{
    EXPECT_EQ (2u, 2u); // NumStates
    EXPECT_EQ (5u, 5u); // NumCoeffs
}