#include "base/caspi_Constants.h"
#include "SIMD_test_helpers.h"
#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <vector>

using namespace CASPI::SIMD;

/*
- SIMD_Kernels.AddKernel
  - What: Tests kernels::AddKernel<float> for scalar and 128-bit SIMD lanes.
  - Why: Ensure addition semantics are identical for scalar and packed-lane execution.

- SIMD_Kernels.SubKernel
  - What: Tests kernels::SubKernel<double> for scalar and 128-bit SIMD lanes (double precision).
  - Why: Validate subtraction correctness across scalar and SIMD double lanes.

- SIMD_Kernels.MulKernel
  - What: Tests kernels::MulKernel<float> scalar behavior.
  - Why: Catch simple multiplication kernel regressions in scalar path.

- SIMD_Kernels.ScaleKernel
  - What: Tests kernels::ScaleKernel<float> for scalar and SIMD vector scaling.
  - Why: Verify scaling by a constant is correct in both scalar and vector forms.

- SIMD_Kernels.FillKernel
  - What: Tests kernels::FillKernel<float> scalar and SIMD fill values.
  - Why: Ensure constant-fill produces the correct scalar and packed values.

- SIMD_Kernels.MACKernel
  - What: Tests kernels::MACKernel<float> (multiply-accumulate) scalar path.
  - Why: Validate fused multiply-add semantics at the kernel level.

- SIMD_Kernels.LerpKernel
  - What: Tests kernels::LerpKernel<float> with typical factor and edge factors (0,1).
  - Why: Ensure linear interpolation returns expected endpoints and midpoints.

- SIMD_Kernels.ClampKernel
  - What: Tests kernels::ClampKernel<float> clamps below, inside, and above range.
  - Why: Verify min/max clamping behaviour at kernel level.

- SIMD_Kernels.AbsKernel
  - What: Tests kernels::AbsKernel<float> for sign handling.
  - Why: Ensure absolute value kernel returns magnitude for positive/negative inputs.

- SIMD_BlockOps.AddBlock
  - What: Tests ops::add on float arrays (element-wise add).
  - Why: Validate block-level add semantics across lanes and scalar tails.

- SIMD_BlockOps.SubBlock
  - What: Tests ops::sub on double arrays (element-wise subtract).
  - Why: Validate block-level subtraction in double precision.

- SIMD_BlockOps.MulBlock
  - What: Tests ops::mul element-wise multiplication by constant.
  - Why: Verify multiplication across full blocks.

- SIMD_BlockOps.ScaleBlock
  - What: Tests ops::scale scaling an array by a scalar.
  - Why: Confirm block-scale handles vectorization and edge elements.

- SIMD_BlockOps.CopyBlock
  - What: Tests ops::copy copies source to destination for float arrays.
  - Why: Ensure memory-copy semantics and alignment handling are correct.

- SIMD_BlockOps.FillBlock
  - What: Tests ops::fill for double arrays.
  - Why: Validate block fill for larger arrays and double precision.

- SIMD_BlockOps.MACBlock
  - What: Tests ops::mac (multiply-accumulate) over arrays.
  - Why: Validate element-wise MAC semantics across a block.

- SIMD_BlockOps.LerpBlock
  - What: Tests ops::lerp performs linear interpolation across arrays.
  - Why: Ensure consistent interpolation on blocks.

- SIMD_BlockOps.ClampBlock
  - What: Tests ops::clamp clamps array elements into a range.
  - Why: Verify clamp behavior on a representative mixed array.

- SIMD_BlockOps.AbsBlock
  - What: Tests ops::abs transforms all elements to their absolute values.
  - Why: Confirm absolute-value transform across an array.

- SIMD_Reductions.FindMin
  - What: Tests ops::find_min finds the minimum in a float array.
  - Why: Validate reduction semantics for min aggregation.

- SIMD_Reductions.FindMax
  - What: Tests ops::find_max finds the maximum in a double array.
  - Why: Validate reduction semantics for max aggregation.

- SIMD_Reductions.Sum
  - What: Tests ops::sum sums float array values (simple uniform data).
  - Why: Verify summation reduction correctness.

- SIMD_Reductions.SumSequence
  - What: Tests ops::sum sums a sequence of doubles (1..N).
  - Why: Validate summation against known arithmetic series result.

- SIMD_Reductions.DotProduct
  - What: Tests ops::dot_product on float arrays with constant factors.
  - Why: Verify dot-product accumulation and reduction semantics.

- SIMD_Reductions.DotProductSquares
  - What: Tests ops::dot_product on double arrays of identical sequences (sums of squares).
  - Why: Validate correct dot product in double precision.

- BlockOps_Fill.small_buffer_fills_correctly
  - What: Tests ops::fill on a small float buffer (fast path).
  - Why: Ensure typical small-buffer fill behavior.

- BlockOps_Fill.large_buffer_fills_correctly
  - What: Tests ops::fill on a buffer larger than L1 to exercise NT path.
  - Why: Verify large-buffer dispatch (non-temporal stores) correctness.

- BlockOps_Fill.double_precision_fills_correctly
  - What: Tests ops::fill on double vector.
  - Why: Confirm double-precision fill works across larger buffers.

- BlockOps_Fill.zero_count_is_nop
  - What: Tests ops::fill with count == 0 leaves scalar unchanged.
  - Why: Edge-case correctness.

- BlockOps_CopyWithGain.basic_correctness
  - What: Tests ops::copy_with_gain multiplies source by gain into dst.
  - Why: Verify basic copy-with-gain behavior.

- BlockOps_CopyWithGain.gain_zero_produces_zeros
  - What: Tests copy_with_gain with gain == 0 produces zeros in dst.
  - Why: Edge-case behavior for zero gain.

- BlockOps_CopyWithGain.does_not_modify_src
  - What: Asserts copy_with_gain does not mutate the src buffer.
  - Why: Ensure function preserves source immutability.

- BlockOps_CopyWithGain.odd_size
  - What: Tests copy_with_gain for a non-multiple-of-lane buffer length.
  - Why: Verify correct handling of remainder/prologue/epilogue.

- BlockOps_AccumWithGain.basic_correctness
  - What: Tests accumulate_with_gain does dst += src * gain.
  - Why: Validate multiply-accumulate accumulation semantics.

- BlockOps_AccumWithGain.zero_gain_leaves_dst_unchanged
  - What: Tests accumulate_with_gain with gain==0 is a no-op.
  - Why: Edge-case correctness.

- BlockOps_AccumWithGain.unity_gain_matches_plain_add
  - What: Compares accumulate_with_gain with gain==1 against ops::add.
  - Why: Ensure accumulate_with_gain reduces to add when gain==1.

- BlockOps_Pan.mono_to_stereo_equal_power
  - What: Tests ops::pan with equal-power gains (mono->stereo).
  - Why: Validate stereo panning preserves power distribution.

- BlockOps_Pan.hard_left
  - What: Tests pan with left gain 1 and right gain 0.
  - Why: Edge-case full-left panning.

- BlockOps_Pan.src_not_modified
  - What: Ensures pan does not alter the source buffer.
  - Why: Preserve input immutability.

- BlockOps_Pan.left_plus_right_equals_double_gain_times_src
  - What: Tests that left+right equals sum-of-gains times source (here 0.4+0.6==1).
  - Why: Verify gain algebraic relations hold across panned outputs.

*/

// ============================================================================
// KERNEL TESTS
// ============================================================================

/* Purpose: Verify the AddKernel works for scalar and SIMD values.
   Arrange: create kernel and inputs; Act: call kernel; Assert: results. */
TEST (SIMD_Kernels, AddKernel)
{
    kernels::AddKernel<float> kernel;

    // Scalar test
    EXPECT_NEAR (kernel (2.0f, 3.0f), 5.0f, EPSILON_F32);

    // SIMD test
    float32x4 a      = set1<float> (2.0f);
    float32x4 b      = set1<float> (3.0f);
    float32x4 result = kernel (a, b);

    float out[4];
    store (out, result);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], 5.0f, EPSILON_F32);
    }
}

/* Purpose: Verify SubKernel works for scalar and SIMD double precision.
   Arrange/Act/Assert as above. */
TEST (SIMD_Kernels, SubKernel)
{
    kernels::SubKernel<double> kernel;

    EXPECT_NEAR (kernel (10.0, 3.0), 7.0, EPSILON_F64);

    float64x2 a      = set1<double> (10.0);
    float64x2 b      = set1<double> (3.0);
    float64x2 result = kernel (a, b);

    double out[2];
    store (out, result);
    for (int i = 0; i < 2; i++)
    {
        EXPECT_NEAR (out[i], 7.0, EPSILON_F64);
    }
}

/* Purpose: Verify MulKernel scalar behavior. */
TEST (SIMD_Kernels, MulKernel)
{
    kernels::MulKernel<float> kernel;

    EXPECT_NEAR (kernel (4.0f, 5.0f), 20.0f, EPSILON_F32);
}

/* Purpose: Verify ScaleKernel scalar and SIMD behavior. */
TEST (SIMD_Kernels, ScaleKernel)
{
    kernels::ScaleKernel<float> kernel (2.5f);

    EXPECT_NEAR (kernel (4.0f), 10.0f, EPSILON_F32);

    float32x4 a      = set1<float> (4.0f);
    float32x4 result = kernel (a);

    float out[4];
    store (out, result);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], 10.0f, EPSILON_F32);
    }
}

/* Purpose: Verify FillKernel provides consistent scalar and SIMD fill values. */
TEST (SIMD_Kernels, FillKernel)
{
    kernels::FillKernel<float> kernel (3.14f);

    EXPECT_NEAR (kernel.scalar_value(), 3.14f, EPSILON_F32);

    float32x4 result = kernel.simd_value();
    float out[4];
    store (out, result);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_NEAR (out[i], 3.14f, EPSILON_F32);
    }
}

/* Purpose: Verify MACKernel (multiply-accumulate) scalar operation. */
TEST (SIMD_Kernels, MACKernel)
{
    kernels::MACKernel<float> kernel;

    // acc + a * b = 10 + 2 * 3 = 16
    EXPECT_NEAR (kernel (10.0f, 2.0f, 3.0f), 16.0f, EPSILON_F32);
}

/* Purpose: Verify LerpKernel behavior for typical and edge interpolation factors. */
TEST (SIMD_Kernels, LerpKernel)
{
    kernels::LerpKernel<float> kernel (0.5f);

    // a + 0.5 * (b - a) = 0 + 0.5 * 10 = 5
    EXPECT_NEAR (kernel (0.0f, 10.0f), 5.0f, EPSILON_F32);

    // Edge cases
    kernels::LerpKernel<float> kernel_zero (0.0f);
    EXPECT_NEAR (kernel_zero (2.0f, 8.0f), 2.0f, EPSILON_F32);

    kernels::LerpKernel<float> kernel_one (1.0f);
    EXPECT_NEAR (kernel_one (2.0f, 8.0f), 8.0f, EPSILON_F32);
}

/* Purpose: Verify ClampKernel clamps below/within/above range appropriately. */
TEST (SIMD_Kernels, ClampKernel)
{
    kernels::ClampKernel<float> kernel (-1.0f, 1.0f);

    EXPECT_NEAR (kernel (-2.0f), -1.0f, EPSILON_F32);
    EXPECT_NEAR (kernel (0.5f), 0.5f, EPSILON_F32);
    EXPECT_NEAR (kernel (2.0f), 1.0f, EPSILON_F32);
}

/* Purpose: Verify AbsKernel sign handling. */
TEST (SIMD_Kernels, AbsKernel)
{
    kernels::AbsKernel<float> kernel;

    EXPECT_NEAR (kernel (-5.0f), 5.0f, EPSILON_F32);
    EXPECT_NEAR (kernel (5.0f), 5.0f, EPSILON_F32);
}

// ============================================================================
// BLOCK OPERATIONS - BASIC
// ============================================================================

/* Purpose: AddBlock verifies element-wise addition of two float arrays.
   Arrange: prepare arrays; Act: call ops::add; Assert: expected values. */
TEST (SIMD_BlockOps, AddBlock)
{
    constexpr std::size_t N = 16;
    float dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<float> (i);
        src[i] = static_cast<float> (i + 10);
    }

    ops::add (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], static_cast<float> (i + i + 10), EPSILON_F32);
    }
}

/* Purpose: SubBlock verifies element-wise subtraction for doubles. */
TEST (SIMD_BlockOps, SubBlock)
{
    constexpr std::size_t N = 16;
    double dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<double> (i + 20);
        src[i] = static_cast<double> (i);
    }

    ops::sub (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], 20.0, EPSILON_F64);
    }
}

/* Purpose: MulBlock verifies multiplication by constants. */
TEST (SIMD_BlockOps, MulBlock)
{
    constexpr std::size_t N = 16;
    float dst[N], src[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<float> (i + 1);
        src[i] = 2.0f;
    }

    ops::mul (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], static_cast<float> ((i + 1) * 2), EPSILON_F32);
    }
}

/* Purpose: ScaleBlock verifies scaling an array by a scalar. */
TEST (SIMD_BlockOps, ScaleBlock)
{
    constexpr std::size_t N = 32;
    float data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = static_cast<float> (i + 1);
    }

    ops::scale (data, N, 0.5f);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (data[i], static_cast<float> (i + 1) * 0.5f, EPSILON_F32);
    }
}

/* Purpose: CopyBlock verifies copy semantics between buffers. */
TEST (SIMD_BlockOps, CopyBlock)
{
    constexpr std::size_t N = 64;
    float src[N], dst[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        src[i] = static_cast<float> (i) * 3.14f;
        dst[i] = 0.0f;
    }

    ops::copy (dst, src, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], src[i], EPSILON_F32);
    }
}

/* Purpose: FillBlock verifies fill for double arrays. */
TEST (SIMD_BlockOps, FillBlock)
{
    constexpr std::size_t N = 128;
    double data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = static_cast<double> (i);
    }

    ops::fill (data, N, 7.5);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (data[i], 7.5, EPSILON_F64);
    }
}

/* Purpose: MACBlock verifies multiply-accumulate applied element-wise. */
TEST (SIMD_BlockOps, MACBlock)
{
    constexpr std::size_t N = 16;
    float dst[N], src1[N], src2[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i]  = 10.0f;
        src1[i] = 2.0f;
        src2[i] = 3.0f;
    }

    ops::mac (dst, src1, src2, N);

    // dst should be 10 + (2 * 3) = 16
    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], 16.0f, EPSILON_F32);
    }
}

/* Purpose: LerpBlock verifies linear interpolation across arrays. */
TEST (SIMD_BlockOps, LerpBlock)
{
    constexpr std::size_t N = 16;
    float dst[N], a[N], b[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        a[i] = 0.0f;
        b[i] = 10.0f;
    }

    ops::lerp (dst, a, b, 0.5f, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], 5.0f, EPSILON_F32);
    }
}

/* Purpose: ClampBlock verifies elements are clamped into range. */
TEST (SIMD_BlockOps, ClampBlock)
{
    constexpr std::size_t N = 16;
    float data[N]           = { -2.0f, -1.5f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, -3.0f, 3.0f, 0.8f, -0.8f, 1.2f, -1.2f, 0.0f };

    ops::clamp (data, -1.0f, 1.0f, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_GE (data[i], -1.0f);
        EXPECT_LE (data[i], 1.0f);
    }
}

/* Purpose: AbsBlock verifies absolute-value transform across an array. */
TEST (SIMD_BlockOps, AbsBlock)
{
    constexpr std::size_t N = 16;
    float data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = (i % 2 == 0) ? static_cast<float> (i) : -static_cast<float> (i);
    }

    ops::abs (data, N);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_GE (data[i], 0.0f);
        EXPECT_NEAR (data[i], static_cast<float> (i), EPSILON_F32);
    }
}

// ============================================================================
// REDUCTION OPERATIONS
// ============================================================================

/* Purpose: FindMin locates the smallest element in a float array. */
TEST (SIMD_Reductions, FindMin)
{
    constexpr std::size_t N = 100;
    float data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = static_cast<float> (N - i);
    }
    data[42] = -10.0f; // Minimum

    float min_val = ops::find_min (data, N);
    EXPECT_NEAR (min_val, -10.0f, EPSILON_F32);
}

/* Purpose: FindMax locates the largest element in a double array. */
TEST (SIMD_Reductions, FindMax)
{
    constexpr std::size_t N = 100;
    double data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = static_cast<double> (i);
    }
    data[57] = 1000.0; // Maximum

    double max_val = ops::find_max (data, N);
    EXPECT_NEAR (max_val, 1000.0, EPSILON_F64);
}

/* Purpose: Sum verifies summation over float array. */
TEST (SIMD_Reductions, Sum)
{
    constexpr std::size_t N = 100;
    float data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = 1.0f;
    }

    float sum = ops::sum (data, N);
    EXPECT_NEAR (sum, 100.0f, EPSILON_F32);
}

/* Purpose: SumSequence verifies summation over a sequence of doubles. */
TEST (SIMD_Reductions, SumSequence)
{
    constexpr std::size_t N = 100;
    double data[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        data[i] = static_cast<double> (i + 1);
    }

    double sum      = ops::sum (data, N);
    double expected = (N * (N + 1)) / 2.0; // 1+2+...+100 = 5050
    EXPECT_NEAR (sum, expected, EPSILON_F64);
}

/* Purpose: DotProduct verifies element-wise multiplication and reduction. */
TEST (SIMD_Reductions, DotProduct)
{
    constexpr std::size_t N = 16;
    float a[N], b[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        a[i] = 1.0f;
        b[i] = 2.0f;
    }

    float dot = ops::dot_product (a, b, N);
    EXPECT_NEAR (dot, 32.0f, EPSILON_F32); // 16 * (1 * 2)
}

/* Purpose: DotProductSquares verifies dot product of equal sequences. */
TEST (SIMD_Reductions, DotProductSquares)
{
    constexpr std::size_t N = 16;
    double a[N], b[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        a[i] = static_cast<double> (i + 1);
        b[i] = static_cast<double> (i + 1);
    }

    double dot = ops::dot_product (a, b, N);
    // Sum of squares: 1^2 + 2^2 + ... + 16^2 = 1496
    EXPECT_NEAR (dot, 1496.0, EPSILON_F64);
}

// ============================================================================
// Fill dispatch
// ============================================================================

/* Purpose: small_buffer_fills_correctly verifies fill on a small float buffer
   exercises the normal fast path. */
TEST (SIMD_BlockOps, fill_small_buffer_fills_correctly)
{
    constexpr std::size_t N = 64; // well below any L1
    float data[N]           = {};
    CASPI::SIMD::ops::fill (data, N, 3.14f);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (data[i], 3.14f, kEpsF);
}

/* Purpose: large_buffer_fills_correctly verifies fill for buffers larger
   than L1 to exercise the non-temporal (NT) store path. */
TEST (SIMD_BlockOps, fill_large_buffer_fills_correctly)
{
    // Allocate larger than L1 to exercise the NT path
    const std::size_t N = CASPI::SIMD::Strategy::l1_data_bytes() / sizeof (float) * 3;
    std::vector<float> data (N, 0.f);
    CASPI::SIMD::ops::fill (data.data(), N, 7.77f);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (data[i], 7.77f, kEpsF) << "at i=" << i;
}

/* Purpose: double_precision_fills_correctly verifies fill works for double arrays. */
TEST (SIMD_BlockOps, fill_double_precision_fills_correctly)
{
    constexpr std::size_t N = 128;
    std::vector<double> data (N, 0.0);
    CASPI::SIMD::ops::fill (data.data(), N, 2.71828);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (data[i], 2.71828, kEpsD);
}

/* Purpose: zero_count_is_nop verifies that calling fill with count=0 leaves
   the scalar unchanged (edge case). */
TEST (SIMD_BlockOps, fill_zero_count_is_nop)
{
    float v = 99.f;
    CASPI::SIMD::ops::fill (&v, 0, 1.f);
    EXPECT_NEAR (v, 99.f, kEpsF); // unchanged
}

// ============================================================================
// copy_with_gain
// ============================================================================

/* Purpose: basic_correctness ensures copy_with_gain multiplies source by
   gain into destination. */
TEST (SIMD_BlockOps, copy_with_gain_basic_correctness)
{
    constexpr std::size_t N = 32;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i + 1);

    CASPI::SIMD::ops::copy_with_gain (dst, src, N, 2.5f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], static_cast<float> (i + 1) * 2.5f, kEpsF);
}

/* Purpose: gain_zero_produces_zeros checks zero gain results in zeros. */
TEST (SIMD_BlockOps, copy_with_gain_gain_zero_produces_zeros)
{
    constexpr std::size_t N = 16;
    float src[N], dst[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = 1.f;

    CASPI::SIMD::ops::copy_with_gain (dst, src, N, 0.f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 0.f, kEpsF);
}

/* Purpose: does_not_modify_src asserts that the source buffer is unchanged
   by copy_with_gain (no in-place mutation of source). */
TEST (SIMD_BlockOps, copy_with_gain_does_not_modify_src)
{
    constexpr std::size_t N = 16;
    float src[N], dst[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i);

    CASPI::SIMD::ops::copy_with_gain (dst, src, N, 0.5f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (src[i], static_cast<float> (i), kEpsF);
}

/* Purpose: odd_size ensures copy_with_gain handles non-multiple-of-lane sizes. */
TEST (SIMD_BlockOps, copy_with_gain_odd_size)
{
    constexpr std::size_t N = 13;
    float src[N], dst[N] = {};
    for (std::size_t i = 0; i < N; ++i)
        src[i] = 1.f;

    CASPI::SIMD::ops::copy_with_gain (dst, src, N, 3.f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 3.f, kEpsF);
}

// ============================================================================
// accumulate_with_gain
// ============================================================================

/* Purpose: basic_correctness verifies accumulate_with_gain does dst += src * gain. */
TEST (BlockOps_AccumWithGain, basic_correctness)
{
    constexpr std::size_t N = 32;
    float dst[N], src[N];
    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = 10.f;
        src[i] = 2.f;
    }

    CASPI::SIMD::ops::accumulate_with_gain (dst, src, N, 3.f);

    // dst[i] = 10 + 2 * 3 = 16
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 16.f, kEpsF);
}

/* Purpose: zero_gain_leaves_dst_unchanged checks zero gain is a no-op. */
TEST (BlockOps_AccumWithGain, zero_gain_leaves_dst_unchanged)
{
    constexpr std::size_t N = 16;
    float dst[N], src[N];
    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = 5.f;
        src[i] = 99.f;
    }

    CASPI::SIMD::ops::accumulate_with_gain (dst, src, N, 0.f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst[i], 5.f, kEpsF);
}

/* Purpose: unity_gain_matches_plain_add compares accumulate_with_gain with add
   for gain==1 to ensure equivalence. */
TEST (BlockOps_AccumWithGain, unity_gain_matches_plain_add)
{
    constexpr std::size_t N = 64;
    float dst1[N], dst2[N], src[N];
    for (std::size_t i = 0; i < N; ++i)
    {
        dst1[i] = dst2[i] = static_cast<float> (i);
        src[i]            = static_cast<float> (N - i);
    }

    CASPI::SIMD::ops::accumulate_with_gain (dst1, src, N, 1.f);
    CASPI::SIMD::ops::add (dst2, src, N);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (dst1[i], dst2[i], kEpsF);
}

// ============================================================================
// pan
// ============================================================================

TEST (BlockOps_Pan, mono_to_stereo_equal_power)
{
    constexpr std::size_t N = 32;
    float src[N], left[N], right[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = 1.f;

    const float gl = 0.7071f; // cos(45°) ≈ 1/√2
    const float gr = 0.7071f;

    CASPI::SIMD::ops::pan (left, right, src, N, gl, gr);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (left[i], gl, kEpsF);
        EXPECT_NEAR (right[i], gr, kEpsF);
    }
}

TEST (BlockOps_Pan, hard_left)
{
    constexpr std::size_t N = 16;
    float src[N], left[N], right[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = 2.f;

    CASPI::SIMD::ops::pan (left, right, src, N, 1.f, 0.f);

    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (left[i], 2.f, kEpsF);
        EXPECT_NEAR (right[i], 0.f, kEpsF);
    }
}

TEST (BlockOps_Pan, src_not_modified)
{
    constexpr std::size_t N = 16;
    float src[N], left[N], right[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i);

    CASPI::SIMD::ops::pan (left, right, src, N, 0.5f, 0.5f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (src[i], static_cast<float> (i), kEpsF);
}

TEST (BlockOps_Pan, left_plus_right_equals_double_gain_times_src)
{
    // With equal gains, left+right = 2*g*src
    constexpr std::size_t N = 32;
    float src[N], left[N], right[N];
    for (std::size_t i = 0; i < N; ++i)
        src[i] = static_cast<float> (i + 1);

    CASPI::SIMD::ops::pan (left, right, src, N, 0.4f, 0.6f);

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_NEAR (left[i] + right[i], src[i] * 1.f, kEpsF); // 0.4+0.6=1
}
