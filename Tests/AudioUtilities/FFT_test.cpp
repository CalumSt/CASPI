#include <complex>
#include <vector>
#include <cmath>
#include <gtest/gtest.h>
#include "Oscillators/caspi_BlepOscillator.h"
#include "../test_helpers.h"
#define private public
#include "Utilities/caspi_FFT_new.h"

/*
 Can initialise an FFT engine
 Can create an FFT config for the requested FFT
 Can generate frequency bins based on FFT size and sample rate
 Can generate twiddle table based on FFT size and sample rate
 Can check data is a power of 2
 Can perform DFT of multiple sizes
 Can perform IDFT of multiple sizes
 Can perform FFT on real data
     Can slice data into even and odd arrays
     Can recurse FFT on even and odd arrays
     Returns when size of arrays is no longer 2
 Can perform IFFT on real data
 Can perform FFT on complex data
 Can perform IFFT on complex data
 If given data that isn't a power of two, can warn or use DFT
 Option between fallback of DFT or NONE
 Can queue multiple FFT configs
 Can assign priority to FFT configs
 Can complete higher priority FFTs first
 */

std::vector<double> constructExpectedFrequencyBins(int fft_size, double sampleRate)
{
    std::vector<double> frequencyBins(fft_size / 2);
    const auto frequencyPerBin = sampleRate / fft_size;
    for (int i = 0; i < fft_size/2; i++)
    {
       frequencyBins.at(i) = static_cast<double>(i) * frequencyPerBin;
    }
    return frequencyBins;
}

auto constructTestData(int blockSize, double sampleRate)
{
    auto data_20Hz  = CASPI::BlepOscillator::renderBlock<CASPI::BlepOscillator::Sine<double>> (20.0,sampleRate,blockSize);
    auto data_100Hz = CASPI::BlepOscillator::renderBlock<CASPI::BlepOscillator::Sine<double>> (100.0,sampleRate,blockSize);
    auto data = std::vector<double>(blockSize);
    for (int i = 0; i < blockSize; i++)
    {
        data.at(i) = data_20Hz.at(i) + data_100Hz.at(i);
    }
    //return data
    return data;
}

const std::vector<int> FFT_sizes = {64,128,256,512,1024,2048,0,127};
const std::vector<double> sampleRateList = {48000.0,44100.0,22050.0};

TEST(FFTtests, constructor_test)
{
    CASPI::FFT_new engine;
    ASSERT_TRUE (true);
    ASSERT_EQ (engine.radix,2);
    ASSERT_EQ(engine.sampleRate, 44100);
    ASSERT_EQ(engine.size, 256);
}

TEST(FFTtests, generateFrequencyBins_test)
{
    auto bins = constructExpectedFrequencyBins(256,44100);
    CASPI::FFT_new engine;
    auto testBins = engine.generateFrequencyBins();
    compareVectors (bins,testBins);
}

TEST(FFTtests, generateTwiddleTable_test)
{
    CASPI::FFT_new engine;
    ASSERT_EQ (engine.size, 256);

}
