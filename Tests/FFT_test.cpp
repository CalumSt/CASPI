#pragma once

#include <complex>
#include <vector>
#include <cmath>
#include <gtest/gtest.h>
#include "Oscillators/caspi_BlepOscillator.h"
#include "Utilities/caspi_FFT.h"
#include "test_helpers.h"

using namespace std;

// Helper function to compare two vectors
template <typename T>
void compareVectors(const std::vector<T>& expected, const std::vector<T>& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], actual[i]) << "Vectors differ at index " << i;
    }
}

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

TEST(FFT,DFT_test)
{
    int numSamples = 512;
    double sampleRate = 512.0;
    double testFrequency = 120.0;
    auto timeData = CASPI::BlepOscillator::renderBlock<CASPI::BlepOscillator::Sine<double>> (testFrequency,sampleRate,numSamples);
    auto outData = timeData;
    CASPI::dft (timeData,outData);

    auto frequencyBins = CASPI::generateFrequencyBins (numSamples,sampleRate);
    // Get closest to test frequency
    auto lowerBound = std::lower_bound (frequencyBins.begin(),frequencyBins.end(),testFrequency);
    // Want the frequency bins around the test frequency to have highest magnitude
    EXPECT_EQ (outData.size(), numSamples);
}

TEST(FFT,fft_size_test)
{
    // check to make sure we get the same size!
    int numSamples = 512;
    auto data = std::vector<std::complex<double>>(numSamples);
    CASPI::fft (data);
    EXPECT_TRUE (data.size() == numSamples);

}

TEST(FFT,FrequencyBins_test)
{
    for (int FFT_size : FFT_sizes)
    {
        for (double sampleRate : sampleRateList)
        {
            // Test generation of frequency bins
            std::vector<double> frequencyBins = CASPI::generateFrequencyBins (FFT_size,sampleRate);
            std::vector<double> expectedFrequencyBins = constructExpectedFrequencyBins (FFT_size,sampleRate);
            compareVectors<double> (frequencyBins,expectedFrequencyBins);
        }

    }
}


TEST(FFT,fft_test)
{
    // Set up FFT size
    int numSamples = 512;
    // check to make sure we get the same size!

    // Generate some data at 20Hz
    auto realData  = CASPI::BlepOscillator::renderBlock<CASPI::BlepOscillator::Sine<double>> (20.0,44100.0,numSamples);

    auto data = std::vector<std::complex<double>>(numSamples);
    // Cast to complex
    for (int i = 0; i < numSamples; i++)
    {
        data.at(i) = std::complex<double> (realData.at(i), 0.0);
    }
    // FFT
    CASPI::fft (data);

    // Take abs
    auto realFFTdata = std::vector<double>(numSamples/2);
    for (int i = 0; i < (numSamples/2); i++)
    {
        realFFTdata.at(i) = abs(data.at(i+1));
    }

    std::vector<double> frequencyBins = CASPI::generateFrequencyBins (numSamples,44100.0);
    EXPECT_TRUE (frequencyBins.size() == realFFTdata.size());

    // Create plot
}
