/*******************************************************************************
 * @file caspi_LFO_test.cpp
 * @brief Unit tests for LFO.
 *
 * TEST INVENTORY
 * --------------
 * Output bounds
 *   LFO_Bipolar_AmplitudeBounds        — all shapes stay in [-1, 1] bipolar
 *   LFO_Unipolar_AmplitudeBounds       — all shapes stay in [0, 1] unipolar
 *   LFO_AmplitudeScaling               — setAmplitude(0.5) halves peak
 *   LFO_AmplitudeSilence               — setAmplitude(0) produces silence
 *
 * DC / symmetry
 *   LFO_Sine_DC                        — sine mean ~0 over whole cycles
 *   LFO_Triangle_DC                    — triangle mean ~0 over whole cycles
 *   LFO_Saw_DC                         — saw mean ~0 over whole cycles
 *   LFO_Square_DC                      — square mean ~0 over whole cycles
 *   LFO_Unipolar_Sine_Mean             — unipolar sine mean ~0.5
 *
 * NaN / Inf
 *   LFO_NoNaNOrInf_AllShapes           — no exceptional floats over 10 cycles
 *
 * Phase reset
 *   LFO_PhaseReset_Sine                — after resetPhase(), first sample ~0
 *   LFO_PhaseReset_Saw                 — after resetPhase(), first sample ~-1
 *   LFO_PhaseReset_MidCycle            — reset mid-cycle restarts correctly
 *   LFO_ForceSync_MatchesResetPhase    — forceSync() and resetPhase() identical
 *
 * One-shot mode
 *   LFO_OneShot_HaltsAfterOneCycle     — output is 0 after phase wraps
 *   LFO_OneShot_PhaseWrappedFlag       — phaseWrapped() true exactly once
 *   LFO_OneShot_ResetRetriggers        — resetPhase() re-enables one-shot
 *   LFO_OneShot_IsHaltedFlag           — isHalted() reflects state correctly
 *
 * Frequency accuracy
 *   LFO_FrequencyAccuracy              — cycle count matches expected rate
 *   LFO_PhaseWrappedCount              — phaseWrapped() fires N times in N cycles
 *
 * Tempo sync
 *   LFO_TempoSync_RateComputation      — BPM/beats maps to correct Hz
 *   LFO_TempoSync_CyclesPerBar         — one cycle per bar at 120 BPM, 4/4
 *
 * Shape correctness
 *   LFO_Sine_PeakAtQuarterCycle        — peak near sample SR/4f
 *   LFO_Triangle_LinearSegments        — monotone rise and fall halves
 *   LFO_Saw_MonotonicallyRising        — strictly increasing within a cycle
 *   LFO_ReverseSaw_MonotonicallyFalling — strictly decreasing within a cycle
 *   LFO_Square_OnlyTwoValues           — only -1 and +1 in bipolar mode
 *
 * renderBlock vs renderSample
 *   LFO_RenderBlockMatchesSample       — identical output from both paths
 *
 * BUILD
 * -----
 *   g++ -O2 -std=c++17 -I../include    \
 *       caspi_LFO_test.cpp             \
 *       -lgtest -lgtest_main -lpthread \
 *       -o lfo_test
 *
 ******************************************************************************/

#include "oscillators/caspi_LFO.h"
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

/*******************************************************************************
 * Constants
 ******************************************************************************/

static constexpr float sample_rate      = 44100.f;
static constexpr float lfo_frequency    = 2.0f;   // 2 Hz — period = 22050 samples
static constexpr int   period_s  = static_cast<int> (sample_rate / lfo_frequency);
static constexpr int   block_size   = 512;
static constexpr float tolerance  = 1e-4f;  // tolerance on peak comparisons
static constexpr float dc_tolerance   = 0.01f;

/*******************************************************************************
 * Type alias
 ******************************************************************************/

using LFO   = CASPI::Oscillators::LFO<float>;
using Shape = CASPI::Oscillators::LfoShape;
using Mode  = CASPI::Oscillators::LfoOutputMode;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

/*
 * renderN — render n samples from osc via renderBlock() into a float vector.
 */
static std::vector<float> renderN (LFO& lfo, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    lfo.renderBlock (buf.data(), n);
    return buf;
}

/*
 * renderSampleLoop — render n samples via repeated renderSample() calls.
 */
static std::vector<float> renderSampleLoop (LFO& lfo, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        buf[static_cast<std::size_t> (i)] = lfo.renderSample();
    }
    return buf;
}

/*
 * dcOf — arithmetic mean of a buffer.
 */
static float dcOf (const std::vector<float>& buf)
{
    const double sum = std::accumulate (buf.begin(), buf.end(), 0.0);
    return static_cast<float> (sum / static_cast<double> (buf.size()));
}

/*
 * peakOf — maximum absolute value in a buffer.
 */
static float peakOf (const std::vector<float>& buf)
{
    float peak = 0.f;
    for (const float s : buf)
    {
        peak = std::max (peak, std::abs (s));
    }
    return peak;
}

/*******************************************************************************
 * Output bounds
 ******************************************************************************/

/*
 * All shapes in bipolar mode must stay strictly within [-1 - epsilon, 1 + epsilon]
 * over 10 complete cycles. Amplitude default is 1.0.
 */
TEST (LFO_Bipolar, AmplitudeBounds)
{
    const Shape shapes[] = { Shape::Sine, Shape::Triangle, Shape::Saw,
                              Shape::ReverseSaw, Shape::Square };

    for (const Shape s : shapes)
    {
        LFO lfo (sample_rate, lfo_frequency, s, Mode::Bipolar);
        const auto buf = renderN (lfo, period_s * 10);

        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            EXPECT_LE (buf[i],  1.0f + tolerance) << "Shape " << static_cast<int> (s) << " exceeded +1 at sample " << i;
            EXPECT_GE (buf[i], -1.0f - tolerance) << "Shape " << static_cast<int> (s) << " exceeded -1 at sample " << i;
        }
    }
}

/*
 * All shapes in unipolar mode must stay within [0 - epsilon, 1 + epsilon].
 * Unipolar conversion is: out = bipolar * 0.5 + 0.5.
 */
TEST (LFO_Unipolar, AmplitudeBounds)
{
    const Shape shapes[] = { Shape::Sine, Shape::Triangle, Shape::Saw,
                              Shape::ReverseSaw, Shape::Square };

    for (const Shape s : shapes)
    {
        LFO lfo (sample_rate, lfo_frequency,s, Mode::Unipolar);
        const auto buf = renderN (lfo, period_s * 10);

        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            EXPECT_LE (buf[i], 1.0f + tolerance) << "Unipolar shape " << static_cast<int> (s) << " exceeded 1 at sample " << i;
            EXPECT_GE (buf[i], 0.0f - tolerance) << "Unipolar shape " << static_cast<int> (s) << " went below 0 at sample " << i;
        }
    }
}

/*
 * setAmplitude(0.5) should halve the peak absolute value relative to the
 * default amplitude of 1.0. Tested on sine over one full cycle.
 * Tolerance of ±20% for statistical variation at a coarse resolution.
 */
TEST (LFO_Bipolar, AmplitudeScaling)
{
    LFO lfoFull (sample_rate, lfo_frequency, Shape::Sine);
    LFO lfoHalf (sample_rate, lfo_frequency, Shape::Sine);
    lfoHalf.setAmplitude (0.5f);

    const float peakFull = peakOf (renderN (lfoFull, period_s));
    const float peakHalf = peakOf (renderN (lfoHalf, period_s));

    EXPECT_NEAR (peakHalf / peakFull, 0.5f, 0.05f);
}

/*
 * setAmplitude(0.0) must produce silence regardless of shape or output mode.
 */
TEST (LFO_Bipolar, AmplitudeSilence)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setAmplitude (0.0f);
    const auto buf = renderN (lfo, period_s);
    for (const float s : buf)
    {
        EXPECT_EQ (s, 0.0f);
    }
}

/*******************************************************************************
 * DC / symmetry
 ******************************************************************************/

/*
 * A bipolar sine LFO has zero mean over any integer number of complete cycles.
 * Tested over 4 cycles to give the accumulator time to settle any initial
 * transient from resetPhase() at phase=0.
 */
TEST (LFO_DC, Sine)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    EXPECT_NEAR (dcOf (renderN (lfo, period_s * 4)), 0.f, dc_tolerance);
}

/*
 * Bipolar triangle is antisymmetric; its mean over complete cycles is 0.
 */
TEST (LFO_DC, Triangle)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Triangle);
    EXPECT_NEAR (dcOf (renderN (lfo, period_s * 4)), 0.f, dc_tolerance);
}

/*
 * Rising sawtooth is a linear ramp from -1 to +1; its mean over a full cycle
 * is 0. dc_tolerance slightly relaxed for the discrete approximation of a ramp.
 */
TEST (LFO_DC, Saw)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Saw);
    EXPECT_NEAR (dcOf (renderN (lfo, period_s * 4)), 0.f, dc_tolerance * 2.f);
}

/*
 * Square wave at 50% duty cycle has equal time at +1 and -1; mean is 0.
 */
TEST (LFO_DC, Square)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Square);
    EXPECT_NEAR (dcOf (renderN (lfo, period_s * 4)), 0.f, dc_tolerance);
}

/*
 * Unipolar sine mean should converge to 0.5 over complete cycles.
 * Unipolar = bipolar * 0.5 + 0.5; mean(bipolar) = 0 → mean(unipolar) = 0.5.
 */
TEST (LFO_DC, UnipolarSineMean)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine, Mode::Unipolar);
    EXPECT_NEAR (dcOf (renderN (lfo, period_s * 4)), 0.5f, dc_tolerance);
}

/*******************************************************************************
 * NaN / Inf
 ******************************************************************************/

/*
 * No shape should produce NaN or Inf over 10 cycles. Checked per-sample to
 * catch transient faults at phase boundaries (e.g. the triangle inflection
 * point, the square discontinuity).
 */
TEST (LFO, NoNaNOrInf_AllShapes)
{
    const Shape shapes[] = { Shape::Sine, Shape::Triangle, Shape::Saw,
                              Shape::ReverseSaw, Shape::Square };

    for (const Shape s : shapes)
    {
        LFO lfo (sample_rate, lfo_frequency, s);
        for (int i = 0; i < period_s * 10; ++i)
        {
            const float v = lfo.renderSample();
            ASSERT_FALSE (std::isnan (v)) << "NaN: shape " << static_cast<int> (s) << " at sample " << i;
            ASSERT_FALSE (std::isinf (v)) << "Inf: shape " << static_cast<int> (s) << " at sample " << i;
        }
    }
}

/*******************************************************************************
 * Phase reset
 ******************************************************************************/

/*
 * After resetPhase() the LFO phase is 0. For a sine LFO, sin(2π×0) = 0,
 * so the first rendered sample must be ~0.
 */
TEST (LFO_PhaseReset, Sine)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    /* Advance phase to mid-cycle, then reset. */
    for (int i = 0; i < period_s / 2; ++i) { lfo.renderSample(); }
    lfo.resetPhase();
    EXPECT_NEAR (lfo.renderSample(), 0.f, 1e-4f);
}

/*
 * After resetPhase() the saw phase is 0. Saw output at phase=0 is:
 *   2*0 - 1 = -1.
 * The first rendered sample uses pBefore = 0, so output = -1.
 */
TEST (LFO_PhaseReset, Saw)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Saw);
    for (int i = 0; i < period_s / 2; ++i) { lfo.renderSample(); }
    lfo.resetPhase();
    EXPECT_NEAR (lfo.renderSample(), -1.f, 1e-3f);
}

/*
 * Resetting phase mid-cycle causes the LFO to restart its waveform from
 * phase 0. The second cycle (post-reset) must be identical to the first
 * cycle rendered from a fresh oscillator.
 */
TEST (LFO_PhaseReset, MidCycle)
{
    LFO lfoRef  (sample_rate, lfo_frequency, Shape::Sine);
    LFO lfoTest (sample_rate, lfo_frequency, Shape::Sine);

    /* Advance lfoTest to mid-cycle and reset. */
    for (int i = 0; i < period_s / 2; ++i) { lfoTest.renderSample(); }
    lfoTest.resetPhase();

    /* Both should now produce identical output. */
    for (int i = 0; i < period_s; ++i)
    {
        EXPECT_NEAR (lfoTest.renderSample(), lfoRef.renderSample(), 1e-4f)
            << "Diverged at sample " << i << " after mid-cycle reset";
    }
}

/*
 * forceSync() must produce identical behaviour to resetPhase() — both reset
 * phase to phaseOffset (default 0) and clear the halted flag.
 */
TEST (LFO_PhaseReset, ForceSyncMatchesResetPhase)
{
    LFO lfoA (sample_rate, lfo_frequency, Shape::Triangle);
    LFO lfoB (sample_rate, lfo_frequency, Shape::Triangle);

    for (int i = 0; i < period_s / 3; ++i)
    {
        lfoA.renderSample();
        lfoB.renderSample();
    }
    lfoA.resetPhase();
    lfoB.forceSync();

    for (int i = 0; i < period_s; ++i)
    {
        EXPECT_EQ (lfoA.renderSample(), lfoB.renderSample())
            << "forceSync/resetPhase diverged at sample " << i;
    }
}

/*******************************************************************************
 * One-shot mode
 ******************************************************************************/

/*
 * In one-shot mode, the LFO completes one cycle then halts. All output after
 * the wrap point must be exactly 0. Verified over two nominal cycle lengths.
 */
TEST (LFO_OneShot, HaltsAfterOneCycle)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setOneShot (true);

    /* Render one full cycle. */
    for (int i = 0; i < period_s; ++i) { lfo.renderSample(); }

    /* Output must be 0 for the next full cycle. */
    for (int i = 0; i < period_s; ++i)
    {
        EXPECT_EQ (lfo.renderSample(), 0.0f) << "Non-zero output after one-shot halt at sample " << i;
    }
}

/*
 * phaseWrapped() must become true exactly once: on the sample where the phase
 * crosses 1.0 in one-shot mode. It must not fire again after halting.
 */
TEST (LFO_OneShot, PhaseWrappedFlag)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setOneShot (true);

    int wrapCount = 0;
    for (int i = 0; i < period_s * 3; ++i)
    {
        lfo.renderSample();
        if (lfo.phaseWrapped()) { ++wrapCount; }
    }
    EXPECT_EQ (wrapCount, 1);
}

/*
 * After a one-shot cycle completes, isHalted() must be true. After resetPhase(),
 * isHalted() must be false and the LFO must produce a new full cycle.
 */
TEST (LFO_OneShot, ResetRetriggers)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setOneShot (true);

    for (int i = 0; i < period_s + 10; ++i) { lfo.renderSample(); }
    EXPECT_TRUE (lfo.isHalted());

    lfo.resetPhase();
    EXPECT_FALSE (lfo.isHalted());

    /* First sample after re-trigger must be non-zero (sine at phase just past 0). */
    /* Render a quarter cycle — peak should appear. */
    float peak = 0.f;
    for (int i = 0; i < period_s / 4 + 1; ++i)
    {
        peak = std::max (peak, std::abs (lfo.renderSample()));
    }
    EXPECT_GT (peak, 0.5f);
}

/*
 * isHalted() must be false before the one-shot cycle completes and true after.
 */
TEST (LFO_OneShot, IsHaltedFlag)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Triangle);
    lfo.setOneShot (true);

    EXPECT_FALSE (lfo.isHalted());

    for (int i = 0; i < period_s - 5; ++i) { lfo.renderSample(); }
    EXPECT_FALSE (lfo.isHalted());

    /* Advance past the wrap point. */
    for (int i = 0; i < 10; ++i) { lfo.renderSample(); }
    EXPECT_TRUE (lfo.isHalted());
}

/*******************************************************************************
 * Frequency accuracy
 ******************************************************************************/

/*
 * The LFO at lfo_frequency Hz should complete exactly lfo_frequency cycles per second.
 * We count phaseWrapped() events over one second and expect them to equal
 * lfo_frequency (2 wraps). Tolerance of ±1 for rounding at the phase boundary.
 */
TEST (LFO_Frequency, AccuracyOverOneSecond)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Saw);
    const int n = static_cast<int> (sample_rate);

    int wrapCount = 0;
    for (int i = 0; i < n; ++i)
    {
        lfo.renderSample();
        if (lfo.phaseWrapped()) { ++wrapCount; }
    }
    EXPECT_NEAR (wrapCount, static_cast<int> (lfo_frequency), 1);
}

/*
 * phaseWrapped() must fire exactly once per cycle and not more. Over N cycles
 * the wrap count must equal N (±1 for boundary rounding).
 */
TEST (LFO_Frequency, PhaseWrappedCountOverNCycles)
{
    static const int kCycles = 5;
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);

    int wrapCount = 0;
    for (int i = 0; i < period_s * kCycles; ++i)
    {
        lfo.renderSample();
        if (lfo.phaseWrapped()) { ++wrapCount; }
    }
    EXPECT_NEAR (wrapCount, kCycles, 1);
}

/*******************************************************************************
 * Tempo sync
 ******************************************************************************/

/*
 * setTempoSync(bpm, beats) computes hz = bpm / (60 * beats).
 * At 120 BPM, 4 beats per cycle: hz = 120 / (60 * 4) = 0.5 Hz.
 * The period in samples should be SR / 0.5 = 88200.
 * We verify by counting wraps over 2 nominal periods.
 */
TEST (LFO_TempoSync, RateComputation)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setTempoSync (120.f, 4.f);  // 0.5 Hz
    lfo.resetPhase();

    const int expectedPeriod = static_cast<int> (sample_rate / 0.5f);
    int wrapCount            = 0;
    for (int i = 0; i < expectedPeriod * 2; ++i)
    {
        lfo.renderSample();
        if (lfo.phaseWrapped()) { ++wrapCount; }
    }
    EXPECT_NEAR (wrapCount, 2, 1);
}

/*
 * At 120 BPM, 4/4 time, 1 cycle per bar = 0.5 Hz.
 * The LFO should complete exactly 1 cycle per bar (2 s at 120 BPM).
 * Verified by counting wraps over exactly one bar duration in samples.
 */
TEST (LFO_TempoSync, CyclesPerBar)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);
    lfo.setTempoSync (120.f, 4.f);
    lfo.resetPhase();

    // 0.5 Hz period = 88200 nominal samples. Allow 1% overshoot.
    const int maxSamples = static_cast<int> (sample_rate * 2.f * 1.01f);
    int wrapCount = 0;
    for (int i = 0; i < maxSamples; ++i)
    {
        lfo.renderSample();
        if (lfo.phaseWrapped()) { ++wrapCount; }
    }
    EXPECT_EQ (wrapCount, 1);
}

/*******************************************************************************
 * Shape correctness
 ******************************************************************************/

/*
 * A sine LFO starting at phase=0 reaches its peak at phase=0.25 (quarter cycle).
 * The maximum sample in the first half-period should occur within ±5% of period_s/4.
 */
TEST (LFO_Shape, Sine_PeakAtQuarterCycle)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Sine);

    float maxVal   = -2.f;
    int   maxIndex = 0;
    for (int i = 0; i < period_s / 2; ++i)
    {
        const float s = lfo.renderSample();
        if (s > maxVal)
        {
            maxVal   = s;
            maxIndex = i;
        }
    }

    const int expectedPeak = period_s / 4;
    EXPECT_NEAR (maxIndex, expectedPeak, static_cast<int> (period_s * 0.05f));
}

/*
 * The triangle LFO rises from -1 to +1 in the first half-cycle and falls
 * back to -1 in the second half. Within each half, the output must be
 * monotonically non-decreasing (rising) or non-increasing (falling).
 */
TEST (LFO_Shape, Triangle_LinearSegments)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Triangle);

    std::vector<float> buf (static_cast<std::size_t> (period_s));
    for (int i = 0; i < period_s; ++i)
    {
        buf[static_cast<std::size_t> (i)] = lfo.renderSample();
    }

    // Find the actual peak index
    int inflection = 0;
    for (int i = 1; i < period_s; ++i)
    {
        if (buf[i] > buf[inflection])
        {
            inflection = i;
        }
    }

    /* First half: rising */
    for (int i = 1; i < inflection; ++i)
    {
        EXPECT_GE (buf[static_cast<std::size_t> (i)], buf[static_cast<std::size_t> (i - 1)] - 1e-4f)
            << "Triangle not rising at sample " << i;
    }

    /* Second half: falling */
    for (int i = inflection + 1; i < period_s - 3; ++i)
    {
        EXPECT_LE (buf[static_cast<std::size_t> (i)], buf[static_cast<std::size_t> (i - 1)] + 1e-4f)
            << "Triangle not falling at sample " << i;
    }
}

/*
 * The rising sawtooth must be strictly monotonically increasing within a single
 * cycle (excluding the reset discontinuity at the wrap point). Each sample must
 * be greater than or equal to the previous, up to floating-point tolerance.
 */
TEST (LFO_Shape, Saw_MonotonicallyRising)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Saw);

    float prev = lfo.renderSample();
    for (int i = 1; i < period_s - 3; ++i)
    {
        const float curr = lfo.renderSample();
        EXPECT_GE (curr, prev - 1e-5f) << "Saw decreased at sample " << i;
        prev = curr;
    }
}

/*
 * The reverse sawtooth must be strictly monotonically decreasing within a cycle
 * (excluding the wrap discontinuity).
 */
TEST (LFO_Shape, ReverseSaw_MonotonicallyFalling)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::ReverseSaw);

    float prev = lfo.renderSample();
    for (int i = 1; i < period_s - 3; ++i)
    {
        const float curr = lfo.renderSample();
        EXPECT_LE (curr, prev + 1e-5f) << "ReverseSaw increased at sample " << i;
        prev = curr;
    }
}

/*
 * The bipolar square LFO must output only -1 or +1 (no intermediate values).
 * At 50% duty cycle (phase threshold = 0.5) both values must appear within
 * one cycle.
 */
TEST (LFO_Shape, Square_OnlyTwoValues)
{
    LFO lfo (sample_rate, lfo_frequency, Shape::Square);

    bool sawNegOne = false;
    bool sawPosOne = false;

    for (int i = 0; i < period_s; ++i)
    {
        const float s = lfo.renderSample();
        EXPECT_TRUE (s == -1.f || s == 1.f) << "Unexpected square value " << s << " at sample " << i;
        if (s == -1.f) { sawNegOne = true; }
        if (s ==  1.f) { sawPosOne = true; }
    }
    EXPECT_TRUE (sawNegOne);
    EXPECT_TRUE (sawPosOne);
}

/*******************************************************************************
 * renderBlock vs renderSample
 ******************************************************************************/

/*
 * renderBlock() and a renderSample() loop must produce bit-identical output
 * from the same initial state. Both paths call computeOutput() and advance
 * phase identically; the parameter smoother is stepped once per call in both.
 */
TEST (LFO, RenderBlockMatchesSample)
{
    const Shape shapes[] = { Shape::Sine, Shape::Triangle, Shape::Saw,
                              Shape::ReverseSaw, Shape::Square };

    for (const Shape s : shapes)
    {
        LFO lfoA (sample_rate,lfo_frequency, s);
        LFO lfoB (sample_rate,lfo_frequency, s);

        const auto blockOut  = renderN          (lfoA, block_size);
        const auto sampleOut = renderSampleLoop (lfoB, block_size);

        for (int i = 0; i < block_size; ++i)
        {
            EXPECT_EQ (blockOut[static_cast<std::size_t> (i)],
                       sampleOut[static_cast<std::size_t> (i)])
                << "Shape " << static_cast<int> (s) << " mismatch at sample " << i;
        }
    }
}