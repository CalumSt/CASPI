#include "analysis/caspi_SpectralProfile.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace CASPI;

constexpr double sampleRate = 48000.0;
constexpr double tolerance = 1e-6;

// ============================================================================
// Test Signal Generators
// ============================================================================

namespace TestSignals
{
    /**
     * @brief Generate pure sine wave
     */
    std::vector<double> generateSine(double frequency, 
                                    double sampleRate,
                                    double duration,
                                    double amplitude = 1.0)
    {
        size_t numSamples = static_cast<size_t>(duration * sampleRate);
        std::vector<double> samples(numSamples);
        
        for (size_t i = 0; i < numSamples; ++i)
        {
            double t = i / sampleRate;
            samples[i] = amplitude * std::sin(2.0 * Constants::PI<double> * frequency * t);
        }
        
        return samples;
    }
    
    /**
     * @brief Generate harmonic series (sum of harmonics)
     */
    std::vector<double> generateHarmonicSeries(double fundamental,
                                              int numHarmonics,
                                              double sampleRate,
                                              double duration)
    {
        size_t numSamples = static_cast<size_t>(duration * sampleRate);
        std::vector<double> samples(numSamples, 0.0);
        
        for (int n = 1; n <= numHarmonics; ++n)
        {
            double amplitude = 1.0 / n;  // 1/n amplitude falloff
            
            for (size_t i = 0; i < numSamples; ++i)
            {
                double t = i / sampleRate;
                samples[i] += amplitude * std::sin(2.0 * Constants::PI<double> * 
                                                   n * fundamental * t);
            }
        }
        
        return samples;
    }
    
    /**
     * @brief Generate simple FM signal
     */
    std::vector<double> generateSimpleFM(double carrier,
                                         double modulator,
                                         double beta,
                                         double sampleRate,
                                         double duration)
    {
        size_t numSamples = static_cast<size_t>(duration * sampleRate);
        std::vector<double> samples(numSamples);
        
        for (size_t i = 0; i < numSamples; ++i)
        {
            double t = i / sampleRate;
            double modSignal = beta * std::sin(2.0 * Constants::PI<double> * modulator * t);
            samples[i] = std::sin(2.0 * Constants::PI<double> * carrier * t + modSignal);
        }
        
        return samples;
    }
}

// ============================================================================
// SpectralProfile Tests
// ============================================================================

// ============================================================================
// SpectralProfile – Core Functionality Tests
// ============================================================================

TEST(SpectralProfileTest, EmptySignal)
{
    std::vector<double> empty;
    SpectralProfile profile(empty, sampleRate);

    EXPECT_TRUE(profile.getPeaks().empty());
    EXPECT_EQ(profile.getFFTSize(), 0u);
}

TEST(SpectralProfileTest, PureSineHasSingleDominantPeak)
{
    auto samples = TestSignals::generateSine(440.0, sampleRate, 0.1);
    SpectralProfile profile(samples, sampleRate);

    EXPECT_TRUE(profile.hasPeakAt(440.0, 5.0));
    EXPECT_GE(profile.getPeaks().size(), 1);
}

TEST(SpectralProfileTest, DifferentFrequenciesProduceDifferentCentroids)
{
    auto low = TestSignals::generateSine(200.0, sampleRate, 0.1);
    auto high = TestSignals::generateSine(2000.0, sampleRate, 0.1);

    SpectralProfile lowProfile(low, sampleRate);
    SpectralProfile highProfile(high, sampleRate);

    EXPECT_LT(lowProfile.getSpectralCentroid(),
              highProfile.getSpectralCentroid());

    EXPECT_TRUE(lowProfile.hasPeakAt(200.0, 5.0));
    EXPECT_TRUE(highProfile.hasPeakAt(2000.0, 5.0));
}

TEST(SpectralProfileTest, EnergyIsLocalizedAroundTone)
{
    auto samples = TestSignals::generateSine(1000.0, sampleRate, 0.1);
    SpectralProfile profile(samples, sampleRate);

    double bandEnergy = profile.getEnergyInRange(900.0, 1100.0);
    double totalEnergy = profile.getTotalEnergy();

    EXPECT_GT(bandEnergy / totalEnergy, 0.7);
}

TEST(SpectralProfileTest, HarmonicSignalHasMultiplePeaks)
{
    auto samples = TestSignals::generateHarmonicSeries(
        100.0, 5, sampleRate, 0.1);

    SpectralProfile profile(samples, sampleRate);

    EXPECT_TRUE(profile.hasPeakAt(100.0, 5.0));
    EXPECT_TRUE(profile.hasPeakAt(200.0, 5.0));
    EXPECT_TRUE(profile.hasPeakAt(300.0, 5.0));
    EXPECT_GT(profile.getPeaks().size(), 3);
}

TEST(SpectralProfileTest, DCSignalHasEnergyNearZeroFrequency)
{
    std::vector<double> dc(2048, 1.0);
    SpectralProfile profile(dc, sampleRate);

    double dcEnergy = profile.getEnergyInRange(0.0, 50.0);
    double totalEnergy = profile.getTotalEnergy();

    EXPECT_GT(dcEnergy / totalEnergy, 0.85);
}

TEST(SpectralProfileTest, SpectralCorrelationIdenticalSignals)
{
    auto a = TestSignals::generateSine(440.0, sampleRate, 0.1);
    auto b = TestSignals::generateSine(440.0, sampleRate, 0.1);

    SpectralProfile pa(a, sampleRate);
    SpectralProfile pb(b, sampleRate);

    EXPECT_NEAR(spectralCorrelation(pa, pb), 1.0, 0.1);
}

TEST(SpectralProfileTest, SpectralCorrelationDifferentSignals)
{
    auto a = TestSignals::generateSine(440.0, sampleRate, 0.1);
    auto b = TestSignals::generateSine(880.0, sampleRate, 0.1);

    SpectralProfile pa(a, sampleRate);
    SpectralProfile pb(b, sampleRate);

    EXPECT_LT(spectralCorrelation(pa, pb), 0.9);
}