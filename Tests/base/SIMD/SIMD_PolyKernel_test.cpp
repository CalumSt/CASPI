/**
 * @file caspi_PolyKernel_test.cpp
 *
 * Tests for caspi_PolyKernel.h covering:
 *   1. PolyKernel scalar evaluation (correctness of Horner's method)
 *   2. PolyKernel SIMD evaluation (all lanes, scalar/SIMD agreement)
 *   3. Generic factories — float and double produce distinct types
 *      and match the same reference polynomial to each precision's tolerance
 *   4. Double-precision accuracy improvement over float (verified numerically)
 *   5. tanh degree selection (float→3, double→7)
 *   6. Block operations: sin_block, cos_block, tanh_block
 *      - correctness vs std:: reference
 *      - in-place (dst==src) alias safety
 *      - alignment: aligned, misaligned, odd-length, single element
 *   7. Block kernel composability with block_op_unary directly
 */

#include "base/SIMD/caspi_Blocks.h"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using namespace CASPI::SIMD;

// ============================================================================
// Tolerances
// ============================================================================

static constexpr float  kF32Eps = 1e-5f;   // general float equality
static constexpr double kF64Eps = 1e-12;   // general double equality

// Approximation tolerances — measured max absolute error + headroom.
//
// sin/cos double: degree-5 Taylor on [-pi/2, pi/2]. Max error ~5.6e-8 (sin),
// ~4.6e-7 (cos). NOT double machine epsilon — the polynomial is speed-optimised.
//
// tanh: polynomial Taylor only valid on a restricted domain (convergence
// radius = pi/2). float deg-3: valid |x|<=0.65. double deg-7: valid |x|<=0.27.
//
// exp2_frac: float deg-5 ~1.7e-4, double deg-11 ~2.7e-11 on [0,1].
static constexpr float  kSinF32  = 2e-7f;
static constexpr double kSinF64  = 1e-7;     // degree-5, not machine epsilon
static constexpr float  kCosF32  = 5e-7f;    // slightly looser at domain edge
static constexpr double kCosF64  = 1e-6;     // degree-5, not machine epsilon
static constexpr float  kTanhF32 = 5e-4f;    // valid domain only: |x| <= 0.65
static constexpr double kTanhF64 = 1e-7;     // degree-7 at |x|=0.6: ~8.7e-8 max error
// Domain limits for tanh — inputs outside these produce large errors
static constexpr float  kTanhDomainF = 0.65f;
static constexpr double kTanhDomainD = 0.27;

// ============================================================================
// Helpers
// ============================================================================

static void store4f (float32x4 v, float out[4]) { store (out, v); }
static void store2d (float64x2 v, double out[2]) { store (out, v); }

static float32x4 load4f (float a, float b, float c, float d)
{
    alignas(16) float buf[4] = {a, b, c, d};
    return load_aligned<float> (buf);
}
static float64x2 load2d (double a, double b)
{
    alignas(16) double buf[2] = {a, b};
    return load_aligned<double> (buf);
}

// ============================================================================
// 1. Scalar evaluation — Horner correctness
// ============================================================================

TEST (PolyKernel_Scalar, constant_via_degree1)
{
    // p(x) = 5 + 0*x
    kernels::PolyKernel<float, 1> p ({5.f, 0.f});
    EXPECT_NEAR (p (0.f), 5.f, kF32Eps);
    EXPECT_NEAR (p (3.f), 5.f, kF32Eps);
    EXPECT_NEAR (p (-7.f), 5.f, kF32Eps);
}

TEST (PolyKernel_Scalar, linear)
{
    // p(x) = 3x + 2
    kernels::PolyKernel<float, 1> p ({2.f, 3.f});
    EXPECT_NEAR (p (0.f),   2.f, kF32Eps);
    EXPECT_NEAR (p (1.f),   5.f, kF32Eps);
    EXPECT_NEAR (p (-1.f), -1.f, kF32Eps);
}

TEST (PolyKernel_Scalar, quadratic)
{
    // p(x) = 2x² + 3x + 1  →  p(2) = 15
    kernels::PolyKernel<float, 2> p ({1.f, 3.f, 2.f});
    EXPECT_NEAR (p (2.f),  15.f, kF32Eps);
    EXPECT_NEAR (p (-1.f),  0.f, kF32Eps);   // 2 - 3 + 1 = 0
}

TEST (PolyKernel_Scalar, cubic_double)
{
    // p(x) = x³ - x² + x - 1  →  p(2) = 5
    kernels::PolyKernel<double, 3> p ({-1.0, 1.0, -1.0, 1.0});
    EXPECT_NEAR (p (2.0),  5.0, kF64Eps);
    EXPECT_NEAR (p (1.0),  0.0, kF64Eps);   // 1-1+1-1 = 0
    EXPECT_NEAR (p (0.0), -1.0, kF64Eps);
}

TEST (PolyKernel_Scalar, degree5_by_hand)
{
    // p(x) = x⁵ + 2x⁴ + 3x³ + 4x² + 5x + 6  at x=2: 32+32+24+16+10+6 = 120
    kernels::PolyKernel<float, 5> p ({6.f, 5.f, 4.f, 3.f, 2.f, 1.f});
    EXPECT_NEAR (p (2.f), 120.f, kF32Eps);
}

TEST (PolyKernel_Scalar, zero_input_returns_constant_term)
{
    kernels::PolyKernel<double, 4> p ({7.0, 3.0, 1.0, 0.5, 0.25});
    EXPECT_NEAR (p (0.0), 7.0, kF64Eps);
}

// ============================================================================
// 2. SIMD evaluation — all lanes, agreement with scalar
// ============================================================================

TEST (PolyKernel_SIMD_f32, linear_all_lanes)
{
    // p(x) = 2x + 1
    kernels::PolyKernel<float, 1> p ({1.f, 2.f});
    auto y = p (load4f (0.f, 1.f, 2.f, 3.f));
    float out[4];
    store4f (y, out);
    EXPECT_NEAR (out[0], 1.f, kF32Eps);
    EXPECT_NEAR (out[1], 3.f, kF32Eps);
    EXPECT_NEAR (out[2], 5.f, kF32Eps);
    EXPECT_NEAR (out[3], 7.f, kF32Eps);
}

TEST (PolyKernel_SIMD_f32, quadratic_all_lanes)
{
    // p(x) = (x+1)²
    kernels::PolyKernel<float, 2> p ({1.f, 2.f, 1.f});
    auto y = p (load4f (0.f, 1.f, 2.f, -1.f));
    float out[4];
    store4f (y, out);
    EXPECT_NEAR (out[0], 1.f, kF32Eps);
    EXPECT_NEAR (out[1], 4.f, kF32Eps);
    EXPECT_NEAR (out[2], 9.f, kF32Eps);
    EXPECT_NEAR (out[3], 0.f, kF32Eps);
}

TEST (PolyKernel_SIMD_f32, scalar_simd_agree)
{
    kernels::PolyKernel<float, 4> p ({1.f, -2.f, 3.f, -4.f, 5.f});
    alignas(16) float xs[4] = {0.5f, 1.0f, 1.5f, 2.0f};
    auto yv = p (load_aligned<float> (xs));
    float out[4];
    store (out, yv);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR (out[i], p (xs[i]), kF32Eps);
}

TEST (PolyKernel_SIMD_f64, quadratic_all_lanes)
{
    // p(x) = 3x² - x + 2
    kernels::PolyKernel<double, 2> p ({2.0, -1.0, 3.0});
    auto y = p (load2d (1.0, 2.0));
    double out[2];
    store2d (y, out);
    EXPECT_NEAR (out[0],  4.0, kF64Eps);   // 3-1+2=4
    EXPECT_NEAR (out[1], 12.0, kF64Eps);   // 12-2+2=12
}

TEST (PolyKernel_SIMD_f64, scalar_simd_agree)
{
    kernels::PolyKernel<double, 3> p ({1.0, -1.0, 0.5, -0.1});
    alignas(16) double xs[2] = {0.5, 1.5};
    auto yv = p (load_aligned<double> (xs));
    double out[2];
    store (out, yv);
    for (int i = 0; i < 2; ++i)
        EXPECT_NEAR (out[i], p (xs[i]), kF64Eps);
}

// ============================================================================
// 3. Generic factory — float and double produce correct types and values
// ============================================================================

TEST (PolyKernel_Factory, sin_float_has_correct_degree)
{
    auto k = sin_poly<float>();
    // Return type should be kernels::PolyKernel<float,5>
    static_assert (std::is_same_v<decltype(k), kernels::PolyKernel<float, 5>>,
                   "sin_poly<float>() must return kernels::PolyKernel<float,5>");
    // Constant term should be 1 (sin(x)/x → 1 as x→0)
    EXPECT_NEAR (k.coeffs[0], 1.f, kF32Eps);
}

TEST (PolyKernel_Factory, sin_double_has_correct_degree)
{
    auto k = sin_poly<double>();
    static_assert (std::is_same_v<decltype(k), kernels::PolyKernel<double, 5>>,
                   "sin_poly<double>() must return kernels::PolyKernel<double,5>");
    EXPECT_NEAR (k.coeffs[0], 1.0, kF64Eps);
}

TEST (PolyKernel_Factory, cos_float_and_double_different_coefficient_precision)
{
    auto kf = cos_poly<float>();
    auto kd = cos_poly<double>();
    // Both degree 5, c1 = -0.5.  The double version is more precise.
    EXPECT_NEAR (static_cast<double> (kf.coeffs[1]), -0.5, 1e-7);
    EXPECT_NEAR (kd.coeffs[1], -0.5, 1e-14);
    // Float loses ~7 decimal digits of precision vs double
    const double diff = std::abs (static_cast<double> (kf.coeffs[1]) - kd.coeffs[1]);
    // diff should be small (narrowing) but double has more precision
    EXPECT_LT (diff, 1e-6);
}

TEST (PolyKernel_Factory, tanh_float_is_degree3)
{
    auto k = tanh_poly<float>();
    static_assert (std::is_same_v<decltype(k), kernels::PolyKernel<float, 3>>,
                   "tanh_poly<float>() must return kernels::PolyKernel<float,3>");
    EXPECT_EQ (k.coeffs.size(), 4u);
}

TEST (PolyKernel_Factory, tanh_double_is_degree7)
{
    auto k = tanh_poly<double>();
    static_assert (std::is_same_v<decltype(k), kernels::PolyKernel<double, 7>>,
                   "tanh_poly<double>() must return kernels::PolyKernel<double,7>");
    EXPECT_EQ (k.coeffs.size(), 8u);
}

TEST (PolyKernel_Factory, log_mantissa_float_and_double)
{
    auto kf = log_mantissa_poly<float>();
    auto kd = log_mantissa_poly<double>();
    static_assert (std::is_same_v<decltype(kf), kernels::PolyKernel<float,  5>>);
    static_assert (std::is_same_v<decltype(kd), kernels::PolyKernel<double, 5>>);
    EXPECT_NEAR (kf.coeffs[0], 2.f,  kF32Eps);
    EXPECT_NEAR (kd.coeffs[0], 2.0,  kF64Eps);
}

TEST (PolyKernel_Factory, exp2_frac_type_check)
{
    auto kf = exp2_frac_poly<float>();
    auto kd = exp2_frac_poly<double>();
    // float→degree-5, double→degree-11
    static_assert (std::is_same_v<decltype(kf), kernels::PolyKernel<float,  5>>);
    static_assert (std::is_same_v<decltype(kd), kernels::PolyKernel<double, 11>>);
    // At x=0: both must equal 1.0 exactly (c[0]=1.0 for both)
    EXPECT_NEAR (kf (0.f), 1.f, 1e-6f);
    EXPECT_NEAR (kd (0.0), 1.0, 1e-12);
    // At x=1: float deg-5 has ~1.7e-4 error, double deg-11 has ~2.7e-11
    EXPECT_NEAR (kf (1.f), 2.f, 2e-4f);   // documented: ~1.7e-4
    EXPECT_NEAR (kd (1.0), 2.0, 5e-11);   // documented: ~2.7e-11
}

// ============================================================================
// 4. Double precision is more accurate than float
// ============================================================================

TEST (PolyKernel_Precision, sin_double_beats_float)
{
    // At the same input, double approximation error is smaller.
    // Use x = 1.0 (not a special value).
    auto kf = sin_poly<float>();
    auto kd = sin_poly<double>();

    const double x = 1.0;
    const double exact = std::sin (x);

    // sin(x) ≈ x * P(x²), caller multiplies by x
    const double approx_f = x * static_cast<double> (kf (static_cast<float> (x * x)));
    const double approx_d = x * kd (x * x);

    const double err_f = std::abs (approx_f - exact);
    const double err_d = std::abs (approx_d - exact);

    EXPECT_LT (err_d, err_f)
        << "double error=" << err_d << " should be < float error=" << err_f;
    // Both use the same degree-5 poly; double input avoids float narrowing error
    // so double path is more accurate, but not arbitrarily so.
    EXPECT_LT (err_d, kSinF64);   // kSinF64 = 1e-7 (not machine epsilon)
    EXPECT_LT (err_f, static_cast<double> (kSinF32));
}

TEST (PolyKernel_Precision, tanh_double_beats_float)
{
    // Compare within the float valid domain (the more restrictive).
    // Both float deg-3 and double deg-7 are valid at x=0.2.
    auto kf = tanh_poly<float>();
    auto kd = tanh_poly<double>();

    const double x = 0.2;   // well within both domains
    const double exact = std::tanh (x);

    const double approx_f = x * static_cast<double> (kf (static_cast<float> (x * x)));
    const double approx_d = x * kd (x * x);

    const double err_f = std::abs (approx_f - exact);
    const double err_d = std::abs (approx_d - exact);

    EXPECT_LT (err_d, err_f)
        << "double error=" << err_d << " should be < float error=" << err_f;
    EXPECT_LT (err_d, kTanhF64);
    EXPECT_LT (err_f, kTanhF32);
}

// ============================================================================
// 5. Approximation accuracy — scalar, point-by-point
// ============================================================================

// Parametric test over a sweep of values
struct SweepParam { double x; };

class SinApproxAccuracy : public ::testing::TestWithParam<SweepParam> {};
class CosApproxAccuracy : public ::testing::TestWithParam<SweepParam> {};
class TanhApproxAccuracy : public ::testing::TestWithParam<SweepParam> {};

TEST_P (SinApproxAccuracy, float_within_tolerance)
{
    const float x = static_cast<float> (GetParam().x);
    auto k = sin_poly<float>();
    EXPECT_NEAR (x * k (x * x), std::sin (x), kSinF32);
}

TEST_P (SinApproxAccuracy, double_within_tolerance)
{
    const double x = GetParam().x;
    auto k = sin_poly<double>();
    EXPECT_NEAR (x * k (x * x), std::sin (x), kSinF64);
}

TEST_P (CosApproxAccuracy, float_within_tolerance)
{
    const float x = static_cast<float> (GetParam().x);
    auto k = cos_poly<float>();
    EXPECT_NEAR (k (x * x), std::cos (x), kCosF32);
}

TEST_P (CosApproxAccuracy, double_within_tolerance)
{
    const double x = GetParam().x;
    auto k = cos_poly<double>();
    EXPECT_NEAR (k (x * x), std::cos (x), kCosF64);
}

TEST_P (TanhApproxAccuracy, float_within_tolerance)
{
    const float x = static_cast<float> (GetParam().x);
    auto k = tanh_poly<float>();
    EXPECT_NEAR (x * k (x * x), std::tanh (x), kTanhF32);
}

TEST_P (TanhApproxAccuracy, double_within_tolerance)
{
    const double x = GetParam().x;
    auto k = tanh_poly<double>();
    EXPECT_NEAR (x * k (x * x), std::tanh (x), kTanhF64);
}

static const double kPi2 = 1.5707963267948966;

INSTANTIATE_TEST_SUITE_P (SinCos, SinApproxAccuracy, ::testing::Values(
    SweepParam{0.0},
    SweepParam{kPi2 * 0.25},
    SweepParam{kPi2 * 0.5},
    SweepParam{kPi2 * 0.75},
    SweepParam{kPi2},
    SweepParam{-kPi2 * 0.25},
    SweepParam{-kPi2 * 0.5},
    SweepParam{-kPi2}
));

INSTANTIATE_TEST_SUITE_P (SinCos, CosApproxAccuracy, ::testing::Values(
    SweepParam{0.0},
    SweepParam{kPi2 * 0.25},
    SweepParam{kPi2 * 0.5},
    SweepParam{kPi2 * 0.75},
    SweepParam{kPi2},
    SweepParam{-kPi2 * 0.5}
));

// Tanh float test points: within the degree-3 valid domain |x| <= 0.65
INSTANTIATE_TEST_SUITE_P (TanhFloat, TanhApproxAccuracy, ::testing::Values(
    SweepParam{0.0},
    SweepParam{0.2},
    SweepParam{0.4},
    SweepParam{0.6},
    SweepParam{-0.2},
    SweepParam{-0.4},
    SweepParam{-0.6}
));

// ============================================================================
// 6. Block operations
// ============================================================================

// --- sin_block ---

TEST (ApproxOps_SinBlock, float_correctness)
{
    constexpr std::size_t N = 32;
    alignas(16) float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::sin_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::sin (src[i]), kSinF32) << "at i=" << i;
}

TEST (ApproxOps_SinBlock, double_correctness)
{
    constexpr std::size_t N = 16;
    alignas(16) double src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<double> (i) * kPi2 / N;

    ops::sin_block<double> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::sin (src[i]), kSinF64) << "at i=" << i;
}

TEST (ApproxOps_SinBlock, inplace_alias_safe)
{
    // dst == src: in-place must produce correct results
    constexpr std::size_t N = 16;
    alignas(16) float buf[N];
    float ref[N];
    for (std::size_t i = 0; i < N; ++i)
    {
        buf[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;
        ref[i] = buf[i];
    }

    // In-place: both pointers are the same
    ops::sin_block<float> (buf, buf, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (buf[i], std::sin (ref[i]), kSinF32) << "at i=" << i;
}

TEST (ApproxOps_SinBlock, zero_input_gives_zero)
{
    constexpr std::size_t N = 8;
    float src[N] = {}, dst[N] = {};
    ops::sin_block<float> (dst, src, N);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 0.f, kSinF32);
}

TEST (ApproxOps_SinBlock, misaligned_pointer)
{
    // Guaranteed misalignment: base is 32-byte aligned, +1 float = +4 bytes
    constexpr std::size_t N = 17;
    alignas(32) float src_buf[N + 1], dst_buf[N + 1];
    float* src = src_buf + 1;
    float* dst = dst_buf + 1;

    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::sin_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::sin (src[i]), kSinF32) << "at i=" << i;
}

TEST (ApproxOps_SinBlock, single_element)
{
    float src = 1.0f, dst = 0.f;
    ops::sin_block<float> (&dst, &src, 1);
    EXPECT_NEAR (dst, std::sin (1.0f), kSinF32);
}

TEST (ApproxOps_SinBlock, zero_count_is_nop)
{
    float src = 1.0f, dst = 99.f;
    ops::sin_block<float> (&dst, &src, 0);
    EXPECT_NEAR (dst, 99.f, kF32Eps);  // unchanged
}

TEST (ApproxOps_SinBlock, odd_length)
{
    constexpr std::size_t N = 13;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::sin_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::sin (src[i]), kSinF32) << "at i=" << i;
}

TEST (ApproxOps_SinBlock, large_buffer_double)
{
    // Buffer large enough to exercise the unrolled SIMD path
    constexpr std::size_t N = 512;
    std::vector<double> src (N), dst (N);
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<double> (i) * kPi2 / N;

    ops::sin_block<double> (dst.data(), src.data(), N);

    double max_err = 0.0;
    for (std::size_t i = 0; i < N; ++i)
        max_err = std::max (max_err, std::abs (dst[i] - std::sin (src[i])));

    EXPECT_LT (max_err, kSinF64);
}

// --- cos_block ---

TEST (ApproxOps_CosBlock, float_correctness)
{
    constexpr std::size_t N = 32;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::cos_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::cos (src[i]), kCosF32) << "at i=" << i;
}

TEST (ApproxOps_CosBlock, double_correctness)
{
    constexpr std::size_t N = 16;
    double src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<double> (i) * kPi2 / N;

    ops::cos_block<double> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::cos (src[i]), kCosF64) << "at i=" << i;
}

TEST (ApproxOps_CosBlock, zero_input_gives_one)
{
    float src = 0.f, dst = 0.f;
    ops::cos_block<float> (&dst, &src, 1);
    EXPECT_NEAR (dst, 1.f, kCosF32);
}

TEST (ApproxOps_CosBlock, odd_length)
{
    constexpr std::size_t N = 11;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::cos_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::cos (src[i]), kCosF32) << "at i=" << i;
}

// sin² + cos² = 1 (uses both block ops on the same input)
TEST (ApproxOps_SinCosBlock, pythagorean_identity)
{
    constexpr std::size_t N = 32;
    float src[N], s[N], c[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * static_cast<float> (kPi2) / N;

    ops::sin_block<float> (s, src, N);
    ops::cos_block<float> (c, src, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        const float identity = s[i] * s[i] + c[i] * c[i];
        EXPECT_NEAR (identity, 1.f, 1e-5f) << "at i=" << i;
    }
}

// --- tanh_block ---

TEST (ApproxOps_TanhBlock, float_correctness)
{
    // degree-3 polynomial valid domain: |x| <= kTanhDomainF = 0.65
    constexpr std::size_t N = 32;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = -kTanhDomainF + static_cast<float> (i) * (2.f * kTanhDomainF) / N;

    ops::tanh_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::tanh (src[i]), kTanhF32) << "at i=" << i;
}

TEST (ApproxOps_TanhBlock, double_correctness)
{
    // degree-7 polynomial valid domain: |x| <= kTanhDomainD = 0.27
    constexpr std::size_t N = 16;
    double src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = -kTanhDomainD + static_cast<double> (i) * (2.0 * kTanhDomainD) / N;

    ops::tanh_block<double> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::tanh (src[i]), kTanhF64) << "at i=" << i;
}

TEST (ApproxOps_TanhBlock, antisymmetry)
{
    // tanh is odd: tanh(-x) = -tanh(x). Test within valid domain |x| <= 0.65.
    constexpr std::size_t N = 6;
    float src_pos[N], src_neg[N], dst_pos[N], dst_neg[N];
    for (std::size_t i = 0; i < N; ++i)
    {
        // 0.1, 0.2, 0.3, 0.4, 0.5, 0.6 — all within kTanhDomainF
        src_pos[i] =  static_cast<float> (i + 1) * 0.1f;
        src_neg[i] = -src_pos[i];
    }

    ops::tanh_block<float> (dst_pos, src_pos, N);
    ops::tanh_block<float> (dst_neg, src_neg, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst_neg[i], -dst_pos[i], kTanhF32) << "at i=" << i;
}

TEST (ApproxOps_TanhBlock, zero_input_gives_zero)
{
    float src = 0.f, dst = 99.f;
    ops::tanh_block<float> (&dst, &src, 1);
    EXPECT_NEAR (dst, 0.f, kTanhF32);
}

TEST (ApproxOps_TanhBlock, misaligned_pointer)
{
    // Tests alignment handling, not accuracy over a wide range.
    // Sweep within valid domain [-0.65, 0.65).
    constexpr std::size_t N = 13;
    alignas(32) float src_buf[N + 1], dst_buf[N + 1];
    float* src = src_buf + 1;
    float* dst = dst_buf + 1;

    for (std::size_t i = 0; i < N; ++i)
        src[i] = -kTanhDomainF + static_cast<float> (i) * (2.f * kTanhDomainF) / N;

    ops::tanh_block<float> (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], std::tanh (src[i]), kTanhF32) << "at i=" << i;
}

TEST (ApproxOps_TanhBlock, double_large_buffer)
{
    // Restrict to valid domain |x| <= kTanhDomainD = 0.27
    constexpr std::size_t N = 256;
    std::vector<double> src (N), dst (N);
    for (std::size_t i = 0; i < N; ++i)
        src[i] = -kTanhDomainD + static_cast<double> (i) * (2.0 * kTanhDomainD) / N;

    ops::tanh_block<double> (dst.data(), src.data(), N);

    double max_err = 0.0;
    for (std::size_t i = 0; i < N; ++i)
        max_err = std::max (max_err, std::abs (dst[i] - std::tanh (src[i])));

    EXPECT_LT (max_err, kTanhF64);
}

// ============================================================================
// 7. Composability: use PolyKernel directly with block_op_unary
// ============================================================================

TEST (PolyKernel_Block, apply_quadratic_to_buffer)
{
    constexpr std::size_t N = 32;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i) * 0.1f;

    // p(x) = 2x + 1
    kernels::PolyKernel<float, 1> p ({1.f, 2.f});
    block_op_unary (dst, src, N, p);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 2.f * src[i] + 1.f, kF32Eps);
}

TEST (PolyKernel_Block, double_apply_to_buffer)
{
    constexpr std::size_t N = 16;
    double src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<double> (i) * 0.1;

    // p(x) = x² + 1
    kernels::PolyKernel<double, 2> p ({1.0, 0.0, 1.0});
    block_op_unary (dst, src, N, p);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], src[i] * src[i] + 1.0, kF64Eps);
}

TEST (PolyKernel_Block, custom_kernel_odd_size)
{
    constexpr std::size_t N = 7;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i);

    kernels::PolyKernel<float, 2> p ({0.f, 0.f, 1.f});  // p(x) = x²
    block_op_unary (dst, src, N, p);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], src[i] * src[i], kF32Eps);
}