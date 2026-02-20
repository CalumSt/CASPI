#include "maths/caspi_FFT.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

static constexpr double TOL = 1e-9;

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

static double maxAbsDiff(const CASPI::CArray& a, const CASPI::CArray& b)
{
    assert(a.size() == b.size());
    double d = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        d = std::max(d, std::abs(a[i] - b[i]));
    }

    return d;
}

static CASPI::CArray referenceDFT(const CASPI::CArray& x)
{
    const size_t N = x.size();
    CASPI::CArray X(N);

    for (size_t k = 0; k < N; ++k)
    {
        for (size_t n = 0; n < N; ++n)
        {
            double angle =
                -2.0 * CASPI::Constants::PI<double> *
                static_cast<double>(k * n) / static_cast<double>(N);

            X[k] += x[n] * CASPI::Complex(std::cos(angle), std::sin(angle));
        }
    }

    return X;
}

// ============================================================================
// 1. Utilities
// ============================================================================

TEST(FFT_Utilities, PowerOfTwo)
{
    EXPECT_TRUE(CASPI::isPowerOfTwo(1));
    EXPECT_TRUE(CASPI::isPowerOfTwo(1024));
    EXPECT_FALSE(CASPI::isPowerOfTwo(3));
}

TEST(FFT_Utilities, NextPowerOfTwo)
{
    EXPECT_EQ(CASPI::nextPowerOfTwo(3), 4u);
    EXPECT_EQ(CASPI::nextPowerOfTwo(9), 16u);
}

TEST(FFT_Utilities, FFTConfig)
{
    CASPI::FFTConfig cfg;
    cfg.size = 256;
    cfg.sampleRate = 44100.0;

    EXPECT_TRUE(cfg.isValid());
    EXPECT_NEAR(cfg.getFrequencyResolution(), 44100.0 / 256.0, 1e-10);

    cfg.size = 255;
    EXPECT_FALSE(cfg.isValid());
}

// ============================================================================
// 2. Bit Reversal
// ============================================================================

TEST(FFT_BitReversal, CorrectPermutation)
{
    CASPI::CArray d(8);

    for (size_t i = 0; i < 8; ++i)
    {
        d[i] = CASPI::Complex(static_cast<double>(i), 0.0);
    }

    CASPI::bitReversalPermutation(d);

    const std::vector<double> expected = {0,4,2,6,1,5,3,7};

    for (size_t i = 0; i < 8; ++i)
    {
        EXPECT_NEAR(d[i].real(), expected[i], 1e-15);
    }
}

TEST(FFT_BitReversal, Involution)
{
    CASPI::CArray d(8);

    for (size_t i = 0; i < 8; ++i)
    {
        d[i] = CASPI::Complex(static_cast<double>(i), 0.0);
    }

    CASPI::bitReversalPermutation(d);
    CASPI::bitReversalPermutation(d);

    for (size_t i = 0; i < 8; ++i)
    {
        EXPECT_NEAR(d[i].real(), static_cast<double>(i), 1e-15);
    }
}

// ============================================================================
// 3. Twiddle Table
// ============================================================================

TEST(FFT_Twiddle, SizeCheck)
{
    auto t = CASPI::computeTwiddleTable(8);

    // Expected N-1 entries
    EXPECT_EQ(t.re.size(), 7u);
    EXPECT_TRUE(t.isValid());
}

// ============================================================================
// 5. Stateless FFT
// ============================================================================

TEST(FFT_Core, Impulse)
{
    const size_t N = 64;

    CASPI::CArray x(N, CASPI::Complex(0.0));
    x[0] = CASPI::Complex(1.0);

    CASPI::CArray ref = referenceDFT(x);

    CASPI::fft(x);

    EXPECT_LE(maxAbsDiff(x, ref), TOL);
}

TEST(FFT_Core, Random)
{
    const size_t N = 64;

    CASPI::CArray x(N);

    for (size_t i = 0; i < N; ++i)
    {
        x[i] = CASPI::Complex(
            static_cast<double>(i % 7) - 3.0,
            static_cast<double>((i * 3 + 1) % 5) - 2.0);
    }

    CASPI::CArray ref = referenceDFT(x);

    CASPI::fft(x);

    EXPECT_LE(maxAbsDiff(x, ref), TOL);
}

// ============================================================================
// 6. Round Trip
// ============================================================================

TEST(FFT_Core, RoundTrip)
{
    const size_t N = 128;

    CASPI::CArray original(N);

    for (size_t i = 0; i < N; ++i)
    {
        original[i] = CASPI::Complex(
            std::sin(2.0 * CASPI::Constants::PI<double> * i / N),
            std::cos(3.0 * CASPI::Constants::PI<double> * i / N));
    }

    CASPI::CArray x = original;

    CASPI::fft(x);
    CASPI::ifft(x);

    EXPECT_LE(maxAbsDiff(x, original), TOL);
}

// ============================================================================
// 8. Spectral Content
// ============================================================================

TEST(FFT_Spectral, SingleTone)
{
    const size_t N  = 512;
    const size_t f0 = 16;

    CASPI::CArray x(N);

    for (size_t n = 0; n < N; ++n)
    {
        x[n] = CASPI::Complex(
            std::cos(2.0 * CASPI::Constants::PI<double> * f0 * n / N), 0.0);
    }

    CASPI::fft(x);

    auto mag = CASPI::getMagnitude(x);

    auto peak = std::max_element(mag.begin(), mag.begin() + N / 2);

    EXPECT_EQ(std::distance(mag.begin(), peak), f0);
    EXPECT_NEAR(mag[f0], static_cast<double>(N) / 2.0, 1e-6);
}

// ============================================================================
// 9. Parseval
// ============================================================================

TEST(FFT_Theory, Parseval)
{
    const size_t N = 128;

    CASPI::CArray x(N);

    for (size_t i = 0; i < N; ++i)
    {
        x[i] = CASPI::Complex(
            std::sin(i * 0.1),
            std::cos(i * 0.2));
    }

    double time_energy = 0.0;

    for (auto& v : x)
    {
        time_energy += std::norm(v);
    }

    CASPI::CArray X = x;
    CASPI::fft(X);

    double freq_energy = 0.0;

    for (auto& v : X)
    {
        freq_energy += std::norm(v);
    }

    freq_energy /= static_cast<double>(N);

    EXPECT_NEAR(time_energy, freq_energy, 1e-7);
}

// ============================================================================
// 10. Linearity
// ============================================================================

TEST(FFT_Theory, Linearity)
{
    const size_t N = 64;

    const CASPI::Complex a(2.5, -1.0);
    const CASPI::Complex b(-0.5, 3.0);

    CASPI::CArray x(N), y(N);

    for (size_t i = 0; i < N; ++i)
    {
        x[i] = CASPI::Complex(std::sin(i), std::cos(i));
        y[i] = CASPI::Complex(std::cos(i), std::sin(i));
    }

    CASPI::CArray lhs(N);

    for (size_t i = 0; i < N; ++i)
    {
        lhs[i] = a * x[i] + b * y[i];
    }

    CASPI::fft(lhs);

    CASPI::CArray Fx = x, Fy = y;

    CASPI::fft(Fx);
    CASPI::fft(Fy);

    CASPI::CArray rhs(N);

    for (size_t i = 0; i < N; ++i)
    {
        rhs[i] = a * Fx[i] + b * Fy[i];
    }

    EXPECT_LE(maxAbsDiff(lhs, rhs), TOL);
}
