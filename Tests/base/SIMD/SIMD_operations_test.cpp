
#include "SIMD_test_helpers.h"
#include <gtest/gtest.h>

using namespace CASPI::SIMD;

TEST (SIMD_float32x4, Broadcast)
{
    float32x4 v = set1 (7.5f);

    float out[4];
    store (out, v);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_FLOAT_EQ (out[i], 7.5f);
    }
}

TEST (SIMD_float32x4, Arithmetic)
{
    float32x4 a = { 1.f, 2.f, 3.f, 4. };
    float32x4 b = { 5.f, 6.f, 7.f, 8. };

    const float32x4 sum  = add (a, b);
    const float32x4 diff = sub (b, a);
    const float32x4 prod = mul (a, b);

    float expected_sum[4]  = { 6.f, 8.f, 10.f, 12.f };
    float expected_diff[4] = { 4.f, 4.f, 4.f, 4.f };
    float expected_prod[4] = { 5.f, 12.f, 21.f, 32.f };

    float out[4];

    store (out, sum);
    for (int i = 0; i < 4; i++)
        EXPECT_NEAR (out[i], expected_sum[i], EPSILON_F32);

    store (out, diff);
    for (int i = 0; i < 4; i++)
        EXPECT_NEAR (out[i], expected_diff[i], EPSILON_F32);

    store (out, prod);
    for (int i = 0; i < 4; i++)
        EXPECT_NEAR (out[i], expected_prod[i], EPSILON_F32);
}

TEST (SIMD_float32x4, Division)
{
    float32x4 a = { 10.f, 20.f, 30.f, 40.f };
    float32x4 b = { 2.f, 4.f, 5.f, 8.f };

    float32x4 result = div (a, b);

    float expected[4] = { 5.f, 5.f, 6.f, 5.f };
    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], expected[i], EPSILON_F32);
    }
}

TEST (SIMD_float32x4, DivisionByOne)
{
    float32x4 a = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b = set1 (1.f);

    float32x4 result = div (a, b);

    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], static_cast<float> (i + 1), EPSILON_F32);
    }
}

#if defined(CASPI_HAS_FMA)
TEST (SIMD_float32x4, FMA)
{
    float32x4 a = {1.f, 2.f, 3.f, 4.f);
    float32x4 b = {5.f, 6.f, 7.f, 8.f);
    float32x4 c = {1.f, 1.f, 1.f, 1.f);

    float32x4 r       = fma (a, b, c);
    float expected[4] = { 6.f, 13.f, 22.f, 33.f };

    float out[4];
    store (out, r);
    for (int i = 0; i < 4; i++)
        EXPECT_NEAR (out[i], expected[i], EPSILON_F32);
}
#endif

TEST (SIMD_float32x4, MulAdd)
{
    // Test mul_add wrapper (works with or without FMA)
    float32x4 a = { 2.f, 3.f, 4.f, 5.f };
    float32x4 b = { 10.f, 10.f, 10.f, 10.f };
    float32x4 c = { 5.f, 5.f, 5.f, 5.f };

    float32x4 result = mul_add (a, b, c); // a*b + c

    float expected[4] = { 25.f, 35.f, 45.f, 55.f };
    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], expected[i], EPSILON_F32);
    }
}

TEST (SIMD_float32x4, FastReciprocal)
{
    float32x4 v      = { 2.f, 4.f, 5.f, 10.f };
    float32x4 result = rcp (v);

    float expected[4] = { 0.5f, 0.25f, 0.2f, 0.1f };
    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        // rcp is kernelsimate, so use larger tolerance
        EXPECT_NEAR (out[i], expected[i], EPSILON_kernels);
    }
}

TEST (SIMD_float32x4, FastReciprocalSqrt)
{
    float32x4 v      = { 1.f, 4.f, 9.f, 16.f };
    float32x4 result = rsqrt (v);

    float expected[4] = { 1.f, 0.5f, 1.f / 3.f, 0.25f };
    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        // rsqrt is kernelsimate, so use larger tolerance
        EXPECT_NEAR (out[i], expected[i], EPSILON_kernels);
    }
}

TEST (SIMD_float32x4, ComparisonEqual)
{
    float32x4 a = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b = { 1.f, 0.f, 3.f, 0.f };

    float32x4 mask = cmp_eq (a, b);

    float out[4];
    store (out, mask);

    // Lanes 0 and 2 should be all-bits-set (true), lanes 1 and 3 should be zero (false)
    uint32_t* bits = reinterpret_cast<uint32_t*> (out);
    EXPECT_EQ (bits[0], 0xFFFFFFFF);
    EXPECT_EQ (bits[1], 0x00000000);
    EXPECT_EQ (bits[2], 0xFFFFFFFF);
    EXPECT_EQ (bits[3], 0x00000000);
}

TEST (SIMD_float32x4, ComparisonLessThan)
{
    float32x4 a = { 1.f, 5.f, 3.f, 8.f };
    float32x4 b = { 2.f, 4.f, 6.f, 7.f };

    float32x4 mask = cmp_lt (a, b);

    float out[4];
    store (out, mask);

    // Lanes where a < b should be 0xFFFFFFFF
    uint32_t* bits = reinterpret_cast<uint32_t*> (out);
    EXPECT_EQ (bits[0], 0xFFFFFFFF); // 1 < 2
    EXPECT_EQ (bits[1], 0x00000000); // 5 < 4 is false
    EXPECT_EQ (bits[2], 0xFFFFFFFF); // 3 < 6
    EXPECT_EQ (bits[3], 0x00000000); // 8 < 7 is false
}

TEST (SIMD_float32x4, MinMax)
{
    float32x4 a = { 1.f, 5.f, 3.f, 8.f };
    float32x4 b = { 4.f, 2.f, 6.f, 7.f };

    float32x4 minv = min (a, b);
    float32x4 maxv = max (a, b);

    float expected_min[4] = { 1.f, 2.f, 3.f, 7.f };
    float expected_max[4] = { 4.f, 5.f, 6.f, 8.f };

    float out[4];

    store (out, minv);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], expected_min[i], EPSILON_F32);
    }

    store (out, maxv);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], expected_max[i], EPSILON_F32);
    }
}

TEST (SIMD_float32x4, HorizontalSum)
{
    float32x4 v = { 1.f, 2.f, 3.f, 4.f };
    float sum   = hsum (v);
    EXPECT_NEAR (sum, 10.f, EPSILON_F32);
}

TEST (SIMD_float32x4, HorizontalMax)
{
    float32x4 v   = { 1.f, 7.f, 3.f, 4.f };
    float max_val = hmax (v);
    EXPECT_NEAR (max_val, 7.f, EPSILON_F32);
}

TEST (SIMD_float32x4, HorizontalMin)
{
    float32x4 v   = { 5.f, 1.f, 8.f, 3.f };
    float min_val = hmin (v);
    EXPECT_NEAR (min_val, 1.f, EPSILON_F32);
}

TEST (SIMD_float32x4, HorizontalMinNegative)
{
    float32x4 v   = { -5.f, -1.f, -8.f, -3.f };
    float min_val = hmin (v);
    EXPECT_NEAR (min_val, -8.f, EPSILON_F32);
}

TEST (SIMD_float32x4, NegateAbsSqrt)
{
    float32x4 v = { -1.f, -4.f, 9.f, -16.f };

    float32x4 neg  = negate (v);
    float32x4 absv = abs (v);
    float32x4 sqr  = sqrt (absv);

    float out[4];

    store (out, neg);
    EXPECT_NEAR (out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR (out[1], 4.f, EPSILON_F32);
    EXPECT_NEAR (out[2], -9.f, EPSILON_F32);
    EXPECT_NEAR (out[3], 16.f, EPSILON_F32);

    store (out, absv);
    EXPECT_NEAR (out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR (out[1], 4.f, EPSILON_F32);
    EXPECT_NEAR (out[2], 9.f, EPSILON_F32);
    EXPECT_NEAR (out[3], 16.f, EPSILON_F32);

    store (out, sqr);
    EXPECT_NEAR (out[0], 1.f, EPSILON_F32);
    EXPECT_NEAR (out[1], 2.f, EPSILON_F32);
    EXPECT_NEAR (out[2], 3.f, EPSILON_F32);
    EXPECT_NEAR (out[3], 4.f, EPSILON_F32);
}

TEST (SIMD_float32x4, ComparisonsAndBlend)
{
    float32x4 a = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b = { 4.f, 3.f, 2.f, 1.f };

    float32x4 mask    = cmp_lt (a, b);
    float32x4 blended = blend (a, b, mask);

    float out[4];
    store (out, blended);
    float expected[4] = { 4.f, 3.f, 3.f, 4.f };

    for (int i = 0; i < 4; i++)
        EXPECT_NEAR (out[i], expected[i], EPSILON_F32);
}

TEST (SIMD_float32x4, BlendAllTrue)
{
    float32x4 a        = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b        = { 10.f, 20.f, 30.f, 40.f };
    float32x4 all_true = cmp_lt (a, b); // All lanes should be true

    float32x4 result = blend (a, b, all_true);

    float out[4];
    store (out, result);

    // All lanes should select from b
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], static_cast<float> ((i + 1) * 10), EPSILON_F32);
    }
}

TEST (SIMD_float32x4, BlendAllFalse)
{
    float32x4 a         = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b         = { 10.f, 20.f, 30.f, 40.f };
    float32x4 all_false = cmp_lt (b, a); // All lanes should be false

    float32x4 result = blend (a, b, all_false);

    float out[4];
    store (out, result);

    // All lanes should select from a
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], static_cast<float> (i + 1), EPSILON_F32);
    }
}

TEST (SIMD_float32x4, ZeroOperations)
{
    float32x4 zero = set1 (0.f);
    float32x4 a    = { 1.f, 2.f, 3.f, 4.f };

    float32x4 sum  = add (a, zero);
    float32x4 prod = mul (a, zero);

    float out_sum[4], out_prod[4];
    store (out_sum, sum);
    store (out_prod, prod);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out_sum[i], static_cast<float> (i + 1), EPSILON_F32);
        EXPECT_NEAR (out_prod[i], 0.f, EPSILON_F32);
    }
}

TEST (SIMD_float32x4, NegativeNumbers)
{
    float32x4 a = { -1.f, -2.f, -3.f, -4.f };
    float32x4 b = { 1.f, 2.f, 3.f, 4.f };

    float32x4 sum  = add (a, b);
    float32x4 prod = mul (a, b);

    float out_sum[4], out_prod[4];
    store (out_sum, sum);
    store (out_prod, prod);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out_sum[i], 0.f, EPSILON_F32);
        EXPECT_NEAR (out_prod[i], -static_cast<float> ((i + 1) * (i + 1)), EPSILON_F32);
    }
}

TEST (SIMD_float32x4, ChainedOperations)
{
    // Test (a + b) * c - d
    float32x4 a = { 1.f, 2.f, 3.f, 4.f };
    float32x4 b = { 1.f, 1.f, 1.f, 1.f };
    float32x4 c = { 2.f, 2.f, 2.f, 2.f };
    float32x4 d = { 1.f, 1.f, 1.f, 1.f };

    float32x4 result = sub (mul (add (a, b), c), d);

    float expected[4] = { 3.f, 5.f, 7.f, 9.f }; // ((1+1)*2-1, (2+1)*2-1, ...)
    float out[4];
    store (out, result);

    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], expected[i], EPSILON_F32);
    }
}

// ============================================================================
// FLOAT64X2 TESTS
// ============================================================================

TEST (SIMD_float64x2, Broadcast)
{
    float64x2 v = set1 (7.5);

    double out[2];
    store (out, v);

    EXPECT_DOUBLE_EQ (out[0], 7.5);
    EXPECT_DOUBLE_EQ (out[1], 7.5);
}

TEST (SIMD_float64x2, Arithmetic)
{
    float64x2 a = { 10.0, 20.0 };
    float64x2 b = { 2.0, 4.0 };

    float64x2 sum  = add (a, b);
    float64x2 diff = sub (a, b);
    float64x2 prod = mul (a, b);
    float64x2 quot = div (a, b);

    double expected_sum[2]  = { 12.0, 24.0 };
    double expected_diff[2] = { 8.0, 16.0 };
    double expected_prod[2] = { 20.0, 80.0 };
    double expected_quot[2] = { 5.0, 5.0 };

    double out[2];

    store (out, sum);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_sum[i], EPSILON_F64);
    }

    store (out, diff);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_diff[i], EPSILON_F64);
    }

    store (out, prod);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_prod[i], EPSILON_F64);
    }

    store (out, quot);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_quot[i], EPSILON_F64);
    }
}

#if defined(CASPI_HAS_FMA)
TEST (SIMD_float64x2, FMA)
{
    float64x2 a = { 2.0, 3.0 };
    float64x2 b = { 4.0, 5.0 };
    float64x2 c = { 1.0, 1.0 };

    float64x2 result = fma (a, b, c); // a * b + c

    double expected[2] = { 9.0, 16.0 }; // 2*4+1=9, 3*5+1=16

    double out[2];
    store (out, result);

    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected[i], EPSILON_F64);
    }
}
#endif

TEST (SIMD_float64x2, MulAdd)
{
    // Test mul_add wrapper (works with or without FMA)
    float64x2 a = { 3.0, 4.0 };
    float64x2 b = { 2.0, 2.0 };
    float64x2 c = { 1.0, 1.0 };

    float64x2 result = mul_add (a, b, c); // a*b + c

    double expected[2] = { 7.0, 9.0 }; // 3*2+1=7, 4*2+1=9
    double out[2];
    store (out, result);

    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected[i], EPSILON_F64);
    }
}

TEST (SIMD_float64x2, LaneWiseOps)
{
    float64x2 v = { -4.0, 9.0 };

    float64x2 neg  = negate (v);
    float64x2 absv = abs (v);
    float64x2 sqr  = sqrt (absv);

    double out[2];

    // Negate
    store (out, neg);
    EXPECT_NEAR (out[0], 4.0, EPSILON_F64);
    EXPECT_NEAR (out[1], -9.0, EPSILON_F64);

    // Absolute value
    store (out, absv);
    EXPECT_NEAR (out[0], 4.0, EPSILON_F64);
    EXPECT_NEAR (out[1], 9.0, EPSILON_F64);

    // Square root
    store (out, sqr);
    EXPECT_NEAR (out[0], 2.0, EPSILON_F64);
    EXPECT_NEAR (out[1], 3.0, EPSILON_F64);
}

TEST (SIMD_float64x2, MinMax)
{
    float64x2 a = { 1.0, 10.0 };
    float64x2 b = { 5.0, 3.0 };

    float64x2 minv = min (a, b);
    float64x2 maxv = max (a, b);

    double expected_min[2] = { 1.0, 3.0 };
    double expected_max[2] = { 5.0, 10.0 };

    double out[2];

    store (out, minv);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_min[i], EPSILON_F64);
    }

    store (out, maxv);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], expected_max[i], EPSILON_F64);
    }
}

TEST (SIMD_float64x2, HorizontalSum)
{
    float64x2 v = { 10.0, 20.0 };
    double sum  = hsum (v);
    EXPECT_NEAR (sum, 30.0, EPSILON_F64);
}

TEST (SIMD_float64x2, HorizontalMaxMin)
{
    float64x2 v = { 5.0, 15.0 };

    double max_val = hmax (v);
    double min_val = hmin (v);

    EXPECT_NEAR (max_val, 15.0, EPSILON_F64);
    EXPECT_NEAR (min_val, 5.0, EPSILON_F64);
}

TEST (SIMD_float64x2, Comparisons)
{
    float64x2 a = { 1.0, 5.0 };
    float64x2 b = { 1.0, 3.0 };

    float64x2 eq = cmp_eq (a, b);
    float64x2 lt = cmp_lt (a, b);

    double out_eq[2];
    double out_lt[2];

    store (out_eq, eq);
    store (out_lt, lt);

    // Lane 0: 1.0 == 1.0 -> true (all bits set)
    // Lane 1: 5.0 == 3.0 -> false (all bits clear)
    uint64_t* eq_bits_0 = reinterpret_cast<uint64_t*> (&out_eq[0]);
    uint64_t* eq_bits_1 = reinterpret_cast<uint64_t*> (&out_eq[1]);
    EXPECT_EQ (*eq_bits_0, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ (*eq_bits_1, 0x0ULL);

    // Lane 0: 1.0 < 1.0 -> false
    // Lane 1: 5.0 < 3.0 -> false
    uint64_t* lt_bits_0 = reinterpret_cast<uint64_t*> (&out_lt[0]);
    uint64_t* lt_bits_1 = reinterpret_cast<uint64_t*> (&out_lt[1]);
    EXPECT_EQ (*lt_bits_0, 0x0ULL);
    EXPECT_EQ (*lt_bits_1, 0x0ULL);
}

TEST (SIMD_float64x2, Blend)
{
    float64x2 a = { 10.0, 20.0 };
    float64x2 b = { 100.0, 200.0 };

    // Create mask: lane 0 = true, lane 1 = false
    float64x2 bitmask = { 15.0, 15.0 };
    float64x2 mask    = cmp_lt (a, bitmask);

    float64x2 result = blend (a, b, mask);

    double out[2];
    store (out, result);

    // Lane 0: 10.0 < 15.0 -> true -> select b[0] = 100.0
    // Lane 1: 20.0 < 15.0 -> false -> select a[1] = 20.0
    EXPECT_NEAR (out[0], 100.0, EPSILON_F64);
    EXPECT_NEAR (out[1], 20.0, EPSILON_F64);
}

TEST (SIMD_float64x2, small_valueNumbers)
{
    // Test with numbers near machine epsilon
    double small_value = 1e-10;
    float64x2 a        = { 1.0 + small_value, 2.0 + small_value };
    float64x2 b        = { 1.0, 2.0 };

    float64x2 diff = sub (a, b);

    double out[2];
    store (out, diff);

    EXPECT_NEAR (out[0], small_value, EPSILON_F64);
    EXPECT_NEAR (out[1], small_value, EPSILON_F64);
}

TEST (SIMD_float64x2, LargeNumbers)
{
    // Test with large numbers
    float64x2 a = { 1e100, 2e100 };
    float64x2 b = { 1e99, 2e99 };

    float64x2 sum = add (a, b);

    double out[2];
    store (out, sum);

    EXPECT_NEAR (out[0], 1.1e100, 1e88); // Relative error
    EXPECT_NEAR (out[1], 2.2e100, 1e88);
}

TEST (SIMD_float64x2, NegationChain)
{
    float64x2 a    = { 5.0, -3.0 };
    float64x2 neg1 = negate (a);
    float64x2 neg2 = negate (neg1);

    double out[2];
    store (out, neg2);

    EXPECT_NEAR (out[0], 5.0, EPSILON_F64);
    EXPECT_NEAR (out[1], -3.0, EPSILON_F64);
}

TEST (SIMD_float64x2, SqrtPrecision)
{
    // Test sqrt with numbers that have exact square roots
    float64x2 a      = { 4.0, 16.0 };
    float64x2 result = sqrt (a);

    double out[2];
    store (out, result);

    EXPECT_DOUBLE_EQ (out[0], 2.0);
    EXPECT_DOUBLE_EQ (out[1], 4.0);
}

// ============================================================================
// New Comparisons — float32x4
// ============================================================================

TEST (Operations_Comparisons_f32, cmp_gt)
{
    auto a = make4 (1.f, 5.f, 3.f, 8.f);
    auto b = make4 (2.f, 4.f, 3.f, 7.f);
    auto r = cmp_gt (a, b);

    uint32_t bits[4];
    float tmp[4];
    unpack4 (r, tmp);
    std::memcpy (bits, tmp, 16);

    EXPECT_EQ (bits[0], 0x00000000u); // 1 > 2 false
    EXPECT_EQ (bits[1], 0xFFFFFFFFu); // 5 > 4 true
    EXPECT_EQ (bits[2], 0x00000000u); // 3 > 3 false (strict)
    EXPECT_EQ (bits[3], 0xFFFFFFFFu); // 8 > 7 true
}

TEST (Operations_Comparisons_f32, cmp_le)
{
    auto a = make4 (1.f, 5.f, 3.f, 8.f);
    auto b = make4 (2.f, 4.f, 3.f, 7.f);
    auto r = cmp_le (a, b);

    uint32_t bits[4];
    float tmp[4];
    unpack4 (r, tmp);
    std::memcpy (bits, tmp, 16);

    EXPECT_EQ (bits[0], 0xFFFFFFFFu); // 1 <= 2 true
    EXPECT_EQ (bits[1], 0x00000000u); // 5 <= 4 false
    EXPECT_EQ (bits[2], 0xFFFFFFFFu); // 3 <= 3 true
    EXPECT_EQ (bits[3], 0x00000000u); // 8 <= 7 false
}

TEST (Operations_Comparisons_f32, cmp_ge)
{
    auto a = make4 (1.f, 5.f, 3.f, 8.f);
    auto b = make4 (2.f, 4.f, 3.f, 7.f);
    auto r = cmp_ge (a, b);

    uint32_t bits[4];
    float tmp[4];
    unpack4 (r, tmp);
    std::memcpy (bits, tmp, 16);

    EXPECT_EQ (bits[0], 0x00000000u); // 1 >= 2 false
    EXPECT_EQ (bits[1], 0xFFFFFFFFu); // 5 >= 4 true
    EXPECT_EQ (bits[2], 0xFFFFFFFFu); // 3 >= 3 true
    EXPECT_EQ (bits[3], 0xFFFFFFFFu); // 8 >= 7 true
}

TEST (Operations_Comparisons_f32, cmp_lt_gt_complementary)
{
    // For strict inequalities: cmp_lt(a,b) == cmp_gt(b,a)
    auto a = make4 (1.f, 5.f, 3.f, 0.f);
    auto b = make4 (2.f, 4.f, 6.f, 0.f);

    float lt[4], gt[4];
    unpack4 (cmp_lt (a, b), lt);
    unpack4 (cmp_gt (b, a), gt);

    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (reinterpret_cast<uint32_t*> (lt)[i],
                   reinterpret_cast<uint32_t*> (gt)[i]);
}

// ============================================================================
// New Comparisons — float64x2
// ============================================================================

TEST (Operations_Comparisons_f64, cmp_gt)
{
    alignas (16) double a[2] = { 1.0, 5.0 };
    alignas (16) double b[2] = { 2.0, 4.0 };
    auto r                   = cmp_gt (load_aligned<double> (a), load_aligned<double> (b));
    double out[2];
    unpack2 (r, out);
    uint64_t bits[2];
    std::memcpy (bits, out, 16);
    EXPECT_EQ (bits[0], 0x0000000000000000ull);
    EXPECT_EQ (bits[1], 0xFFFFFFFFFFFFFFFFull);
}

TEST (Operations_Comparisons_f64, cmp_le_equal_case)
{
    alignas (16) double a[2] = { 3.0, 3.0 };
    alignas (16) double b[2] = { 3.0, 4.0 };
    auto r                   = cmp_le (load_aligned<double> (a), load_aligned<double> (b));
    double out[2];
    unpack2 (r, out);
    uint64_t bits[2];
    std::memcpy (bits, out, 16);
    EXPECT_EQ (bits[0], 0xFFFFFFFFFFFFFFFFull); // equal → true for <=
    EXPECT_EQ (bits[1], 0xFFFFFFFFFFFFFFFFull); // 3 <= 4 true
}

// ============================================================================
// Bitwise operations — float32x4
// ============================================================================

TEST (Operations_Bitwise_f32, and_vec)
{
    // All-ones AND all-ones = all-ones
    auto ones         = make4 (0.f, 0.f, 0.f, 0.f);
    uint32_t all_ones = 0xFFFFFFFFu;
    float f_ones;
    std::memcpy (&f_ones, &all_ones, 4);
    auto vmask = make4 (f_ones, f_ones, f_ones, f_ones);

    auto a = make4 (1.0f, 2.0f, 3.0f, 4.0f);
    auto r = and_vec (a, vmask); // a & ~0 = a

    float out[4], expected[4];
    unpack4 (a, expected);
    unpack4 (r, out);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (reinterpret_cast<uint32_t*> (out)[i],
                   reinterpret_cast<uint32_t*> (expected)[i]);
}

TEST (Operations_Bitwise_f32, xor_self_is_zero)
{
    auto a = make4 (1.5f, -2.3f, 0.0f, 1e10f);
    auto r = xor_vec (a, a);
    float out[4];
    unpack4 (r, out);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (reinterpret_cast<uint32_t*> (out)[i], 0u);
}

TEST (Operations_Bitwise_f32, or_with_zero_is_identity)
{
    auto a    = make4 (1.5f, -2.3f, 3.0f, -4.0f);
    auto zero = set1<float> (0.f);
    auto r    = or_vec (a, zero);
    float out[4], orig[4];
    unpack4 (r, out);
    unpack4 (a, orig);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (reinterpret_cast<uint32_t*> (out)[i],
                   reinterpret_cast<uint32_t*> (orig)[i]);
}

TEST (Operations_Bitwise_f32, andnot_selects_second_where_first_is_zero)
{
    // andnot(mask, v): where mask bit = 0, keep v bit; where mask bit = 1, clear.
    // Using all-zero mask: andnot(0, v) = v
    auto mask = set1<float> (0.f);
    auto v    = make4 (1.f, 2.f, 3.f, 4.f);
    auto r    = andnot_vec (mask, v);
    float out[4], orig[4];
    unpack4 (r, out);
    unpack4 (v, orig);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (reinterpret_cast<uint32_t*> (out)[i],
                   reinterpret_cast<uint32_t*> (orig)[i]);
}

TEST (Operations_Bitwise_f32, abs_via_andnot)
{
    // abs(x) = andnot(sign_bit_mask, x)
    auto sign_mask = set1<float> (-0.0f);
    auto v         = make4 (-1.f, 2.f, -3.f, 4.f);
    auto r         = andnot_vec (sign_mask, v);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], 1.f, kEpsF);
    EXPECT_NEAR (out[1], 2.f, kEpsF);
    EXPECT_NEAR (out[2], 3.f, kEpsF);
    EXPECT_NEAR (out[3], 4.f, kEpsF);
}

// ============================================================================
// FMA variants — float32x4
// ============================================================================

TEST (Operations_FMA_f32, mul_sub)
{
    auto a = make4 (2.f, 3.f, 4.f, 5.f);
    auto b = make4 (3.f, 3.f, 3.f, 3.f);
    auto c = make4 (1.f, 1.f, 1.f, 1.f);
    // a*b - c = [5, 8, 11, 14]
    auto r = mul_sub (a, b, c);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], 5.f, kEpsF);
    EXPECT_NEAR (out[1], 8.f, kEpsF);
    EXPECT_NEAR (out[2], 11.f, kEpsF);
    EXPECT_NEAR (out[3], 14.f, kEpsF);
}

TEST (Operations_FMA_f32, nmadd)
{
    auto a = make4 (2.f, 2.f, 2.f, 2.f);
    auto b = make4 (3.f, 3.f, 3.f, 3.f);
    auto c = make4 (10.f, 10.f, 10.f, 10.f);
    // -(a*b) + c = -6 + 10 = 4
    auto r = nmadd (a, b, c);
    float out[4];
    unpack4 (r, out);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR (out[i], 4.f, kEpsF);
}

TEST (Operations_FMA_f32, nmsub)
{
    auto a = make4 (2.f, 2.f, 2.f, 2.f);
    auto b = make4 (3.f, 3.f, 3.f, 3.f);
    auto c = make4 (1.f, 1.f, 1.f, 1.f);
    // -(a*b) - c = -6 - 1 = -7
    auto r = nmsub (a, b, c);
    float out[4];
    unpack4 (r, out);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR (out[i], -7.f, kEpsF);
}

TEST (Operations_FMA_f32, mul_add_mul_sub_symmetric)
{
    // mul_add(a,b,c) + mul_sub(a,b,c) == 2*a*b
    auto a = make4 (3.f, 4.f, 5.f, 6.f);
    auto b = make4 (2.f, 2.f, 2.f, 2.f);
    auto c = make4 (1.f, 1.f, 1.f, 1.f);

    auto sum    = add (mul_add (a, b, c), mul_sub (a, b, c)); // = 2*a*b
    auto two_ab = mul (mul (a, b), set1<float> (2.f));

    float got[4], exp[4];
    unpack4 (sum, got);
    unpack4 (two_ab, exp);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR (got[i], exp[i], kEpsF);
}

// ============================================================================
// Rounding — float32x4
// ============================================================================

TEST (Operations_Round_f32, floor_positive)
{
    auto v = make4 (1.1f, 2.9f, 3.0f, 4.5f);
    auto r = floor (v);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], 1.f, kEpsF);
    EXPECT_NEAR (out[1], 2.f, kEpsF);
    EXPECT_NEAR (out[2], 3.f, kEpsF);
    EXPECT_NEAR (out[3], 4.f, kEpsF);
}

TEST (Operations_Round_f32, floor_negative)
{
    auto v = make4 (-1.1f, -2.9f, -3.0f, -4.5f);
    auto r = floor (v);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], -2.f, kEpsF);
    EXPECT_NEAR (out[1], -3.f, kEpsF);
    EXPECT_NEAR (out[2], -3.f, kEpsF);
    EXPECT_NEAR (out[3], -5.f, kEpsF);
}

TEST (Operations_Round_f32, ceil_positive)
{
    auto v = make4 (1.1f, 2.0f, 3.9f, 4.001f);
    auto r = ceil (v);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], 2.f, kEpsF);
    EXPECT_NEAR (out[1], 2.f, kEpsF);
    EXPECT_NEAR (out[2], 4.f, kEpsF);
    EXPECT_NEAR (out[3], 5.f, kEpsF);
}

TEST (Operations_Round_f32, ceil_negative)
{
    auto v = make4 (-1.9f, -2.1f, -3.0f, -0.5f);
    auto r = ceil (v);
    float out[4];
    unpack4 (r, out);
    EXPECT_NEAR (out[0], -1.f, kEpsF);
    EXPECT_NEAR (out[1], -2.f, kEpsF);
    EXPECT_NEAR (out[2], -3.f, kEpsF);
    EXPECT_NEAR (out[3], 0.f, kEpsF);
}

TEST (Operations_Round_f32, round_ties_away_from_zero)
{
    auto v = make4 (0.5f, 1.5f, -0.5f, -1.5f);
    auto r = round (v);
    float out[4];
    unpack4 (r, out);
    // std::round ties away from zero
    EXPECT_NEAR (out[0], 1.f, kEpsF);
    EXPECT_NEAR (out[1], 2.f, kEpsF);
    EXPECT_NEAR (out[2], -1.f, kEpsF);
    EXPECT_NEAR (out[3], -2.f, kEpsF);
}

TEST (Operations_Round_f32, floor_ceil_round_match_std)
{
    // Verify SIMD results match scalar stdlib for a range of values
    alignas (16) float vals[4] = { -3.7f, -0.3f, 0.7f, 3.3f };
    auto v                     = load_aligned<float> (vals);

    float fout[4], cout[4], rout[4];
    unpack4 (floor (v), fout);
    unpack4 (ceil (v), cout);
    unpack4 (round (v), rout);

    for (int i = 0; i < 4; ++i)
    {
        EXPECT_NEAR (fout[i], std::floor (vals[i]), kEpsF);
        EXPECT_NEAR (cout[i], std::ceil (vals[i]), kEpsF);
        EXPECT_NEAR (rout[i], std::round (vals[i]), kEpsF);
    }
}

// ============================================================================
// Rounding — float64x2
// ============================================================================

TEST (Operations_Round_f64, floor_ceil_round_match_std)
{
    alignas (16) double vals[2] = { -2.7, 1.3 };
    auto v                      = load_aligned<double> (vals);

    double fout[2], cout[2], rout[2];
    unpack2 (floor (v), fout);
    unpack2 (ceil (v), cout);
    unpack2 (round (v), rout);

    for (int i = 0; i < 2; ++i)
    {
        EXPECT_NEAR (fout[i], std::floor (vals[i]), kEpsD);
        EXPECT_NEAR (cout[i], std::ceil (vals[i]), kEpsD);
        EXPECT_NEAR (rout[i], std::round (vals[i]), kEpsD);
    }
}

// ============================================================================
// double rcp
// ============================================================================

TEST (Operations_Rcp_f64, exact_values)
{
    alignas (16) double vals[2] = { 2.0, 4.0 };
    auto v                      = load_aligned<double> (vals);
    double out[2];
    unpack2 (rcp (v), out);
    EXPECT_NEAR (out[0], 0.5, kEpsD);
    EXPECT_NEAR (out[1], 0.25, kEpsD);
}