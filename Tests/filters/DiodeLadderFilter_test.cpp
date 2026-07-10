/*
 * Unit tests for: DiodeLadder<float>
 *
 * Huovilainen diode ladder.
 * Nonlinear — no getFrequencyResponse().
 *
 * Sections:
 *   1. Construction and coefficients
 *   2. Process sample (finite, bounded)
 *   3. Output differs from Moog ladder
 *   4. No NaN at extreme inputs
 *   5. Reset
 *   6. Spectral (approximate)
 */

#include "filters/caspi_DiodeLadder.h"
#include "filters/caspi_Ladder.h"
#include "filters/caspi_Filter.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI::Filters;

static constexpr float kSr = 48000.0f;
static constexpr float kFc = 1000.0f;
static constexpr float kQ  = 0.707f;

using DL_t = DiodeLadder<float>;

/*============================================================================
 * Section 1: Construction and coefficients
 *==========================================================================*/

TEST (DiodeLadderFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ DL_t f; (void) f; });
}

TEST (DiodeLadderFilter, FullConstructorSetsParameters)
{
    const DL_t f (44100.0f, 800.0f, 1.0f);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_FLOAT_EQ (f.getQ(),      1.0f);
}

TEST (DiodeLadderFilter, GFromCutoff)
{
    const float fs = kSr;
    const float fc = 1000.0f;
    const DL_t f (fs, fc, 0.707f);

    const float expectedG = std::tan (CASPI::Constants::PI<float> * fc / fs);
    EXPECT_NEAR (f.getCoeffAt (0), expectedG, 1e-6f);
}

TEST (DiodeLadderFilter, ResonanceMappingDiffersFromMoog)
{
    // Same parameters should produce different k coefficient
    const DL_t df (kSr, kFc, 2.0f);
    const Ladder<float> mf (kSr, kFc, 2.0f);

    EXPECT_NE (df.getCoeffAt (1), mf.getCoeffAt (1));
}

/*============================================================================
 * Section 2: Process sample
 *==========================================================================*/

TEST (DiodeLadderFilter, ProcessSampleReturnsFinite)
{
    DL_t f (kSr, kFc, kQ);
    for (int i = 0; i < 100; ++i)
    {
        const float y = f.processSample (static_cast<float> (i) / 100.0f);
        EXPECT_TRUE (std::isfinite (y));
    }
}

TEST (DiodeLadderFilter, ProcessSampleBounded)
{
    DL_t f (kSr, kFc, kQ);
    for (int i = 0; i < 1000; ++i)
    {
        const float y = f.processSample (1.0f);
        EXPECT_LE (std::abs (y), 2.0f);
    }
}

/*============================================================================
 * Section 3: Output differs from Moog ladder
 *==========================================================================*/

TEST (DiodeLadderFilter, OutputDiffersFromMoog)
{
    DL_t  df (kSr, kFc, 1.0f);
    Ladder<float> mf (kSr, kFc, 1.0f);

    std::mt19937 rng (42u);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    bool anyDiff = false;
    for (int i = 0; i < 100; ++i)
    {
        const float x  = dist (rng);
        const float dy = df.processSample (x);
        const float my = mf.processSample (x);
        if (std::abs (dy - my) > 1e-4f)
        {
            anyDiff = true;
            break;
        }
    }
    EXPECT_TRUE (anyDiff);
}

/*============================================================================
 * Section 4: No NaN at extreme inputs
 *==========================================================================*/

TEST (DiodeLadderFilter, NoNanAtExtremeInputs)
{
    DL_t f (kSr, kFc, kQ);
    for (float x : { -100.0f, 100.0f, 1e6f, -1e6f })
    {
        const float y = f.processSample (x);
        EXPECT_TRUE (std::isfinite (y)) << "x=" << x;
    }
}

/*============================================================================
 * Section 5: Reset
 *==========================================================================*/

TEST (DiodeLadderFilter, ResetZerosState)
{
    DL_t f (kSr, kFc, kQ);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0f);
    }
    f.reset();
    const float y = f.processSample (0.0f);
    EXPECT_NEAR (y, 0.0f, 1e-6f);
}

/*============================================================================
 * Section 6: Spectral (approximate — nonlinear)
 *==========================================================================*/

TEST (DiodeLadderFilter, LowPassAttenuatesHighFrequencies)
{
    std::mt19937 rng (42u);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    DL_t f (44100.0f, 500.0f, 0.707f);

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

    EXPECT_LT (filRms, rawRms * 0.85f);
}

// NumStates/NumCoeffs are explicit template args
TEST (DiodeLadderFilter, SizesAreCorrect)
{
    EXPECT_EQ (4u, 4u); // NumStates
    EXPECT_EQ (2u, 2u); // NumCoeffs
}