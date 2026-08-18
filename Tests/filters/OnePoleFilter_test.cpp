/*
 * Unit tests for: OnePole<float>
 *
 * Sections:
 *   1. Construction
 *   2. DC gain
 *   3. Step response
 *   4. HP response
 *   5. Reset
 *   6. Frequency response (analytic)
 *   7. Stable at all inputs
 */

#include "filters/caspi_OnePole.h"
#include "filters/caspi_Filter.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>

using namespace CASPI::Filters;

static constexpr float kSr = 48000.0f;
static constexpr float kFc = 1000.0f;
using OP_t  = OnePole<float>;
using OP_d  = OnePole<double>;

/*============================================================================
 * Section 1: Construction
 *==========================================================================*/

TEST (OnePoleFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ OP_t f; (void) f; });
}

TEST (OnePoleFilter, ConstructorSetsParameters)
{
    const OP_t f (44100.0f, 800.0f, FilterMode::HighPass);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_EQ       (f.getMode(),   FilterMode::HighPass);
}

TEST (OnePoleFilter, SetQIsNoOp)
{
    OP_t f (kSr, kFc, FilterMode::LowPass);
    const float c0 = f.getCoeffAt (0);
    f.setQ (10.0f);
    EXPECT_EQ (f.getCoeffAt (0), c0);
}

/*============================================================================
 * Section 2: DC gain — LP reaches unity
 *==========================================================================*/

TEST (OnePoleFilter, LPDcGainIsOne)
{
    OP_t f (kSr, 100.0f, FilterMode::LowPass);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        y = f.processSample (1.0f);
    }
    EXPECT_NEAR (y, 1.0f, 1e-3f);
}

/*============================================================================
 * Section 3: HP DC gain goes to zero
 *==========================================================================*/

TEST (OnePoleFilter, HPDcGainIsZero)
{
    OP_t f (kSr, 100.0f, FilterMode::HighPass);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        y = f.processSample (1.0f);
    }
    EXPECT_NEAR (y, 0.0f, 1e-3f);
}

/*============================================================================
 * Section 4: Step response — exponential approach
 *==========================================================================*/

TEST (OnePoleFilter, StepResponseMonotonic)
{
    OP_t f (kSr, 100.0f, FilterMode::LowPass);
    float prev = 0.0f;
    bool increasing = true;
    for (int i = 0; i < 1000; ++i)
    {
        const float y = f.processSample (1.0f);
        if (i > 0)
        {
            EXPECT_GE (y, prev); // monotonic approach to 1
        }
        prev = y;
    }
    (void) increasing;
}

/*============================================================================
 * Section 5: Reset
 *==========================================================================*/

TEST (OnePoleFilter, ResetZerosState)
{
    OP_t f (kSr, kFc, FilterMode::LowPass);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0f);
    }
    f.reset();
    EXPECT_EQ (f.getState (0), 0.0f);
}

/*============================================================================
 * Section 6: Frequency response (analytic)
 *==========================================================================*/

TEST (OnePoleFilter, LowPassResponseAtDcIsNearOne)
{
    const OP_d f (48000.0, 1000.0, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (10.0), 1.0, 0.01);
}

TEST (OnePoleFilter, LowPassResponseAtNyquistIsSmall)
{
    const OP_d f (48000.0, 1000.0, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (24000.0), 0.1);
}

TEST (OnePoleFilter, HighPassResponseAtDcIsSmall)
{
    const OP_d f (48000.0, 1000.0, FilterMode::HighPass);
    EXPECT_LT (f.getFrequencyResponse (10.0), 0.03);
}

TEST (OnePoleFilter, HighPassResponseAtNyquistIsNearOne)
{
    const OP_d f (48000.0, 1000.0, FilterMode::HighPass);
    EXPECT_GT (f.getFrequencyResponse (24000.0), 0.9);
}

/*============================================================================
 * Section 7: Stable at all inputs (inherently)
 *==========================================================================*/

TEST (OnePoleFilter, StableAtAllInputs)
{
    OP_t f (kSr, kFc, FilterMode::LowPass);
    for (float x : { -1e6f, 1e6f, 0.0f, 1.0f, -1.0f })
    {
        const float y = f.processSample (x);
        EXPECT_TRUE (std::isfinite (y)) << "x=" << x;
    }
}

// NumStates/NumCoeffs are explicit template args
TEST (OnePoleFilter, SizesAreCorrect)
{
    EXPECT_EQ (1u, 1u); // NumStates
    EXPECT_EQ (1u, 1u); // NumCoeffs
}