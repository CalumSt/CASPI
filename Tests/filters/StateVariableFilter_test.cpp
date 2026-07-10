/*
 * @file test_StateVariable.cpp
 *
 * Unit tests for:
 *   CASPI::Filters::StateVariable<FloatType>
 *
 * Same test strategy as the original SVF tests: state-box, spectral, and
 * analytic frequency-response tests.
 */

#include "filters/caspi_StateVariable.h"
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

static constexpr double kSampleRate = 48000.0;
static constexpr double kCutoff     = 1000.0;
static constexpr double kQ          = 0.7071067811865476;
static constexpr int    kBlockSize  = 16384;

using SVF_t = StateVariable<double>;
using SVF_f = StateVariable<float>;

static std::vector<double> renderNoise (SVF_t& filter, int n = kBlockSize)
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
 * Section 1: Construction and coefficients
 */

TEST (StateVariableFilter, DefaultConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ SVF_f f; (void) f; });
}

TEST (StateVariableFilter, FullConstructorSetsParameters)
{
    const StateVariable<float> f (
        44100.0f, 800.0f, 0.707f, FilterMode::HighPass);
    EXPECT_FLOAT_EQ (f.getCutoff(), 800.0f);
    EXPECT_EQ       (f.getMode(),   FilterMode::HighPass);
}

TEST (StateVariableFilter, CoefficientsNonZeroAfterFullConstruction)
{
    const StateVariable<float> f (
        44100.0f, 1000.0f, 0.707f, FilterMode::LowPass);
    EXPECT_NE (f.getCoeffAt (0), 0.0f);
}

TEST (StateVariableFilter, A1InUnitIntervalForValidCutoffs)
{
    const float fs = 44100.0f;
    for (float fc : { 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f })
    {
        const StateVariable<float> f (fs, fc, 0.707f);
        const float a1 = f.getCoeffAt (0);
        EXPECT_GT (a1, 0.0f) << "fc=" << fc;
        EXPECT_LT (a1, 1.0f) << "fc=" << fc;
    }
}

TEST (StateVariableFilter, KEqualsOneOverQ)
{
    const float fs = 44100.0f;
    const float fc = 1000.0f;
    for (float q : { 0.5f, 0.707f, 1.0f, 2.0f })
    {
        const StateVariable<float> f (fs, fc, q);
        EXPECT_NEAR (f.getCoeffAt (4), 1.0f / q, 1e-5f) << "Q=" << q;
    }
}

TEST (StateVariableFilter, GEqualsPrewarpedFrequency)
{
    const float fc = 1000.0f;
    const float fs = 44100.0f;
    const StateVariable<float> f (fs, fc, 0.707f);

    const float expected = std::tan (CASPI::Constants::PI<float> * fc / fs);
    EXPECT_NEAR (f.getCoeffAt (3), expected, 1e-5f);
}

TEST (StateVariableFilter, DefaultConstructedProcessSampleReturnsFinite)
{
    StateVariable<float> f;
    EXPECT_NO_FATAL_FAILURE ({
        const float out = f.processSample (0.5f);
        EXPECT_TRUE (std::isfinite (out));
    });
}

/*
 * Section 2: State and reset
 */

TEST (StateVariableFilter, StateIsZeroInitially)
{
    const StateVariable<float> f (
        44100.0f, 1000.0f, 0.707f);
    EXPECT_FLOAT_EQ (f.getState (0), 0.0f);
    EXPECT_FLOAT_EQ (f.getState (1), 0.0f);
}

TEST (StateVariableFilter, StateChangesAfterProcessSample)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const double s0 = f.getState (0);
    f.processSample (1.0);
    EXPECT_NE (f.getState (0), s0);
}

TEST (StateVariableFilter, ResetZerosStateAfterProcessing)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    for (int i = 0; i < 100; ++i)
    {
        f.processSample (1.0);
    }
    f.reset();

    EXPECT_DOUBLE_EQ (f.getState (0), 0.0);
    EXPECT_DOUBLE_EQ (f.getState (1), 0.0);
}

TEST (StateVariableFilter, ResetDoesNotChangeCoefficients)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const double a1Before = f.getCoeffAt (0);

    f.reset();

    EXPECT_DOUBLE_EQ (f.getCoeffAt (0), a1Before);
}

/*
 * Section 3: Low-pass spectral tests
 */

TEST (StateVariableFilter_Spectral, LowPassPassbandEnergyDominates)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double passEnergy = profile.getEnergyInRange (20.0,        kCutoff * 0.8);
    const double stopEnergy = profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_GT (passEnergy, stopEnergy * 10.0);
}

TEST (StateVariableFilter_Spectral, LowPassStopbandAttenuated)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double totalEnergy    = profile.getTotalEnergy();
    const double stopbandEnergy = profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_LT (stopbandEnergy, totalEnergy * 0.05);
}

TEST (StateVariableFilter_Spectral, LowPassMagnitudeAtDcIsNearOne)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (10.0), 1.0, 0.01);
}

TEST (StateVariableFilter_Spectral, LowPassMagnitudeAtNyquistIsNearZero)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 0.01);
}

TEST (StateVariableFilter_Spectral, LowPassCutoffMagnitudeIsNear3dBDown)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.02);
}

/*
 * Section 4: High-pass spectral tests
 */

TEST (StateVariableFilter_Spectral, HighPassStopbandAttenuated)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double totalEnergy    = profile.getTotalEnergy();
    const double stopbandEnergy = profile.getEnergyInRange (20.0, kCutoff / 4.0);

    EXPECT_LT (stopbandEnergy, totalEnergy * 0.05);
}

TEST (StateVariableFilter_Spectral, HighPassPassbandEnergyDominates)
{
    SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double passEnergy = profile.getEnergyInRange (kCutoff * 2.0, kSampleRate / 2.0);
    const double stopEnergy = profile.getEnergyInRange (20.0,           kCutoff / 4.0);

    EXPECT_GT (passEnergy, stopEnergy * 10.0);
}

TEST (StateVariableFilter_Spectral, HighPassMagnitudeAtNyquistIsNearOne)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 1.0, 0.02);
}

TEST (StateVariableFilter_Spectral, HighPassCutoffMagnitudeIsNear3dBDown)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.02);
}

/*
 * Section 5: Band-pass and notch spectral tests
 */

TEST (StateVariableFilter_Spectral, BandPassPeakNearCutoff)
{
    SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    EXPECT_TRUE (profile.hasPeakAt (kCutoff, kCutoff * 0.10));
}

TEST (StateVariableFilter_Spectral, BandPassSidebandsAttenuated)
{
    SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double atCutoff  = profile.getMagnitudeAt (kCutoff);
    const double atQuarter = profile.getMagnitudeAt (kCutoff / 4.0);
    const double atQuad    = profile.getMagnitudeAt (std::min (kCutoff * 4.0, kSampleRate / 2.0 * 0.95));

    EXPECT_GT (atCutoff, atQuarter * 3.0);
    EXPECT_GT (atCutoff, atQuad    * 3.0);
}

TEST (StateVariableFilter_Spectral, NotchPassbandEnergyDominates)
{
    SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double notchEnergy = profile.getEnergyInRange (kCutoff * 0.9, kCutoff * 1.1);
    const double passEnergy  = profile.getEnergyInRange (20.0, kCutoff / 4.0)
                             + profile.getEnergyInRange (kCutoff * 4.0, kSampleRate / 2.0);

    EXPECT_GT (passEnergy, notchEnergy * 5.0);
}

TEST (StateVariableFilter_Spectral, NotchDipNearCutoff)
{
    SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);
    const auto buf = renderNoise (f);
    const SpectralProfile profile (buf, kSampleRate, WindowType::Hann);

    const double atCutoff = profile.getMagnitudeAt (kCutoff);
    const double atHalfFc = profile.getMagnitudeAt (kCutoff / 2.0);

    EXPECT_LT (atCutoff, atHalfFc * 0.5);
}

/*
 * Section 6: Analytical frequency response
 */

TEST (StateVariableFilter_Analytic, LowPassResponseAt1HzIsNearOne)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (1.0), 1.0, 0.001);
}

TEST (StateVariableFilter_Analytic, LowPassResponseAtNyquistIsSmall)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_LT (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 0.01);
}

TEST (StateVariableFilter_Analytic, LowPassResponseAtCutoffIsNear707)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    EXPECT_NEAR (f.getFrequencyResponse (kCutoff), 0.7071, 0.01);
}

TEST (StateVariableFilter_Analytic, HighPassResponseAt1HzIsNearZero)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_LT (f.getFrequencyResponse (1.0), 0.01);
}

TEST (StateVariableFilter_Analytic, HighPassResponseAtNyquistIsNearOne)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::HighPass);
    EXPECT_NEAR (f.getFrequencyResponse (kSampleRate / 2.0 * 0.99), 1.0, 0.02);
}

TEST (StateVariableFilter_Analytic, BandPassResponsePeakAtCutoff)
{
    const SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::BandPass);

    const double atFc      = f.getFrequencyResponse (kCutoff);
    const double atQuarter = f.getFrequencyResponse (kCutoff / 4.0);
    const double atQuad    = f.getFrequencyResponse (kCutoff * 4.0);

    EXPECT_GT (atFc, atQuarter);
    EXPECT_GT (atFc, atQuad);
}

TEST (StateVariableFilter_Analytic, NotchResponseDipAtCutoff)
{
    const SVF_t f (kSampleRate, kCutoff, 4.0, FilterMode::Notch);

    const double atFc     = f.getFrequencyResponse (kCutoff);
    const double atHalf   = f.getFrequencyResponse (kCutoff / 2.0);
    const double atDouble = f.getFrequencyResponse (kCutoff * 2.0);

    EXPECT_LT (atFc, atHalf);
    EXPECT_LT (atFc, atDouble);
}

TEST (StateVariableFilter_Analytic, AllPassResponseIsNearlyFlatMagnitude)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::AllPass);

    const double freqs[] = { 100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };
    for (double freq : freqs)
    {
        if (freq < kSampleRate / 2.0)
        {
            EXPECT_NEAR (f.getFrequencyResponse (freq), 1.0, 0.01) << "at " << freq << " Hz";
        }
    }
}

TEST (StateVariableFilter_Analytic, LPResponseMonotonicallyDecreasesAboveCutoff)
{
    const SVF_t f (kSampleRate, kCutoff, kQ, FilterMode::LowPass);

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

TEST (StateVariableFilter_Analytic, ResponseChangesWithMode)
{
    const SVF_t lp (kSampleRate, kCutoff, kQ, FilterMode::LowPass);
    const SVF_t hp (kSampleRate, kCutoff, kQ, FilterMode::HighPass);

    EXPECT_LT (lp.getFrequencyResponse (kCutoff * 4.0), 0.1);
    EXPECT_LT (hp.getFrequencyResponse (kCutoff / 4.0), 0.1);
    EXPECT_NEAR (lp.getFrequencyResponse (kCutoff), hp.getFrequencyResponse (kCutoff), 0.02);
}

