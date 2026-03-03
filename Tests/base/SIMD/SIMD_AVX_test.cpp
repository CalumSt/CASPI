#include "SIMD_test_helpers.h"
#include <gtest/gtest.h>

using namespace CASPI::SIMD;

// ============================================================================
// AVX TESTS (256-bit vectors)
// ============================================================================

#if defined(CASPI_HAS_AVX)

TEST(SIMD_float32x8, ConstructionScalars) {
    float32x8 v = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], float(i + 1));
    }
}

TEST(SIMD_float32x8, LoadStore) {
    alignas(32) float arr[8] = {8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
    float32x8 v = {8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], arr[i]);
    }
}

TEST(SIMD_float32x8, Broadcast) {
    float32x8 v = set1 (3.14159f);
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_NEAR(out[i], 3.14159f, EPSILON_F32);
    }
}

TEST(SIMD_float32x8, Arithmetic) {
    float32x8 a = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    float32x8 b = {8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
    float32x8 sum = addx8(a, b);
    alignas(32) float out[8];
    storex8(out, sum);
    float expected[8] = {9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f};
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], expected[i]);
    }
}

TEST(SIMD_float64x4, ConstructionScalars) {
    float64x4 v = {(1.0, 2.0, 3.0, 4.0};

    alignas(32) double out[4];
    storex4(out, v);

    EXPECT_DOUBLE_EQ(out[0], 1.0);
    EXPECT_DOUBLE_EQ(out[1], 2.0);
    EXPECT_DOUBLE_EQ(out[2], 3.0);
    EXPECT_DOUBLE_EQ(out[3], 4.0);
}

TEST(SIMD_float64x4, LoadStore) {
    alignas(32) double arr[4] = {5.5, 6.6, 7.7, 8.8};
    float64x4 v = loadx4(arr);

    alignas(32) double out[4];
    storex4(out, v);

    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], arr[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x4, Broadcast) {
    float64x4 v = set1x4(3.14159);

    alignas(32) double out[4];
    storex4(out, v);

    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], 3.14159, EPSILON_F64);
    }
}

TEST(SIMD_float64x4, Arithmetic) {
    float64x4 a = {10.0, 20.0, 30.0, 40.0};
    float64x4 b = {2.0, 4.0, 6.0, 8.0};

    float64x4 sum = addx4(a, b);
    float64x4 diff = subx4(a, b);
    float64x4 prod = mulx4(a, b);
    float64x4 quot = divx4(a, b);

    double expected_sum[4] = {12.0, 24.0, 36.0, 48.0};
    double expected_diff[4] = {8.0, 16.0, 24.0, 32.0};
    double expected_prod[4] = {20.0, 80.0, 180.0, 320.0};
    double expected_quot[4] = {5.0, 5.0, 5.0, 5.0};

    alignas(32) double out[4];

    storex4(out, sum);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_sum[i], EPSILON_F64);
    }

    storex4(out, diff);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_diff[i], EPSILON_F64);
    }

    storex4(out, prod);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_prod[i], EPSILON_F64);
    }

    storex4(out, quot);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_quot[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x4, LargeAudioBuffer) {
    // Process a larger buffer with AVX
    const int numSamples = 1024;
    alignas(32) double input[numSamples];
    alignas(32) double output[numSamples];

    // Initialize
    for (int i = 0; i < numSamples; i++) {
        input[i] = std::sin(2.0 * CASPI::Constants::PI<float> * i / 100.0);
    }

    // Process 4 samples at a time
    const double gain = 0.75;
    float64x4 gainVec = set1x4(gain);

    for (int i = 0; i < numSamples; i += 4) {
        float64x4 samples = loadx4(&input[i]);
        samples = mulx4(samples, gainVec);
        storex4(&output[i], samples);
    }

    // Verify
    for (int i = 0; i < numSamples; i++) {
        EXPECT_NEAR(output[i], input[i] * gain, EPSILON_F64);
    }
}

#endif // CASPI_HAS_AVX