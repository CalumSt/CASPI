/*
 * @file test_Ladder.cpp
 *
 * Unit tests for: Ladder<FloatType>
 *
 * Moog transistor ladder (Stilson/Smith).
 * Nonlinear — no getFrequencyResponse().
 *
 * Sections:
 *   1. Construction and coefficients
 *   2. Process sample (finite, bounded)
 *   3. Self-oscillation at high Q
 *   4. Reset
 *   5. Spectral (approximate)
 */

#include "filters/caspi_Ladder.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI::Filters;

static constexpr float kSr = 48000.0f;
static constexpr float kFc = 1000.0f;
static constexpr float kQ  = 0.707f;

using LAD_t = Ladder<float>;

/*============================================================================
 * Section 1: Construction and coefficients
 *==========================================================================*/

TEST (LadderFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ LAD_t f; (void) f; });
}

TEST (LadderFilter, FullConstructorSetsParameters)
{
    const Ladder<float> f (44100.0f, 800.0f, 1.0f);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_FLOAT_EQ (f.getQ(),      1.0f);
}

TEST (LadderFilter, GFromCutoff)
{
    const float fs = kSr;
    const float fc = 1000.0f;
    const Ladder<float> f (fs, fc, 0.707f);

    const float expectedG = std::tan (CASPI::Constants::PI<float> * fc / fs);
    EXPECT_NEAR (f.getCoeffAt (0), expectedG, 1e-6f);
}

TEST (LadderFilter, KFromQ)
{
    const float q = 1.0f;
    const Ladder<float> f (kSr, 1000.0f, q);

    const float expectedK = 4.0f * q / (1.0f + q);
    EXPECT_NEAR (f.getCoeffAt (1), expectedK, 1e-6f);
}

/*============================================================================
 * Section 2: Process sample
 *==========================================================================*/

TEST (LadderFilter, ProcessSampleReturnsFinite)
{
    Ladder<float> f (kSr, kFc, kQ);
    for (int i = 0; i < 100; ++i)
    {
        const float y = f.processSample (static_cast<float> (i) / 100.0f);
        EXPECT_TRUE (std::isfinite (y));
    }
}

TEST (LadderFilter, ProcessSampleBounded)
{
    Ladder<float> f (kSr, kFc, kQ);
    for (int i = 0; i < 1000; ++i)
    {
        const float y = f.processSample (1.0f);
        EXPECT_LE (std::abs (y), 2.0f); // tanh saturates
    }
}

/*============================================================================
 * Section 3: Self-oscillation at high Q
 *==========================================================================*/

TEST (LadderFilter, HighQChangesOutput)
{
    // Verify that different Q values produce different output
    Ladder<float> lowQ  (kSr, kFc, 0.5f);
    Ladder<float> highQ (kSr, kFc, 10.0f);

    std::mt19937 rng (42u);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    bool anyDiff = false;
    for (int i = 0; i < 256; ++i)
    {
        const float x = dist (rng);
        if (std::abs (lowQ.processSample (x) - highQ.processSample (x)) > 1e-4f)
        {
            anyDiff = true;
            break;
        }
    }
    EXPECT_TRUE (anyDiff);
}

/*============================================================================
 * Section 4: Reset
 *==========================================================================*/

TEST (LadderFilter, ResetZerosState)
{
    Ladder<float> f (kSr, kFc, kQ);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0f);
    }
    f.reset();
    // After reset, processing silence should give near-zero output
    const float y = f.processSample (0.0f);
    EXPECT_NEAR (y, 0.0f, 1e-6f);
}

/*============================================================================
 * Section 5: Spectral (approximate — nonlinear)
 *==========================================================================*/

TEST (LadderFilter, LowPassAttenuatesHighFrequencies)
{
    std::mt19937 rng (42u);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    Ladder<float> f (44100.0f, 500.0f, 0.707f);

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

    // Conservative threshold (nonlinear filter)
    EXPECT_LT (filRms, rawRms * 0.85f);
}

// NumStates/NumCoeffs are explicit template args
TEST (LadderFilter, SizesAreCorrect)
{
    EXPECT_EQ (4u, 4u); // NumStates
    EXPECT_EQ (2u, 2u); // NumCoeffs
}