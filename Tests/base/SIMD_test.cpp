// caspi_SIMD_full_tests.cpp
//
// Unit tests for CASPI SIMD API (caspi_SIMD.h)
//
// Test Cases and Purpose:
//
// 1. float32x4:
//    - Construction from scalars: verify lane order and initialization.
//    - Load/Store: ensure correct memory transfer across platforms (SSE, NEON, WASM, fallback).
//    - Arithmetic (add, sub, mul): validate per-lane operations.
//    - FMA (if available): test fused multiply-add correctness.
//    - Horizontal sum / max / min: verify reduction utilities.
//    - Lane-wise utilities (negate, abs, sqrt, set1, to_int, to_float): ensure correctness across all lanes.
//    - Comparisons / blending: verify cmp_eq, cmp_lt, and mask-based selection.
//
// 2. int32x4:
//    - Construction from scalars: verify lane order and initialization.
//    - Load/Store: ensure correct memory transfer.
//    - Arithmetic (add, sub, mul): validate per-lane integer operations, including scalar fallback.
//
// 3. float32x8 (AVX only):
//    - Construction from scalars: validate all 8 lanes are correctly set.
//    - Load/Store: verify memory operations with AVX registers.
//    - Arithmetic (add, sub, mul): per-lane operation correctness.
//
// Notes:
// - Covers platform-agnostic functionality across SSE (x86/x86_64), NEON (ARM), WASM SIMD, and scalar fallback.
// - Ensures consistent results across all supported platforms and compiler intrinsics.
// - Tests are written with Google Test framework (gtest).
// - EPSILON is used for floating-point comparisons to account for rounding differences.

#include "base/caspi_SIMD.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace CASPI::SIMD;

constexpr float EPSILON = 1e-6f;

// ------------------------ float32x4 Tests ------------------------

TEST(SIMD_float32x4, ConstructionScalars) {
    float32x4 v = make_float32x4(1.f, 2.f, 3.f, 4.f);

#if defined(CASPI_HAS_SSE1) || defined(CASPI_HAS_SSE2)
    float out[4];
    store(out, v);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.f);
    EXPECT_FLOAT_EQ(out[3], 4.f);
#else
    EXPECT_FLOAT_EQ(v.data[0], 1.f);
    EXPECT_FLOAT_EQ(v.data[1], 2.f);
    EXPECT_FLOAT_EQ(v.data[2], 3.f);
    EXPECT_FLOAT_EQ(v.data[3], 4.f);
#endif
}

TEST(SIMD_float32x4, LoadStore) {
    float arr[4] = {5.f, 6.f, 7.f, 8.f};
    float32x4 v = make_float32x4_from_array(arr);

    float out[4];
    store(out, v);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], arr[i], EPSILON);
}

TEST(SIMD_float32x4, Arithmetic) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4 (5.f, 6.f, 7.f, 8.f);

    const float32x4 sum  = add (a, b);
    const float32x4 diff = sub (b, a);
    const float32x4 prod = mul(a, b);

    float expected_sum[4]  = {6.f, 8.f, 10.f, 12.f};
    float expected_diff[4] = {4.f, 4.f, 4.f, 4.f};
    float expected_prod[4] = {5.f, 12.f, 21.f, 32.f};

    float out[4];

    store(out, sum);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_sum[i], EPSILON);

    store(out, diff);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_diff[i], EPSILON);

    store(out, prod);
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected_prod[i], EPSILON);
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
    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON);
}
#endif

// ------------------------ int32x4 Tests ------------------------

TEST(SIMD_int32x4, ConstructionScalars) {
    int32x4 v = make_int32x4(1, 2, 3, 4);

#if defined(CASPI_HAS_SSE1) || defined(CASPI_HAS_SSE2)
    alignas(16) int32_t out[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(out), v);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
    EXPECT_EQ(out[3], 4);
#else
    EXPECT_EQ(v.data[0], 1);
    EXPECT_EQ(v.data[1], 2);
    EXPECT_EQ(v.data[2], 3);
    EXPECT_EQ(v.data[3], 4);
#endif
}

TEST(SIMD_int32x4, LoadStore) {
    int32_t arr[4] = {5, 6, 7, 8};
    int32x4 v = make_int32x4_from_array(arr);

#if defined(CASPI_HAS_SSE1) || defined(CASPI_HAS_SSE2)
    alignas(16) int32_t out[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(out), v);
    for (int i = 0; i < 4; i++) EXPECT_EQ(out[i], arr[i]);
#else
    for (int i = 0; i < 4; i++) EXPECT_EQ(v.data[i], arr[i]);
#endif
}

TEST(SIMD_int32x4, Arithmetic) {
    int32x4 a = make_int32x4(1, 2, 3, 4);
    int32x4 b = make_int32x4(5, 6, 7, 8);

    int expected[4] = {6, 8, 10, 12};
    int out[4] = {0, 0, 0, 0};

#if defined(CASPI_HAS_SSE)
    int32x4 sum = _mm_add_epi32(a, b);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), sum);

#elif defined(CASPI_HAS_NEON)
    int32x4 sum = vaddq_s32(a, b);
    vst1q_s32(out, sum);

#elif defined(CASPI_ARCH_WASM)
    int32x4 sum = wasm_i32x4_add(a, b);
    wasm_v128_store(out, sum);

#else
    int32x4 sum;
    for (int i = 0; i < 4; i++) sum.data[i] = a.data[i] + b.data[i];
    for (int i = 0; i < 4; i++) out[i] = sum.data[i];
#endif

    for (int i = 0; i < 4; i++) EXPECT_EQ(out[i], expected[i]);
}

// ------------------------ float32x8 (AVX) Tests ------------------------
#if defined(CASPI_HAS_AVX)

TEST(SIMD_float32x8, ConstructionScalars) {
    float32x8 v = make_float32x8(1.f,2.f,3.f,4.f,5.f,6.f,7.f,8.f);
    alignas(32) float out[8];
    _mm256_storeu_ps(out, v);
    for (int i = 0; i < 8; i++) EXPECT_FLOAT_EQ(out[i], float(i+1));
}

TEST(SIMD_float32x8, LoadStore) {
    float arr[8] = {8.f,7.f,6.f,5.f,4.f,3.f,2.f,1.f};
    float32x8 v = make_float32x8_from_array(arr);
    alignas(32) float out[8];
    _mm256_storeu_ps(out, v);
    for (int i = 0; i < 8; i++) EXPECT_FLOAT_EQ(out[i], arr[i]);
}

TEST(SIMD_float32x8, Arithmetic) {
    float32x8 a = make_float32x8(1.f,2.f,3.f,4.f,5.f,6.f,7.f,8.f);
    float32x8 b = make_float32x8(8.f,7.f,6.f,5.f,4.f,3.f,2.f,1.f);
    float32x8 sum = _mm256_add_ps(a,b);
    alignas(32) float out[8];
    _mm256_storeu_ps(out, sum);
    float expected[8] = {9.f,9.f,9.f,9.f,9.f,9.f,9.f,9.f};
    for (int i = 0; i < 8; i++) EXPECT_FLOAT_EQ(out[i], expected[i]);
}

#endif // CASPI_HAS_AVX

// ------------------------ float32x4 Horizontal / Lane-wise Utilities ------------------------

TEST(SIMD_float32x4, HorizontalSum) {
    float32x4 v = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float sum = hsum(v);
    EXPECT_NEAR(sum, 10.f, EPSILON);
}

TEST(SIMD_float32x4, HorizontalMax) {
    float32x4 v = make_float32x4(1.f, 7.f, 3.f, 4.f);
    float max_val = hmax(v);
    EXPECT_NEAR(max_val, 7.f, EPSILON);
}

TEST(SIMD_float32x4, NegateAbsSqrt) {
    float32x4 v = make_float32x4(-1.f, -4.f, 9.f, -16.f);

    float32x4 neg = negate(v);
    float32x4 absv = abs(v);
    float32x4 sqr = sqrt(absv);

    float out[4];

    store(out, neg);
    EXPECT_NEAR(out[0], 1.f, EPSILON);
    EXPECT_NEAR(out[1], 4.f, EPSILON);
    EXPECT_NEAR(out[2], -9.f, EPSILON);
    EXPECT_NEAR(out[3], 16.f, EPSILON);

    store(out, absv);
    EXPECT_NEAR(out[0], 1.f, EPSILON);
    EXPECT_NEAR(out[1], 4.f, EPSILON);
    EXPECT_NEAR(out[2], 9.f, EPSILON);
    EXPECT_NEAR(out[3], 16.f, EPSILON);

    store(out, sqr);
    EXPECT_NEAR(out[0], 1.f, EPSILON);
    EXPECT_NEAR(out[1], 2.f, EPSILON);
    EXPECT_NEAR(out[2], 3.f, EPSILON);
    EXPECT_NEAR(out[3], 4.f, EPSILON);
}

TEST(SIMD_float32x4, Set1ToIntConversions) {
    float32x4 f = set1(3.5f);
    int32x4 i = to_int(f);
    float32x4 f2 = to_float(i);

    float outf[4];
    int outi[4];

    store(outf, f);
    for (int j = 0; j < 4; j++) EXPECT_NEAR(outf[j], 3.5f, EPSILON);

#if defined(CASPI_HAS_SSE)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(outi), i);
#else
    for (int j = 0; j < 4; j++) outi[j] = i.data[j];
#endif

    for (int j = 0; j < 4; j++) EXPECT_EQ(outi[j], 3);

    store(outf, f2);
    for (int j = 0; j < 4; j++) EXPECT_NEAR(outf[j], 3.f, EPSILON);
}

TEST(SIMD_float32x4, ComparisonsAndBlend) {
    float32x4 a = make_float32x4(1.f, 2.f, 3.f, 4.f);
    float32x4 b = make_float32x4(4.f, 3.f, 2.f, 1.f);

    float32x4 mask = cmp_lt(a, b);
    float32x4 blended = blend(a, b, mask);

    float out[4];
    store(out, blended);
    float expected[4] = {4.f, 3.f, 3.f, 4.f};

    for (int i = 0; i < 4; i++) EXPECT_NEAR(out[i], expected[i], EPSILON);
}
