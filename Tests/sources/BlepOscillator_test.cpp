/*******************************************************************************
 * @file BlepOscillator_test.cpp
 * @brief Unit tests for BlepOscillator.
 *
 * BUILD
 * -----
 *   g++ -O2 -std=c++17 -I../include              \
 *       caspi_BlepOscillator_test.cpp             \
 *       -lgtest -lgtest_main -lpthread            \
 *       -o blep_test
 *
 ******************************************************************************/

#include "oscillators/caspi_BlepOscillator.h"
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

/*******************************************************************************
 * Constants
 ******************************************************************************/

static constexpr float kSR       = 44100.f;
static constexpr float kFreq     = 440.f;
static constexpr float kHighFreq = 10000.f;
static constexpr int kPeriod     = static_cast<int> (kSR / kFreq + 0.5f);
static constexpr int kBlock      = 512;
static constexpr float kAmpTol   = 1.1f;
static constexpr float kDCTol    = 0.02f;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

using Osc   = CASPI::Oscillators::BlepOscillator<float>;
using Shape = CASPI::Oscillators::WaveShape;

static std::vector<float> renderN (Osc& osc, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    osc.renderBlock (buf.data(), n);
    return buf;
}

static float dcOf (Osc& osc, int n)
{
    const auto buf   = renderN (osc, n);
    const double sum = std::accumulate (buf.begin(), buf.end(), 0.0);
    return static_cast<float> (sum / n);
}

static float peakOf (Osc& osc, int n)
{
    const auto buf = renderN (osc, n);
    float peak     = 0.f;
    for (const float s : buf)
    {
        peak = std::max (peak, std::abs (s));
    }
    return peak;
}

/*
 * binEnergy — DFT energy at a single frequency bin.
 * Used to compare aliasing between BLEP and naive oscillators.
 */
static float binEnergy (const std::vector<float>& buf, float binFreq, float sr)
{
    double re = 0.0, im = 0.0;
    const double w = CASPI::Constants::TWO_PI<double> * static_cast<double> (binFreq / sr);
    for (int i = 0; i < static_cast<int> (buf.size()); ++i)
    {
        const double idx  = static_cast<double> (i);
        re               += static_cast<double> (buf[static_cast<std::size_t> (i)]) * std::cos (w * idx);
        im               += static_cast<double> (buf[static_cast<std::size_t> (i)]) * std::sin (w * idx);
    }
    return static_cast<float> (std::sqrt (re * re + im * im) / static_cast<double> (buf.size()));
}

/*******************************************************************************
 * Naive oscillators — aliasing baseline
 ******************************************************************************/

static std::vector<float> naiveSaw (float hz, float sr, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    const float dt = hz / sr;
    float p        = 0.f;
    for (int i = 0; i < n; ++i)
    {
        buf[static_cast<std::size_t> (i)]  = 2.f * p - 1.f;
        p                                 += dt;
        if (p >= 1.f)
        {
            p -= 1.f;
        }
    }
    return buf;
}

static std::vector<float> naiveSquare (float hz, float sr, int n, float pw = 0.5f)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    const float dt = hz / sr;
    float p        = 0.f;
    for (int i = 0; i < n; ++i)
    {
        buf[static_cast<std::size_t> (i)]  = (p < pw) ? -1.f : 1.f;
        p                                 += dt;
        if (p >= 1.f)
        {
            p -= 1.f;
        }
    }
    return buf;
}

/*******************************************************************************
 * Amplitude bounds
 ******************************************************************************/

TEST (BlepOscillator, SineAmplitudeBounds)
{
    Osc osc (Shape::Sine, kSR, kFreq);
    EXPECT_LE (peakOf (osc, kPeriod * 10), kAmpTol);
}

TEST (BlepOscillator, SawAmplitudeBounds)
{
    Osc osc (Shape::Saw, kSR, kFreq);
    EXPECT_LE (peakOf (osc, kPeriod * 10), kAmpTol);
}

TEST (BlepOscillator, SquareAmplitudeBounds)
{
    Osc osc (Shape::Square, kSR, kFreq);
    EXPECT_LE (peakOf (osc, kPeriod * 10), kAmpTol);
}

TEST (BlepOscillator, TriangleAmplitudeBounds)
{
    Osc osc (Shape::Triangle, kSR, kFreq);
    /* Extra headroom for integrator startup transient */
    EXPECT_LE (peakOf (osc, kPeriod * 20), 1.5f);
}

/*******************************************************************************
 * DC offset
 ******************************************************************************/

TEST (BlepOscillator, SineDC)
{
    Osc osc (Shape::Sine, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

TEST (BlepOscillator, SawDC)
{
    Osc osc (Shape::Saw, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

TEST (BlepOscillator, SquareDCAtHalfDuty)
{
    Osc osc (Shape::Square, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

TEST (BlepOscillator, SquareDCAtAsymmetricDuty)
{
    /* Square convention: (p < pw) ? -1 : +1
     * DC = 1 - 2*scaledPw
     * Normalised 0.25 => scaled = 0.01 + 0.25*(0.99-0.01) = 0.255
     * Expected DC = 1 - 2*0.255 = +0.49
     * (Normalised 0.75 => DC ≈ -0.49; the sign depends on the convention) */
    Osc osc (Shape::Square, kSR, kFreq);
    osc.pulseWidth.setBaseNormalised (0.25f);
    const float dc = dcOf (osc, kPeriod * 4);
    EXPECT_GT (dc, 0.3f);
    EXPECT_LT (dc, 0.7f);
}

TEST (BlepOscillator, TriangleDCAfterSettling)
{
    Osc osc (Shape::Triangle, kSR, kFreq);
    for (int i = 0; i < kPeriod * 5; ++i)
    {
        osc.renderSample();
    }
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol * 2.f);
}

/*******************************************************************************
 * Phase reset
 ******************************************************************************/

TEST (BlepOscillator, PhaseResetSine)
{
    Osc osc (Shape::Sine, kSR, kFreq);
    for (int i = 0; i < kPeriod / 2; ++i)
    {
        osc.renderSample();
    }
    osc.resetPhase();
    /* sin(TWO_PI * 0) == 0 */
    EXPECT_NEAR (osc.renderSample(), 0.f, 1e-4f);
}

TEST (BlepOscillator, PhaseResetSaw)
{
    Osc osc (Shape::Saw, kSR, kFreq);
    for (int i = 0; i < kPeriod / 2; ++i)
    {
        osc.renderSample();
    }
    osc.resetPhase();
    /* Phase=0 is the BLEP correction point: naive=-1, polyBlep(0,dt)=-1,
     * output = naive - blep = 0. This is correct.
     * Skip the correction sample; the next is past the window and near -1+2*dt. */
    osc.renderSample();
    EXPECT_NEAR (osc.renderSample(), -1.f + 2.f * (kFreq / kSR), 0.05f);
}

/*******************************************************************************
 * Pulse width
 ******************************************************************************/

TEST (BlepOscillator, PulseWidthClampingNoNaN)
{
    Osc osc (Shape::Square, kSR, kFreq);
    osc.pulseWidth.setBaseNormalised (0.0f);
    EXPECT_FALSE (std::isnan (osc.renderSample()));
    osc.pulseWidth.setBaseNormalised (1.0f);
    EXPECT_FALSE (std::isnan (osc.renderSample()));
}

/*******************************************************************************
 * Shape switching
 ******************************************************************************/

TEST (BlepOscillator, ShapeSwitchNoNaN)
{
    Osc osc (Shape::Saw, kSR, kFreq);
    for (int i = 0; i < kPeriod; ++i)
    {
        osc.renderSample();
    }
    osc.setShape (Shape::Triangle);
    bool foundNaN = false;
    for (int i = 0; i < kPeriod * 2; ++i)
    {
        if (std::isnan (osc.renderSample()))
        {
            foundNaN = true;
        }
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * Hard sync
 ******************************************************************************/

TEST (BlepOscillator, ForceSyncNoNaN)
{
    Osc secondary (Shape::Saw, kSR, kFreq * 2.f);
    secondary.forceSync();
    const float s = secondary.renderSample();
    EXPECT_FALSE (std::isnan (s));
    EXPECT_FALSE (std::isinf (s));
}

TEST (BlepOscillator, PhaseWrappedFlagIsSet)
{
    Osc osc (Shape::Saw, kSR, kFreq);
    bool sawWrap = false;
    for (int i = 0; i < kPeriod * 2; ++i)
    {
        osc.renderSample();
        if (osc.phaseWrapped())
        {
            sawWrap = true;
        }
    }
    EXPECT_TRUE (sawWrap);
}

TEST (BlepOscillator, PrimarySecondaryHardSync)
{
    Osc primary (Shape::Saw, kSR, kFreq);
    Osc secondary (Shape::Saw, kSR, kFreq * 3.f);
    bool foundNaN = false;

    for (int i = 0; i < kPeriod * 4; ++i)
    {
        primary.renderSample();
        if (primary.phaseWrapped())
        {
            secondary.forceSync();
        }
        if (std::isnan (secondary.renderSample()))
        {
            foundNaN = true;
        }
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * Triangle stability
 ******************************************************************************/

TEST (BlepOscillator, TriangleNoDivergence)
{
    Osc osc (Shape::Triangle, kSR, kFreq);
    float maxVal = 0.f;
    for (int i = 0; i < kPeriod * 100; ++i)
    {
        maxVal = std::max (maxVal, std::abs (osc.renderSample()));
    }
    EXPECT_LE (maxVal, 2.0f);
}

TEST (BlepOscillator, TriangleLowFreqStability)
{
    Osc osc (Shape::Triangle, kSR, 20.f);
    const int n  = static_cast<int> (kSR / 20.f) * 10;
    float maxVal = 0.f;
    for (int i = 0; i < n; ++i)
    {
        maxVal = std::max (maxVal, std::abs (osc.renderSample()));
    }
    EXPECT_FALSE (std::isinf (maxVal));
    EXPECT_FALSE (std::isnan (maxVal));
}

/*******************************************************************************
 * renderBlock matches renderSample loop
 ******************************************************************************/

TEST (BlepOscillator, RenderBlockMatchesRenderSample)
{
    Osc oscA (Shape::Saw, kSR, kFreq);
    Osc oscB (Shape::Saw, kSR, kFreq);

    std::vector<float> blockOut (static_cast<std::size_t> (kBlock));
    oscA.renderBlock (blockOut.data(), kBlock);

    std::vector<float> sampleOut (static_cast<std::size_t> (kBlock));
    for (int i = 0; i < kBlock; ++i)
    {
        sampleOut[static_cast<std::size_t> (i)] = oscB.renderSample();
    }

    for (int i = 0; i < kBlock; ++i)
    {
        EXPECT_NEAR (blockOut[static_cast<std::size_t> (i)], sampleOut[static_cast<std::size_t> (i)], 1e-5f)
            << "Mismatch at sample " << i;
    }
}

/*******************************************************************************
 * BLEP vs Naive aliasing
 ******************************************************************************/

TEST (BlepVsNaive, SawAliasReduction)
{
    const int n         = static_cast<int> (kSR);
    const float binFreq = kSR / 4.f;

    Osc oscBlep (Shape::Saw, kSR, kHighFreq);
    const auto blepBuf  = renderN (oscBlep, n);
    const auto naiveBuf = naiveSaw (kHighFreq, kSR, n);

    EXPECT_LT (binEnergy (blepBuf, binFreq, kSR), binEnergy (naiveBuf, binFreq, kSR));
}

TEST (BlepVsNaive, SquareAliasReduction)
{
    const int n         = static_cast<int> (kSR);
    const float binFreq = kSR / 4.f;

    Osc oscBlep (Shape::Square, kSR, kHighFreq);
    const auto blepBuf  = renderN (oscBlep, n);
    const auto naiveBuf = naiveSquare (kHighFreq, kSR, n);

    EXPECT_LT (binEnergy (blepBuf, binFreq, kSR), binEnergy (naiveBuf, binFreq, kSR));
}