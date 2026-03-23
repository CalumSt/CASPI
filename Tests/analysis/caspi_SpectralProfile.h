#ifndef CASPI_SPECTRAL_PROFILE_H
#define CASPI_SPECTRAL_PROFILE_H
/*************************************************************************
 * @file caspi_SpectralProfile.h
 * @brief Spectral analysis utilities for audio signal characterization
 *
 * Provides frequency-domain analysis including peak detection, harmonic
 * analysis, and spectral moments for audio signals.
 * Currently, not for real-time use due to computational complexity.
 * Instead, provides heuristics for offline analysis.
 *
 * @author CS Islay
 ************************************************************************/

#include "base/caspi_Constants.h"
#include "maths/caspi_FFT.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace CASPI
{

    /**
     * @struct SpectralPeak
     * @brief Represents a single spectral peak
     */
    struct SpectralPeak
    {
            double frequency; ///< Peak frequency in Hz
            double magnitude; ///< Peak magnitude (linear)
            double phase; ///< Peak phase in radians
            size_t binIndex; ///< FFT bin index

            bool operator< (const SpectralPeak& other) const
            {
                return magnitude > other.magnitude; // Sort by descending magnitude
            }
    };

    enum class WindowType
    {
        Rectangular,
        Hann,
        Hamming,
        Blackman
    };

    /**
     * @class SpectralProfile
     * @brief Complete frequency-domain characterization of an audio signal
     *
     * Computes and stores spectral features including:
     * - Full magnitude/phase spectrum
     * - Peak detection and analysis
     * - Spectral moments (centroid, spread, bandwidth)
     *
     */
    class SpectralProfile
    {
        public:
            /**
             * @brief Construct empty profile
             */
            SpectralProfile() = default;

            /**
             * @brief Construct and analyze audio samples
             * @param samples Time-domain samples
             * @param sampleRate Sample rate in Hz
             * @param windowType Window function to apply (default: Hann)
             * @param minPeakMagnitude Minimum magnitude for peak detection (default: 0.01)
             *
             * Performs FFT and analyzes spectral content automatically.
             */
            SpectralProfile (const std::vector<double>& samples,
                             const double sampleRate,
                             WindowType windowType         = WindowType::Hann,
                             const double minPeakMagnitude = 0.01)
                : sampleRate_ (sampleRate), fftSize_ (samples.size()), minPeakMagnitude_ (minPeakMagnitude)
            {
                analyze (samples, windowType);
            }

            /**
             * @brief Get next power-of-2 size for FFT
             * @param size Desired size
             * @return Next power of 2 >= size
             */
            static size_t getNextPowerOf2 (const size_t size)
            {
                if (size == 0)
                {
                    return 1;
                }

                size_t powerOf2 = 1;
                while (powerOf2 < size)
                {
                    powerOf2 <<= 1;
                }

                return powerOf2;
            }

            /**
             * @brief Check if size is power of 2
             */
            static bool isPowerOf2 (size_t size)
            {
                return size > 0 && (size & (size - 1)) == 0;
            }

            // ====================================================================
            // Analysis Methods
            // ====================================================================

            /**
             * @brief Analyze audio samples
             * @param samples Time-domain samples
             * @param windowType Window function to apply
             *
             * Reference: Harris, F. J. (1978). "On the use of windows for harmonic
             * analysis with the discrete Fourier transform." Proceedings of the IEEE.
             */
            void analyze (const std::vector<double>& samples, WindowType windowType = WindowType::Hann)
            {
                if (samples.empty())
                {
                    return;
                }

                // Find next power of 2 for FFT size
                fftSize_ = getNextPowerOf2 (samples.size());

                // Pad with zeros to reach power of 2
                std::vector<double> paddedSamples (fftSize_, 0.0);
                const size_t copySize = std::min (samples.size(), fftSize_);
                std::copy (samples.begin(), samples.begin() + copySize, paddedSamples.begin());

                // Now safe to pass to FFT
                const auto windowed = applyWindow (paddedSamples, windowType);
                auto fftData        = realToComplex (windowed);

                fft (fftData);

                // Extract magnitude and phase spectra
                extractSpectra (fftData);

                // Detect spectral peaks
                detectPeaks();

                // Compute spectral moments
                computeMoments();
            }

            // ====================================================================
            // Accessors
            // ====================================================================

            const std::vector<double>& getFrequencies() const { return frequencies_; }
            const std::vector<double>& getMagnitudes() const { return magnitudes_; }
            const std::vector<double>& getPhases() const { return phases_; }
            const std::vector<SpectralPeak>& getPeaks() const { return peaks_; }

            size_t getFFTSize() const { return fftSize_; }
            double getSampleRate() const { return sampleRate_; }

            // Moments
            double getSpectralCentroid() const { return spectralCentroid_; }
            double getSpectralSpread() const { return spectralSpread_; }
            double getBandwidth() const { return bandwidth_; }

            // ====================================================================
            // Query Methods
            // ====================================================================

            /**
             * @brief Check if there's a spectral peak at a given frequency
             * @param frequency Target frequency in Hz
             * @param tolerance Frequency tolerance in Hz
             * @return True if peak exists within tolerance
             */
            bool hasPeakAt (double frequency, double tolerance = 2.0) const
            {
                for (const auto& peak : peaks_)
                {
                    if (std::abs (peak.frequency - frequency) <= tolerance)
                    {
                        return true;
                    }
                }
                return false;
            }

            /**
             * @brief Get magnitude at a specific frequency
             * @param frequency Frequency in Hz
             * @return Interpolated magnitude
             */
            double getMagnitudeAt (double frequency) const
            {
                double bin     = frequency * fftSize_ / sampleRate_;
                size_t binLow  = static_cast<size_t> (std::floor (bin));
                size_t binHigh = static_cast<size_t> (std::ceil (bin));

                if (binHigh >= magnitudes_.size())
                {
                    return 0.0;
                }

                // Linear interpolation
                double frac = bin - binLow;
                return magnitudes_[binLow] * (1.0 - frac) + magnitudes_[binHigh] * frac;
            }

            /**
             * @brief Find peak closest to target frequency
             * @param frequency Target frequency in Hz
             * @param maxDistance Maximum search distance in Hz
             * @return Pointer to peak, or nullptr if none found
             */
            const SpectralPeak* findNearestPeak (double frequency, double maxDistance = 50.0) const
            {
                const SpectralPeak* nearest = nullptr;
                double minDist              = maxDistance;

                for (const auto& peak : peaks_)
                {
                    double dist = std::abs (peak.frequency - frequency);
                    if (dist < minDist)
                    {
                        minDist = dist;
                        nearest = &peak;
                    }
                }

                return nearest;
            }

            /**
             * @brief Get total energy in frequency range
             * @param lowFreq Low frequency bound in Hz
             * @param highFreq High frequency bound in Hz
             * @return Energy (sum of squared magnitudes)
             */
            double getEnergyInRange (double lowFreq, double highFreq) const
            {
                size_t lowBin  = static_cast<size_t> (lowFreq * fftSize_ / sampleRate_);
                size_t highBin = static_cast<size_t> (highFreq * fftSize_ / sampleRate_);

                highBin = std::min (highBin, magnitudes_.size() - 1);

                double energy = 0.0;
                for (size_t i = lowBin; i <= highBin; ++i)
                {
                    energy += magnitudes_[i] * magnitudes_[i];
                }

                return energy;
            }

            /**
             * @brief Get total spectral energy
             * @return Total energy
             */
            double getTotalEnergy() const
            {
                return getEnergyInRange (0.0, sampleRate_ / 2.0);
            }

        private:
            // ====================================================================
            // Internal Analysis Methods
            // ====================================================================

            std::vector<double> applyWindow (const std::vector<double>& samples, WindowType type) const
            {
                std::vector<double> windowed (samples.size());
                const size_t N = samples.size();

                for (size_t i = 0; i < N; ++i)
                {
                    double w = 1.0;

                    switch (type)
                    {
                        case WindowType::Hann:
                            // Hann window: 0.5 - 0.5*cos(2πn/N)
                            w = 0.5 * (1.0 - std::cos (2.0 * Constants::PI<double> * i / (N - 1)));
                            break;

                        case WindowType::Hamming:
                            // Hamming window: 0.54 - 0.46*cos(2πn/N)
                            w = 0.54 - 0.46 * std::cos (2.0 * Constants::PI<double> * i / (N - 1));
                            break;

                        case WindowType::Blackman:
                            // Blackman window
                            w = 0.42 - 0.5 * std::cos (2.0 * Constants::PI<double> * i / (N - 1))
                                + 0.08 * std::cos (4.0 * Constants::PI<double> * i / (N - 1));
                            break;

                        case WindowType::Rectangular:
                        default:
                            w = 1.0;
                            break;
                    }

                    windowed[i] = samples[i] * w;
                }

                return windowed;
            }

            CArray realToComplex (const std::vector<double>& samples) const
            {
                CArray data (samples.size());
                for (size_t i = 0; i < samples.size(); ++i)
                {
                    data[i] = Complex (samples[i], 0.0);
                }
                return data;
            }

            void extractSpectra (const CArray& fftData)
            {
                const size_t numBins = fftData.size() / 2; // Nyquist limit

                frequencies_.resize (numBins);
                magnitudes_.resize (numBins);
                phases_.resize (numBins);

                const double binWidth = sampleRate_ / fftSize_;

                for (size_t i = 0; i < numBins; ++i)
                {
                    frequencies_[i] = i * binWidth;
                    magnitudes_[i]  = std::abs (fftData[i]) / fftSize_; // Normalize
                    phases_[i]      = std::arg (fftData[i]);
                }
            }

            /**
             * @brief Detect spectral peaks using parabolic interpolation
             *
             * Reference: Smith, J. O. (2011). "Spectral Audio Signal Processing."
             * CCRMA, Stanford University. Chapter on Peak Detection.
             */
            void detectPeaks()
            {
                peaks_.clear();

                // Find local maxima
                for (size_t i = 1; i < magnitudes_.size() - 1; ++i)
                {
                    if (magnitudes_[i] > magnitudes_[i - 1] && magnitudes_[i] > magnitudes_[i + 1] && magnitudes_[i] > minPeakMagnitude_)
                    {
                        // Parabolic interpolation for refined frequency
                        double alpha = magnitudes_[i - 1];
                        double beta  = magnitudes_[i];
                        double gamma = magnitudes_[i + 1];

                        double p = 0.5 * (alpha - gamma) / (alpha - 2.0 * beta + gamma);

                        SpectralPeak peak;
                        peak.binIndex  = i;
                        peak.frequency = (i + p) * sampleRate_ / fftSize_;
                        peak.magnitude = beta - 0.25 * (alpha - gamma) * p;
                        peak.phase     = phases_[i];

                        peaks_.push_back (peak);
                    }
                }

                // Sort by magnitude (descending)
                std::sort (peaks_.begin(), peaks_.end());
            }

            /**
             * @brief Compute spectral moments
             *
             * Reference: Peeters, G. (2004). "A large set of audio features for
             * sound description." CUIDADO Project.
             */
            void computeMoments()
            {
                double sumMag            = 0.0;
                double sumWeightedFreq   = 0.0;
                double sumWeightedFreqSq = 0.0;

                for (size_t i = 0; i < frequencies_.size(); ++i)
                {
                    double mag  = magnitudes_[i];
                    double freq = frequencies_[i];

                    sumMag            += mag;
                    sumWeightedFreq   += freq * mag;
                    sumWeightedFreqSq += freq * freq * mag;
                }

                if (sumMag > 0.0)
                {
                    // Spectral centroid: center of mass of spectrum
                    spectralCentroid_ = sumWeightedFreq / sumMag;

                    // Spectral spread: standard deviation
                    double variance = (sumWeightedFreqSq / sumMag) - (spectralCentroid_ * spectralCentroid_);
                    spectralSpread_ = std::sqrt (std::max (0.0, variance));

                    // Bandwidth: often defined as 2*spread or spread at -3dB
                    bandwidth_ = 2.0 * spectralSpread_;
                }
                else
                {
                    spectralCentroid_ = 0.0;
                    spectralSpread_   = 0.0;
                    bandwidth_        = 0.0;
                }
            }

            // ====================================================================
            // Member Variables
            // ====================================================================

            double sampleRate_       = 48000.0;
            size_t fftSize_          = 0;
            double minPeakMagnitude_ = 0.01;

            // Spectral data
            std::vector<double> frequencies_;
            std::vector<double> magnitudes_;
            std::vector<double> phases_;
            std::vector<SpectralPeak> peaks_;

            // Spectral moments
            double spectralCentroid_ = 0.0;
            double spectralSpread_   = 0.0;
            double bandwidth_        = 0.0;
    };

    /**
     * @brief Compute spectral correlation between two profiles
     * @param a First spectral profile
     * @param b Second spectral profile
     * @return Pearson correlation coefficient [-1, 1]
     *
     * Measures similarity between two spectra. +1 = identical, 0 = uncorrelated,
     * -1 = inverted.
     */
    inline double spectralCorrelation (const SpectralProfile& a, const SpectralProfile& b)
    {
        const auto& magA = a.getMagnitudes();
        const auto& magB = b.getMagnitudes();

        if (magA.size() != magB.size() || magA.empty())
        {
            return 0.0;
        }

        // Compute means
        double meanA = std::accumulate (magA.begin(), magA.end(), 0.0) / magA.size();
        double meanB = std::accumulate (magB.begin(), magB.end(), 0.0) / magB.size();

        // Compute correlation
        double numerator = 0.0;
        double denomA    = 0.0;
        double denomB    = 0.0;

        for (size_t i = 0; i < magA.size(); ++i)
        {
            double diffA = magA[i] - meanA;
            double diffB = magB[i] - meanB;

            numerator += diffA * diffB;
            denomA    += diffA * diffA;
            denomB    += diffB * diffB;
        }

        double denom = std::sqrt (denomA * denomB);

        if (denom < 1e-10)
        {
            return 0.0;
        }

        return numerator / denom;
    }

} // namespace CASPI

#endif // CASPI_SPECTRAL_PROFILE_H