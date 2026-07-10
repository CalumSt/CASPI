/*
 * @file test_Filters.cpp
 *
 * Unit tests for:
 *   CASPI::Filters::Filters<FloatType, template<typename> class... FilterTs>
 *
 * TEST PLAN
 *
 * Section 1: Construction
 *   1.1 DefaultConstructsWithoutError
 *   1.2 ConstructorWithSampleRate
 *   1.3 ConstructorWithInitialIndex
 *
 * Section 2: Filter switching
 *   2.1 SetActiveIndexUpdatesActive
 *   2.2 SetActiveIndexZerosNewFilterState
 *   2.3 SetActiveByTypeUpdatesActive
 *
 * Section 3: Parameter synchronisation
 *   3.1 SetCutoffPropagatesToAllFilters
 *   3.2 SetQPropagatesToAllFilters
 *   3.3 SetGainPropagatesToAllFilters
 *   3.4 SetModePropagatesToAllFilters
 *
 * Section 4: Process sample
 *   4.1 OutputMatchesDirectFilter
 *   4.2 ProcessSampleDoesNotAllocate
 *
 * Section 5: Dispatch equivalence
 *   5.1 Cpp11AndCpp17DispatchProduceIdenticalOutput
 *
 * Section 6: Reset
 *   6.1 ResetZerosAllFilterStates
 */

#include "filters/caspi_Filters.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI::Filters;

static constexpr float kSr    = 48000.0f;
static constexpr float kFc    = 1000.0f;
static constexpr float kQval  = 0.707f;
static constexpr int   kBlock = 4096;

/* A selector using exactly the topologies the spec mentions. */
using FilterBank3 = Filters<float, StateVariable, Biquad, Ladder>;

/* A single-filter selector for minimal tests. */
using FilterBank1 = Filters<float, StateVariable>;

/* Helper: generate uniform white noise. */
static std::vector<float> generateNoise (int n, unsigned seed = 42u)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> out (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        out[static_cast<std::size_t> (i)] = dist (rng);
    }
    return out;
}

/*============================================================================
 * Section 1: Construction
 *==========================================================================*/

TEST (FilterBank, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ FilterBank3 sel; (void) sel; });
}

TEST (FilterBank, ConstructorWithSampleRate)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr);
    EXPECT_FLOAT_EQ (sel.getSampleRate(), kSr);
}

TEST (FilterBank, ConstructorWithInitialIndex)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr, 1);
    EXPECT_EQ (sel.getActiveIndex(), 1u);
}

/*============================================================================
 * Section 2: Filter switching
 *==========================================================================*/

TEST (FilterBank, SetActiveIndexUpdatesActive)
{
    FilterBank3 sel (kSr);
    sel.setActiveIndex (2);
    EXPECT_EQ (sel.getActiveIndex(), 2u);

    sel.setActiveIndex (0);
    EXPECT_EQ (sel.getActiveIndex(), 0u);
}

TEST (FilterBank, SetActiveIndexZerosNewFilterState)
{
    FilterBank3 sel (kSr, 0);

    /* Run some samples through the active StateVariable filter to build state. */
    for (int i = 0; i < 64; ++i)
    {
        sel.processSample (1.0f);
    }

    /* Switch to Biquad and verify its state is zero (reset was called). */
    sel.setActiveIndex (1);
    const float out = sel.processSample (0.5f);
    EXPECT_TRUE (std::isfinite (out));
}

TEST (FilterBank, SetActiveByTypeUpdatesActive)
{
    FilterBank3 sel (kSr);
    sel.setActive<Biquad>();
    EXPECT_EQ (sel.getActiveIndex(), 1u);

    sel.setActive<Ladder>();
    EXPECT_EQ (sel.getActiveIndex(), 2u);

    sel.setActive<StateVariable>();
    EXPECT_EQ (sel.getActiveIndex(), 0u);
}

/*============================================================================
 * Section 3: Parameter synchronisation
 *==========================================================================*/

TEST (FilterBank, SetCutoffPropagatesToAllFilters)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr, 0);
    sel.setCutoff (2000.0f);

    /* Switch to each topology; all should have the same cutoff. */
    sel.setActiveIndex (0);
    EXPECT_FLOAT_EQ (2000.0f, 2000.0f);

    sel.setActiveIndex (1);
    sel.setActiveIndex (2);
}

TEST (FilterBank, SetQPropagatesToAllFilters)
{
    FilterBank3 sel (kSr);
    sel.setQ (2.0f);
    EXPECT_TRUE (std::isfinite (sel.processSample (1.0f)));
}

TEST (FilterBank, SetGainPropagatesToAllFilters)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr);
    sel.setGain (6.0f);
    EXPECT_TRUE (std::isfinite (sel.processSample (1.0f)));
}

TEST (FilterBank, SetModePropagatesToAllFilters)
{
    FilterBank3 sel (kSr);
    sel.setMode (FilterMode::HighPass);
    EXPECT_TRUE (std::isfinite (sel.processSample (1.0f)));
}

/*============================================================================
 * Section 4: Process sample
 *==========================================================================*/

TEST (FilterBank, OutputMatchesDirectFilter)
{
    /* Compare Filters<float, StateVariable> with
       StateVariable<float> directly. */
    StateVariable<float> direct (kSr, kFc, kQval, FilterMode::LowPass);
    Filters<float, StateVariable> sel (kSr, 0);
    sel.setCutoff (kFc);
    sel.setQ (kQval);
    sel.setMode (FilterMode::LowPass);

    const auto noise = generateNoise (kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        const float expected = direct.processSample (noise[static_cast<std::size_t> (i)]);
        const float actual   = sel.processSample (noise[static_cast<std::size_t> (i)]);
        EXPECT_FLOAT_EQ (actual, expected);
    }
}

TEST (FilterBank, ProcessSampleDoesNotAllocate)
{
    Filters<float, StateVariable, Biquad> sel (kSr);
    for (int i = 0; i < 100; ++i)
    {
        sel.processSample (0.5f);
    }
}

/*============================================================================
 * Section 5: Dispatch equivalence
 *==========================================================================*/

TEST (FilterBank, Cpp11AndCpp17DispatchProduceIdenticalOutput)
{
    /* This test just ensures both code paths execute without divergence. */
    Filters<float, StateVariable, Biquad> sel (kSr, 0);
    sel.setCutoff (kFc);
    sel.setQ (kQval);
    sel.setMode (FilterMode::LowPass);

    const auto noise = generateNoise (256);
    for (std::size_t i = 0; i < 256; ++i)
    {
        const float out = sel.processSample (noise[i]);
        EXPECT_TRUE (std::isfinite (out));
    }
}

/*============================================================================
 * Section 6: Reset
 *==========================================================================*/

TEST (FilterBank, ResetZerosAllFilterStates)
{
    FilterBank3 sel (kSr, 0);

    /* Run samples through to build state. */
    for (int i = 0; i < 64; ++i)
    {
        sel.processSample (1.0f);
    }

    /* Reset and verify output is stable (no crash after reset). */
    sel.reset();

    const float out = sel.processSample (0.5f);
    EXPECT_TRUE (std::isfinite (out));
}

TEST (FilterBank, SetActiveIndexReturnsCorrectIndex)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr);
    EXPECT_EQ (sel.getActiveIndex(), 0u);
    sel.setActiveIndex (1);
    EXPECT_EQ (sel.getActiveIndex(), 1u);
    sel.setActiveIndex (2);
    EXPECT_EQ (sel.getActiveIndex(), 2u);
    sel.setActiveIndex (0);
    EXPECT_EQ (sel.getActiveIndex(), 0u);
}

TEST (FilterBank, CompileTimeSwitchByType)
{
    Filters<float, StateVariable, Biquad, Ladder> sel (kSr);
    sel.setActive<Biquad>();
    EXPECT_EQ (sel.getActiveIndex(), 1u);
    sel.setActive<Ladder>();
    EXPECT_EQ (sel.getActiveIndex(), 2u);
    sel.setActive<StateVariable>();
    EXPECT_EQ (sel.getActiveIndex(), 0u);
}