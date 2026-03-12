/*******************************************************************************
 * @file  caspi_WavetableOscillator_test.cpp
 * @brief Unit tests for WaveTable, WaveTableBank, and WavetableOscillator.
 *
 * BUILD
 * -----
 *   g++ -O2 -std=c++17 -I../include                   \
 *       caspi_WavetableOscillator_test.cpp             \
 *       -lgtest -lgtest_main -lpthread                 \
 *       -o wavetable_test
 *
 * TEST GROUPS
 * -----------
 *   WaveTable         — fill correctness, interpolation continuity
 *   WaveTableBank     — morph arithmetic: endpoints, midpoint average
 *   WavetableOscillator — amplitude bounds, DC, phase reset, phaseWrapped,
 *                         hard sync, renderBlock vs renderSample parity,
 *                         modulation, morphing, phase mod, bank hot-swap,
 *                         interpolation mode, spectral content
 *
 ******************************************************************************/

#include "oscillators/caspi_WavetableOscillator.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

/*******************************************************************************
 * Constants
 ******************************************************************************/

static constexpr float kSR     = 44100.f;
static constexpr float kFreq   = 440.f;
static constexpr int   kPeriod = static_cast<int> (kSR / kFreq + 0.5f);
static constexpr int   kBlock  = 512;
static constexpr float kAmpTol = 1.05f;   /* table is [-1,1]; allow rounding */
static constexpr float kDCTol  = 0.02f;

/*******************************************************************************
 * Type aliases
 ******************************************************************************/

static constexpr std::size_t kTableSize = 2048;
static constexpr std::size_t kNumMorph  = 4;

using Table  = CASPI::Oscillators::WaveTable<float, kTableSize>;
using Bank1  = CASPI::Oscillators::WaveTableBank<float, kTableSize, 1>;
using Bank4  = CASPI::Oscillators::WaveTableBank<float, kTableSize, kNumMorph>;
using Osc1   = CASPI::Oscillators::WavetableOscillator<float, kTableSize, 1>;
using Osc4   = CASPI::Oscillators::WavetableOscillator<float, kTableSize, kNumMorph>;
using IMode  = CASPI::Oscillators::InterpolationMode;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static std::vector<float> renderN (Osc1& osc, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    osc.renderBlock (buf.data(), n);
    return buf;
}

static std::vector<float> renderN (Osc4& osc, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    osc.renderBlock (buf.data(), n);
    return buf;
}

static float dcOf (Osc1& osc, int n)
{
    const auto   buf = renderN (osc, n);
    const double sum = std::accumulate (buf.begin(), buf.end(), 0.0);
    return static_cast<float> (sum / n);
}

static float peakOf (Osc1& osc, int n)
{
    const auto buf = renderN (osc, n);
    float peak = 0.f;
    for (const float s : buf) { peak = std::max (peak, std::abs (s)); }
    return peak;
}

/*
 * binEnergy — DFT energy at a single frequency bin.
 * Used to verify signal presence and spectral content.
 */
static float binEnergy (const std::vector<float>& buf, float binFreq, float sr)
{
    double re = 0.0, im = 0.0;
    const double w = CASPI::Constants::TWO_PI<double> * static_cast<double> (binFreq / sr);
    for (int i = 0; i < static_cast<int> (buf.size()); ++i)
    {
        const double idx = static_cast<double> (i);
        re += static_cast<double> (buf[static_cast<std::size_t> (i)]) * std::cos (w * idx);
        im += static_cast<double> (buf[static_cast<std::size_t> (i)]) * std::sin (w * idx);
    }
    return static_cast<float> (std::sqrt (re * re + im * im)
                              / static_cast<double> (buf.size()));
}

/*******************************************************************************
 * WaveTable — fill and lookup
 ******************************************************************************/

TEST (WaveTable, SineFillAmplitudeBounds)
{
    Table t;
    t.fillSine();
    for (std::size_t i = 0; i < kTableSize; ++i)
    {
        EXPECT_LE (std::abs (t[i]), 1.0f + 1e-5f) << "at index " << i;
    }
}

TEST (WaveTable, SawFillFirstSample)
{
    /* Sawtooth: 2*(i/N) - 1.  At i=0 this is -1. */
    Table t;
    t.fillSaw();
    EXPECT_NEAR (t[0], -1.f, 1e-5f);
}

TEST (WaveTable, TriangleFillMidpoint)
{
    /* Triangle reaches +1 at the midpoint (i = N/2). */
    Table t;
    t.fillTriangle();
    EXPECT_NEAR (t[kTableSize / 2], 1.f, 0.01f);
}

TEST (WaveTable, FillWithCustomCallable)
{
    /* Constant +0.5 waveform. */
    Table t;
    t.fillWith ([] (float) { return 0.5f; });
    for (std::size_t i = 0; i < kTableSize; ++i)
    {
        EXPECT_NEAR (t[i], 0.5f, 1e-6f);
    }
}

TEST (WaveTable, LinearReadAtPhaseZeroMatchesFirstSample)
{
    Table t;
    t.fillSine();
    EXPECT_NEAR (t.readLinear (0.f), t[0], 1e-6f);
}

TEST (WaveTable, LinearReadContinuity)
{
    /* Two adjacent fractional phases should give nearly identical output
     * on a smooth waveform. */
    Table t;
    t.fillSine();
    EXPECT_NEAR (t.readLinear (0.4999f), t.readLinear (0.5001f), 0.01f);
}

TEST (WaveTable, HermiteReadContinuity)
{
    Table t;
    t.fillSine();
    EXPECT_NEAR (t.readHermite (0.4999f), t.readHermite (0.5001f), 0.01f);
}

TEST (WaveTable, HermiteAndLinearAgreeOnSine)
{
    /* For a large table and a smooth waveform the two kernels should agree
     * within 1e-3 across the full phase range. */
    Table t;
    t.fillSine();
    float maxDiff = 0.f;
    for (int i = 0; i < 1000; ++i)
    {
        const float p = static_cast<float> (i) / 1000.f;
        maxDiff = std::max (maxDiff, std::abs (t.readLinear (p) - t.readHermite (p)));
    }
    EXPECT_LT (maxDiff, 1e-3f);
}

TEST (WaveTable, LinearReadNoNaN)
{
    Table t;
    t.fillSaw();
    bool found = false;
    for (int i = 0; i < 1000; ++i)
    {
        const float p = static_cast<float> (i) / 1000.f;
        if (std::isnan (t.readLinear (p))) { found = true; }
    }
    EXPECT_FALSE (found);
}

/*******************************************************************************
 * WaveTableBank — morph arithmetic
 ******************************************************************************/

TEST (WaveTableBank, MorphAtZeroMatchesFirstTable)
{
    Bank4 bank;
    bank[0].fillSine();
    bank[1].fillSaw();
    bank[2].fillTriangle();
    bank[3].fillSine();

    const float p = 0.3f;
    EXPECT_NEAR (bank.readLinear (p, 0.f), bank[0].readLinear (p), 1e-5f);
}

TEST (WaveTableBank, MorphAtMaxMatchesLastTable)
{
    Bank4 bank;
    bank[0].fillSine();
    bank[1].fillSaw();
    bank[2].fillTriangle();
    bank[3].fillSine();

    const float p     = 0.3f;
    const float morph = static_cast<float> (kNumMorph - 1);
    EXPECT_NEAR (bank.readLinear (p, morph), bank[3].readLinear (p), 1e-5f);
}

TEST (WaveTableBank, MorphMidpointIsAverageOfAdjacentTables)
{
    /* At morph == 1.5 the output should be the average of tables 1 and 2. */
    Bank4 bank;
    bank[0].fillSine();
    bank[1].fillSaw();
    bank[2].fillTriangle();
    bank[3].fillSine();

    const float p        = 0.2f;
    const float morph    = 1.5f;
    const float expected = 0.5f * bank[1].readLinear (p)
                         + 0.5f * bank[2].readLinear (p);
    EXPECT_NEAR (bank.readLinear (p, morph), expected, 1e-5f);
}

TEST (WaveTableBank, SingleTableIgnoresMorphPos)
{
    /* For NumTables == 1 the morph argument is irrelevant. */
    Bank1 bank;
    bank[0].fillSine();
    const float p = 0.25f;
    EXPECT_NEAR (bank.readLinear (p, 99.f), bank[0].readLinear (p), 1e-6f);
}

/*******************************************************************************
 * WavetableOscillator — amplitude and DC
 ******************************************************************************/

TEST (WavetableOscillator, SineAmplitudeBounds)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    EXPECT_LE (peakOf (osc, kPeriod * 10), kAmpTol);
}

TEST (WavetableOscillator, SineDC)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

TEST (WavetableOscillator, SawDC)
{
    Bank1 bank;
    bank[0].fillSaw();
    Osc1 osc (bank, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

TEST (WavetableOscillator, TriangleDC)
{
    Bank1 bank;
    bank[0].fillTriangle();
    Osc1 osc (bank, kSR, kFreq);
    EXPECT_NEAR (dcOf (osc, kPeriod * 4), 0.f, kDCTol);
}

/*******************************************************************************
 * WavetableOscillator — amplitude parameter
 ******************************************************************************/

TEST (WavetableOscillator, AmplitudeZeroProducesSilence)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    osc.setAmplitude (0.f);
    EXPECT_NEAR (peakOf (osc, kPeriod * 4), 0.f, 1e-5f);
}

TEST (WavetableOscillator, AmplitudeHalfReducesPeakByHalf)
{
    Bank1 bankFull, bankHalf;
    bankFull[0].fillSine();
    bankHalf[0].fillSine();

    Osc1 oscFull (bankFull, kSR, kFreq);
    const float fullPeak = peakOf (oscFull, kPeriod * 4);

    Osc1 oscHalf (bankHalf, kSR, kFreq);
    oscHalf.setAmplitude (0.5f);
    const float halfPeak = peakOf (oscHalf, kPeriod * 4);

    EXPECT_NEAR (halfPeak, fullPeak * 0.5f, 0.05f);
}

/*******************************************************************************
 * WavetableOscillator — phase reset
 ******************************************************************************/

TEST (WavetableOscillator, PhaseResetRestoresStartOfCycle)
{
    /* Sine at phase 0 is 0; after reset the first rendered sample should be ~0. */
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    for (int i = 0; i < kPeriod / 2; ++i) { osc.renderSample(); }
    osc.resetPhase();
    EXPECT_NEAR (osc.renderSample(), 0.f, 0.02f);
}

TEST (WavetableOscillator, PhaseOffsetAppliedOnReset)
{
    /* Sine at phase 0.25 is 1 (sin(PI/2) == 1). */
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    osc.setPhaseOffset (0.25f);
    osc.resetPhase();
    EXPECT_NEAR (osc.renderSample(), 1.f, 0.02f);
}

/*******************************************************************************
 * WavetableOscillator — phase wrapped flag
 ******************************************************************************/

TEST (WavetableOscillator, PhaseWrappedFlagIsSet)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    bool sawWrap = false;
    for (int i = 0; i < kPeriod * 2; ++i)
    {
        osc.renderSample();
        if (osc.phaseWrapped()) { sawWrap = true; }
    }
    EXPECT_TRUE (sawWrap);
}

/*******************************************************************************
 * WavetableOscillator — hard sync
 ******************************************************************************/

TEST (WavetableOscillator, ForceSyncNoNaN)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 secondary (bank, kSR, kFreq * 2.f);
    secondary.forceSync();
    const float s = secondary.renderSample();
    EXPECT_FALSE (std::isnan (s));
    EXPECT_FALSE (std::isinf (s));
}

TEST (WavetableOscillator, ForceSyncResetsToStartOfCycle)
{
    /* After forceSync on a sine bank, the first rendered sample should be ~0. */
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    for (int i = 0; i < kPeriod / 2; ++i) { osc.renderSample(); }
    osc.forceSync();
    EXPECT_NEAR (osc.renderSample(), 0.f, 0.02f);
}

TEST (WavetableOscillator, PrimarySecondaryHardSync)
{
    Bank1 bankP, bankS;
    bankP[0].fillSine();
    bankS[0].fillSaw();
    Osc1 primary   (bankP, kSR, kFreq);
    Osc1 secondary (bankS, kSR, kFreq * 2.f);
    bool foundNaN = false;

    for (int i = 0; i < kPeriod * 4; ++i)
    {
        primary.renderSample();
        if (primary.phaseWrapped()) { secondary.forceSync(); }
        if (std::isnan (secondary.renderSample())) { foundNaN = true; }
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * WavetableOscillator — renderBlock matches renderSample
 ******************************************************************************/

TEST (WavetableOscillator, RenderBlockMatchesRenderSample)
{
    Bank1 bankA, bankB;
    bankA[0].fillSine();
    bankB[0].fillSine();

    Osc1 oscBlock  (bankA, kSR, kFreq);
    Osc1 oscSample (bankB, kSR, kFreq);

    std::vector<float> blockOut (static_cast<std::size_t> (kBlock));
    oscBlock.renderBlock (blockOut.data(), kBlock);

    std::vector<float> sampleOut (static_cast<std::size_t> (kBlock));
    for (int i = 0; i < kBlock; ++i)
    {
        sampleOut[static_cast<std::size_t> (i)] = oscSample.renderSample();
    }

    for (int i = 0; i < kBlock; ++i)
    {
        EXPECT_NEAR (blockOut [static_cast<std::size_t> (i)],
                     sampleOut[static_cast<std::size_t> (i)],
                     1e-5f)
            << "Mismatch at sample " << i;
    }
}

/*******************************************************************************
 * WavetableOscillator — per-sample frequency modulation
 ******************************************************************************/

TEST (WavetableOscillator, FrequencyModulationNoNaN)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    bool foundNaN = false;

    for (int i = 0; i < kBlock; ++i)
    {
        osc.frequency.addModulation (std::sin (static_cast<float> (i) * 0.01f) * 100.f);
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
        osc.frequency.clearModulation();
    }
    EXPECT_FALSE (foundNaN);
}

TEST (WavetableOscillator, AmplitudeModulationNoNaN)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    bool foundNaN = false;

    for (int i = 0; i < kBlock; ++i)
    {
        osc.amplitude.addModulation (0.5f * std::sin (static_cast<float> (i) * 0.05f));
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
        osc.amplitude.clearModulation();
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * WavetableOscillator — wave morphing
 ******************************************************************************/

TEST (WavetableOscillator, MorphAtZeroMatchesSingleTableOutput)
{
    /* A single-table bank with the same waveform should produce identical
     * output to a morph bank with morphPosition set to 0. */
    Bank1 bank1;
    Bank4 bank4;
    bank1[0].fillSine();
    bank4[0].fillSine();
    for (std::size_t t = 1; t < kNumMorph; ++t) { bank4[t].fillSaw(); }

    Osc1 osc1 (bank1, kSR, kFreq);
    Osc4 osc4 (bank4, kSR, kFreq);
    osc4.setMorphPosition (0.f);

    float maxDiff = 0.f;
    for (int i = 0; i < kBlock; ++i)
    {
        maxDiff = std::max (maxDiff, std::abs (osc1.renderSample() - osc4.renderSample()));
    }
    EXPECT_LT (maxDiff, 0.02f);
}

TEST (WavetableOscillator, MorphPositionModulationNoNaN)
{
    Bank4 bank;
    bank[0].fillSine();
    bank[1].fillSaw();
    bank[2].fillTriangle();
    bank[3].fillSine();

    Osc4 osc (bank, kSR, kFreq);
    bool foundNaN = false;

    for (int i = 0; i < kBlock; ++i)
    {
        /* Sweep morph across the full range. */
        const float t = static_cast<float> (i) / static_cast<float> (kBlock);
        osc.morphPosition.addModulation (t);
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
        osc.morphPosition.clearModulation();
    }
    EXPECT_FALSE (foundNaN);
}

TEST (WavetableOscillator, MorphBetweenSineAndSilenceReducesPeak)
{
    /* bank[0] = sine, bank[1] = zero.  At morph 0.5 peak should be ~0.5. */
    Bank4 bank;
    bank[0].fillSine();
    /* bank[1..3] stay zero-filled by default. */

    Osc4 osc (bank, kSR, kFreq);
    osc.setMorphPosition (0.5f);

    const auto buf = renderN (osc, kPeriod * 4);
    float peak = 0.f;
    for (const float s : buf) { peak = std::max (peak, std::abs (s)); }

    EXPECT_GT (peak, 0.3f);
    EXPECT_LT (peak, 0.7f);
}

/*******************************************************************************
 * WavetableOscillator — phase modulation (PM)
 ******************************************************************************/

TEST (WavetableOscillator, PhaseModDepthZeroIsIdentityToNoPM)
{
    Bank1 bankA, bankB;
    bankA[0].fillSine();
    bankB[0].fillSine();

    Osc1 oscNoPM (bankA, kSR, kFreq);
    Osc1 oscPM   (bankB, kSR, kFreq);
    oscPM.setPhaseModDepth (0.f);

    for (int i = 0; i < kBlock; ++i)
    {
        EXPECT_NEAR (oscNoPM.renderSample(), oscPM.renderSample(), 1e-5f)
            << "at sample " << i;
    }
}

TEST (WavetableOscillator, PhaseModDepthNonZeroChangesOutput)
{
    Bank1 bankA, bankB;
    bankA[0].fillSine();
    bankB[0].fillSine();

    Osc1 oscNoPM (bankA, kSR, kFreq);
    Osc1 oscPM   (bankB, kSR, kFreq);
    oscPM.setPhaseModDepth (0.25f);

    /* Most samples should differ due to the quarter-cycle phase offset. */
    int diffCount = 0;
    for (int i = 0; i < kPeriod; ++i)
    {
        if (std::abs (oscNoPM.renderSample() - oscPM.renderSample()) > 0.05f)
        {
            ++diffCount;
        }
    }
    EXPECT_GT (diffCount, kPeriod / 4);
}

TEST (WavetableOscillator, PhaseModNoNaN)
{
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);
    osc.setPhaseModDepth (0.5f);
    bool foundNaN = false;
    for (int i = 0; i < kPeriod * 4; ++i)
    {
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * WavetableOscillator — interpolation mode switch
 ******************************************************************************/

TEST (WavetableOscillator, HermiteAndLinearCloseForSineAtLargeTable)
{
    Bank1 bankL, bankH;
    bankL[0].fillSine();
    bankH[0].fillSine();

    Osc1 oscLinear  (bankL, kSR, kFreq);
    Osc1 oscHermite (bankH, kSR, kFreq);
    oscHermite.setInterpolationMode (IMode::Hermite);

    float maxDiff = 0.f;
    for (int i = 0; i < kPeriod * 4; ++i)
    {
        maxDiff = std::max (maxDiff,
                            std::abs (oscLinear.renderSample() - oscHermite.renderSample()));
    }
    EXPECT_LT (maxDiff, 1e-3f);
}

TEST (WavetableOscillator, HermiteModeNoNaN)
{
    Bank1 bank;
    bank[0].fillSaw();
    Osc1 osc (bank, kSR, kFreq);
    osc.setInterpolationMode (IMode::Hermite);
    bool foundNaN = false;
    for (int i = 0; i < kPeriod * 4; ++i)
    {
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
    }
    EXPECT_FALSE (foundNaN);
}

/*******************************************************************************
 * WavetableOscillator — bank hot-swap
 ******************************************************************************/

TEST (WavetableOscillator, BankSwapNoNaN)
{
    Bank1 bankA, bankB;
    bankA[0].fillSine();
    bankB[0].fillSaw();

    Osc1 osc (bankA, kSR, kFreq);
    for (int i = 0; i < kPeriod; ++i) { osc.renderSample(); }

    osc.setBank (bankB);

    bool foundNaN = false;
    for (int i = 0; i < kPeriod; ++i)
    {
        if (std::isnan (osc.renderSample())) { foundNaN = true; }
    }
    EXPECT_FALSE (foundNaN);
}

TEST (WavetableOscillator, BankSwapChangesOutput)
{
    Bank1 bankA, bankB;
    bankA[0].fillSine();
    bankB[0].fillSaw();

    Osc1 osc (bankA, kSR, kFreq);

    std::vector<float> sineBuf (static_cast<std::size_t> (kPeriod));
    osc.renderBlock (sineBuf.data(), kPeriod);
    osc.resetPhase();

    osc.setBank (bankB);

    std::vector<float> sawBuf (static_cast<std::size_t> (kPeriod));
    osc.renderBlock (sawBuf.data(), kPeriod);

    int diffCount = 0;
    for (int i = 0; i < kPeriod; ++i)
    {
        if (std::abs (sineBuf[static_cast<std::size_t> (i)]
                    - sawBuf [static_cast<std::size_t> (i)]) > 0.05f)
        {
            ++diffCount;
        }
    }
    EXPECT_GT (diffCount, kPeriod / 2);
}

/*******************************************************************************
 * WavetableOscillator — spectral content
 ******************************************************************************/

TEST (WavetableOscillator, SineHasFundamentalEnergyAndLowHarmonics)
{
    /* A sine table should have strong energy at kFreq and negligible energy
     * at 2*kFreq. */
    Bank1 bank;
    bank[0].fillSine();
    Osc1 osc (bank, kSR, kFreq);

    const int n = static_cast<int> (kSR);
    const auto buf = renderN (osc, n);

    const float fundamental = binEnergy (buf, kFreq,       kSR);
    const float harmonic2   = binEnergy (buf, kFreq * 2.f, kSR);

    EXPECT_GT (fundamental, 0.4f);   /* strong fundamental */
    EXPECT_LT (harmonic2,   0.05f);  /* negligible second harmonic */
}

TEST (WavetableOscillator, FrequencyChangeShiftsFundamental)
{
    const float kFreq2 = 880.f;
    Bank1 bank;
    bank[0].fillSine();

    const int n = static_cast<int> (kSR);

    Osc1 osc440 (bank, kSR, kFreq);
    const auto buf440 = renderN (osc440, n);

    Osc1 osc880 (bank, kSR, kFreq2);
    const auto buf880 = renderN (osc880, n);

    EXPECT_GT (binEnergy (buf440, 440.f, kSR), binEnergy (buf440, 880.f, kSR));
    EXPECT_GT (binEnergy (buf880, 880.f, kSR), binEnergy (buf880, 440.f, kSR));
}