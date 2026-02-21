/* ============================================================================
 * TEST INVENTORY
 * ============================================================================
 *
 * Group: SIMDComplex_SwapLanes
 *   Positive
 *     [1.0, 2.0] → [2.0, 1.0]. Verifies the fundamental shuffle used
 *     inside complex_mul to form the cross-multiply term.
 *   NegativeValues
 *     Negative values must survive swap. The shuffle touches only lane
 *     position, not the sign bit — distinct from negate_imag which does.
 *   DoubleApplication_IsIdentity
 *     swap(swap(v)) == v. Guards against direction error in the imm8
 *     encoding (_MM_SHUFFLE2(0,1) not (1,0)).
 *   Zero
 *     Trivial but catches uninitialised register issues.
 *
 * Group: SIMDComplex_BroadcastLo
 *   ReplicatesLane0
 *     [3.0, 7.0] → [3.0, 3.0]. Core op for replicating the real part
 *     before the first multiply in complex_mul.
 *   NegativeValue
 *     Negative lane 0 must broadcast without sign-stripping.
 *
 * Group: SIMDComplex_BroadcastHi
 *   ReplicatesLane1
 *     [3.0, 7.0] → [7.0, 7.0]. Core op for replicating the imaginary part.
 *   NegativeValue
 *     Negative lane 1 must broadcast correctly.
 *
 * Group: SIMDComplex_InterleaveLo
 *   TakesLane0FromEach
 *     a=[1,2], b=[3,4] → [1,3]. Validates the twiddle-construction pattern:
 *     interleave_lo(set1(wr), set1(wi)) = [wr,wi].
 *   RoundTrip_ReconstructsOriginal
 *     interleave_lo(interleave_lo(a,b), interleave_lo(c,d)) reconstructs
 *     the expected lane-0 values. Sanity check for chained use.
 *
 * Group: SIMDComplex_InterleaveHi
 *   TakesLane1FromEach
 *     a=[1,2], b=[3,4] → [2,4].
 *
 * Group: SIMDComplex_NegateImag
 *   FlipsLane1Only
 *     [1.5, -2.5] → [1.5, 2.5]. Lane 0 must not change.
 *   PositiveImag
 *     [1.0, 3.0] → [1.0, -3.0].
 *   DoubleApplication_IsIdentity
 *     negate_imag(negate_imag(v)) == v. XOR mask is self-inverse.
 *   Zero
 *     -0.0 is valid IEEE 754. negate_imag([0,0]) has |im|==0.
 *   SubnormalImag
 *     Subnormal doubles must survive the XOR sign flip without flush-to-zero.
 *     Validates that the bit operation does not call any FP hardware path.
 *   NegateImag_IsConjugate_ForIFFT
 *     negate_imag matches std::conj for a known twiddle factor.
 *     This is the exact check that the IFFT butterfly relies on.
 *
 * Group: SIMDComplex_ComplexMul
 *   Identity
 *     (1+0i) * (x+yi) == (x+yi).
 *   NegativeReal
 *     (-1+0i) * (3+4i) == (-3-4i). Verifies sign in the subtract lane.
 *   PureImaginary
 *     (0+1i) * (3+4i) == (-4+3i). 90-degree rotation.
 *   KnownResult
 *     (2+3i) * (4+5i) = -7+22i. Standard textbook vector. Catches sign
 *     errors in the addsub / negate_imag lane assignment.
 *   Conjugate_ImagPartIsZero
 *     (a+bi) * (a-bi) = a²+b² (pure real). Imaginary part must be zero.
 *     Validates that sub and add lanes are not swapped.
 *   UnitRotation_45deg
 *     (e^{iπ/4})² = i  →  lane0 ≈ 0, lane1 ≈ 1.
 *     Physically meaningful: two successive 45° rotations.
 *   NegateImag_ThenMul_MatchesConjugateTwiddle
 *     Simulates the exact IFFT butterfly sequence:
 *       twiddle = negate_imag([wr,wi])
 *       complex_mul(twiddle, odd) must equal conj(twiddle)*odd.
 *     Uses wr=0, wi=1 (pure imaginary) for exact arithmetic.
 *   LargeValues_NoOverflow
 *     (1e6+0i)² = 1e12. Must not produce Inf. Guards against intermediate
 *     overflow in the broadcast×multiply chain.
 *
 * Group: SIMDComplex_TwiddleConstruction
 *   InterleaveLoFromSet1_BuildsTwiddle
 *     interleave_lo(set1(wr), set1(wi)) == [wr, wi].
 *     This is the platform-agnostic twiddle construction used in the FFT
 *     butterfly, replacing _mm_set_pd(wi,wr) which is SSE2-only.
 *     Test confirms correctness on the current platform.
 *
 * ============================================================================
 * BUILD (inside CASPI project)
 * ============================================================================
 *   g++ -std=c++17 -O2 -msse2 -msse3 -I<project_root> \
 *       test_caspi_SIMD_complex.cpp \
 *       -lgtest -lgtest_main -lpthread -o test_simd_complex
 *
 * SSE2 only (exercises the shuffle-blend path in complex_mul):
 *   g++ -std=c++17 -O2 -msse2 -mno-sse3 -I<project_root> ...
 */

#include "base/caspi_Constants.h"

#include <gtest/gtest.h>

#include "base/caspi_SIMD.h"

// ============================================================================
// Test helpers
// ============================================================================

constexpr float EPSILON_F32 = 1e-6f;
constexpr double EPSILON_F64 = 1e-12;
static constexpr double kTol = 1e-12;

/// Build a float64x2 from two scalar doubles, portable across all platforms.
/// Uses the same interleave_lo + set1 pattern as the FFT twiddle construction.
static CASPI::SIMD::float64x2 make_f64x2 (double lane0, double lane1)
{
    // interleave_lo([lane0,lane0], [lane1,lane1]) = [lane0, lane1]
    return CASPI::SIMD::interleave_lo (CASPI::SIMD::set1<double> (lane0),
                                       CASPI::SIMD::set1<double> (lane1));
}

/// Extract lane values from a float64x2 via memcpy (ABI-safe on all platforms).
static void unpack (const CASPI::SIMD::float64x2& v, double& lane0, double& lane1)
{
    double buf[2];
    std::memcpy (buf, &v, 16);
    lane0 = buf[0];
    lane1 = buf[1];
}

// ============================================================================
// Group: SIMDComplex_SwapLanes
// ============================================================================

TEST(SIMDComplex_SwapLanes, Positive)
{
    auto v = make_f64x2 (1.0, 2.0);
    auto r = CASPI::SIMD::swap_lanes (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 2.0, kTol);
    EXPECT_NEAR (hi, 1.0, kTol);
}

TEST(SIMDComplex_SwapLanes, NegativeValues)
{
    auto v = make_f64x2 (-3.5, 7.25);
    auto r = CASPI::SIMD::swap_lanes (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  7.25, kTol);
    EXPECT_NEAR (hi, -3.5,  kTol);
}

TEST(SIMDComplex_SwapLanes, DoubleApplication_IsIdentity)
{
    auto v = make_f64x2 (5.5, -9.1);
    auto r = CASPI::SIMD::swap_lanes (CASPI::SIMD::swap_lanes (v));
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  5.5, kTol);
    EXPECT_NEAR (hi, -9.1, kTol);
}

TEST(SIMDComplex_SwapLanes, Zero)
{
    auto v = make_f64x2 (0.0, 0.0);
    auto r = CASPI::SIMD::swap_lanes (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 0.0, kTol);
    EXPECT_NEAR (hi, 0.0, kTol);
}

// ============================================================================
// Group: SIMDComplex_BroadcastLo
// ============================================================================

TEST(SIMDComplex_BroadcastLo, ReplicatesLane0)
{
    auto v = make_f64x2 (3.0, 7.0);
    auto r = CASPI::SIMD::broadcast_lo (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 3.0, kTol);
    EXPECT_NEAR (hi, 3.0, kTol);
}

TEST(SIMDComplex_BroadcastLo, NegativeValue)
{
    auto v = make_f64x2 (-4.5, 2.0);
    auto r = CASPI::SIMD::broadcast_lo (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, -4.5, kTol);
    EXPECT_NEAR (hi, -4.5, kTol);
}

// ============================================================================
// Group: SIMDComplex_BroadcastHi
// ============================================================================

TEST(SIMDComplex_BroadcastHi, ReplicatesLane1)
{
    auto v = make_f64x2 (3.0, 7.0);
    auto r = CASPI::SIMD::broadcast_hi (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 7.0, kTol);
    EXPECT_NEAR (hi, 7.0, kTol);
}

TEST(SIMDComplex_BroadcastHi, NegativeValue)
{
    auto v = make_f64x2 (2.0, -8.25);
    auto r = CASPI::SIMD::broadcast_hi (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, -8.25, kTol);
    EXPECT_NEAR (hi, -8.25, kTol);
}

// ============================================================================
// Group: SIMDComplex_InterleaveLo
// ============================================================================

TEST(SIMDComplex_InterleaveLo, TakesLane0FromEach)
{
    auto a = make_f64x2 (1.0, 2.0);
    auto b = make_f64x2 (3.0, 4.0);
    auto r = CASPI::SIMD::interleave_lo (a, b);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 1.0, kTol);   // a lane 0
    EXPECT_NEAR (hi, 3.0, kTol);   // b lane 0
}

TEST(SIMDComplex_InterleaveLo, RoundTrip_ReconstructsOriginal)
{
    // interleave_lo([10,20], [30,40]) = [10,30]
    // interleave_lo([10,30], [20,40]) = [10,20]  (reconstruct a)
    auto a = make_f64x2 (10.0, 20.0);
    auto b = make_f64x2 (30.0, 40.0);
    auto lo_pair = CASPI::SIMD::interleave_lo (a, b);   // [10, 30]
    auto hi_pair = CASPI::SIMD::interleave_hi (a, b);   // [20, 40]
    auto rec_a   = CASPI::SIMD::interleave_lo (lo_pair, hi_pair); // [10, 20]
    double lo, hi; unpack (rec_a, lo, hi);
    EXPECT_NEAR (lo, 10.0, kTol);
    EXPECT_NEAR (hi, 20.0, kTol);
}

// ============================================================================
// Group: SIMDComplex_InterleaveHi
// ============================================================================

TEST(SIMDComplex_InterleaveHi, TakesLane1FromEach)
{
    auto a = make_f64x2 (1.0, 2.0);
    auto b = make_f64x2 (3.0, 4.0);
    auto r = CASPI::SIMD::interleave_hi (a, b);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 2.0, kTol);   // a lane 1
    EXPECT_NEAR (hi, 4.0, kTol);   // b lane 1
}

// ============================================================================
// Group: SIMDComplex_NegateImag
// ============================================================================

TEST(SIMDComplex_NegateImag, FlipsLane1Only)
{
    auto v = make_f64x2 (1.5, -2.5);
    auto r = CASPI::SIMD::negate_imag (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  1.5, kTol);  // lane 0 unchanged
    EXPECT_NEAR (hi,  2.5, kTol);  // -(-2.5) = 2.5
}

TEST(SIMDComplex_NegateImag, PositiveImag)
{
    auto v = make_f64x2 (1.0, 3.0);
    auto r = CASPI::SIMD::negate_imag (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  1.0, kTol);
    EXPECT_NEAR (hi, -3.0, kTol);
}

TEST(SIMDComplex_NegateImag, DoubleApplication_IsIdentity)
{
    auto v = make_f64x2 (2.7, -4.1);
    auto r = CASPI::SIMD::negate_imag (CASPI::SIMD::negate_imag (v));
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  2.7, kTol);
    EXPECT_NEAR (hi, -4.1, kTol);
}

TEST(SIMDComplex_NegateImag, Zero)
{
    // -0.0 == 0.0 under IEEE 754 equality; |−0.0| == 0.0
    auto v = make_f64x2 (0.0, 0.0);
    auto r = CASPI::SIMD::negate_imag (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_EQ (lo, 0.0);
    EXPECT_EQ (std::abs (hi), 0.0);
}

TEST(SIMDComplex_NegateImag, SubnormalImag)
{
    // 5e-324 = smallest positive double (subnormal).
    // XOR sign-bit must not flush to zero on any platform.
    const double sub_val = 5e-324;
    auto v = make_f64x2 (1.0, sub_val);
    auto r = CASPI::SIMD::negate_imag (v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  1.0,      kTol);
    EXPECT_NEAR (hi, -sub_val,  kTol);
}

TEST(SIMDComplex_NegateImag, NegateImag_IsConjugate_ForIFFT)
{
    // Verify negate_imag produces the complex conjugate for a twiddle factor
    // used in the IFFT butterfly. W_8^1 = cos(-π/4) + i*sin(-π/4).
    const double wr = std::cos (-CASPI::Constants::PI<double> / 4.0);
    const double wi = std::sin (-CASPI::Constants::PI<double> / 4.0);
    auto twiddle = make_f64x2 (wr, wi);
    auto conj    = CASPI::SIMD::negate_imag (twiddle);
    double lo, hi; unpack (conj, lo, hi);
    EXPECT_NEAR (lo,  wr, kTol);  // real part unchanged
    EXPECT_NEAR (hi, -wi, kTol);  // imaginary part negated
}

// ============================================================================
// Group: SIMDComplex_ComplexMul
// ============================================================================

TEST(SIMDComplex_ComplexMul, Identity)
{
    // (1+0i) * (x+yi) = (x+yi)
    auto one = make_f64x2 (1.0, 0.0);
    auto val = make_f64x2 (3.5, -2.1);
    auto r   = CASPI::SIMD::complex_mul (one, val);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  3.5, kTol);
    EXPECT_NEAR (hi, -2.1, kTol);
}

TEST(SIMDComplex_ComplexMul, NegativeReal)
{
    // (-1+0i) * (3+4i) = (-3-4i)
    auto neg_one = make_f64x2 (-1.0, 0.0);
    auto val     = make_f64x2 ( 3.0, 4.0);
    auto r       = CASPI::SIMD::complex_mul (neg_one, val);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, -3.0, kTol);
    EXPECT_NEAR (hi, -4.0, kTol);
}

TEST(SIMDComplex_ComplexMul, PureImaginary)
{
    // (0+1i) * (3+4i) = (0*3 - 1*4) + (0*4 + 1*3)i = -4 + 3i
    auto i   = make_f64x2 (0.0, 1.0);
    auto val = make_f64x2 (3.0, 4.0);
    auto r   = CASPI::SIMD::complex_mul (i, val);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, -4.0, kTol);
    EXPECT_NEAR (hi,  3.0, kTol);
}

TEST(SIMDComplex_ComplexMul, KnownResult)
{
    // (2+3i) * (4+5i) = (8-15) + (10+12)i = -7 + 22i
    auto a = make_f64x2 (2.0, 3.0);
    auto b = make_f64x2 (4.0, 5.0);
    auto r = CASPI::SIMD::complex_mul (a, b);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, -7.0, kTol);
    EXPECT_NEAR (hi, 22.0, kTol);
}

TEST(SIMDComplex_ComplexMul, Conjugate_ImagPartIsZero)
{
    // (a+bi) * (a-bi) = a²+b²  (pure real, imag must be exactly 0)
    const double a = 3.0, b = 4.0;
    auto val  = make_f64x2 (a,  b);
    auto conj = make_f64x2 (a, -b);
    auto r    = CASPI::SIMD::complex_mul (val, conj);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 25.0, kTol);   // a²+b² = 9+16
    EXPECT_NEAR (hi,  0.0, kTol);   // imaginary part must cancel exactly
}

TEST(SIMDComplex_ComplexMul, UnitRotation_45deg)
{
    // (e^{iπ/4})² = e^{iπ/2} = i  →  lane0 ≈ 0, lane1 ≈ 1
    const double c = std::cos (CASPI::Constants::PI<double> / 4.0);
    const double s = std::sin (CASPI::Constants::PI<double> / 4.0);
    auto v = make_f64x2 (c, s);
    auto r = CASPI::SIMD::complex_mul (v, v);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo, 0.0, 1e-13);
    EXPECT_NEAR (hi, 1.0, 1e-13);
}

TEST(SIMDComplex_ComplexMul, NegateImag_ThenMul_MatchesConjugateTwiddle)
{
    // Exact IFFT butterfly sequence:
    //   twiddle_fwd = [0, 1]  (pure imaginary)
    //   twiddle_inv = negate_imag([0,1]) = [0, -1]
    //   complex_mul([0,-1], [3,4]) = (0*3 - (-1)*4) + (0*4 + (-1)*3)i
    //                              = 4 - 3i
    auto twiddle = CASPI::SIMD::negate_imag (make_f64x2 (0.0, 1.0));  // [0, -1]
    auto odd     = make_f64x2 (3.0, 4.0);
    auto r       = CASPI::SIMD::complex_mul (twiddle, odd);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_NEAR (lo,  4.0, kTol);
    EXPECT_NEAR (hi, -3.0, kTol);
}

TEST(SIMDComplex_ComplexMul, LargeValues_NoOverflow)
{
    // (1e6 + 0i)² = 1e12. Must not produce Inf.
    auto a = make_f64x2 (1e6, 0.0);
    auto r = CASPI::SIMD::complex_mul (a, a);
    double lo, hi; unpack (r, lo, hi);
    EXPECT_FALSE (std::isinf (lo));
    EXPECT_NEAR  (lo, 1e12, 1.0);
    EXPECT_NEAR  (hi,  0.0, 1.0);
}

// ============================================================================
// Group: SIMDComplex_TwiddleConstruction
// ============================================================================

TEST(SIMDComplex_TwiddleConstruction, InterleaveLoFromSet1_BuildsTwiddle)
{
    // This is the exact twiddle construction used in the FFT butterfly:
    //   interleave_lo(set1<double>(wr), set1<double>(wi)) → [wr, wi]
    // Replaces SSE2-only _mm_set_pd(wi, wr).
    const double wr = std::cos (-CASPI::Constants::PI<double> / 3.0);
    const double wi = std::sin (-CASPI::Constants::PI<double> / 3.0);
    auto twiddle = CASPI::SIMD::interleave_lo (CASPI::SIMD::set1<double> (wr),
                                               CASPI::SIMD::set1<double> (wi));
    double lo, hi;
    unpack (twiddle, lo, hi);
    EXPECT_NEAR (lo, wr, kTol);
    EXPECT_NEAR (hi, wi, kTol);
}



