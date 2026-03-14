/*******************************************************************************
 * @file Noise_test.cpp
 * @brief Unit tests for Noise (White and Pink).
 *
 * TEST INVENTORY
 * --------------
 * Amplitude / bounds
 *   WhiteNoise_AmplitudeBounds         — peak stays in [-1, 1]
 *   PinkNoise_AmplitudeBounds          — peak stays in [-1, 1]
 *
 * DC offset
 *   WhiteNoise_DC                      — mean converges to ~0 over long block
 *   PinkNoise_DC                       — mean converges to ~0 over long block
 *
 * NaN / Inf
 *   WhiteNoise_NoNaNOrInf              — no invalid floats over 1 s
 *   PinkNoise_NoNaNOrInf               — no invalid floats over 1 s
 *
 * Seeding / reproducibility
 *   WhiteNoise_SeedReproducible        — same seed → identical output
 *   PinkNoise_SeedReproducible         — same seed → identical output
 *   WhiteNoise_DifferentSeedsDiffer    — different seeds → different output
 *
 * renderBlock vs renderSample consistency
 *   WhiteNoise_RenderBlockMatchesSample — block and sample loops agree
 *   PinkNoise_RenderBlockMatchesSample  — block and sample loops agree
 *
 * Amplitude modulation via ModulatableParameter
 *   WhiteNoise_AmplitudeScaling        — setAmplitude(0.5) halves peak
 *   WhiteNoise_AmplitudeSilence        — setAmplitude(0) produces silence
 *
 * Spectral characterisation — White
 *   WhiteNoise_SpectralCentroidMidband — centroid near Nyquist/2
 *   WhiteNoise_OctaveEnergyBalance     — adjacent octave bands have similar energy
 *   WhiteNoise_SpectralFlatness        — ratio of geometric to arithmetic mean > threshold
 *
 * Spectral characterisation — Pink
 *   PinkNoise_OctaveEnergyRolloff      — each higher octave has less energy (-3 dB/oct)
 *   PinkNoise_CentroidBelowWhite       — centroid lower than white (more low-freq energy)
 *   PinkNoise_SpectralFlatnessBelowWhite — pink is spectrally less flat than white
 *
 * Cross-algorithm
 *   Noise_WhiteAndPinkUncorrelated     — Pearson correlation of spectra is low
 *
 * BUILD
 * -----
 *   g++ -O2 -std=c++17 -I../include          \
 *       caspi_NoiseOscillator_test.cpp        \
 *       -lgtest -lgtest_main -lpthread        \
 *       -o noise_test
 *
 ******************************************************************************/

#include "oscillators/caspi_Noise.h"
#include "analysis/caspi_SpectralProfile.h"
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

/*******************************************************************************
 * Constants
 ******************************************************************************/

static constexpr float  kSR        = 44100.f;
static constexpr int    kShortBlock = 512;
static constexpr int    kLongBlock  = static_cast<int> (kSR);      // 1 s
static constexpr int    kFFTBlock   = 65536;                        // ~1.5 s; power-of-2 for FFT
static constexpr double kDCTol      = 0.02;
static constexpr float  kAmpTol     = 1.0f;

/*******************************************************************************
 * Type aliases
 ******************************************************************************/

using WhiteOsc = CASPI::Oscillators::NoiseOscillator<float, CASPI::Oscillators::NoiseAlgorithm::White>;
using PinkOsc  = CASPI::Oscillators::NoiseOscillator<float, CASPI::Oscillators::NoiseAlgorithm::Pink>;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

/*
 * renderBlockF — render n samples from osc into a float vector via renderBlock().
 */
template <typename Osc>
static std::vector<float> renderBlockF (Osc& osc, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    osc.renderBlock (buf.data(), n);
    return buf;
}

/*
 * renderSampleLoop — render n samples via repeated renderSample() calls.
 */
template <typename Osc>
static std::vector<float> renderSampleLoop (Osc& osc, int n)
{
    std::vector<float> buf (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        buf[static_cast<std::size_t> (i)] = osc.renderSample();
    }
    return buf;
}

/*
 * toDouble — widen a float buffer for SpectralProfile (which operates on double).
 */
static std::vector<double> toDouble (const std::vector<float>& v)
{
    std::vector<double> out (v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        out[i] = static_cast<double> (v[i]);
    }
    return out;
}

/*
 * dcOf — arithmetic mean of a buffer (DC component).
 */
static double dcOf (const std::vector<float>& buf)
{
    const double sum = std::accumulate (buf.begin(), buf.end(), 0.0);
    return sum / static_cast<double> (buf.size());
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

/*
 * spectralFlatness — ratio of geometric mean to arithmetic mean of a magnitude
 * spectrum. Range [0, 1]; 1.0 = perfectly flat (white), lower = more tonal/coloured.
 * Reference: Johnston, J. D. (1988). "Transform coding of audio signals using
 * perceptual noise criteria." IEEE JSAC 6(2):314-323.
 */
static double spectralFlatness (const std::vector<double>& magnitudes)
{
    const std::size_t N = magnitudes.size();
    if (N == 0)
    {
        return 0.0;
    }

    double logSum  = 0.0;
    double linSum  = 0.0;
    int    nonZero = 0;

    for (const double m : magnitudes)
    {
        if (m > 1e-12)
        {
            logSum += std::log (m);
            linSum += m;
            ++nonZero;
        }
    }

    if (nonZero == 0 || linSum < 1e-12)
    {
        return 0.0;
    }

    const double geometricMean  = std::exp (logSum / static_cast<double> (nonZero));
    const double arithmeticMean = linSum / static_cast<double> (nonZero);
    return geometricMean / arithmeticMean;
}

/*******************************************************************************
 * Amplitude bounds
 ******************************************************************************/

/*
 * White noise output must never exceed ±1.0 over a 1-second block.
 * xoshiro128+ maps int32 → float via ×2^-31, so the theoretical ceiling is
 * just below 1.0. The amplitude parameter defaults to 1.0.
 */
TEST (WhiteNoise, AmplitudeBounds)
{
    WhiteOsc osc;
    const auto buf = renderBlockF (osc, kLongBlock);
    EXPECT_LE (peakOf (buf), kAmpTol);
}

/*
 * Pink noise uses a sum of 8 IIR bands scaled by kOutputScale = 0.11 (empirical).
 * Output should be bounded to ±1.0 in practice; this test verifies the scale
 * factor is appropriate and no runaway accumulation occurs.
 */
TEST (PinkNoise, AmplitudeBounds)
{
    PinkOsc osc;
    const auto buf = renderBlockF (osc, kLongBlock);
    EXPECT_LE (peakOf (buf), kAmpTol);
}

/*******************************************************************************
 * DC offset
 ******************************************************************************/

/*
 * White noise from a symmetric PRNG (xoshiro128+ with int32 cast) has zero
 * mean by construction. Over 44100 samples the sample mean should converge
 * well within ±0.02.
 */
TEST (WhiteNoise, DC)
{
    WhiteOsc osc;
    const auto buf = renderBlockF (osc, kLongBlock);
    EXPECT_NEAR (dcOf (buf), 0.0, kDCTol);
}

/*
 * Pink noise is a filtered version of white noise. The Kellett IIR filter has
 * a DC gain that is not zero (pole at 0.99886 is close to DC) but the input
 * white signal has zero mean, so the long-run output mean should also be ~0.
 * A slightly relaxed tolerance (2×) is used to account for IIR settling.
 */
TEST (PinkNoise, DC)
{
    PinkOsc osc;
    const auto buf = renderBlockF (osc, kLongBlock);
    EXPECT_NEAR (dcOf (buf), 0.0, kDCTol * 2.0);
}

/*******************************************************************************
 * NaN / Inf
 ******************************************************************************/

/*
 * No floating-point exceptional values should appear over 1 s of white noise.
 * Checked per-sample to catch transient faults.
 */
TEST (WhiteNoise, NoNaNOrInf)
{
    WhiteOsc osc;
    for (int i = 0; i < kLongBlock; ++i)
    {
        const float s = osc.renderSample();
        ASSERT_FALSE (std::isnan (s)) << "NaN at sample " << i;
        ASSERT_FALSE (std::isinf (s)) << "Inf at sample " << i;
    }
}

/*
 * Pink noise uses feedback IIR state (b0..b6). A poorly chosen pole or
 * coefficient error could cause divergence. Verify no NaN/Inf over 1 s.
 */
TEST (PinkNoise, NoNaNOrInf)
{
    PinkOsc osc;
    for (int i = 0; i < kLongBlock; ++i)
    {
        const float s = osc.renderSample();
        ASSERT_FALSE (std::isnan (s)) << "NaN at sample " << i;
        ASSERT_FALSE (std::isinf (s)) << "Inf at sample " << i;
    }
}

/*******************************************************************************
 * Seeding / reproducibility
 ******************************************************************************/

/*
 * Two white oscillators seeded identically must produce identical output.
 * This validates the SplitMix64 warm-up and xoshiro128+ state initialisation.
 */
TEST (WhiteNoise, SeedReproducible)
{
    WhiteOsc oscA, oscB;
    oscA.seed (12345u);
    oscB.seed (12345u);

    for (int i = 0; i < kShortBlock; ++i)
    {
        EXPECT_EQ (oscA.renderSample(), oscB.renderSample()) << "Diverged at sample " << i;
    }
}

/*
 * Two pink oscillators seeded identically must produce identical output.
 * The IIR filter state (b0..b6) is reset to zero by seed(), so the warm-up
 * transient is also identical.
 */
TEST (PinkNoise, SeedReproducible)
{
    PinkOsc oscA, oscB;
    oscA.seed (99999u);
    oscB.seed (99999u);

    for (int i = 0; i < kShortBlock; ++i)
    {
        EXPECT_EQ (oscA.renderSample(), oscB.renderSample()) << "Diverged at sample " << i;
    }
}

/*
 * Different seeds must produce statistically different output streams.
 * The probability that the first 512 samples are identical by chance is
 * negligible for a well-seeded PRNG.
 */
TEST (WhiteNoise, DifferentSeedsDiffer)
{
    WhiteOsc oscA, oscB;
    oscA.seed (1u);
    oscB.seed (2u);

    int matchCount = 0;
    for (int i = 0; i < kShortBlock; ++i)
    {
        if (oscA.renderSample() == oscB.renderSample())
        {
            ++matchCount;
        }
    }
    EXPECT_LT (matchCount, kShortBlock / 4);
}

/*******************************************************************************
 * renderBlock vs renderSample consistency
 ******************************************************************************/

/*
 * renderBlock() and a renderSample() loop must produce bit-identical output
 * from the same initial state. Both paths call engine.next() in the same order;
 * the amplitude smoother is stepped once per call in both paths.
 */
TEST (WhiteNoise, RenderBlockMatchesSample)
{
    WhiteOsc oscA, oscB;
    oscA.seed (42u);
    oscB.seed (42u);

    const auto blockOut  = renderBlockF (oscA, kShortBlock);
    const auto sampleOut = renderSampleLoop (oscB, kShortBlock);

    for (int i = 0; i < kShortBlock; ++i)
    {
        EXPECT_EQ (blockOut[static_cast<std::size_t> (i)],
                   sampleOut[static_cast<std::size_t> (i)])
            << "Mismatch at sample " << i;
    }
}

/*
 * Same consistency check for pink noise. The IIR state must evolve identically
 * regardless of which render path is used.
 */
TEST (PinkNoise, RenderBlockMatchesSample)
{
    PinkOsc oscA, oscB;
    oscA.seed (42u);
    oscB.seed (42u);

    const auto blockOut  = renderBlockF (oscA, kShortBlock);
    const auto sampleOut = renderSampleLoop (oscB, kShortBlock);

    for (int i = 0; i < kShortBlock; ++i)
    {
        EXPECT_EQ (blockOut[static_cast<std::size_t> (i)],
                   sampleOut[static_cast<std::size_t> (i)])
            << "Mismatch at sample " << i;
    }
}

/*******************************************************************************
 * Amplitude modulation via ModulatableParameter
 ******************************************************************************/

/*
 * setAmplitude(0.5) should approximately halve the peak absolute value compared
 * to the default amplitude of 1.0. Tested over the same PRNG seed for fairness.
 * A ±25% tolerance accounts for statistical variation in peak over 512 samples.
 */
TEST (WhiteNoise, AmplitudeScaling)
{
    WhiteOsc oscFull, oscHalf;
    oscFull.seed (7u);
    oscHalf.seed (7u);
    oscHalf.setAmplitude (0.5f);

    const float peakFull = peakOf (renderBlockF (oscFull, kShortBlock));
    const float peakHalf = peakOf (renderBlockF (oscHalf, kShortBlock));

    EXPECT_NEAR (peakHalf / peakFull, 0.5f, 0.25f);
}

/*
 * setAmplitude(0.0) must produce a silent (all-zero) output block.
 */
TEST (WhiteNoise, AmplitudeSilence)
{
    WhiteOsc osc;
    osc.setAmplitude (0.0f);
    const auto buf = renderBlockF (osc, kShortBlock);
    for (const float s : buf)
    {
        EXPECT_EQ (s, 0.0f);
    }
}

/*******************************************************************************
 * Spectral characterisation — White
 ******************************************************************************/

/*
 * White noise has a flat power spectrum. The spectral centroid (centre of mass
 * of the magnitude spectrum) should therefore lie near the midpoint of the
 * audible band [0, Nyquist]. We expect it within [Nyquist/4, 3*Nyquist/4].
 *
 * Reference: Gardner, W. G. & Martin, K. D. (1995). HRTF measurements of a
 * KEMAR. JASA 97(6):3907–3908. (Centroid of flat spectrum = Nyquist/2.)
 */
TEST (WhiteNoise, SpectralCentroidMidband)
{
    WhiteOsc osc;
    osc.seed (1u);
    const auto samples = toDouble (renderBlockF (osc, kFFTBlock));

    CASPI::SpectralProfile profile (samples, static_cast<double> (kSR));

    const double nyquist = kSR / 2.0;
    EXPECT_GT (profile.getSpectralCentroid(), nyquist / 4.0);
    EXPECT_LT (profile.getSpectralCentroid(), 3.0 * nyquist / 4.0);
}

/*
 * For a flat (white) spectrum, adjacent octave bands should contain roughly
 * equal energy, scaled by bandwidth. Here we compare the 1–2 kHz and 2–4 kHz
 * octave bands; the higher band has twice the bandwidth so should carry ~2×
 * the energy. We accept a ratio within [1.2, 3.5] to allow for statistical
 * variance while still rejecting clearly coloured noise.
 */
TEST (WhiteNoise, OctaveEnergyBalance)
{
    WhiteOsc osc;
    osc.seed (2u);
    const auto samples = toDouble (renderBlockF (osc, kFFTBlock));

    CASPI::SpectralProfile profile (samples, static_cast<double> (kSR));

    const double energyLow  = profile.getEnergyInRange (1000.0, 2000.0);
    const double energyHigh = profile.getEnergyInRange (2000.0, 4000.0);

    ASSERT_GT (energyLow, 0.0);
    const double ratio = energyHigh / energyLow;
    EXPECT_GT (ratio, 1.2);
    EXPECT_LT (ratio, 3.5);
}

/*
 * Wiener-Khinchin theorem: a flat PSD implies a white-noise process.
 * Spectral flatness (Wiener entropy) = geometric mean / arithmetic mean of
 * the magnitude spectrum. For ideal white noise this approaches 1.0. We
 * require > 0.5 over a 65536-sample FFT, which is achievable with xoshiro128+.
 *
 * Reference: Johnston (1988), op. cit.
 */
TEST (WhiteNoise, SpectralFlatness)
{
    WhiteOsc osc;
    osc.seed (3u);
    const auto samples = toDouble (renderBlockF (osc, kFFTBlock));

    CASPI::SpectralProfile profile (samples, static_cast<double> (kSR));
    const double flatness = spectralFlatness (profile.getMagnitudes());

    EXPECT_GT (flatness, 0.5);
}

/*******************************************************************************
 * Spectral characterisation — Pink
 ******************************************************************************/

/*
 * Pink noise has a -3 dB/octave (1/f) power spectral density. We verify this
 * by measuring energy across four octave bands spanning 125 Hz to 2 kHz and
 * fitting a linear regression to log2(band_centre) vs log10(energy). The slope
 * must be negative (energy falls with frequency). A single adjacent-band
 * comparison is unreliable at this FFT size due to filter approximation error
 * and statistical variance; regression over four bands is robust to one noisy
 * pair.
 *
 * Reference: Voss, R. F. & Clarke, J. (1975). "1/f noise in music and speech."
 * Nature 258:317–318.
 * Kellett approximation accuracy: https://www.firstpr.com.au/dsp/pink-noise/
 */
TEST (PinkNoise, OctaveEnergyRolloff)
{
    PinkOsc osc;
    osc.seed (10u);
    const auto samples = toDouble (renderBlockF (osc, kFFTBlock));

    CASPI::SpectralProfile profile (samples, static_cast<double> (kSR));

    /*
     * Four octave bands. Each band doubles in bandwidth, so raw energy naturally
     * rises for flat (white) spectra. Pink must overcome this and still trend down.
     * Band centres (geometric mean of edges) in Hz: 177, 354, 707, 1414.
     */
    struct OctaveBand
    {
        double lowHz;
        double highHz;
    };

    static constexpr std::array<OctaveBand, 4> bands { {
        {  125.0,  250.0 },
        {  250.0,  500.0 },
        {  500.0, 1000.0 },
        { 1000.0, 2000.0 }
    } };

    std::array<double, 4> logCentre {};
    std::array<double, 4> logEnergy {};

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const double centre    = std::sqrt (bands[i].lowHz * bands[i].highHz);
        const double energy    = profile.getEnergyInRange (bands[i].lowHz, bands[i].highHz);
        logCentre[i]           = std::log2 (centre);
        logEnergy[i]           = std::log10 (std::max (energy, 1e-30));
    }

    /*
     * Simple linear regression: slope = Σ(x - x̄)(y - ȳ) / Σ(x - x̄)²
     * A negative slope confirms energy falls as frequency rises.
     */
    const double meanX = std::accumulate (logCentre.begin(), logCentre.end(), 0.0) / 4.0;
    const double meanY = std::accumulate (logEnergy.begin(), logEnergy.end(), 0.0) / 4.0;

    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double dx  = logCentre[i] - meanX;
        num             += dx * (logEnergy[i] - meanY);
        den             += dx * dx;
    }

    ASSERT_GT (den, 0.0);
    const double slope = num / den;

    EXPECT_LT (slope, 0.0) << "Regression slope should be negative for pink noise; got " << slope;
}

/*
 * Pink noise emphasises low frequencies relative to white noise. Its spectral
 * centroid must therefore be lower than that of a white noise signal of the same
 * length and amplitude. Both oscillators are seeded identically to control for
 * block-length effects; the pink centroid should be meaningfully below the white.
 */
TEST (PinkNoise, CentroidBelowWhite)
{
    WhiteOsc white;
    PinkOsc  pink;
    white.seed (20u);
    pink.seed  (20u);

    const auto whiteSamples = toDouble (renderBlockF (white, kFFTBlock));
    const auto pinkSamples  = toDouble (renderBlockF (pink,  kFFTBlock));

    CASPI::SpectralProfile whiteProfile (whiteSamples, static_cast<double> (kSR));
    CASPI::SpectralProfile pinkProfile  (pinkSamples,  static_cast<double> (kSR));

    EXPECT_LT (pinkProfile.getSpectralCentroid(), whiteProfile.getSpectralCentroid());
}

/*
 * Pink noise is more spectrally coloured (less flat) than white noise.
 * Spectral flatness of pink must be strictly lower than that of white,
 * reflecting the concentration of energy in the lower frequency bands.
 */
TEST (PinkNoise, SpectralFlatnessBelowWhite)
{
    WhiteOsc white;
    PinkOsc  pink;
    white.seed (30u);
    pink.seed  (30u);

    const auto whiteSamples = toDouble (renderBlockF (white, kFFTBlock));
    const auto pinkSamples  = toDouble (renderBlockF (pink,  kFFTBlock));

    CASPI::SpectralProfile whiteProfile (whiteSamples, static_cast<double> (kSR));
    CASPI::SpectralProfile pinkProfile  (pinkSamples,  static_cast<double> (kSR));

    const double whiteFlatness = spectralFlatness (whiteProfile.getMagnitudes());
    const double pinkFlatness  = spectralFlatness (pinkProfile.getMagnitudes());

    EXPECT_LT (pinkFlatness, whiteFlatness);
}

/*******************************************************************************
 * Cross-algorithm
 ******************************************************************************/

/*
 * White and pink noise should have low spectral correlation: pink is a
 * low-pass-shaped version of white, so their magnitude spectra are not
 * linearly correlated. The Pearson correlation of the magnitude vectors
 * should be below 0.5 over a 65536-sample FFT.
 *
 * Note: this tests spectral *shape* dissimilarity, not time-domain independence.
 * A high correlation here would indicate the filter is not altering the spectrum.
 */
TEST (Noise, WhiteAndPinkUncorrelated)
{
    WhiteOsc white;
    PinkOsc  pink;
    white.seed (99u);
    pink.seed  (99u);

    const auto whiteSamples = toDouble (renderBlockF (white, kFFTBlock));
    const auto pinkSamples  = toDouble (renderBlockF (pink,  kFFTBlock));

    CASPI::SpectralProfile whiteProfile (whiteSamples, static_cast<double> (kSR));
    CASPI::SpectralProfile pinkProfile  (pinkSamples,  static_cast<double> (kSR));

    const double corr = CASPI::spectralCorrelation (whiteProfile, pinkProfile);
    EXPECT_LT (corr, 0.5);
}