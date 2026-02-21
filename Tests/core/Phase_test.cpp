// test_caspi_Phase.cpp
#include "core/caspi_Phase.h"
#include "base/caspi_Constants.h"
#include <cmath>
#include <gtest/gtest.h>

using CASPI::Phase;

constexpr double tolerance  = 1e-8;
constexpr double kWrapLimit = 1.0;

TEST (PhaseTest, PhaseIncrementsAndWraps)
{
    Phase<double> p;
    p.increment = 0.25; // Set increment directly (was frequency/sampleRate)
    EXPECT_NEAR (p.advanceAndWrap (kWrapLimit), 0.0, tolerance);
    EXPECT_NEAR (p.phase, 0.25, tolerance);
    p.advanceAndWrap (kWrapLimit); // phase = 0.5
    p.advanceAndWrap (kWrapLimit); // phase = 0.75
    p.advanceAndWrap (kWrapLimit); // phase = 1.0 -> wraps to 0
    EXPECT_NEAR (p.phase, 0.0, tolerance);
}

TEST (PhaseTest, ResetPhase)
{
    Phase<double> p;
    p.increment = 0.5;
    p.advanceAndWrap (kWrapLimit);
    p.resetPhase();
    EXPECT_NEAR (p.phase, 0.0, tolerance);
}

TEST (PhaseTest, DifferentWrapLimits)
{
    Phase<double> p;
    p.increment = 1.0; // Set increment to 1.0

    // Test with 2π wrap limit (common for sine waves)
    EXPECT_NEAR (p.advanceAndWrap (CASPI::Constants::TWO_PI<double>), 0.0, tolerance);
    EXPECT_NEAR (p.phase, 1.0, tolerance);

    // Test with 1.0 wrap limit (common for other waveforms)
    p.resetPhase();
    p.increment = 0.5;
    EXPECT_NEAR (p.advanceAndWrap (1.0), 0.0, tolerance);
    EXPECT_NEAR (p.phase, 0.5, tolerance);
}

TEST (PhaseTest, PhaseAccumulatorWorks)
{
    Phase<double> p;
    p.increment = 0.125; // Use 1/8 which is exactly representable in binary

    // Check multiple advances - test wrapping behavior specifically
    // After 8 calls, phase should wrap back to near 0
    for (int i = 0; i < 8; ++i)
    {
        double expectedPhase  = (i + 1) * 0.125;
        double expectedReturn = i * 0.125;

        if (expectedPhase >= 1.0)
        {
            expectedPhase = std::fmod (expectedPhase, 1.0);
        }

        EXPECT_NEAR (p.advanceAndWrap (1.0), expectedReturn, tolerance);
        EXPECT_NEAR (p.phase, expectedPhase, 1e-15); // Tight tolerance for exact binary fractions
    }

    // Test that after full cycle, we're back near zero
    EXPECT_NEAR (p.phase, 0.0, tolerance);
}

TEST (PhaseTest, ZeroIncrement)
{
    Phase<double> p;
    p.increment = 0.0;

    // Phase should not advance
    EXPECT_NEAR (p.advanceAndWrap (1.0), 0.0, tolerance);
    EXPECT_NEAR (p.phase, 0.0, tolerance);

    p.phase = 0.5;
    EXPECT_NEAR (p.advanceAndWrap (1.0), 0.5, tolerance);
    EXPECT_NEAR (p.phase, 0.5, tolerance);
}