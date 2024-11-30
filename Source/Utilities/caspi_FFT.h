#ifndef CASPI_FFT_H
#define CASPI_FFT_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <Utilities/caspi_Assert.h>
#include <Utilities/caspi_Constants.h>
#include <valarray>
#include <vector>

using Complex = std::complex<double>;
using CArray = std::vector<Complex>;


namespace CASPI {
/*
 * An in-place discrete fourier transform, primarily for testing.
 */
inline void dft (const CArray& inData, CArray& outData)
{
    const size_t N = inData.size();
    for (size_t k = 0; k < N; ++k) {
        Complex sum = 0.0;
        for (size_t n = 0; n < N; ++n) {
            auto phaseFactor = -2.0 * CASPI::Constants::PI<double> * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(N);
            auto exponentFactor = exp(Complex(0.0, phaseFactor));
            sum += inData[n] * exponentFactor;
        }
        outData[k] = sum;

        for (size_t f = 0; f < N; ++f)
        {
            std::printf("%02zu: %.2f, %.2f\n", f, real(outData[f]), imag(outData[f]));
        }

    }
}

/*
 * An in-place real only discrete fourier transform, primarily for testing.
 */
inline void dft (const std::vector<double>& inData, std::vector<double>& outData)
{
    const size_t N = inData.size();
    for (size_t k = 0; k < N; ++k) {
        double sum = 0.0;
        for (size_t n = 0; n < N; ++n) {
            auto phaseFactor = 2.0 * CASPI::Constants::PI<double> * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(N);
            auto exponentFactor = sin(phaseFactor);
            sum += inData[n] * exponentFactor;
        }
        outData[k] = sum;

        for (size_t f = 0; f < N; ++f)
        {
            std::printf("%02zu: %.2f, %.2f\n", f, (outData[f] ));
        }

    }
}

inline void fft(CArray& data) {
    const size_t N = data.size();
    if (N <= 1) { return; }

	// Slice even and odd arrays
	auto even = CArray(N / 2);
    auto odd = even;

    for (size_t i = 0; i < N / 2; ++i) {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }

    // Recurse FFT
    fft(even);
    fft(odd);

	// Butterfly with radix-2
    for (size_t k = 0; k < N / 2; ++k) {
        auto phaseFactor = exp(Complex(0.0, -2.0 * CASPI::Constants::PI<double> * static_cast<double>(k) / static_cast<double>(N)));
        data[k] = even[k] + (phaseFactor * odd[k]);
        data[k + N / 2] = even[k] - (phaseFactor * odd[k]);
    }
}

void timeToFreqFft (std::vector<double>& data)
{
    auto N = data.size();
    auto temp = std::vector<Complex> (N);
    for (size_t i = 0; i < data.size(); ++i)
    {
        temp[i] = Complex (data[i], 0.0);
    }
    fft (temp);

    for (size_t i = 0; i < N / 2; ++i)
    {
        data[i] = abs (temp[i]);
    }
    data;
}

inline std::vector<double> generateFrequencyBins(int fft_size,double sampleRate)
{
    auto frequencyPerBin = sampleRate / (fft_size);
    auto frequencyBins = std::vector<double>(fft_size/2);
    for (int i = 0; i < fft_size / 2; i++) { frequencyBins.at(i) = (frequencyPerBin * static_cast<double>(i)); }
    return frequencyBins;
}

/// Internal Function to cast real data to complex to FFT it
void castRealToComplex(std::vector<double>& data) {

}

/// Useful struct to define a lookup table for a given size
/// only holds 1 LUT
struct twiddleLookup {
    bool genTwiddleLookup(size_t N) {
        if (N <= 1) { return false; }
        if (N % 2 != 0) { return false; }
        lookup = CArray(N / 2);
        for (size_t k = 0; k < N / 2; ++k) {
            lookup[k] = exp(Complex(0.0, -2.0 * CASPI::Constants::PI<double> * k / N));
         }
         isGenerated = true;
         return true;
    }

    Complex getTwiddle(size_t k) {
        CASPI_ASSERT(isGenerated, "Twiddle lookup table has not been generated.");
        CASPI_ASSERT(k < lookup.size(), "Index out of bounds.");
        return lookup[k];
    }

    bool readTwiddeFromFile(std::string fileName) {
        return false;
    }

    private:
        std::vector<Complex> lookup;
        bool isGenerated = false;
};

}
#endif //CASPI_FFT_H
