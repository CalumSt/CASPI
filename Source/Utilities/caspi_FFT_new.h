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
*        The base FFTs are provided as static functions, and the FFT_engine
*        is templated and is intended for repeated FFTs of similar sizes.
*
************************************************************************/

namespace CASPI {

class FFT_new {
public:
    [[nodiscard]] std::vector<double> generateFrequencyBins() const
    {
        const auto frequencyPerBin = sampleRate / static_cast<double>(size);
        auto frequencyBins   = std::vector<double> (size / 2);
        for (int i = 0; i < size / 2; i++)
        {
            frequencyBins.at (i) = (frequencyPerBin * static_cast<double> (i));
        }
        return frequencyBins;
    }

    void generateTwiddleTable(int size,double fs)
    {

        for (auto i = 0; i < size; ++i) {
            for (auto j = 0; j < size; ++j) {
            }
        }
    }

private:
      size_t size       = 256;
      double radix      = 2;
      double sampleRate = 44100;

    };

struct FFTConfig
{
    size_t size       = 256;
    double radix      = 2;
    double sampleRate = 44100;
};

static void dft (const std::vector<double>& inData, std::vector<double>& outData) {

}

using Complex = std::complex<double>;
using CArray  = std::vector<Complex>;

static void perform (CArray& data)
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
    perform (even);
    perform (odd);

    // Butterfly with radix-2
    for (size_t k = 0; k < N / 2; ++k)
    {
        auto phaseFactor = exp (Complex (0.0, -2.0 * CASPI::Constants::PI<double> * static_cast<double> (k) / static_cast<double> (N)));
        data[k]          = even[k] + (phaseFactor * odd[k]);
        data[k + N / 2]  = even[k] - (phaseFactor * odd[k]);
    }
}

static void fft (CArray& data)
{
    perform (data);
}

}
#endif //CASPI_FFT_NEW_H
