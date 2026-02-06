#include "../../CASPI/maths/caspi_Maths.h"
#include <gtest/gtest.h>

TEST (MathsTest,MidiToHz_test)
{
    const std::vector<int> noteNums = {69, 25, 128};
    const std::vector<double>  results = {440.0,34.648,13289.75};
    for (int i = 0; i < noteNums.size(); i++)
    {
        auto result = CASPI::Maths::midiNoteToHz<double> (noteNums.at(i));
        ASSERT_NEAR (result, results.at(i),1e-3);
    }

}



// ============================================================================
// Bessel Function Tests
// ============================================================================

TEST(BesselFunctionTest, J0SpecialValues)
{


    // J_0(0) = 1
    EXPECT_NEAR(CASPI::Maths::besselJ(0, 0.0), 1.0, 1e-10);

    // J_0(2.4) ≈ 0 (first zero)
    EXPECT_NEAR(CASPI::Maths::besselJ(0, 2.4048), 0.0, 1e-3);

    // J_0(5.5) ≈ 0 (second zero)
    EXPECT_NEAR(CASPI::Maths::besselJ(0, 5.5201), 0.0, 1e-3);
}

TEST(BesselFunctionTest, J1SpecialValues)
{


    // J_1(0) = 0
    EXPECT_NEAR(CASPI::Maths::besselJ(1, 0.0), 0.0, 1e-10);

    // J_1(1) ≈ 0.4400505857
    EXPECT_NEAR(CASPI::Maths::besselJ(1, 1.0), 0.4400505857, 1e-6);
}

TEST(BesselFunctionTest, SymmetryProperty)
{


    // J_{-n}(x) = (-1)^n * J_n(x)

    double x = 2.5;

    // J_{-1}(x) = -J_1(x)
    EXPECT_NEAR(CASPI::Maths::besselJ(-1, x), -CASPI::Maths::besselJ(1, x), 1e-10);

    // J_{-2}(x) = J_2(x)
    EXPECT_NEAR(CASPI::Maths::besselJ(-2, x), CASPI::Maths::besselJ(2, x), 1e-10);

    // J_{-3}(x) = -J_3(x)
    EXPECT_NEAR(CASPI::Maths::besselJ(-3, x), -CASPI::Maths::besselJ(3, x), 1e-10);
}

TEST(BesselFunctionTest, RecurrenceRelation)
{


    // J_{n-1}(x) + J_{n+1}(x) = (2n/x) * J_n(x)

    double x = 3.0;
    int n = 2;

    double left = CASPI::Maths::besselJ(n - 1, x) + CASPI::Maths::besselJ(n + 1, x);
    double right = (2.0 * n / x) * CASPI::Maths::besselJ(n, x);

    EXPECT_NEAR(left, right, 1e-6);
}

TEST(BesselFunctionTest, PowerConservation)
{


    // Σ J_n²(x) = 1 for all x (power conservation in FM)

    double beta = 2.0;
    double sumPower = 0.0;

    // J_0²
    sumPower += CASPI::Maths::besselJ(0, beta) * CASPI::Maths::besselJ(0, beta);

    // 2 * Σ J_n² (factor of 2 for upper and lower sidebands)
    for (int n = 1; n <= 20; ++n)
    {
        double jn = CASPI::Maths::besselJ(n, beta);
        sumPower += 2.0 * jn * jn;
    }

    EXPECT_NEAR(sumPower, 1.0, 1e-3);
}

// ============================================================================
// Edge Cases and Robustness Tests
// ============================================================================
TEST(BesselFunctionTest, LargeArgument)
{


    // Bessel function should remain bounded for large arguments
    double j0_20 = CASPI::Maths::besselJ(0, 20.0);
    EXPECT_LE(std::abs(j0_20), 1.0);

    double j5_20 = CASPI::Maths::besselJ(5, 20.0);
    EXPECT_LE(std::abs(j5_20), 1.0);
}

TEST(BesselFunctionTest, NegativeArgument)
{


    // J_n(-x) = (-1)^n * J_n(x)
    double x = 2.5;

    EXPECT_NEAR(CASPI::Maths::besselJ(0, -x), CASPI::Maths::besselJ(0, x), 1e-10);   // n=0, even
    EXPECT_NEAR(CASPI::Maths::besselJ(1, -x), -CASPI::Maths::besselJ(1, x), 1e-10);  // n=1, odd
    EXPECT_NEAR(CASPI::Maths::besselJ(2, -x), CASPI::Maths::besselJ(2, x), 1e-10);   // n=2, even
}

TEST(BesselFunctionTest, ZeroArgument)
{
    EXPECT_DOUBLE_EQ(CASPI::Maths::besselJ(0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(CASPI::Maths::besselJ(1, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(CASPI::Maths::besselJ(5, 0.0), 0.0);
}

TEST(BesselFunctionTest, SmallXSeries)
{
    double x = 1e-5;
    EXPECT_NEAR(CASPI::Maths::besselJ(0, x), 1.0, 1e-10);
    EXPECT_NEAR(CASPI::Maths::besselJ(1, x), x/2.0, 1e-10);
    EXPECT_NEAR(CASPI::Maths::besselJ(2, x), (x*x)/8.0, 1e-10);
}

TEST(BesselFunctionTest, LargeXAsymptotic)
{
    double x = 50.0;
    double val = CASPI::Maths::besselJ(1, x);
    double expected = std::sqrt(2.0/(CASPI::Constants::PI<double>*x)) * std::cos(x - CASPI::Constants::PI<double>/2.0 - CASPI::Constants::PI<double>/4.0);
    EXPECT_NEAR(val, expected, 1e-10);
}

#if defined(CASPI_HAS_STD_BESSEL)
TEST(BesselFunctionTest, CompareStdBessel)
{
    for (int n = -3; n <= 3; ++n)
    {
        for (double x = 0.0; x <= 10.0; x += 0.5)
        {
            double myVal = besselJ(n, x);
            double stdVal = std::cyl_bessel_j(n, x);
            EXPECT_NEAR(myVal, stdVal, 1e-10) << "n=" << n << " x=" << x;
        }
    }
}
#endif

