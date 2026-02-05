#include "analysis/caspi_FMTheory.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace CASPI;

constexpr double sampleRate = 48000.0;
constexpr double tolerance = 1e-6;

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

