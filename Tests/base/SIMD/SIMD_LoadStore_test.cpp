
#include "SIMD_test_helpers.h"
#include <gtest/gtest.h>
// ============================================================================
// FLOAT32X4 TESTS
// ============================================================================

TEST(SIMD_float32x4, ConstructionScalars)
{
    CASPI::SIMD::float32x4 v = {1.f, 2.f, 3.f, 4.};

    float out[4];
    CASPI::SIMD::store (out, v);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.f);
    EXPECT_FLOAT_EQ(out[3], 4.f);
}

TEST(SIMD_float32x4, LoadStore)
{
    float arr[4]             = { 5.f, 6.f, 7.f, 8.f };
    CASPI::SIMD::float32x4 v = {5.f, 6.f, 7.f, 8.f};

    float out[4];
    CASPI::SIMD::store (out, v);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR(out[i], arr[i], EPSILON_F32);
    }
}



TEST(SIMD_float64x2, ConstructionScalars)
{
    CASPI::SIMD::float64x2 v = {1.0, 2.0};

    double out[2];
    CASPI::SIMD::store (out, v);
    EXPECT_EQ(out[0], 1.0);
    EXPECT_EQ(out[1], 2.0);
}

TEST (SIMD_float64x2, LoadStore)
{
    double arr[2]            = { 5.5, 6.6 };
    CASPI::SIMD::float64x2 v   = CASPI::SIMD::load (arr);

    double out[2];
    CASPI::SIMD::store (out, v);

    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], arr[i], EPSILON_F64);
    }
}


TEST(SIMD_Correctness, LoadStoreRoundtrip) {
    // Verify load/store preserve data
    alignas(16) float original[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    alignas (16) float roundtrip[4];

    CASPI::SIMD::float32x4 v = CASPI::SIMD::load_aligned<float> (original);
    CASPI::SIMD::store_aligned (roundtrip, v);

    for (int i = 0; i < 4; i++) {
        EXPECT_FLOAT_EQ(original[i], roundtrip[i]);
    }
}