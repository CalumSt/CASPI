#ifndef CASPI_FFT_H
#define CASPI_FFT_H
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


* @file caspi_FFT.h
* @author CS Islay
* @class FFT
* @brief A class implementing a radix-2 FFT.
*
************************************************************************/

#include <algorithm>
#include <cmath>
#include <complex>
#include <Utilities/caspi_Assert.h>
#include <Utilities/caspi_Constants.h>
#include <valarray>
#include <vector>


namespace CASPI
{

class FFT
{
    using Complex = std::complex<double>;
    using CArray  = std::vector<Complex>;
    /**
     * An in-place discrete fourier transform, primarily for testing.
     */
    static  void dft (const CArray& inData, CArray& outData)
    {
        const size_t N = inData.size();
        for (size_t k = 0; k < N; ++k)
        {
            Complex sum = 0.0;
            for (size_t n = 0; n < N; ++n)
            {
                auto phaseFactor     = -2.0 * CASPI::Constants::PI<double> * static_cast<double> (k) * static_cast<double> (n) / static_cast<double> (N);
                auto exponentFactor  = exp (Complex (0.0, phaseFactor));
                sum                 += inData[n] * exponentFactor;
            }
            outData[k] = sum;
        }
    }

    /**
     * An in-place real only discrete fourier transform, primarily for testing.
     */
    static void dft (const std::vector<double>& inData, std::vector<double>& outData)
    {
        const size_t N = inData.size();
        for (size_t k = 0; k < N; ++k)
        {
            double sum = 0.0;
            for (size_t n = 0; n < N; ++n)
            {
                auto phaseFactor     = 2.0 * CASPI::Constants::PI<double> * static_cast<double> (k) * static_cast<double> (n) / static_cast<double> (N);
                auto exponentFactor  = sin (phaseFactor);
                sum += inData[n] * exponentFactor;
            }
            outData[k] = sum / (static_cast<double> (N) / 2.0);
        }
    }

    inline void fft (CArray& data)
    {
        const size_t N = data.size();
        if (N <= 1)
        {
            return;
        }

        // Slice even and odd arrays
        auto even = CArray (N / 2);
        auto odd  = even;

        for (size_t i = 0; i < N / 2; ++i)
        {
            even[i] = data[2 * i];
            odd[i]  = data[2 * i + 1];
        }

        // Recurse FFT
        fft (even);
        fft (odd);

        // Butterfly with radix-2
        for (size_t k = 0; k < N / 2; ++k)
        {
            auto phaseFactor = exp (Complex (0.0, -2.0 * CASPI::Constants::PI<double> * static_cast<double> (k) / static_cast<double> (N)));
            data[k]          = even[k] + (phaseFactor * odd[k]);
            data[k + N / 2]  = even[k] - (phaseFactor * odd[k]);
        }
    }

    void timeToFreqFft (std::vector<double>& data)
    {
        auto N    = data.size();
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

    inline std::vector<double> generateFrequencyBins (const int fft_size, const double sampleRate)
    {
        auto frequencyPerBin = sampleRate / (fft_size);
        auto frequencyBins   = std::vector<double> (fft_size / 2);
        for (int i = 0; i < fft_size / 2; i++)
        {
            frequencyBins.at (i) = (frequencyPerBin * static_cast<double> (i));
        }
        return frequencyBins;
    }
};
};

#endif //CASPI_FFT_H
