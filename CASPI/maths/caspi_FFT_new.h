#ifndef CASPI_FFT_NEW_H
#define CASPI_FFT_NEW_H
/************************************************************************
 .d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888

* @file caspi_FFT_new.h
* @author CS Islay
* @brief FFT related functionality and processing classes.
*        Provides static FFT functions and FFT_Engine for repeated transforms.
*        Implements Cooley-Tukey radix-2 FFT algorithm.
************************************************************************/

#include <complex>
#include <vector>
#include <cmath>
#include <stdexcept>
#include "base/caspi_Constants.h"
#include "base/caspi_Assert.h"

namespace CASPI
{

// Type aliases for clarity
using Complex = std::complex<double>;
using CArray = std::vector<Complex>;

// ============================================================================
// FFT Configuration
// ============================================================================

struct FFTConfig
{
    size_t size       = 256;      ///< FFT size (must be power of 2)
    double sampleRate = 44100.0;  ///< Sample rate in Hz

    /**
     * @brief Validate that FFT size is a power of 2
     */
    bool isValid() const
    {
        return size > 0 && (size & (size - 1)) == 0;
    }

    /**
     * @brief Get frequency resolution (bin width) in Hz
     */
    double getFrequencyResolution() const
    {
        return sampleRate / static_cast<double>(size);
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Check if a number is a power of 2
 */
inline bool isPowerOfTwo(size_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/**
 * @brief Find next power of 2 greater than or equal to n
 */
inline size_t nextPowerOfTwo(size_t n)
{
    if (n == 0) return 1;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    #if SIZE_MAX > 0xFFFFFFFF
    n |= n >> 32;
    #endif
    n++;

    return n;
}

/**
 * @brief Generate frequency bins for FFT output
 * @param fftSize Size of FFT
 * @param sampleRate Sample rate in Hz
 * @return Vector of frequency values for each bin
 */
inline std::vector<double> generateFrequencyBins(size_t fftSize, double sampleRate)
{
    const auto frequencyPerBin = sampleRate / static_cast<double>(fftSize);
    auto frequencyBins = std::vector<double>(fftSize / 2);

    for (size_t i = 0; i < fftSize / 2; ++i)
    {
        frequencyBins[i] = frequencyPerBin * static_cast<double>(i);
    }

    return frequencyBins;
}

/**
 * @brief Convert real samples to complex array
 */
inline CArray realToComplex(const std::vector<double>& real)
{
    CArray complex(real.size());
    for (size_t i = 0; i < real.size(); ++i)
    {
        complex[i] = Complex(real[i], 0.0);
    }
    return complex;
}

/**
 * @brief Extract magnitude spectrum from complex FFT output
 */
inline std::vector<double> getMagnitude(const CArray& fftData)
{
    std::vector<double> magnitude(fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
    {
        magnitude[i] = std::abs(fftData[i]);
    }
    return magnitude;
}

/**
 * @brief Extract power spectrum (magnitude squared) from complex FFT output
 */
inline std::vector<double> getPower(const CArray& fftData)
{
    std::vector<double> power(fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
    {
        double mag = std::abs(fftData[i]);
        power[i] = mag * mag;
    }
    return power;
}

/**
 * @brief Extract phase spectrum from complex FFT output
 */
inline std::vector<double> getPhase(const CArray& fftData)
{
    std::vector<double> phase(fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
    {
        phase[i] = std::arg(fftData[i]);
    }
    return phase;
}

// ============================================================================
// Windowing Functions
// ============================================================================

/**
 * @brief Apply Hann window to reduce spectral leakage
 */
inline std::vector<double> applyHannWindow(const std::vector<double>& samples)
{
    std::vector<double> windowed(samples.size());
    const double N = static_cast<double>(samples.size());

    for (size_t i = 0; i < samples.size(); ++i)
    {
        double window = 0.5 * (1.0 - std::cos(2.0 * CASPI::Constants::PI<double> * i / (N - 1.0)));
        windowed[i] = samples[i] * window;
    }

    return windowed;
}

/**
 * @brief Apply Hamming window
 */
inline std::vector<double> applyHammingWindow(const std::vector<double>& samples)
{
    std::vector<double> windowed(samples.size());
    const double N = static_cast<double>(samples.size());

    for (size_t i = 0; i < samples.size(); ++i)
    {
        double window = 0.54 - 0.46 * std::cos(2.0 * CASPI::Constants::PI<double> * i / (N - 1.0));
        windowed[i] = samples[i] * window;
    }

    return windowed;
}

/**
 * @brief Apply Blackman window (better sidelobe rejection)
 */
inline std::vector<double> applyBlackmanWindow(const std::vector<double>& samples)
{
    std::vector<double> windowed(samples.size());
    const double N = static_cast<double>(samples.size());
    const double a0 = 0.42;
    const double a1 = 0.5;
    const double a2 = 0.08;

    for (size_t i = 0; i < samples.size(); ++i)
    {
        double window = a0
                      - a1 * std::cos(2.0 * CASPI::Constants::PI<double> * i / (N - 1.0))
                      + a2 * std::cos(4.0 * CASPI::Constants::PI<double> * i / (N - 1.0));
        windowed[i] = samples[i] * window;
    }

    return windowed;
}

// ============================================================================
// Core FFT Algorithm (Cooley-Tukey Radix-2)
// ============================================================================

/**
 * @brief Recursive FFT implementation (Cooley-Tukey algorithm)
 * @param data Complex input/output array (modified in place)
 *
 * This is the core FFT algorithm. It uses divide-and-conquer:
 * 1. Split input into even and odd indexed samples
 * 2. Recursively compute FFT of each half
 * 3. Combine results using butterfly operations
 *
 * Time complexity: O(N log N)
 * Space complexity: O(N) due to recursion
 */
inline void fftRecursive(CArray& data)
{
    const size_t N = data.size();

    // Base case: size 1 FFT is just the input
    if (N <= 1)
        return;

    // Size must be power of 2 for radix-2 FFT
    CASPI_ASSERT(isPowerOfTwo(N), "FFT size must be power of 2");

    // Divide: separate even and odd indexed samples
    CArray even(N / 2);
    CArray odd(N / 2);

    for (size_t i = 0; i < N / 2; ++i)
    {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }

    // Conquer: recursively compute FFT of each half
    fftRecursive(even);
    fftRecursive(odd);

    // Combine: butterfly operations with twiddle factors
    for (size_t k = 0; k < N / 2; ++k)
    {
        // Twiddle factor: W_N^k = e^(-2πi k/N)
        double angle = -2.0 * CASPI::Constants::PI<double> * static_cast<double>(k) / static_cast<double>(N);
        Complex twiddleFactor = std::exp(Complex(0.0, angle));
        Complex t = twiddleFactor * odd[k];

        // Butterfly: combine results
        data[k] = even[k] + t;
        data[k + N / 2] = even[k] - t;
    }
}

/**
 * @brief Inverse FFT (recursively)
 * @param data Complex input/output array (modified in place)
 */
inline void ifftRecursive(CArray& data)
{
    const size_t N = data.size();

    if (N <= 1)
        return;

    CASPI_ASSERT(isPowerOfTwo(N), "IFFT size must be power of 2");

    CArray even(N / 2);
    CArray odd(N / 2);

    for (size_t i = 0; i < N / 2; ++i)
    {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }

    ifftRecursive(even);
    ifftRecursive(odd);

    for (size_t k = 0; k < N / 2; ++k)
    {
        // Positive angle for IFFT (conjugate of forward transform)
        double angle = 2.0 * CASPI::Constants::PI<double> * static_cast<double>(k) / static_cast<double>(N);
        Complex twiddleFactor = std::exp(Complex(0.0, angle));
        Complex t = twiddleFactor * odd[k];

        data[k] = even[k] + t;
        data[k + N / 2] = even[k] - t;
    }
}

// ============================================================================
// Public FFT Interface
// ============================================================================

/**
 * @brief Compute Forward FFT
 * @param data Complex array (modified in place)
 *
 * Computes the Discrete Fourier Transform using FFT algorithm.
 * Input is modified in place to contain the frequency domain representation.
 *
 * Example:
 *   CArray signal = realToComplex(samples);
 *   fft(signal);
 *   auto magnitude = getMagnitude(signal);
 */
inline void fft(CArray& data)
{
    fftRecursive(data);
}

/**
 * @brief Compute Inverse FFT
 * @param data Complex array (modified in place)
 *
 * Computes the Inverse Discrete Fourier Transform.
 * Scales output by 1/N to maintain proper normalization.
 */
inline void ifft(CArray& data)
{
    ifftRecursive(data);

    // Scale by 1/N for proper IFFT normalization
    const double scale = 1.0 / static_cast<double>(data.size());
    for (auto& value : data)
    {
        value *= scale;
    }
}

/**
 * @brief Compute Forward FFT on real-valued input
 * @param realData Real-valued input samples
 * @return Complex FFT output
 */
inline CArray fftReal(const std::vector<double>& realData)
{
    CArray complexData = realToComplex(realData);
    fft(complexData);
    return complexData;
}

// ============================================================================
// FFT_new Class (Stateful FFT Engine)
// ============================================================================

/**
 * @brief Stateful FFT engine for repeated transforms of the same size
 *
 * This class maintains configuration and can optimize for repeated FFTs
 * of the same size. Future enhancements could include twiddle factor caching.
 */
class FFT_new
{
public:
    /**
     * @brief Constructor with default configuration
     */
    FFT_new() = default;

    /**
     * @brief Constructor with custom configuration
     */
    explicit FFT_new(const FFTConfig& config) : config_(config)
    {
        if (!config_.isValid())
        {
            throw std::invalid_argument("FFT size must be power of 2");
        }
    }

    /**
     * @brief Set FFT size
     */
    void setSize(size_t size)
    {
        if (!isPowerOfTwo(size))
        {
            throw std::invalid_argument("FFT size must be power of 2");
        }
        config_.size = size;
    }

    /**
     * @brief Set sample rate
     */
    void setSampleRate(double sampleRate)
    {
        config_.sampleRate = sampleRate;
    }

    /**
     * @brief Get current FFT size
     */
    size_t getSize() const { return config_.size; }

    /**
     * @brief Get current sample rate
     */
    double getSampleRate() const { return config_.sampleRate; }

    /**
     * @brief Generate frequency bins for current configuration
     */
    std::vector<double> generateFrequencyBins() const
    {
        return CASPI::generateFrequencyBins(config_.size, config_.sampleRate);
    }

    /**
     * @brief Get frequency resolution (bin width)
     */
    double getFrequencyResolution() const
    {
        return config_.getFrequencyResolution();
    }

    /**
     * @brief Perform FFT using engine's configuration
     */
    void perform(CArray& data) const
    {
        if (data.size() != config_.size)
        {
            throw std::invalid_argument("Data size does not match FFT configuration");
        }
        fft(data);
    }

    /**
     * @brief Perform IFFT using engine's configuration
     */
    void performInverse(CArray& data) const
    {
        if (data.size() != config_.size)
        {
            throw std::invalid_argument("Data size does not match FFT configuration");
        }
        ifft(data);
    }

private:
    FFTConfig config_;
};

} // namespace CASPI

#endif // CASPI_FFT_NEW_H