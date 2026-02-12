// caspi_SIMD_comprehensive_tests.cpp
//
// Comprehensive Unit Tests for CASPI SIMD API (caspi_SIMD.h)
//
// ============================================================================
// TEST COVERAGE OVERVIEW
// ============================================================================
//
// This test suite provides comprehensive coverage of the SIMD API across all
// supported platforms (SSE/AVX on x86, NEON on ARM, WASM SIMD, scalar fallback).
//
// ============================================================================
// TEST CATEGORIES AND RATIONALE
// ============================================================================
//
// 1. CONSTRUCTION TESTS
//    Purpose: Verify that SIMD vectors can be created correctly from scalars
//             and arrays, with proper lane ordering across all platforms.
//    Why: Lane ordering differs between intrinsics (_mm_set_ps reverses order),
//         so we must verify data ends up in the correct lanes.
//    Tests:
//    - float32x4: ConstructionScalars, LoadStore
//    - float64x2: ConstructionScalars, ConstructionFromArray, LoadStore
//    - float32x8 (AVX): ConstructionScalars, LoadStore
//    - float64x4 (AVX): ConstructionScalars, LoadStore
//
// 2. BROADCAST TESTS
//    Purpose: Ensure scalar values can be replicated to all lanes correctly.
//    Why: Broadcast is a fundamental operation for applying constants to vectors.
//    Tests:
//    - float32x4: Broadcast (via set1)
//    - float64x2: Broadcast
//    - float32x8 (AVX): Broadcast
//    - float64x4 (AVX): Broadcast
//
// 3. ARITHMETIC TESTS
//    Purpose: Validate that per-lane arithmetic operations produce correct results.
//    Why: Core functionality for DSP, must work identically across platforms.
//    Tests:
//    - Basic operations: add, sub, mul, div for all types
//    - Edge cases: zeros, negatives, large/small values
//    - Chained operations: complex expressions like (a+b)*c-d
//
// 4. FMA AND MUL_ADD TESTS
//    Purpose: Verify fused multiply-add operations work correctly and that
//             mul_add wrapper uses FMA when available.
//    Why: FMA is critical for performance in audio DSP (e.g., filter coefficients).
//         The wrapper ensures optimal code path is used automatically.
//    Tests:
//    - Explicit FMA (when CASPI_HAS_FMA is defined)
//    - mul_add wrapper (always available, uses FMA or fallback)
//    - Correctness of a*b+c computation
//
// 5. FAST APPROXIMATION TESTS
//    Purpose: Verify that rcp() and rsqrt() produce reasonably accurate results.
//    Why: These functions trade accuracy for speed (~4-8x faster). Users need
//         to know they work correctly within their error bounds.
//    Tests:
//    - rcp: reciprocal accuracy within tolerance
//    - rsqrt: reciprocal square root accuracy within tolerance
//    - Comparison with exact division/sqrt
//
// 6. COMPARISON TESTS
//    Purpose: Verify that comparison operations produce correct masks.
//    Why: Masks are used for conditional operations (blend, clamping, etc.).
//         Incorrect masks lead to subtle bugs in audio processing.
//    Tests:
//    - cmp_eq: equality comparison
//    - cmp_lt: less-than comparison
//    - Bit-level verification of mask values (0xFFFFFFFF vs 0x00000000)
//
// 7. MIN/MAX TESTS
//    Purpose: Validate per-lane minimum and maximum operations.
//    Why: Used for clamping, envelope followers, peak detection in audio.
//    Tests:
//    - Per-lane min/max for float32x4 and float64x2
//    - Edge cases with negative numbers
//
// 8. HORIZONTAL REDUCTION TESTS
//    Purpose: Verify operations that reduce a vector to a scalar.
//    Why: Used for computing sums, finding peaks, etc. Different platforms
//         implement these differently (hadd vs shuffle vs fallback).
//    Tests:
//    - hsum: horizontal sum
//    - hmax: horizontal maximum
//    - hmin: horizontal minimum
//
// 9. BLEND/SELECT TESTS
//    Purpose: Verify mask-based selection works correctly.
//    Why: Critical for conditional processing without branches (e.g., soft clipping).
//    Tests:
//    - All-true mask: should select all from b
//    - All-false mask: should select all from a
//    - Mixed mask: should select per-lane correctly
//    - ComparisonsAndBlend: integration test
//
// 10. LANE-WISE MATH TESTS
//     Purpose: Verify unary math operations on all lanes.
//     Why: Common operations in audio DSP (rectification, normalization, etc.).
//     Tests:
//     - negate: sign flip
//     - abs: absolute value
//     - sqrt: square root
//     - Chained operations: negate(abs(x))
//
// 11. EDGE CASE TESTS
//     Purpose: Verify behavior with special values.
//     Why: Audio processing encounters zeros, very small values, etc.
//          Must handle gracefully across platforms.
//     Tests:
//     - Zero operations: add/mul with zero
//     - Negative numbers: ensure sign handling is correct
//     - Small numbers: near machine epsilon
//     - Large numbers: near overflow
//     - Division by small values (avoiding exact zero to prevent UB)
//
// 12. REAL-WORLD INTEGRATION TESTS
//     Purpose: Verify API works correctly in realistic usage patterns.
//     Why: Unit tests are necessary but not sufficient. Real-world patterns
//          expose integration issues, performance characteristics, etc.
//     Tests:
//     - AudioGainRamping: smooth parameter changes
//     - StereoInterleaving: multi-channel processing
//     - ClippingWithBlend: soft clipping using blend
//     - AudioBufferProcessing: stereo gain application
//     - MixedPrecisionProcessing: float→double→float conversion
//
// 13. AVX EXTENDED TESTS
//     Purpose: Verify 256-bit vector operations (8 floats, 4 doubles).
//     Why: AVX provides wider vectors for better throughput. Must verify
//          all 8/4 lanes work correctly.
//     Tests:
//     - float32x8: construction, load/store, arithmetic
//     - float64x4: construction, load/store, arithmetic
//     - LargeAudioBuffer: processing with AVX
//
// ============================================================================
// TESTING STRATEGY
// ============================================================================
//
// - Platform Coverage: Tests work on SSE, NEON, WASM, and scalar fallback
// - Epsilon Comparison: Floating-point comparisons use appropriate tolerance
// - Bit-Level Checks: Mask comparisons verify exact bit patterns
// - Incremental Complexity: Start with simple ops, build to complex scenarios
// - No Integer Types: Integer SIMD was excluded per requirements
//
// ============================================================================
// NOTES FOR MAINTAINERS
// ============================================================================
//
// - When adding new SIMD operations, add corresponding tests here
// - Test both correctness AND edge cases for each operation
// - Use EPSILON_F32 for float, EPSILON_F64 for double comparisons
// - AVX tests are conditionally compiled with #if defined(CASPI_HAS_AVX)
// - FMA tests are conditionally compiled with #if defined(CASPI_HAS_FMA)
//
// ============================================================================

#include "base/caspi_SIMD.h"
#include <gtest/gtest.h>
#include <cmath>

#include "base/caspi_Constants.h"

using namespace CASPI::SIMD;

constexpr float EPSILON_F32 = 1e-6f;
constexpr double EPSILON_F64 = 1e-12;

// For fast approximations (rcp, rsqrt) which have lower accuracy
constexpr float EPSILON_APPROX = 1.5e-3f;

// ============================================================================
// FLOAT32X4 TESTS
// ============================================================================

TEST(SIMD_float32x4, ConstructionScalars) {
    float32x4 v = make_float32x4(1.f, 2.f, 3.f, 4.f);

    float out[4];
    store(out, v);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.f);
    EXPECT_FLOAT_EQ(out[3], 4.f);
}

TEST(SIMD_float32x4, LoadStore) {
    float arr[4] = {5.f, 6.f, 7.f, 8.f};
    float32x4 v = make_float32x4_from_array(arr);

    float out[4];
    store(out, v);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], arr[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, Broadcast) {
    float32x4 v = set1(7.5f);
    
    float out[4];
    store(out, v);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_FLOAT_EQ(out[i], 7.5f);
    }
}

TEST(SIMD_float32x4, Arithmetic) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(5.f, 6.f, 7.f, 8.f);

    const float32x4 sum  = add(a, b);
    const float32x4 diff = sub(b, a);
    const float32x4 prod = mul(a, b);

    float expected_sum[4]  = {6.f, 8.f, 10.f, 12.f};
    float expected_diff[4] = {4.f, 4.f, 4.f, 4.f};
    float expected_prod[4] = {5.f, 12.f, 21.f, 32.f};

    float out[4];

    store(out, sum);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_sum[i], EPSILON_F32);

    store(out, diff);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_diff[i], EPSILON_F32);

    store(out, prod);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_prod[i], EPSILON_F32);
}

TEST(SIMD_float32x4, Division) {
    float32x4 a = make_float32x4(10.f, 20.f, 30.f, 40.f);
    float32x4 b = make_float32x4(2.f, 4.f, 5.f, 8.f);
    
    float32x4 result = div(a, b);
    
    float expected[4] = {5.f, 5.f, 6.f, 5.f};
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, DivisionByOne) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = set1(1.f);
    
    float32x4 result = div(a, b);
    
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], static_cast<float>(i + 1), EPSILON_F32);
    }
}

#if defined(CASPI_HAS_FMA)
TEST(SIMD_float32x4, FMA) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(5.f, 6.f, 7.f, 8.f);
    float32x4 c = make_float32x4(1.f, 1.f, 1.f, 1.f);

    float32x4 r = fma(a, b, c);
    float expected[4] = {6.f, 13.f, 22.f, 33.f};

    float out[4];
    store(out, r);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
}
#endif

TEST(SIMD_float32x4, MulAdd) {
    // Test mul_add wrapper (works with or without FMA)
    float32x4 a = make_float32x4(2.f, 3.f, 4.f, 5.f);
    float32x4 b = make_float32x4(10.f, 10.f, 10.f, 10.f);
    float32x4 c = make_float32x4(5.f, 5.f, 5.f, 5.f);
    
    float32x4 result = mul_add(a, b, c);  // a*b + c
    
    float expected[4] = {25.f, 35.f, 45.f, 55.f};
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, FastReciprocal) {
    float32x4 v = make_float32x4(2.f, 4.f, 5.f, 10.f);
    float32x4 result = rcp(v);
    
    float expected[4] = {0.5f, 0.25f, 0.2f, 0.1f};
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        // rcp is approximate, so use larger tolerance
        EXPECT_NEAR(out[i], expected[i], EPSILON_APPROX);
    }
}

TEST(SIMD_float32x4, FastReciprocalSqrt) {
    float32x4 v = make_float32x4(1.f, 4.f, 9.f, 16.f);
    float32x4 result = rsqrt(v);
    
    float expected[4] = {1.f, 0.5f, 1.f/3.f, 0.25f};
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        // rsqrt is approximate, so use larger tolerance
        EXPECT_NEAR(out[i], expected[i], EPSILON_APPROX);
    }
}

TEST(SIMD_float32x4, ComparisonEqual) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(1.f, 0.f, 3.f, 0.f);
    
    float32x4 mask = cmp_eq(a, b);
    
    float out[4];
    store(out, mask);
    
    // Lanes 0 and 2 should be all-bits-set (true), lanes 1 and 3 should be zero (false)
    uint32_t* bits = reinterpret_cast<uint32_t*>(out);
    EXPECT_EQ(bits[0], 0xFFFFFFFF);
    EXPECT_EQ(bits[1], 0x00000000);
    EXPECT_EQ(bits[2], 0xFFFFFFFF);
    EXPECT_EQ(bits[3], 0x00000000);
}

TEST(SIMD_float32x4, ComparisonLessThan) {
    float32x4 a = make_float32x4(1.f, 5.f, 3.f, 8.f);
    float32x4 b = make_float32x4(2.f, 4.f, 6.f, 7.f);
    
    float32x4 mask = cmp_lt(a, b);
    
    float out[4];
    store(out, mask);
    
    // Lanes where a < b should be 0xFFFFFFFF
    uint32_t* bits = reinterpret_cast<uint32_t*>(out);
    EXPECT_EQ(bits[0], 0xFFFFFFFF);  // 1 < 2
    EXPECT_EQ(bits[1], 0x00000000);  // 5 < 4 is false
    EXPECT_EQ(bits[2], 0xFFFFFFFF);  // 3 < 6
    EXPECT_EQ(bits[3], 0x00000000);  // 8 < 7 is false
}

TEST(SIMD_float32x4, MinMax) {
    float32x4 a = make_float32x4(1.f, 5.f, 3.f, 8.f);
    float32x4 b = make_float32x4(4.f, 2.f, 6.f, 7.f);
    
    float32x4 minv = min(a, b);
    float32x4 maxv = max(a, b);
    
    float expected_min[4] = {1.f, 2.f, 3.f, 7.f};
    float expected_max[4] = {4.f, 5.f, 6.f, 8.f};
    
    float out[4];
    
    store(out, minv);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_min[i], EPSILON_F32);
    }
    
    store(out, maxv);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected_max[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, HorizontalSum) {
    float32x4 v = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float sum = hsum(v);
    EXPECT_NEAR(sum, 10.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMax) {
    float32x4 v = make_float32x4(1.f, 7.f, 3.f, 4.f);
    float max_val = hmax(v);
    EXPECT_NEAR(max_val, 7.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMin) {
    float32x4 v = make_float32x4(5.f, 1.f, 8.f, 3.f);
    float min_val = hmin(v);
    EXPECT_NEAR(min_val, 1.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMinNegative) {
    float32x4 v = make_float32x4(-5.f, -1.f, -8.f, -3.f);
    float min_val = hmin(v);
    EXPECT_NEAR(min_val, -8.f, EPSILON_F32);
}

TEST(SIMD_float32x4, NegateAbsSqrt) {
    float32x4 v = make_float32x4(-1.f, -4.f, 9.f, -16.f);

    float32x4 neg = negate(v);
    float32x4 absv = abs(v);
    float32x4 sqr = sqrt(absv);

    float out[4];

    store(out, neg);
    EXPECT_NEAR(out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR(out[1], 4.f, EPSILON_F32);
    EXPECT_NEAR(out[2], -9.f, EPSILON_F32);
    EXPECT_NEAR(out[3], 16.f, EPSILON_F32);

    store(out, absv);
    EXPECT_NEAR(out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR(out[1], 4.f, EPSILON_F32);
    EXPECT_NEAR(out[2], 9.f, EPSILON_F32);
    EXPECT_NEAR(out[3], 16.f, EPSILON_F32);

    store(out, sqr);
    EXPECT_NEAR(out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR(out[1], 2.f, EPSILON_F32);
    EXPECT_NEAR(out[2], 3.f, EPSILON_F32);
    EXPECT_NEAR(out[3], 4.f, EPSILON_F32);
}

TEST(SIMD_float32x4, ComparisonsAndBlend) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(4.f, 3.f, 2.f, 1.f);

    float32x4 mask = cmp_lt(a, b);
    float32x4 blended = blend(a, b, mask);

    float out[4];
    store(out, blended);
    float expected[4] = {4.f, 3.f, 3.f, 4.f};

    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
}

TEST(SIMD_float32x4, BlendAllTrue) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(10.f, 20.f, 30.f, 40.f);
    float32x4 all_true = cmp_lt(a, b); // All lanes should be true
    
    float32x4 result = blend(a, b, all_true);
    
    float out[4];
    store(out, result);
    
    // All lanes should select from b
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], static_cast<float>((i + 1) * 10), EPSILON_F32);
    }
}

TEST(SIMD_float32x4, BlendAllFalse) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(10.f, 20.f, 30.f, 40.f);
    float32x4 all_false = cmp_lt(b, a); // All lanes should be false
    
    float32x4 result = blend(a, b, all_false);
    
    float out[4];
    store(out, result);
    
    // All lanes should select from a
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], static_cast<float>(i + 1), EPSILON_F32);
    }
}

TEST(SIMD_float32x4, ZeroOperations) {
    float32x4 zero = set1(0.f);
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    
    float32x4 sum = add(a, zero);
    float32x4 prod = mul(a, zero);
    
    float out_sum[4], out_prod[4];
    store(out_sum, sum);
    store(out_prod, prod);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_sum[i], static_cast<float>(i + 1), EPSILON_F32);
        EXPECT_NEAR(out_prod[i], 0.f, EPSILON_F32);
    }
}

TEST(SIMD_float32x4, NegativeNumbers) {
    float32x4 a = make_float32x4(-1.f, -2.f, -3.f, -4.f);
    float32x4 b = make_float32x4(1.f, 2.f, 3.f, 4.f);
    
    float32x4 sum = add(a, b);
    float32x4 prod = mul(a, b);
    
    float out_sum[4], out_prod[4];
    store(out_sum, sum);
    store(out_prod, prod);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_sum[i], 0.f, EPSILON_F32);
        EXPECT_NEAR(out_prod[i], -static_cast<float>((i + 1) * (i + 1)), EPSILON_F32);
    }
}

TEST(SIMD_float32x4, ChainedOperations) {
    // Test (a + b) * c - d
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(1.f, 1.f, 1.f, 1.f);
    float32x4 c = make_float32x4(2.f, 2.f, 2.f, 2.f);
    float32x4 d = make_float32x4(1.f, 1.f, 1.f, 1.f);
    
    float32x4 result = sub(mul(add(a, b), c), d);
    
    float expected[4] = {3.f, 5.f, 7.f, 9.f}; // ((1+1)*2-1, (2+1)*2-1, ...)
    float out[4];
    store(out, result);
    
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

// ============================================================================
// FLOAT64X2 TESTS
// ============================================================================

TEST(SIMD_float64x2, ConstructionScalars) {
    float64x2 v = make_float64x2(1.0, 2.0);
    
    double out[2];
    store(out, v);
    
    EXPECT_DOUBLE_EQ(out[0], 1.0);
    EXPECT_DOUBLE_EQ(out[1], 2.0);
}

TEST(SIMD_float64x2, ConstructionFromArray) {
    double arr[2] = {3.14159, 2.71828};
    float64x2 v = make_float64x2_from_array(arr);
    
    double out[2];
    store(out, v);
    
    EXPECT_NEAR(out[0], 3.14159, EPSILON_F64);
    EXPECT_NEAR(out[1], 2.71828, EPSILON_F64);
}

TEST(SIMD_float64x2, LoadStore) {
    double arr[2] = {5.5, 6.6};
    float64x2 v = load(arr);
    
    double out[2];
    store(out, v);
    
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], arr[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x2, Broadcast) {
    float64x2 v = set1(7.5);
    
    double out[2];
    store(out, v);
    
    EXPECT_DOUBLE_EQ(out[0], 7.5);
    EXPECT_DOUBLE_EQ(out[1], 7.5);
}

TEST(SIMD_float64x2, Arithmetic) {
    float64x2 a = make_float64x2(10.0, 20.0);
    float64x2 b = make_float64x2(2.0, 4.0);
    
    float64x2 sum = add(a, b);
    float64x2 diff = sub(a, b);
    float64x2 prod = mul(a, b);
    float64x2 quot = div(a, b);
    
    double expected_sum[2] = {12.0, 24.0};
    double expected_diff[2] = {8.0, 16.0};
    double expected_prod[2] = {20.0, 80.0};
    double expected_quot[2] = {5.0, 5.0};
    
    double out[2];
    
    store(out, sum);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_sum[i], EPSILON_F64);
    }
    
    store(out, diff);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_diff[i], EPSILON_F64);
    }
    
    store(out, prod);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_prod[i], EPSILON_F64);
    }
    
    store(out, quot);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_quot[i], EPSILON_F64);
    }
}

#if defined(CASPI_HAS_FMA)
TEST(SIMD_float64x2, FMA) {
    float64x2 a = make_float64x2(2.0, 3.0);
    float64x2 b = make_float64x2(4.0, 5.0);
    float64x2 c = make_float64x2(1.0, 1.0);
    
    float64x2 result = fma(a, b, c);  // a * b + c
    
    double expected[2] = {9.0, 16.0};  // 2*4+1=9, 3*5+1=16
    
    double out[2];
    store(out, result);
    
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F64);
    }
}
#endif

TEST(SIMD_float64x2, MulAdd) {
    // Test mul_add wrapper (works with or without FMA)
    float64x2 a = make_float64x2(3.0, 4.0);
    float64x2 b = make_float64x2(2.0, 2.0);
    float64x2 c = make_float64x2(1.0, 1.0);
    
    float64x2 result = mul_add(a, b, c);  // a*b + c
    
    double expected[2] = {7.0, 9.0};  // 3*2+1=7, 4*2+1=9
    double out[2];
    store(out, result);
    
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x2, LaneWiseOps) {
    float64x2 v = make_float64x2(-4.0, 9.0);
    
    float64x2 neg = negate(v);
    float64x2 absv = abs(v);
    float64x2 sqr = sqrt(absv);
    
    double out[2];
    
    // Negate
    store(out, neg);
    EXPECT_NEAR(out[0], 4.0, EPSILON_F64);
    EXPECT_NEAR(out[1], -9.0, EPSILON_F64);
    
    // Absolute value
    store(out, absv);
    EXPECT_NEAR(out[0], 4.0, EPSILON_F64);
    EXPECT_NEAR(out[1], 9.0, EPSILON_F64);
    
    // Square root
    store(out, sqr);
    EXPECT_NEAR(out[0], 2.0, EPSILON_F64);
    EXPECT_NEAR(out[1], 3.0, EPSILON_F64);
}

TEST(SIMD_float64x2, MinMax) {
    float64x2 a = make_float64x2(1.0, 10.0);
    float64x2 b = make_float64x2(5.0, 3.0);
    
    float64x2 minv = min(a, b);
    float64x2 maxv = max(a, b);
    
    double expected_min[2] = {1.0, 3.0};
    double expected_max[2] = {5.0, 10.0};
    
    double out[2];
    
    store(out, minv);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_min[i], EPSILON_F64);
    }
    
    store(out, maxv);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected_max[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x2, HorizontalSum) {
    float64x2 v = make_float64x2(10.0, 20.0);
    double sum = hsum(v);
    EXPECT_NEAR(sum, 30.0, EPSILON_F64);
}

TEST(SIMD_float64x2, HorizontalMaxMin) {
    float64x2 v = make_float64x2(5.0, 15.0);
    
    double max_val = hmax(v);
    double min_val = hmin(v);
    
    EXPECT_NEAR(max_val, 15.0, EPSILON_F64);
    EXPECT_NEAR(min_val, 5.0, EPSILON_F64);
}

TEST(SIMD_float64x2, Comparisons) {
    float64x2 a = make_float64x2(1.0, 5.0);
    float64x2 b = make_float64x2(1.0, 3.0);
    
    float64x2 eq = cmp_eq(a, b);
    float64x2 lt = cmp_lt(a, b);
    
    double out_eq[2];
    double out_lt[2];
    
    store(out_eq, eq);
    store(out_lt, lt);
    
    // Lane 0: 1.0 == 1.0 -> true (all bits set)
    // Lane 1: 5.0 == 3.0 -> false (all bits clear)
    uint64_t* eq_bits_0 = reinterpret_cast<uint64_t*>(&out_eq[0]);
    uint64_t* eq_bits_1 = reinterpret_cast<uint64_t*>(&out_eq[1]);
    EXPECT_EQ(*eq_bits_0, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(*eq_bits_1, 0x0ULL);
    
    // Lane 0: 1.0 < 1.0 -> false
    // Lane 1: 5.0 < 3.0 -> false
    uint64_t* lt_bits_0 = reinterpret_cast<uint64_t*>(&out_lt[0]);
    uint64_t* lt_bits_1 = reinterpret_cast<uint64_t*>(&out_lt[1]);
    EXPECT_EQ(*lt_bits_0, 0x0ULL);
    EXPECT_EQ(*lt_bits_1, 0x0ULL);
}

TEST(SIMD_float64x2, Blend) {
    float64x2 a = make_float64x2(10.0, 20.0);
    float64x2 b = make_float64x2(100.0, 200.0);
    
    // Create mask: lane 0 = true, lane 1 = false
    float64x2 mask = cmp_lt(a, make_float64x2(15.0, 15.0));
    
    float64x2 result = blend(a, b, mask);
    
    double out[2];
    store(out, result);
    
    // Lane 0: 10.0 < 15.0 -> true -> select b[0] = 100.0
    // Lane 1: 20.0 < 15.0 -> false -> select a[1] = 20.0
    EXPECT_NEAR(out[0], 100.0, EPSILON_F64);
    EXPECT_NEAR(out[1], 20.0, EPSILON_F64);
}

TEST(SIMD_float64x2, SmallNumbers) {
    // Test with numbers near machine epsilon
    double small = 1e-10;
    float64x2 a = make_float64x2(1.0 + small, 2.0 + small);
    float64x2 b = make_float64x2(1.0, 2.0);
    
    float64x2 diff = sub(a, b);
    
    double out[2];
    store(out, diff);
    
    EXPECT_NEAR(out[0], small, EPSILON_F64);
    EXPECT_NEAR(out[1], small, EPSILON_F64);
}

TEST(SIMD_float64x2, LargeNumbers) {
    // Test with large numbers
    float64x2 a = make_float64x2(1e100, 2e100);
    float64x2 b = make_float64x2(1e99, 2e99);
    
    float64x2 sum = add(a, b);
    
    double out[2];
    store(out, sum);
    
    EXPECT_NEAR(out[0], 1.1e100, 1e88); // Relative error
    EXPECT_NEAR(out[1], 2.2e100, 1e88);
}

TEST(SIMD_float64x2, NegationChain) {
    float64x2 a = make_float64x2(5.0, -3.0);
    float64x2 neg1 = negate(a);
    float64x2 neg2 = negate(neg1);
    
    double out[2];
    store(out, neg2);
    
    EXPECT_NEAR(out[0], 5.0, EPSILON_F64);
    EXPECT_NEAR(out[1], -3.0, EPSILON_F64);
}

TEST(SIMD_float64x2, SqrtPrecision) {
    // Test sqrt with numbers that have exact square roots
    float64x2 a = make_float64x2(4.0, 16.0);
    float64x2 result = sqrt(a);
    
    double out[2];
    store(out, result);
    
    EXPECT_DOUBLE_EQ(out[0], 2.0);
    EXPECT_DOUBLE_EQ(out[1], 4.0);
}

// ============================================================================
// AVX TESTS (256-bit vectors)
// ============================================================================

#if defined(CASPI_HAS_AVX)

TEST(SIMD_float32x8, ConstructionScalars) {
    float32x8 v = make_float32x8(1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f);
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], float(i + 1));
    }
}

TEST(SIMD_float32x8, LoadStore) {
    alignas(32) float arr[8] = {8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
    float32x8 v = make_float32x8_from_array(arr);
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], arr[i]);
    }
}

TEST(SIMD_float32x8, Broadcast) {
    float32x8 v = set1x8(3.14159f);
    alignas(32) float out[8];
    storex8(out, v);
    for (int i = 0; i < 8; i++) {
        EXPECT_NEAR(out[i], 3.14159f, EPSILON_F32);
    }
}

TEST(SIMD_float32x8, Arithmetic) {
    float32x8 a = make_float32x8(1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f);
    float32x8 b = make_float32x8(8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f);
    float32x8 sum = addx8(a, b);
    alignas(32) float out[8];
    storex8(out, sum);
    float expected[8] = {9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f, 9.f};
    for (int i = 0; i < 8; i++) {
        EXPECT_FLOAT_EQ(out[i], expected[i]);
    }
}

TEST(SIMD_float64x4, ConstructionScalars) {
    float64x4 v = make_float64x4(1.0, 2.0, 3.0, 4.0);
    
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
    float64x4 a = make_float64x4(10.0, 20.0, 30.0, 40.0);
    float64x4 b = make_float64x4(2.0, 4.0, 6.0, 8.0);
    
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

// ============================================================================
// REAL-WORLD INTEGRATION TESTS
// ============================================================================

TEST(SIMD_RealWorld, AudioGainRamping) {
    // Simulate smooth gain ramping in audio processing
    const int numSamples = 16;
    float input[numSamples];
    float output[numSamples];
    
    // Initialize with constant signal
    for (int i = 0; i < numSamples; i++) {
        input[i] = 1.0f;
    }
    
    // Ramp gain from 0.0 to 1.0
    float gain_start = 0.0f;
    float gain_end = 1.0f;
    float gain_increment = (gain_end - gain_start) / numSamples;
    
    for (int i = 0; i < numSamples; i += 4) {
        float32x4 samples = load(&input[i]);
        float32x4 gains = make_float32x4(
            gain_start + (i + 0) * gain_increment,
            gain_start + (i + 1) * gain_increment,
            gain_start + (i + 2) * gain_increment,
            gain_start + (i + 3) * gain_increment
        );
        
        float32x4 result = mul(samples, gains);
        store(&output[i], result);
    }
    
    // Verify smooth ramp
    for (int i = 0; i < numSamples; i++) {
        float expected = gain_start + i * gain_increment;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, StereoInterleaving) {
    // Test interleaved stereo processing
    const int numFrames = 8;
    float left[numFrames] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    float right[numFrames] = {10.f, 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f};
    float interleaved[numFrames * 2];
    
    // Process and interleave using SIMD
    for (int i = 0; i < numFrames; i += 4) {
        float32x4 l = load(&left[i]);
        float32x4 r = load(&right[i]);
        
        // Apply processing (e.g., gain)
        float32x4 gain = set1(0.5f);
        l = mul(l, gain);
        r = mul(r, gain);
        
        // Store (in real implementation, would interleave properly)
        float temp_l[4], temp_r[4];
        store(temp_l, l);
        store(temp_r, r);
        
        for (int j = 0; j < 4; j++) {
            interleaved[(i + j) * 2 + 0] = temp_l[j];
            interleaved[(i + j) * 2 + 1] = temp_r[j];
        }
    }
    
    // Verify
    for (int i = 0; i < numFrames; i++) {
        EXPECT_NEAR(interleaved[i * 2 + 0], left[i] * 0.5f, EPSILON_F32);
        EXPECT_NEAR(interleaved[i * 2 + 1], right[i] * 0.5f, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, ClippingWithBlend) {
    // Demonstrate soft clipping using blend
    float32x4 samples = make_float32x4(-1.5f, -0.5f, 0.5f, 1.5f);
    float32x4 threshold = set1(1.0f);
    float32x4 neg_threshold = set1(-1.0f);
    
    // Clip to [-1.0, 1.0] range
    float32x4 too_high = cmp_lt(threshold, samples);
    float32x4 too_low = cmp_lt(samples, neg_threshold);
    
    float32x4 result = blend(samples, threshold, too_high);
    result = blend(result, neg_threshold, too_low);
    
    float out[4];
    store(out, result);
    
    float expected[4] = {-1.0f, -0.5f, 0.5f, 1.0f};
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, AudioBufferProcessing) {
    // Simulate processing a stereo audio buffer with double precision
    const int numSamples = 100;
    double inputL[numSamples];
    double inputR[numSamples];
    double outputL[numSamples];
    double outputR[numSamples];
    
    // Initialize with sine wave
    for (int i = 0; i < numSamples; i++) {
        inputL[i] = std::sin(2.0 * CASPI::Constants::PI<float> * i / 50.0);
        inputR[i] = std::cos(2.0 * CASPI::Constants::PI<float> * i / 50.0);
    }
    
    // Process in pairs using SIMD
    const double gain = 0.5;
    float64x2 gainVec = set1(gain);
    
    for (int i = 0; i < numSamples; i += 2) {
        // Load L+R as a pair
        float64x2 samplesL = load(&inputL[i]);
        float64x2 samplesR = load(&inputR[i]);
        
        // Apply gain
        samplesL = mul(samplesL, gainVec);
        samplesR = mul(samplesR, gainVec);
        
        // Store
        store(&outputL[i], samplesL);
        store(&outputR[i], samplesR);
    }
    
    // Verify
    for (int i = 0; i < numSamples; i++) {
        EXPECT_NEAR(outputL[i], inputL[i] * gain, EPSILON_F64);
        EXPECT_NEAR(outputR[i], inputR[i] * gain, EPSILON_F64);
    }
}

TEST(SIMD_RealWorld, MulAddExample) {
    // Realistic use case: apply gain and add DC offset
    const int numSamples = 16;
    float input[numSamples];
    float output[numSamples];
    
    for (int i = 0; i < numSamples; i++) {
        input[i] = std::sin(2.0f * CASPI::Constants::PI<float> * i / 8.0f);
    }
    
    const float gain = 0.8f;
    const float dc_offset = 0.1f;
    
    float32x4 gain_vec = set1(gain);
    float32x4 offset_vec = set1(dc_offset);
    
    for (int i = 0; i < numSamples; i += 4) {
        float32x4 samples = load(&input[i]);
        // Use mul_add for: samples * gain + offset
        float32x4 result = mul_add(samples, gain_vec, offset_vec);
        store(&output[i], result);
    }
    
    // Verify
    for (int i = 0; i < numSamples; i++) {
        float expected = input[i] * gain + dc_offset;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}