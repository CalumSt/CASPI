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
 * Analytical tests (Section 6)
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
 * TEST PLAN SUMMARY
 *
 * Section 1: SvfFilter - construction and coefficients
 *   1.1  DefaultConstructsWithoutError
 *   1.2  FullConstructorSetsParameters
 *   1.3  CoefficientsNonZeroAfterFullConstruction
 *   1.4  A1InUnitIntervalForValidCutoffs
 *   1.5  KEqualsOneOverQ
 *   1.6  GEqualsPrewarpedFrequency
 *   1.7  DefaultConstructedProcessSampleReturnsFinite
 *
 * Section 2: SvfFilter - state and reset
 *   2.1  StateIsZeroInitially
 *   2.2  StateChangesAfterProcessSample
 *   2.3  ResetZerosStateAfterProcessing
 *   2.4  ResetDoesNotChangeCoefficients
 *
 * Section 3: SvfFilter - low-pass spectral tests
 *   3.1  LowPassPassbandEnergyDominates
 *   3.2  LowPassStopbandAttenuated
 *   3.3  LowPassMagnitudeAtDcIsNearOne
 *   3.4  LowPassMagnitudeAtNyquistIsNearZero
 *   3.5  LowPassCutoffMagnitudeIsNear3dBDown
 *
 * 4Section 3: SvfFilter - high-pass spectral tests
 *   4.1  HighPassStopbandAttenuated
 *   4.2  HighPassPassbandEnergyDominates
 *   4.3  HighPassMagnitudeAtNyquistIsNearOne
 *   4.4  HighPassCutoffMagnitudeIsNear3dBDown
 *
 * Section 5: SvfFilter - band-pass and notch spectral tests
 *   5.1  BandPassPeakNearCutoff
 *   5.2  BandPassSidebandsAttenuated
 *   5.3  NotchDipNearCutoff
 *   5.4  NotchPassbandEnergyDominates
 *
 * Section 6: SvfFilter - analytical frequency response
 *   6.1  LowPassResponseAt1HzIsNearOne
 *   6.2  LowPassResponseAtNyquistIsSmall
 *   6.3  LowPassResponseAtCutoffIsNear707
 *   6.4  HighPassResponseAt1HzIsNearZero
 *   6.5  HighPassResponseAtNyquistIsNearOne
 *   6.6  BandPassResponsePeakAtCutoff
 *   6.7  NotchResponseDipAtCutoff
 *   6.8  AllPassResponseIsNearlyFlatMagnitude
 *   6.9  LPResponseMonotonicallyDecreasesAboveCutoff
 *   6.10 ResponseChangesWithMode
 */

#include "filters/caspi_SvfFilter.h"
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
* renderNoise
*
* Renders kBlockSize samples of white noise through a filter.
* Uses a fixed seed so tests are deterministic.
*/
static std::vector<double> renderNoise (SvfFilter<double>& filter, int n = kBlockSize)
{
    std::mt19937 rng (42u);
    std::uniform_real_distribution<double> dist (-1.0, 1.0);

    std::vector<double> out (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        out[static_cast<std::size_t> (i)] = filter.processSample (dist (rng));
    }
    return out;
}

/*
 * Section 1: SvfFilter - construction and coefficients
 */

TEST (SvfFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ SvfFilter<float> f; (void) f; });
}

TEST (SvfFilter, FullConstructorSetsParameters)
{
    const SvfFilter<float> f (44100.0f, 800.0f, 0.707f, FilterMode::HighPass);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_EQ       (f.getMode(),   FilterMode::HighPass);
}

TEST (SvfFilter, CoefficientsNonZeroAfterFullConstruction)
{
    const SvfFilter<float> f (44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    EXPECT_NE (f.getCoeffAt (0), 0.0f);
}

TEST (SvfFilter, A1InUnitIntervalForValidCutoffs)
{
    const float fs = 44100.0f;
    for (float fc : { 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f })
    {
        const SvfFilter<float> f (fs, fc, 0.707f);
        const float a1 = f.getCoeffAt (0);
        EXPECT_GT (a1, 0.0f) << "fc=" << fc;
        EXPECT_LT (a1, 1.0f) << "fc=" << fc;
    }
}

TEST (SvfFilter, KEqualsOneOverQ)
{
    const float fs = 44100.0f;
    const float fc = 1000.0f;
    for (float q : { 0.5f, 0.707f, 1.0f, 2.0f })
    {
        const SvfFilter<float> f (fs, fc, q);
        EXPECT_NEAR (f.getCoeffAt (4), 1.0f / q, 1e-5f) << "Q=" << q;
    }
}

TEST (SvfFilter, GEqualsPrewarpedFrequency)
{
    const float fc = 1000.0f;
    const float fs = 44100.0f;
    const SvfFilter<float> f (fs, fc, 0.707f);

    const float expected = std::tan (CASPI::Constants::PI<float> * fc / fs);
    EXPECT_NEAR (f.getCoeffAt (3), expected, 1e-5f);
}

TEST (SvfFilter, DefaultConstructedProcessSampleReturnsFinite)
{
    SvfFilter<float> f;
    EXPECT_NO_FATAL_FAILURE ({
        const float out = f.processSample (0.5f);
        EXPECT_TRUE (std::isfinite (out));
    });
}

/*
 * Section 2: SvfFilter - state and reset
 */

TEST (SvfFilter, StateIsZeroInitially)
{
    const SvfFilter<float> f (44100.0f, 1000.0f, 0.707f);
    EXPECT_FLOAT_EQ (f.getState (0), 0.0f);
    EXPECT_FLOAT_EQ (f.getState (1), 0.0f);
}

TEST (SvfFilter, StateChangesAfterProcessSample)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const double s0 = f.getState (0);
    f.processSample (1.0);
    EXPECT_NE (f.getState (0), s0);
}

TEST (SvfFilter, ResetZerosStateAfterProcessing)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0);
    }
    f.reset();

    EXPECT_DOUBLE_EQ (f.getState (0), 0.0);
    EXPECT_DOUBLE_EQ (f.getState (1), 0.0);
}

TEST (SvfFilter, ResetDoesNotChangeCoefficients)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const double a1Before = f.getCoeffAt (0);

    f.reset();

    EXPECT_DOUBLE_EQ (f.getCoeffAt (0), a1Before);
}

/*
 * Section 3: SvfFilter - low-pass spectral tests
 */

TEST (SvfFilter_Spectral, LowPassPassbandEnergyDominates)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double passEnergy = profile.getEnergyInRange (20.0,        kCutoff * 0.8);
    const double stopEnergy = profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_GT (passEnergy, stopEnergy * 10.0);
}

TEST (SvfFilter_Spectral, LowPassStopbandAttenuated)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double totalEnergy    = profile.getTotalEnergy();
    const double stopbandEnergy = profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_LT (stopbandEnergy, totalEnergy * 0.05);
}

TEST (SvfFilter_Spectral, LowPassMagnitudeAtDcIsNearOne)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (10.0), 1.0, 0.01);
}

TEST (SvfFilter_Spectral, LowPassMagnitudeAtNyquistIsNearZero)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 0.01);
}

TEST (SvfFilter_Spectral, LowPassCutoffMagnitudeIsNear3dBDown)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.02);
}

/*
 * 4Section 3: SvfFilter - high-pass spectral tests
 */

TEST (SvfFilter_Spectral, HighPassStopbandAttenuated)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double totalEnergy    = profile.getTotalEnergy();
    const double stopbandEnergy = profile.getEnergyInRange (20.0, kCutoff / 4.0);

    EXPECT_LT (stopbandEnergy, totalEnergy * 0.05);
}

TEST (SvfFilter_Spectral, HighPassPassbandEnergyDominates)
{
    SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double passEnergy = profile.getEnergyInRange (kCutoff * 2.0, kSampleRate / 2.0);
    const double stopEnergy = profile.getEnergyInRange (20.0,           kCutoff / 4.0);

    EXPECT_GT (passEnergy, stopEnergy * 10.0);
}

TEST (SvfFilter_Spectral, HighPassMagnitudeAtNyquistIsNearOne)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 1.0, 0.02);
}

TEST (SvfFilter_Spectral, HighPassCutoffMagnitudeIsNear3dBDown)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.02);
}

/*
 * Section 5: SvfFilter - band-pass and notch spectral tests
 */

TEST (SvfFilter_Spectral, BandPassPeakNearCutoff)
{
    SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    EXPECT_TRUE (profile.hasPeakAt (kCutoff, kCutoff * 0.10));
}

TEST (SvfFilter_Spectral, BandPassSidebandsAttenuated)
{
    SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double atCutoff  = profile.getMagnitudeAt (kCutoff);
    const double atQuarter = profile.getMagnitudeAt (kCutoff / 4.0);
    const double atQuad    = profile.getMagnitudeAt (std::min (kCutoff * 4.0, kSampleRate / 2.0 * 0.95));

    EXPECT_GT (atCutoff, atQuarter * 3.0);
    EXPECT_GT (atCutoff, atQuad    * 3.0);
}

TEST (SvfFilter_Spectral, NotchDipNearCutoff)
{
    SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double atCutoff = profile.getMagnitudeAt (kCutoff);
    const double atHalfFc = profile.getMagnitudeAt (kCutoff / 2.0);

    EXPECT_LT (atCutoff, atHalfFc * 0.5);
}

TEST (SvfFilter_Spectral, NotchPassbandEnergyDominates)
{
    SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double notchEnergy = profile.getEnergyInRange (kCutoff * 0.9, kCutoff * 1.1);
    const double passEnergy  = profile.getEnergyInRange (20.0, kCutoff / 4.0)
                             + profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_GT (passEnergy, notchEnergy * 5.0);
}

/*
 * Section 6: SvfFilter - analytical frequency response
 */

TEST (SvfFilter_Analytic, LowPassResponseAt1HzIsNearOne)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (1.0), 1.0, 0.001);
}

TEST (SvfFilter_Analytic, LowPassResponseAtNyquistIsSmall)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 0.01);
}

TEST (SvfFilter_Analytic, LowPassResponseAtCutoffIsNear707)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.01);
}

TEST (SvfFilter_Analytic, HighPassResponseAt1HzIsNearZero)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_LT (f.getFrequencyResponse (1.0), 0.01);
}

TEST (SvfFilter_Analytic, HighPassResponseAtNyquistIsNearOne)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 1.0, 0.02);
}

TEST (SvfFilter_Analytic, BandPassResponsePeakAtCutoff)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);

    const double atFc      = f.getFrequencyResponse (kCutoff);
    const double atQuarter = f.getFrequencyResponse (kCutoff / 4.0);
    const double atQuad    = f.getFrequencyResponse (kCutoff * 4.0);

    EXPECT_GT (atFc, atQuarter);
    EXPECT_GT (atFc, atQuad);
}

TEST (SvfFilter_Analytic, NotchResponseDipAtCutoff)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);

    const double atFc     = f.getFrequencyResponse (kCutoff);
    const double atHalf   = f.getFrequencyResponse (kCutoff / 2.0);
    const double atDouble = f.getFrequencyResponse (kCutoff * 2.0);

    EXPECT_LT (atFc, atHalf);
    EXPECT_LT (atFc, atDouble);
}

TEST (SvfFilter_Analytic, AllPassResponseIsNearlyFlatMagnitude)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::AllPass);

    const double freqs[] = { 100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };
    for (double freq : freqs)
    {
        if (freq < kSampleRate / 2.0)
        {
            EXPECT_NEAR (f.getFrequencyResponse (freq), 1.0, 0.01) << "at " << freq << " Hz";
        }
    }
}

TEST (SvfFilter_Analytic, LPResponseMonotonicallyDecreasesAboveCutoff)
{
    const SvfFilter<double> f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);

    double prev = f.getFrequencyResponse (kCutoff * 1.5);
    for (double mult : { 2.0, 3.0, 4.0, 6.0, 8.0 })
    {
        const double freq = kCutoff * mult;
        if (freq >= kSampleRate / 2.0)
        {
            break;
        }
        const double curr = f.getFrequencyResponse (freq);
        EXPECT_LT (curr, prev) << "at " << freq << " Hz";
        prev = curr;
    }
}

TEST (SvfFilter_Analytic, ResponseChangesWithMode)
{
    const SvfFilter<double> lp (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const SvfFilter<double> hp (kSampleRate, kCutoff, kQ, FilterMode::HighPass);

    EXPECT_LT (lp.getFrequencyResponse (kCutoff * 4.0), 0.1);
    EXPECT_LT (hp.getFrequencyResponse (kCutoff / 4.0), 0.1);
    EXPECT_NEAR (lp.getFrequencyResponse (kCutoff), hp.getFrequencyResponse (kCutoff), 0.02);
}