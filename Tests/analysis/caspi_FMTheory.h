#ifndef CASPI_FM_THEORY_H
#define CASPI_FM_THEORY_H
/*************************************************************************
 * @file caspi_FMTheory.h
 * @brief FM synthesis theory verification using Bessel functions
 * 
 * Provides mathematical predictions for FM synthesis spectra based on
 * Chowning's FM synthesis theory and Bessel function analysis.
 * 
 * References:
 * - Chowning, J. (1973). "The synthesis of complex audio spectra by 
 *   means of frequency modulation." Journal of the Audio Engineering Society.
 * - Abramowitz, M., & Stegun, I. A. (1964). "Handbook of Mathematical 
 *   Functions." National Bureau of Standards.
 * 
 * @author CS Islay
 ************************************************************************/

#include "caspi_SpectralProfile.h"
#include "base/caspi_Constants.h"
#include <vector>
#include <cmath>
#include <algorithm>

#include "maths/caspi_Maths.h"

namespace CASPI
{
namespace FMTheory
{

/**
 * @brief Predict FM sideband amplitudes using Bessel functions
 * @param beta Modulation index (depth of frequency deviation / modulating frequency)
 * @param numSidebands Number of sidebands to predict on each side of carrier
 * @return Vector of sideband amplitudes [J_0, J_1, J_2, ..., J_n]
 * 
 * For FM synthesis with modulation index β:
 * - Carrier amplitude: J_0(β)
 * - n-th sideband amplitude: J_n(β)
 * 
 * Spectrum consists of:
 * - Carrier at f_c with amplitude J_0(β)
 * - Sidebands at f_c ± n*f_m with amplitude J_n(β)
 * 
 * Example: β = 2.0 produces:
 * - J_0(2.0) ≈ 0.224 (carrier)
 * - J_1(2.0) ≈ 0.577 (1st sidebands)
 * - J_2(2.0) ≈ 0.353 (2nd sidebands)
 * - J_3(2.0) ≈ 0.129 (3rd sidebands)
 * 
 * Reference: Chowning (1973), Equation 4
 */
inline std::vector<double> predictSidebandAmplitudes(const double beta, const int numSidebands)
{
    std::vector<double> amplitudes;
    amplitudes.reserve(numSidebands + 1);
    
    // J_0(β) = carrier amplitude
    amplitudes.push_back(Maths::besselJ(0, beta));
    
    // J_n(β) = n-th sideband amplitude
    for (int n = 1; n <= numSidebands; ++n)
    {
        amplitudes.push_back(Maths::besselJ(n, beta));
    }
    
    return amplitudes;
}

/**
 * @brief Find modulation index β from spectrum
 * @param profile Measured spectral profile
 * @param carrier Carrier frequency in Hz
 * @param modulator Modulator frequency in Hz
 * @param maxBeta Maximum β to search (default 10.0)
 * @return Estimated modulation index
 * 
 * Estimates β by finding the value that best matches the measured
 * sideband pattern to the theoretical Bessel function predictions.
 */
inline double estimateModulationIndex (const SpectralProfile& profile,
                                   const double carrier, const double modulator,
                                   const double maxBeta = 10.0)
{
    double bestBeta = 0.0;
    double bestError = 1e10;

    for (double beta = 0.0; beta <= maxBeta; beta += 0.5)
    {
        auto predicted = predictSidebandAmplitudes(beta, 10);
        double error = 0.0;

        for (size_t n = 0; n < predicted.size(); ++n)
        {
            double freq = carrier + n * modulator;
            double measuredAmp = profile.getMagnitudeAt(freq);
            error += std::pow(measuredAmp - predicted[n], 2);
        }

        if (error < bestError)
        {
            bestError = error;
            bestBeta = beta;
        }
    }

    // Refine with smaller step around bestBeta
    double step = 0.01;
    for (double beta = std::max(0.0, bestBeta - 0.5);
         beta <= bestBeta + 0.5; beta += step)
    {
        auto predicted = predictSidebandAmplitudes(beta, 10);
        double error = 0.0;
        for (size_t n = 0; n < predicted.size(); ++n)
            error += std::pow(profile.getMagnitudeAt(carrier + n*modulator) - predicted[n], 2);

        if (error < bestError)
        {
            bestError = error;
            bestBeta = beta;
        }
    }

    return bestBeta;
}

/**
 * @brief Verify measured spectrum matches FM theory
 * @param measured Measured spectral profile
 * @param carrier Carrier frequency in Hz
 * @param modulator Modulator frequency in Hz
 * @param beta Expected modulation index
 * @param tolerance Relative tolerance for amplitude matching (default 0.1 = 10%)
 * @return True if spectrum matches theory within tolerance
 * 
 * Verifies that:
 * 1. Sidebands appear at f_c ± n*f_m
 * 2. Sideband amplitudes match J_n(β) predictions
 * 3. No unexpected spectral peaks exist
 * 
 * Example usage:
 * @code
 * SpectralProfile profile = analyzeFMSignal();
 * bool matches = verifyFMSpectrum(profile, 440.0, 110.0, 2.0, 0.15);
 * @endcode
 */
inline bool verifyFMSpectrum(const SpectralProfile& measured,
                            double carrier,
                            double modulator,
                            double beta,
                            double tolerance = 0.1)
{
    // Get theoretical predictions
    auto predicted = predictSidebandAmplitudes(beta, 15);
    
    // Normalize predictions to match measured carrier amplitude
    const auto* carrierPeak = measured.findNearestPeak(carrier, modulator * 0.5);
    if (!carrierPeak)
        return false;  // No carrier found
    
    double normalizationFactor = carrierPeak->magnitude / std::abs(predicted[0]);
    
    // Verify carrier and sidebands
    for (size_t n = 0; n < predicted.size(); ++n)
    {
        double expectedAmp = std::abs(predicted[n]) * normalizationFactor;
        
        // Skip if theoretical amplitude is negligible
        if (expectedAmp < 0.01)
            continue;
        
        // Check upper sideband: f_c + n*f_m
        double upperFreq = carrier + n * modulator;
        if (upperFreq < measured.getSampleRate() / 2.0)
        {
            const auto* peak = measured.findNearestPeak(upperFreq, modulator * 0.2);
            
            if (peak)
            {
                double relativeError = std::abs(peak->magnitude - expectedAmp) / expectedAmp;
                if (relativeError > tolerance)
                    return false;
            }
            else if (expectedAmp > 0.05)  // Should have peak if amplitude significant
            {
                return false;
            }
        }
        
        // Check lower sideband: f_c - n*f_m (skip n=0, that's carrier)
        if (n > 0)
        {
            double lowerFreq = carrier - n * modulator;
            if (lowerFreq > 0)
            {
                const auto* peak = measured.findNearestPeak(lowerFreq, modulator * 0.2);
                
                if (peak)
                {
                    double relativeError = std::abs(peak->magnitude - expectedAmp) / expectedAmp;
                    if (relativeError > tolerance)
                        return false;
                }
                else if (expectedAmp > 0.05)
                {
                    return false;
                }
            }
        }
    }
    
    return true;
}

/**
 * @brief Predict total number of significant sidebands
 * @param beta Modulation index
 * @param threshold Minimum relative amplitude (default 0.01 = 1%)
 * @return Number of significant sidebands on each side
 * 
 * Counts sidebands where |J_n(β)| > threshold
 * 
 * Reference: Carson's rule states bandwidth ≈ 2(β + 1)f_m
 */
inline int predictSignificantSidebands(double beta, double threshold = 0.01)
{
    int count = 0;
    
    // Check Bessel functions until they fall below threshold
    for (int n = 1; n <= 50; ++n)
    {
        double amplitude = std::abs(Maths::besselJ(n, beta));
        
        if (amplitude < threshold)
            break;
        
        count = n;
    }
    
    return count;
}

/**
 * @brief Check if modulation index is at a carrier null (Bessel zero)
 * @param beta Modulation index
 * @param tolerance Tolerance around zero (default 0.1)
 * @return True if β is near a zero of J_0
 * 
 * Known zeros of J_0(x):
 * - x ≈ 2.4048  (1st zero)
 * - x ≈ 5.5201  (2nd zero)
 * - x ≈ 8.6537  (3rd zero)
 * - x ≈ 11.7915 (4th zero)
 * 
 * At these values, carrier is suppressed and energy is in sidebands.
 */
inline bool isCarrierNull(double beta, double tolerance = 0.1)
{
    static const double besselZeros[] = {2.4048, 5.5201, 8.6537, 11.7915, 14.9309};
    
    for (double zero : besselZeros)
    {
        if (std::abs(beta - zero) < tolerance)
            return true;
    }
    
    return false;
}

/**
 * @brief Predict FM spectrum power distribution
 * @param beta Modulation index
 * @param numSidebands Number of sidebands to include
 * @return Power distribution: [carrier, sideband1, sideband2, ...]
 * 
 * Power in each component is proportional to J_n²(β)
 * Total power should sum to 1 (normalized)
 */
inline std::vector<double> predictPowerDistribution(double beta, int numSidebands)
{
    auto amplitudes = predictSidebandAmplitudes(beta, numSidebands);
    
    std::vector<double> power;
    power.reserve(amplitudes.size());
    
    // Power = amplitude²
    for (double amp : amplitudes)
    {
        power.push_back(amp * amp);
    }
    
    return power;
}

/**
 * @brief Compute spectral centroid of FM spectrum
 * @param carrier Carrier frequency
 * @param modulator Modulator frequency  
 * @param beta Modulation index
 * @param numSidebands Number of sidebands to include
 * @return Predicted spectral centroid in Hz
 * 
 * Centroid shifts higher with increasing β as sideband energy increases
 */
inline double predictSpectralCentroid(double carrier, 
                                     double modulator,
                                     double beta,
                                     int numSidebands)
{
    auto powers = predictPowerDistribution(beta, numSidebands);
    
    double weightedSum = 0.0;
    double totalPower = 0.0;
    
    // Carrier
    weightedSum += carrier * powers[0];
    totalPower += powers[0];
    
    // Sidebands
    for (int n = 1; n < static_cast<int>(powers.size()); ++n)
    {
        double upperFreq = carrier + n * modulator;
        double lowerFreq = carrier - n * modulator;
        
        // Upper sideband
        weightedSum += upperFreq * powers[n];
        totalPower += powers[n];
        
        // Lower sideband (if above 0 Hz)
        if (lowerFreq > 0)
        {
            weightedSum += lowerFreq * powers[n];
            totalPower += powers[n];
        }
    }
    
    return (totalPower > 0) ? weightedSum / totalPower : carrier;
}

} // namespace FMTheory
} // namespace CASPI

#endif // CASPI_FM_THEORY_H