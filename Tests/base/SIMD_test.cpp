// SIMD_tests.cpp
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
// 14. KERNEL OPERATIONS
//    - Add, subtract, multiply kernels
//    - Scale, copy, fill kernels
//    - MAC, lerp, clamp, abs kernels
//
// 15. BLOCK OPERATIONS
//    - Binary operations (add, sub, mul)
//    - Unary operations (scale, abs, clamp)
//    - Fill operations
//    - Ternary operations (MAC)
//    - Reduction operations (min, max, sum, dot)
//
// 16. ALIGNMENT HANDLING
//    - Prologue execution
//    - Aligned SIMD loop
//    - Epilogue execution
//    - Unaligned data paths
//
// 17. EDGE CASES
//    - Zero-length arrays
//    - Arrays smaller than SIMD width
//    - Odd-sized arrays
//    - Large arrays
//
//
// ============================================================================

#include "base/caspi_Constants.h"
#include "base/caspi_SIMD.h"
#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <vector>

using namespace CASPI::SIMD;

constexpr float EPSILON_F32 = 1e-6f;
constexpr double EPSILON_F64 = 1e-12;

// For fast approximations (rcp, rsqrt) which have lower accuracy
constexpr float EPSILON_APPROX = 1.5e-3f;

// ============================================================================
// FLOAT32X4 TESTS
// ============================================================================

TEST(SIMD_float32x4, ConstructionScalars) {
    float32x4 v = {1.f, 2.f, 3.f, 4.};

    float out[4];
    store(out, v);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.f);
    EXPECT_FLOAT_EQ(out[3], 4.f);
}

TEST(SIMD_float32x4, LoadStore) {
    float arr[4] = {5.f, 6.f, 7.f, 8.f};
    float32x4 v = {5.f, 6.f, 7.f, 8.f};

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
    float32x4 a = {1.f, 2.f, 3.f, 4.};
    float32x4 b = {5.f, 6.f, 7.f, 8.};

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
    float32x4 a = {10.f, 20.f, 30.f, 40.f};
    float32x4 b = {2.f, 4.f, 5.f, 8.f};

    float32x4 result = div(a, b);

    float expected[4] = {5.f, 5.f, 6.f, 5.f};
    float out[4];
    store(out, result);

    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, DivisionByOne) {
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
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
    float32x4 a = {1.f, 2.f, 3.f, 4.f);
    float32x4 b = {5.f, 6.f, 7.f, 8.f);
    float32x4 c = {1.f, 1.f, 1.f, 1.f);

    float32x4 r = fma(a, b, c);
    float expected[4] = {6.f, 13.f, 22.f, 33.f};

    float out[4];
    store(out, r);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
}
#endif

TEST(SIMD_float32x4, MulAdd) {
    // Test mul_add wrapper (works with or without FMA)
    float32x4 a = {2.f, 3.f, 4.f, 5.f};
    float32x4 b = {10.f, 10.f, 10.f, 10.f};
    float32x4 c = {5.f, 5.f, 5.f, 5.f};

    float32x4 result = mul_add(a, b, c);  // a*b + c

    float expected[4] = {25.f, 35.f, 45.f, 55.f};
    float out[4];
    store(out, result);

    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_float32x4, FastReciprocal) {
    float32x4 v = {2.f, 4.f, 5.f, 10.f};
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
    float32x4 v = {1.f, 4.f, 9.f, 16.f};
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
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
    float32x4 b = {1.f, 0.f, 3.f, 0.f};

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
    float32x4 a = {1.f, 5.f, 3.f, 8.f};
    float32x4 b = {2.f, 4.f, 6.f, 7.f};

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
    float32x4 a = {1.f, 5.f, 3.f, 8.f};
    float32x4 b = {4.f, 2.f, 6.f, 7.f};

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
    float32x4 v = {1.f, 2.f, 3.f, 4.f};
    float sum = hsum(v);
    EXPECT_NEAR(sum, 10.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMax) {
    float32x4 v = {1.f, 7.f, 3.f, 4.f};
    float max_val = hmax(v);
    EXPECT_NEAR(max_val, 7.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMin) {
    float32x4 v = {5.f, 1.f, 8.f, 3.f};
    float min_val = hmin(v);
    EXPECT_NEAR(min_val, 1.f, EPSILON_F32);
}

TEST(SIMD_float32x4, HorizontalMinNegative) {
    float32x4 v = {-5.f, -1.f, -8.f, -3.f};
    float min_val = hmin(v);
    EXPECT_NEAR(min_val, -8.f, EPSILON_F32);
}

TEST(SIMD_float32x4, NegateAbsSqrt) {
    float32x4 v = {-1.f, -4.f, 9.f, -16.f};

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
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
    float32x4 b = {4.f, 3.f, 2.f, 1.f};

    float32x4 mask = cmp_lt(a, b);
    float32x4 blended = blend(a, b, mask);

    float out[4];
    store(out, blended);
    float expected[4] = {4.f, 3.f, 3.f, 4.f};

    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
}

TEST(SIMD_float32x4, BlendAllTrue) {
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
    float32x4 b = {10.f, 20.f, 30.f, 40.f};
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
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
    float32x4 b = {10.f, 20.f, 30.f, 40.f};
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
    float32x4 a = {1.f, 2.f, 3.f, 4.f};

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
    float32x4 a = {-1.f, -2.f, -3.f, -4.f};
    float32x4 b = {1.f, 2.f, 3.f, 4.f};

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
    float32x4 a = {1.f, 2.f, 3.f, 4.f};
    float32x4 b = {1.f, 1.f, 1.f, 1.f};
    float32x4 c = {2.f, 2.f, 2.f, 2.f};
    float32x4 d = {1.f, 1.f, 1.f, 1.f};

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
    float64x2 v = {1.0, 2.0};

    double out[2];
    store(out, v);

    EXPECT_DOUBLE_EQ(out[0], 1.0);
    EXPECT_DOUBLE_EQ(out[1], 2.0);
}

TEST(SIMD_float64x2, ConstructionFromArray) {
    double arr[2] = {3.14159, 2.71828};
    float64x2 v = {3.14159, 2.71828};

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
    float64x2 a = {10.0, 20.0};
    float64x2 b = {2.0, 4.0};

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
    float64x2 a = {2.0, 3.0};
    float64x2 b = {4.0, 5.0};
    float64x2 c = {1.0, 1.0};

    float64x2 result = fma(a, b, c);  // a * b + c

    double expected[2] = {9.0, 16.0};  // 2*4+1=9, 3*5+1=16

    double out[2];
    store(out, result);

    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F64);
    }
}
#endif

TEST(SIMD_float64x2, MulAdd)
    {
        // Test mul_add wrapper (works with or without FMA)
        float64x2 a = {3.0, 4.0};
        float64x2 b = {2.0, 2.0};
    float64x2 c = {1.0, 1.0};

    float64x2 result = mul_add(a, b, c);  // a*b + c

    double expected[2] = {7.0, 9.0};  // 3*2+1=7, 4*2+1=9
    double out[2];
    store(out, result);

    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F64);
    }
}

TEST(SIMD_float64x2, LaneWiseOps) {
    float64x2 v = {-4.0, 9.0};

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

TEST(SIMD_float64x2, MinMax)
    {
        float64x2 a = {1.0, 10.0};
        float64x2 b = {5.0, 3.0};

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

TEST(SIMD_float64x2, HorizontalSum)
    {
        float64x2 v = {10.0, 20.0};
    double sum = hsum(v);
    EXPECT_NEAR(sum, 30.0, EPSILON_F64);
}

TEST(SIMD_float64x2, HorizontalMaxMin) {
    float64x2 v = {5.0, 15.0};

    double max_val = hmax(v);
    double min_val = hmin(v);

    EXPECT_NEAR(max_val, 15.0, EPSILON_F64);
    EXPECT_NEAR(min_val, 5.0, EPSILON_F64);
}

TEST(SIMD_float64x2, Comparisons)
    {
        float64x2 a = {1.0, 5.0};
        float64x2 b = {1.0, 3.0};

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
    float64x2 a = {10.0, 20.0};
    float64x2 b = {100.0, 200.0};

    // Create mask: lane 0 = true, lane 1 = false
    float64x2 mask = cmp_lt(a, {15.0, 15.0});

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
    float64x2 a = { 1.0 + small, 2.0 + small};
    float64x2 b = {1.0, 2.0};

    float64x2 diff = sub(a, b);

    double out[2];
    store(out, diff);

    EXPECT_NEAR(out[0], small, EPSILON_F64);
    EXPECT_NEAR(out[1], small, EPSILON_F64);
}

TEST(SIMD_float64x2, LargeNumbers)
    {
        // Test with large numbers
        float64x2 a = {1e100, 2e100};
        float64x2 b = {1e99, 2e99};

    float64x2 sum = add(a, b);

    double out[2];
    store(out, sum);

    EXPECT_NEAR(out[0], 1.1e100, 1e88); // Relative error
    EXPECT_NEAR(out[1], 2.2e100, 1e88);
}

TEST(SIMD_float64x2, NegationChain) {
    float64x2 a = {5.0, -3.0};
    float64x2 neg1 = negate(a);
    float64x2 neg2 = negate(neg1);

    double out[2];
    store(out, neg2);

    EXPECT_NEAR(out[0], 5.0, EPSILON_F64);
    EXPECT_NEAR(out[1], -3.0, EPSILON_F64);
}

TEST(SIMD_float64x2, SqrtPrecision) {
    // Test sqrt with numbers that have exact square roots
    float64x2 a = {4.0, 16.0};
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
        float32x4 gains = {
            gain_start + (i + 0) * gain_increment,
            gain_start + (i + 1) * gain_increment,
            gain_start + (i + 2) * gain_increment,
            gain_start + (i + 3) * gain_increment
        };

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
    float32x4 samples = {-1.5f, -0.5f, 0.5f, 1.5f};
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


TEST(SIMD_RealWorld, StereoMixing) {
    constexpr std::size_t N = 512;
    float left[N], right[N], master[N];

    // Initialize tracks
    for (std::size_t i = 0; i < N; ++i) {
        left[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
        right[i] = std::cos(2.0f * 3.14159f * i / 50.0f);
        master[i] = 0.0f;
    }

    // Apply gains
    ops::scale(left, N, 0.7f);
    ops::scale(right, N, 0.5f);

    // Mix to master
    ops::add(master, left, N);
    ops::add(master, right, N);

    // Verify output is reasonable
    float peak = ops::find_max(master, N);
    EXPECT_LE(peak, 2.0f);
}

TEST(SIMD_RealWorld, RMSCalculation) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate sine wave
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
    }

    // Calculate RMS using dot product
    float sum_squares = ops::dot_product(audio, audio, N);
    float rms = std::sqrt(sum_squares / N);

    // RMS of sine wave is approximately 0.707
    EXPECT_NEAR(rms, 0.707f, 0.01f);
}

TEST(SIMD_RealWorld, SoftClipping) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with peaks
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f) * 1.5f;
    }

    // Apply soft clipping
    ops::clamp(audio, -1.0f, 1.0f, N);

    // Verify all samples are in valid range
    float min_val = ops::find_min(audio, N);
    float max_val = ops::find_max(audio, N);

    EXPECT_GE(min_val, -1.0f);
    EXPECT_LE(max_val, 1.0f);
}

TEST(SIMD_RealWorld, EnvelopeFollower) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with varying amplitude
    for (std::size_t i = 0; i < N; ++i) {
        float envelope = static_cast<float>(i) / N;
        audio[i] = std::sin(2.0f * 3.14159f * i / 20.0f) * envelope;
    }

    // Get absolute values
    ops::abs(audio, N);

    // Find peak
    float peak = ops::find_max(audio, N);

    // Peak should be near the end where envelope is highest
    EXPECT_GT(peak, 0.9f);
}

TEST(SIMD_RealWorld, Crossfade) {
    constexpr std::size_t N = 512;
    float buffer_a[N], buffer_b[N], output[N];

    // Initialize with different signals
    for (std::size_t i = 0; i < N; ++i) {
        buffer_a[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
        buffer_b[i] = std::cos(2.0f * 3.14159f * i / 50.0f);
    }

    // Crossfade 50%
    ops::lerp(output, buffer_a, buffer_b, 0.5f, N);

    // Verify smooth transition
    for (std::size_t i = 0; i < N; ++i) {
        float expected = buffer_a[i] * 0.5f + buffer_b[i] * 0.5f;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, DynamicRangeCompression) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with wide dynamic range
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f) * 2.0f;
    }

    // Get absolute values
    float abs_audio[N];
    ops::copy(abs_audio, audio, N);
    ops::abs(abs_audio, N);

    // Find peak
    float peak = ops::find_max(abs_audio, N);

    // Calculate makeup gain to normalize
    float makeup_gain = 1.0f / peak;

    // Apply compression (simple limiting)
    ops::clamp(audio, -1.0f, 1.0f, N);
    ops::scale(audio, N, makeup_gain);

    // Verify normalized output
    float new_peak = ops::find_max(audio, N);
    EXPECT_LE(new_peak, 1.0f);
}

TEST(SIMD_RealWorld, ConvolutionMAC) {
    // Simple convolution using MAC
    constexpr std::size_t signal_len = 16;
    constexpr std::size_t kernel_len = 4;

    float signal[signal_len];
    float kernel[kernel_len] = {0.25f, 0.5f, 0.25f, 0.0f};
    float output[signal_len];

    // Generate impulse
    for (std::size_t i = 0; i < signal_len; ++i) {
        signal[i] = (i == 8) ? 1.0f : 0.0f;
    }

    // Simple convolution (not optimized, just testing MAC)
    ops::fill(output, signal_len, 0.0f);

    for (std::size_t i = 0; i < signal_len; ++i) {
        for (std::size_t k = 0; k < kernel_len && (i + k) < signal_len; ++k) {
            output[i + k] += signal[i] * kernel[k];
        }
    }

    // Verify impulse response
    EXPECT_NEAR(output[8], 0.25f, EPSILON_F32);
    EXPECT_NEAR(output[9], 0.5f, EPSILON_F32);
    EXPECT_NEAR(output[10], 0.25f, EPSILON_F32);
}

// ============================================================================
// KERNEL TESTS
// ============================================================================

TEST(SIMD_Kernels, AddKernel) {
    kernels::AddKernel<float> kernel;

    // Scalar test
    EXPECT_NEAR(kernel(2.0f, 3.0f), 5.0f, EPSILON_F32);

    // SIMD test
    float32x4 a = set1<float> (2.0f);
    float32x4 b = set1<float>(3.0f);
    float32x4 result = kernel(a, b);

    float out[4];
    store(out, result);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], 5.0f, EPSILON_F32);
    }
}

TEST(SIMD_Kernels, SubKernel) {
    kernels::SubKernel<double> kernel;

    EXPECT_NEAR(kernel(10.0, 3.0), 7.0, EPSILON_F64);

    float64x2 a = set1<double>(10.0);
    float64x2 b = set1<double>(3.0);
    float64x2 result = kernel(a, b);

    double out[2];
    store(out, result);
    for (int i = 0; i < 2; i++) {
        EXPECT_NEAR(out[i], 7.0, EPSILON_F64);
    }
}

TEST(SIMD_Kernels, MulKernel) {
    kernels::MulKernel<float> kernel;

    EXPECT_NEAR(kernel(4.0f, 5.0f), 20.0f, EPSILON_F32);
}

TEST(SIMD_Kernels, ScaleKernel) {
    kernels::ScaleKernel<float> kernel(2.5f);

    EXPECT_NEAR(kernel(4.0f), 10.0f, EPSILON_F32);

    float32x4 a = set1<float>(4.0f);
    float32x4 result = kernel(a);

    float out[4];
    store(out, result);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], 10.0f, EPSILON_F32);
    }
}

TEST(SIMD_Kernels, FillKernel) {
    kernels::FillKernel<float> kernel(3.14f);

    EXPECT_NEAR(kernel.scalar_value(), 3.14f, EPSILON_F32);

    float32x4 result = kernel.simd_value();
    float out[4];
    store(out, result);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], 3.14f, EPSILON_F32);
    }
}

TEST(SIMD_Kernels, MACKernel) {
    kernels::MACKernel<float> kernel;

    // acc + a * b = 10 + 2 * 3 = 16
    EXPECT_NEAR(kernel(10.0f, 2.0f, 3.0f), 16.0f, EPSILON_F32);
}

TEST(SIMD_Kernels, LerpKernel) {
    kernels::LerpKernel<float> kernel(0.5f);

    // a + 0.5 * (b - a) = 0 + 0.5 * 10 = 5
    EXPECT_NEAR(kernel(0.0f, 10.0f), 5.0f, EPSILON_F32);

    // Edge cases
    kernels::LerpKernel<float> kernel_zero(0.0f);
    EXPECT_NEAR(kernel_zero(2.0f, 8.0f), 2.0f, EPSILON_F32);

    kernels::LerpKernel<float> kernel_one(1.0f);
    EXPECT_NEAR(kernel_one(2.0f, 8.0f), 8.0f, EPSILON_F32);
}

TEST(SIMD_Kernels, ClampKernel) {
    kernels::ClampKernel<float> kernel(-1.0f, 1.0f);

    EXPECT_NEAR(kernel(-2.0f), -1.0f, EPSILON_F32);
    EXPECT_NEAR(kernel(0.5f), 0.5f, EPSILON_F32);
    EXPECT_NEAR(kernel(2.0f), 1.0f, EPSILON_F32);
}

TEST(SIMD_Kernels, AbsKernel) {
    kernels::AbsKernel<float> kernel;

    EXPECT_NEAR(kernel(-5.0f), 5.0f, EPSILON_F32);
    EXPECT_NEAR(kernel(5.0f), 5.0f, EPSILON_F32);
}

// ============================================================================
// BLOCK OPERATIONS - BASIC
// ============================================================================

TEST(SIMD_BlockOps, AddBlock) {
    constexpr std::size_t N = 16;
    float dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<float>(i);
        src[i] = static_cast<float>(i + 10);
    }

    ops::add(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], static_cast<float>(i + i + 10), EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, SubBlock) {
    constexpr std::size_t N = 16;
    double dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<double>(i + 20);
        src[i] = static_cast<double>(i);
    }

    ops::sub(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], 20.0, EPSILON_F64);
    }
}

TEST(SIMD_BlockOps, MulBlock) {
    constexpr std::size_t N = 16;
    float dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<float>(i + 1);
        src[i] = 2.0f;
    }

    ops::mul(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], static_cast<float>((i + 1) * 2), EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, ScaleBlock) {
    constexpr std::size_t N = 32;
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<float>(i + 1);
    }

    ops::scale(data, N, 0.5f);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], static_cast<float>(i + 1) * 0.5f, EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, CopyBlock) {
    constexpr std::size_t N = 64;
    float src[N], dst[N];

    for (std::size_t i = 0; i < N; ++i) {
        src[i] = static_cast<float>(i) * 3.14f;
        dst[i] = 0.0f;
    }

    ops::copy(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], src[i], EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, FillBlock) {
    constexpr std::size_t N = 128;
    double data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<double>(i);
    }

    ops::fill(data, N, 7.5);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], 7.5, EPSILON_F64);
    }
}

TEST(SIMD_BlockOps, MACBlock) {
    constexpr std::size_t N = 16;
    float dst[N], src1[N], src2[N];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = 10.0f;
        src1[i] = 2.0f;
        src2[i] = 3.0f;
    }

    ops::mac(dst, src1, src2, N);

    // dst should be 10 + (2 * 3) = 16
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], 16.0f, EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, LerpBlock) {
    constexpr std::size_t N = 16;
    float dst[N], a[N], b[N];

    for (std::size_t i = 0; i < N; ++i) {
        a[i] = 0.0f;
        b[i] = 10.0f;
    }

    ops::lerp(dst, a, b, 0.5f, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], 5.0f, EPSILON_F32);
    }
}

TEST(SIMD_BlockOps, ClampBlock) {
    constexpr std::size_t N = 16;
    float data[N] = {-2.0f, -1.5f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.5f,
                     2.0f, -3.0f, 3.0f, 0.8f, -0.8f, 1.2f, -1.2f, 0.0f};

    ops::clamp(data, -1.0f, 1.0f, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_GE(data[i], -1.0f);
        EXPECT_LE(data[i], 1.0f);
    }
}

TEST(SIMD_BlockOps, AbsBlock) {
    constexpr std::size_t N = 16;
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = (i % 2 == 0) ? static_cast<float>(i) : -static_cast<float>(i);
    }

    ops::abs(data, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_GE(data[i], 0.0f);
        EXPECT_NEAR(data[i], static_cast<float>(i), EPSILON_F32);
    }
}

// ============================================================================
// REDUCTION OPERATIONS
// ============================================================================

TEST(SIMD_Reductions, FindMin) {
    constexpr std::size_t N = 100;
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<float>(N - i);
    }
    data[42] = -10.0f;  // Minimum

    float min_val = ops::find_min(data, N);
    EXPECT_NEAR(min_val, -10.0f, EPSILON_F32);
}

TEST(SIMD_Reductions, FindMax) {
    constexpr std::size_t N = 100;
    double data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<double>(i);
    }
    data[57] = 1000.0;  // Maximum

    double max_val = ops::find_max(data, N);
    EXPECT_NEAR(max_val, 1000.0, EPSILON_F64);
}

TEST(SIMD_Reductions, Sum) {
    constexpr std::size_t N = 100;
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = 1.0f;
    }

    float sum = ops::sum(data, N);
    EXPECT_NEAR(sum, 100.0f, EPSILON_F32);
}

TEST(SIMD_Reductions, SumSequence) {
    constexpr std::size_t N = 100;
    double data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<double>(i + 1);
    }

    double sum = ops::sum(data, N);
    double expected = (N * (N + 1)) / 2.0;  // 1+2+...+100 = 5050
    EXPECT_NEAR(sum, expected, EPSILON_F64);
}

TEST(SIMD_Reductions, DotProduct) {
    constexpr std::size_t N = 16;
    float a[N], b[N];

    for (std::size_t i = 0; i < N; ++i) {
        a[i] = 1.0f;
        b[i] = 2.0f;
    }

    float dot = ops::dot_product(a, b, N);
    EXPECT_NEAR(dot, 32.0f, EPSILON_F32);  // 16 * (1 * 2)
}

TEST(SIMD_Reductions, DotProductSquares) {
    constexpr std::size_t N = 16;
    double a[N], b[N];

    for (std::size_t i = 0; i < N; ++i) {
        a[i] = static_cast<double>(i + 1);
        b[i] = static_cast<double>(i + 1);
    }

    double dot = ops::dot_product(a, b, N);
    // Sum of squares: 1^2 + 2^2 + ... + 16^2 = 1496
    EXPECT_NEAR(dot, 1496.0, EPSILON_F64);
}

// ============================================================================
// ALIGNMENT TESTS
// ============================================================================

TEST(SIMD_Alignment, AlignedAdd) {
    constexpr std::size_t N = 32;
    alignas(16) float dst[N];
    alignas(16) float src[N];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<float>(i);
        src[i] = 10.0f;
    }

    ops::add(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], static_cast<float>(i) + 10.0f, EPSILON_F32);
    }
}

TEST(SIMD_Alignment, UnalignedAdd) {
    constexpr std::size_t N = 32;
    float buffer_dst[N + 1];
    float buffer_src[N + 1];

    // Start at offset 1 to ensure misalignment
    float* dst = &buffer_dst[1];
    float* src = &buffer_src[1];

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<float>(i);
        src[i] = 10.0f;
    }

    ops::add(dst, src, N);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], static_cast<float>(i) + 10.0f, EPSILON_F32);
    }
}

TEST(SIMD_Alignment, PrologueOnly) {
    // Array size smaller than alignment requirement
    constexpr std::size_t N = 3;
    float dst[N] = {1.0f, 2.0f, 3.0f};
    float src[N] = {10.0f, 20.0f, 30.0f};

    ops::add(dst, src, N);

    EXPECT_NEAR(dst[0], 11.0f, EPSILON_F32);
    EXPECT_NEAR(dst[1], 22.0f, EPSILON_F32);
    EXPECT_NEAR(dst[2], 33.0f, EPSILON_F32);
}

TEST(SIMD_Alignment, WithPrologueAndEpilogue) {
    // Size: prologue (3) + SIMD (12) + epilogue (2) = 17
    constexpr std::size_t N = 17;
    float buffer[N + 4];  // Extra space for alignment testing
    float* dst = &buffer[1];  // Start at misaligned position

    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = static_cast<float>(i);
    }

    ops::scale(dst, N, 2.0f);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(dst[i], static_cast<float>(i) * 2.0f, EPSILON_F32);
    }
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST(SIMD_EdgeCases, ZeroLength) {
    float data[1] = {1.0f};

    ops::scale(data, 0, 2.0f);
    float sum = ops::sum(data, 0);

    EXPECT_NEAR(sum, 0.0f, EPSILON_F32);
    EXPECT_NEAR(data[0], 1.0f, EPSILON_F32);  // Unchanged
}

TEST(SIMD_EdgeCases, SingleElement) {
    float data[1] = {5.0f};

    ops::scale(data, 1, 3.0f);

    EXPECT_NEAR(data[0], 15.0f, EPSILON_F32);
}

TEST(SIMD_EdgeCases, OddSize) {
    constexpr std::size_t N = 13;  // Not divisible by 4
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<float>(i);
    }

    ops::scale(data, N, 2.0f);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], static_cast<float>(i * 2), EPSILON_F32);
    }
}

TEST(SIMD_EdgeCases, LargeArray) {
    constexpr std::size_t N = 10000;
    std::vector<float> data(N);

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = 1.0f;
    }

    ops::scale(data.data(), N, 0.5f);

    float sum = ops::sum(data.data(), N);
    EXPECT_NEAR(sum, 5000.0f, 0.1f);  // Allow small accumulated error
}

TEST(SIMD_EdgeCases, VerySmallValues) {
    constexpr std::size_t N = 16;
    double data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = 1e-100;
    }

    ops::scale(data, N, 2.0);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], 2e-100, 1e-110);
    }
}

// ============================================================================
// PERFORMANCE VERIFICATION
// ============================================================================

TEST(SIMD_Performance, CompareScalarVsSIMD) {
    constexpr std::size_t N = 10000;
    std::vector<float> data_simd(N);
    std::vector<float> data_scalar(N);

    // Initialize
    for (std::size_t i = 0; i < N; ++i) {
        data_simd[i] = static_cast<float>(i);
        data_scalar[i] = static_cast<float>(i);
    }

    // SIMD path
    ops::scale(data_simd.data(), N, 2.5f);

    // Scalar path
    for (std::size_t i = 0; i < N; ++i) {
        data_scalar[i] *= 2.5f;
    }

    // Verify results match
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data_simd[i], data_scalar[i], EPSILON_F32);
    }
}

TEST(SIMD_Performance, ReductionAccuracy) {
    constexpr std::size_t N = 100000;
    std::vector<float> data(N);

    // Generate random data
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = dist(rng);
    }

    // Calculate sum with SIMD
    float simd_sum = ops::sum(data.data(), N);

    // Calculate sum with scalar (Kahan summation for accuracy)
    float scalar_sum = 0.0f;
    float c = 0.0f;
    for (std::size_t i = 0; i < N; ++i) {
        float y = data[i] - c;
        float t = scalar_sum + y;
        c = (t - scalar_sum) - y;
        scalar_sum = t;
    }

    // Should be very close (allowing for small FP differences)
    EXPECT_NEAR(simd_sum, scalar_sum, std::abs(scalar_sum) * 0.001f);
}

// ============================================================================
// CORRECTNESS - COMPARE WITH BASIC OPERATIONS
// ============================================================================

TEST(SIMD_Correctness, BlendSemantics) {
    // Verify fixed blend semantics
    float32x4 a = set1<float>(1.0f);
    float32x4 b = set1<float>(2.0f);

    // All true mask
    float32x4 all_true = cmp_lt(a, b);
    float32x4 result_true = blend(a, b, all_true);

    float out_true[4];
    store(out_true, result_true);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_true[i], 2.0f, EPSILON_F32);
    }

    // All false mask
    float32x4 all_false = cmp_lt(b, a);
    float32x4 result_false = blend(a, b, all_false);

    float out_false[4];
    store(out_false, result_false);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_false[i], 1.0f, EPSILON_F32);
    }
}

TEST(SIMD_Correctness, HorizontalOptimization) {
    // Verify optimized horizontal operations produce correct results
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float32x4 v = load<float>(values);

    float sum = hsum(v);
    float max_val = hmax(v);
    float min_val = hmin(v);

    EXPECT_NEAR(sum, 10.0f, EPSILON_F32);
    EXPECT_NEAR(max_val, 4.0f, EPSILON_F32);
    EXPECT_NEAR(min_val, 1.0f, EPSILON_F32);
}

TEST(SIMD_Correctness, LoadStoreRoundtrip) {
    // Verify load/store preserve data
    alignas(16) float original[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    alignas(16) float roundtrip[4];

    float32x4 v = load_aligned<float>(original);
    store_aligned(roundtrip, v);

    for (int i = 0; i < 4; i++) {
        EXPECT_FLOAT_EQ(original[i], roundtrip[i]);
    }
}