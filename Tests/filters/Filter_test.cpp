/*
 * @file caspi_Filter_test.cpp
 *
 * Unit tests for:
 *   CASPI::Filters::AtomicCoefficients<FloatType, N>
 *   CASPI::Filters::FilterBase<Derived, FloatType, NumStates, NumCoeffs>
 *   CASPI::Filters::SvfFilter<FloatType>
 *
 * TEST STRATEGY
 *
 * State-box tests (Sections 1-4)
 *   Inspect internal state, coefficients, and parameter propagation directly.
 *   No audio rendered.
 *
 * Spectral tests (Sections 5-7)
 *   Render white noise through a filter, then analyse with SpectralProfile to
 *   verify passband / stopband energy ratios and cutoff frequency placement.
 *   All spectral tests use a 48 kHz, 16384-sample Hann-windowed block.
 *   Energy thresholds are conservative to account for windowing artefacts.
 *
 * Analytical tests (Section 8)
 *   Call getFrequencyResponse() and verify the mathematical model directly,
 *   independently of the time-domain render path.
 *
 * CONSTRUCTION PATTERN
 *
 * SvfFilter contains std::atomic and is non-copyable. All tests construct
 * filters in-place using the full constructor:
 *
 *   SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
 *
 * There is no factory helper function.
 *
 * TEST PLAN SUMMARY
 *
 * Section 1: AtomicCoefficients
 *   1.1  DefaultConstructedReadsZero
 *   1.2  SwapPublishesNewValues
 *   1.3  SwapIsConsistentAfterToggle
 *   1.4  IndexOperatorMatchesGet
 *   1.5  ResetZerosBothBuffers
 *
 * Section 2: FilterBase - parameter API and state
 *   2.1  DefaultCutoffIs1kHz
 *   2.2  DefaultQIsButterworth
 *   2.3  SetCutoffUpdatesField
 *   2.4  SetQUpdatesField
 *   2.5  SetModeUpdatesField
 *   2.6  SetParametersUpdatesAllFields
 *   2.7  ResetZerosStates
 *   2.8  SetStateAndGetStateRoundTrip
 *   2.9  OnSampleRateChangedForwardsToUpdateCoefficients
 *   2.10 SetCutoffTriggersUpdateOnce
 *   2.11 SetParametersTriggersUpdateOnce
 *
 */

#include "filters/caspi_Filter.h"
#include "analysis/caspi_SpectralProfile.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI::Filters;
using CASPI::SpectralProfile;
using CASPI::WindowType;

/*
 * Test constants
 */
static constexpr double kSampleRate = 48000.0;
static constexpr double kCutoff     = 1000.0;
static constexpr double kQ          = 0.7071067811865476;
static constexpr int    kBlockSize  = 16384;


/*
 * Minimal CRTP stub for testing FilterBase in isolation.
 * Counts updateCoefficients() calls; processSample is an identity pass-through.
 */
template <typename FloatType>
class FilterTracker : public FilterBase<FilterTracker<FloatType>, FloatType, 2, 5>
{
    public:
        int updateCount = 0;

        void updateCoefficients() noexcept
        {
            ++updateCount;
        }

        FloatType processSample (FloatType in) noexcept override
        {
            return in;
        }
};

/*
 * Section 1: AtomicCoefficients
 */

TEST (AtomicCoefficients, DefaultConstructedReadsZero)
{
    AtomicCoefficients<float, 5> ac;
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_FLOAT_EQ (ac[i], 0.0f) << "index " << i;
    }
}

TEST (AtomicCoefficients, SwapPublishesNewValues)
{
    AtomicCoefficients<float, 3> ac;
    const std::array<float, 3> vals { 1.0f, 2.0f, 3.0f };
    ac.swap (vals);

    EXPECT_FLOAT_EQ (ac[0], 1.0f);
    EXPECT_FLOAT_EQ (ac[1], 2.0f);
    EXPECT_FLOAT_EQ (ac[2], 3.0f);
}

TEST (AtomicCoefficients, SwapIsConsistentAfterToggle)
{
    AtomicCoefficients<float, 2> ac;
    const std::array<float, 2> first  { 10.0f, 20.0f };
    const std::array<float, 2> second { 30.0f, 40.0f };

    ac.swap (first);
    ac.swap (second);

    EXPECT_FLOAT_EQ (ac[0], 30.0f);
    EXPECT_FLOAT_EQ (ac[1], 40.0f);
}

TEST (AtomicCoefficients, IndexOperatorMatchesGet)
{
    AtomicCoefficients<double, 4> ac;
    const std::array<double, 4> vals { 1.0, 2.0, 3.0, 4.0 };
    ac.swap (vals);

    const auto& arr = ac.get();
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_DOUBLE_EQ (ac[i], arr[i]);
    }
}

TEST (AtomicCoefficients, ResetZerosBothBuffers)
{
    AtomicCoefficients<float, 3> ac;
    const std::array<float, 3> vals { 5.0f, 6.0f, 7.0f };
    ac.swap (vals);
    ac.reset();

    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ (ac[i], 0.0f);
    }
}

/*
 * Section 2: FilterBase parameter API
 */

TEST (FilterBase, DefaultCutoffIs1kHz)
{
    FilterTracker<float> f;
    EXPECT_FLOAT_EQ (f.getCutoff(), 1000.0f);
}

TEST (FilterBase, DefaultQIsButterworth)
{
    FilterTracker<float> f;
    EXPECT_NEAR (f.getQ(), 0.7071068f, 1e-5f);
}

TEST (FilterBase, SetCutoffUpdatesField)
{
    FilterTracker<float> f;
    f.setCutoff (800.0f);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
}

TEST (FilterBase, SetQUpdatesField)
{
    FilterTracker<float> f;
    f.setQ (2.0f);
    EXPECT_FLOAT_EQ (f.getQ(), 2.0f);
}

TEST (FilterBase, SetModeUpdatesField)
{
    FilterTracker<float> f;
    f.setMode (FilterMode::HighPass);
    EXPECT_EQ (f.getMode(), FilterMode::HighPass);
}

TEST (FilterBase, SetParametersUpdatesAllFields)
{
    FilterTracker<float> f;
    f.setParameters (500.0f, 1.5f, FilterMode::BandPass);

    EXPECT_FLOAT_EQ (f.getCutoff(), 500.0f);
    EXPECT_FLOAT_EQ (f.getQ(),      1.5f);
    EXPECT_EQ       (f.getMode(),   FilterMode::BandPass);
}

TEST (FilterBase, ResetZerosStates)
{
    FilterTracker<float> f;
    f.setState (0, 3.14f);
    f.setState (1, 2.72f);
    f.reset();

    EXPECT_FLOAT_EQ (f.getState (0), 0.0f);
    EXPECT_FLOAT_EQ (f.getState (1), 0.0f);
}

TEST (FilterBase, SetStateAndGetStateRoundTrip)
{
    FilterTracker<float> f;
    f.setState (0, 1.23f);
    f.setState (1, 4.56f);

    EXPECT_FLOAT_EQ (f.getState (0), 1.23f);
    EXPECT_FLOAT_EQ (f.getState (1), 4.56f);
}

TEST (FilterBase, OnSampleRateChangedForwardsToUpdateCoefficients)
{
    FilterTracker<float> f;
    f.setCutoff (1000.0f);
    const int countBefore = f.updateCount;

    f.onSampleRateChanged (44100.0f);

    EXPECT_GT (f.updateCount, countBefore);
}

TEST (FilterBase, SetCutoffTriggersUpdateOnce)
{
    FilterTracker<float> f;
    f.updateCount = 0;
    f.setCutoff (800.0f);
    EXPECT_EQ (f.updateCount, 1);
}

TEST (FilterBase, SetParametersTriggersUpdateOnce)
{
    FilterTracker<float> f;
    f.updateCount = 0;
    f.setParameters (800.0f, 1.0f, FilterMode::LowPass);
    EXPECT_EQ (f.updateCount, 1);
}