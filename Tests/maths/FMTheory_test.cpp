#include "analysis/caspi_FMTheory.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace CASPI;

constexpr double sampleRate = 48000.0;
constexpr double tolerance = 1e-6;

// ============================================================================
// Bessel Function Tests
// ============================================================================

TEST(BesselFunctionTest, J0SpecialValues)
{


    // J_0(0) = 1
    EXPECT_NEAR(FMTheory::besselJ(0, 0.0), 1.0, 1e-10);

    // J_0(2.4) ≈ 0 (first zero)
    EXPECT_NEAR(FMTheory::besselJ(0, 2.4048), 0.0, 1e-3);

    // J_0(5.5) ≈ 0 (second zero)
    EXPECT_NEAR(FMTheory::besselJ(0, 5.5201), 0.0, 1e-3);
}

TEST(BesselFunctionTest, J1SpecialValues)
{


    // J_1(0) = 0
    EXPECT_NEAR(FMTheory::besselJ(1, 0.0), 0.0, 1e-10);

    // J_1(1) ≈ 0.4400505857
    EXPECT_NEAR(FMTheory::besselJ(1, 1.0), 0.4400505857, 1e-6);
}

TEST(BesselFunctionTest, SymmetryProperty)
{


    // J_{-n}(x) = (-1)^n * J_n(x)

    double x = 2.5;

    // J_{-1}(x) = -J_1(x)
    EXPECT_NEAR(FMTheory::besselJ(-1, x), -FMTheory::besselJ(1, x), 1e-10);

    // J_{-2}(x) = J_2(x)
    EXPECT_NEAR(FMTheory::besselJ(-2, x), FMTheory::besselJ(2, x), 1e-10);

    // J_{-3}(x) = -J_3(x)
    EXPECT_NEAR(FMTheory::besselJ(-3, x), -FMTheory::besselJ(3, x), 1e-10);
}

TEST(BesselFunctionTest, RecurrenceRelation)
{


    // J_{n-1}(x) + J_{n+1}(x) = (2n/x) * J_n(x)

    double x = 3.0;
    int n = 2;

    double left = FMTheory::besselJ(n - 1, x) + FMTheory::besselJ(n + 1, x);
    double right = (2.0 * n / x) * FMTheory::besselJ(n, x);

    EXPECT_NEAR(left, right, 1e-6);
}

TEST(BesselFunctionTest, PowerConservation)
{


    // Σ J_n²(x) = 1 for all x (power conservation in FM)

    double beta = 2.0;
    double sumPower = 0.0;

    // J_0²
    sumPower += FMTheory::besselJ(0, beta) * FMTheory::besselJ(0, beta);

    // 2 * Σ J_n² (factor of 2 for upper and lower sidebands)
    for (int n = 1; n <= 20; ++n)
    {
        double jn = FMTheory::besselJ(n, beta);
        sumPower += 2.0 * jn * jn;
    }

    EXPECT_NEAR(sumPower, 1.0, 1e-3);
}

// ============================================================================
// FM Theory Tests
// ============================================================================

TEST(FMTheoryTest, PredictSidebandAmplitudes)
{


    double beta = 2.0;
    auto amplitudes = FMTheory::predictSidebandAmplitudes(beta, 5);

    // Should have carrier + 5 sidebands = 6 values
    EXPECT_EQ(amplitudes.size(), 6);

    // Verify against known Bessel values
    EXPECT_NEAR(amplitudes[0], 0.2239, 1e-3);  // J_0(2)
    EXPECT_NEAR(amplitudes[1], 0.5767, 1e-3);  // J_1(2)
    EXPECT_NEAR(amplitudes[2], 0.3528, 1e-3);  // J_2(2)
}

TEST(FMTheoryTest, CarrierNullDetection)
{


    // Should detect carrier nulls
    EXPECT_TRUE(FMTheory::isCarrierNull(2.4, 0.1));
    EXPECT_TRUE(FMTheory::isCarrierNull(5.5, 0.1));

    // Should not detect non-nulls
    EXPECT_FALSE(FMTheory::isCarrierNull(1.0, 0.1));
    EXPECT_FALSE(FMTheory::isCarrierNull(3.0, 0.1));
}

TEST(FMTheoryTest, PredictSignificantSidebands)
{


    // Low β should have few sidebands
    EXPECT_LE(FMTheory::predictSignificantSidebands(0.5), 3);

    // Medium β should have more sidebands
    int count2 = FMTheory::predictSignificantSidebands(2.0);
    EXPECT_GE(count2, 3);
    EXPECT_LE(count2, 6);

    // High β should have many sidebands
    EXPECT_GE(FMTheory::predictSignificantSidebands(5.0), 8);
}

TEST(FMTheoryTest, PowerDistribution)
{


    double beta = 1.0;
    auto power = FMTheory::predictPowerDistribution(beta, 10);

    // Total power should be ≈1 (accounting for upper and lower sidebands)
    double totalPower = power[0];  // Carrier
    for (size_t n = 1; n < power.size(); ++n)
    {
        totalPower += 2.0 * power[n];  // Upper and lower sidebands
    }

    EXPECT_NEAR(totalPower, 1.0, 1e-3);
}

// ============================================================================
// Edge Cases and Robustness Tests
// ============================================================================
TEST(BesselFunctionTest, LargeArgument)
{


    // Bessel function should remain bounded for large arguments
    double j0_20 = FMTheory::besselJ(0, 20.0);
    EXPECT_LE(std::abs(j0_20), 1.0);

    double j5_20 = FMTheory::besselJ(5, 20.0);
    EXPECT_LE(std::abs(j5_20), 1.0);
}

TEST(BesselFunctionTest, NegativeArgument)
{


    // J_n(-x) = (-1)^n * J_n(x)
    double x = 2.5;

    EXPECT_NEAR(FMTheory::besselJ(0, -x), FMTheory::besselJ(0, x), 1e-10);   // n=0, even
    EXPECT_NEAR(FMTheory::besselJ(1, -x), -FMTheory::besselJ(1, x), 1e-10);  // n=1, odd
    EXPECT_NEAR(FMTheory::besselJ(2, -x), FMTheory::besselJ(2, x), 1e-10);   // n=2, even
}

