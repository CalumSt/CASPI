#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <complex>
#include <cmath>
#include <numeric>
#include <algorithm>

#include "base/caspi_Constants.h"
#include "maths/caspi_FFT.h"

using namespace CASPI;

template <typename FloatType>
std::vector<FloatType> range(FloatType start, FloatType end, FloatType step)
{
    std::vector<FloatType> result;
    for (FloatType i = start; i < end; i += step) {
        result.push_back(i);
    }
    return result;

}

template <typename FloatType>
std::vector<FloatType> range (FloatType start, FloatType end, int numberOfSteps)
{
    std::vector<FloatType> result;
    auto timeStep = (end - start) / numberOfSteps;
    for (int i = 0; i < numberOfSteps; i++)
    {
        auto value = start + static_cast<FloatType> (timeStep * i);
        result.push_back (value);
    }
    return result;
}

// Helper function to compare two vectors
template <typename T>
void compareVectors(const std::vector<T>& expected, const std::vector<T>& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], actual[i]) << "Vectors differ at index " << i;
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

namespace TestHelpers
{
    inline double calculateRMS(const std::vector<double>& samples)
    {
        double sum = 0.0;
        for (double s : samples)
            sum += s * s;
        return std::sqrt(sum / samples.size());
    }

    inline double calculateDCOffset(const std::vector<double>& samples)
    {
        return std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    }

    inline int countZeroCrossings(const std::vector<double>& samples)
    {
        int count = 0;
        for (size_t i = 1; i < samples.size(); ++i)
        {
            if ((samples[i-1] < 0.0 && samples[i] >= 0.0) ||
                (samples[i-1] >= 0.0 && samples[i] < 0.0))
                count++;
        }
        return count;
    }

    inline std::vector<double> applyHannWindow(const std::vector<double>& samples)
    {
        std::vector<double> windowed(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
        {
            double w = 0.5 * (1.0 - std::cos(Constants::TWO_PI<double> * i / (samples.size() - 1)));
            windowed[i] = samples[i] * w;
        }
        return windowed;
    }

    inline CArray realToComplex(const std::vector<double>& samples)
    {
        CArray data(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            data[i] = Complex(samples[i], 0.0);
        return data;
    }

    inline std::vector<double> getMagnitudeSpectrum(const CArray& fftData)
    {
        std::vector<double> mag(fftData.size() / 2);
        for (size_t i = 0; i < mag.size(); ++i)
            mag[i] = std::abs(fftData[i]);
        return mag;
    }

    inline int countSignificantPeaks(const std::vector<double>& spectrum, double threshold = 0.1)
    {
        double maxMag = *std::max_element(spectrum.begin(), spectrum.end());
        double thresholdMag = maxMag * threshold;
        int count = 0;
        bool inPeak = false;

        for (size_t i = 1; i < spectrum.size() - 1; ++i)
        {
            if (spectrum[i] > thresholdMag && !inPeak)
            {
                if (spectrum[i] > spectrum[i-1] && spectrum[i] > spectrum[i+1])
                {
                    count++;
                    inPeak = true;
                }
            }
            else if (spectrum[i] <= thresholdMag)
            {
                inPeak = false;
            }
        }
        return count;
    }

    inline double calculatePeriodCorrelation(const std::vector<double>& samples, size_t period)
    {
        if (samples.size() < 2 * period)
            return 0.0;

        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < period; ++i)
        {
            meanA += samples[i];
            meanB += samples[i + period];
        }
        meanA /= period;
        meanB /= period;

        double numerator = 0.0, denomA = 0.0, denomB = 0.0;
        for (size_t i = 0; i < period; ++i)
        {
            double diffA = samples[i] - meanA;
            double diffB = samples[i + period] - meanB;
            numerator += diffA * diffB;
            denomA += diffA * diffA;
            denomB += diffB * diffB;
        }

        if (denomA <= 0.0 || denomB <= 0.0)
            return 0.0;

        return numerator / std::sqrt(denomA * denomB);
    }
}


#endif //TEST_HELPERS_H
