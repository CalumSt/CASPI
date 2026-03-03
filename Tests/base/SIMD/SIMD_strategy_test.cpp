#include "SIMD_test_helpers.h"
#include <gtest/gtest.h>

/*
- Strategy.alignment_AlreadyAlignedReturnsZero
  - What: Verify samples_to_alignment returns 0 for an already-aligned float*.
  - Why: Zero-shift is the expected minimal shift; ensures aligned pointers are handled correctly.

- Strategy.alignment_OneSampleMisalignedReturnsOne
  - What: Verify samples_to_alignment returns 1 for a pointer misaligned by one byte.
  - Why: Confirms the function counts samples correctly for small misalignments.

- Strategy.alignment_AlignmentAndMinimality
  - What: Iterate offsets to confirm returned shift produces an aligned pointer and is minimal.
  - Why: Property-based check across offsets to ensure correctness and minimality of computed shift.

- Strategy.alignment_UpperBound
  - What: Ensure the returned sample count does not exceed alignment / sizeof(float).
  - Why: Enforces expected upper bound on samples_to_alignment result.

- Strategy.alignment_AlignedAdd
  - What: Test ops::add when source and destination are aligned to 16 bytes.
  - Why: Validate vectorized path correctness for aligned buffers.

- Strategy.alignment_UnalignedAdd
  - What: Test ops::add when buffers are intentionally misaligned.
  - Why: Ensure prologue/epilogue handling and unaligned paths produce correct results.

- Strategy.alignment_PrologueOnly
  - What: Test ops::add with an array smaller than SIMD lane width (only scalar prologue runs).
  - Why: Confirm scalar-only path correctness when no SIMD body is possible.

- Strategy.alignment_WithPrologueAndEpilogue
  - What: Test ops::scale with a misaligned start that yields prologue, SIMD body, and epilogue.
  - Why: Validate correct processing across prologue/body/epilogue boundaries.

- Strategy.l1_data_bytes_is_positive_and_power_of_two
  - What: Verify l1_data_bytes() returns a positive power-of-two value.
  - Why: Many cache-aware heuristics assume L1 size is a power of two; this is a sanity check.

- Strategy.l1_data_bytes_in_plausible_range
  - What: Ensure l1_data_bytes() returns a value in a broad plausible range (1 KB–2 MB).
  - Why: Catch mis-detections that would produce absurd values.

- Strategy.nt_threshold_is_consistent
  - What: Check nt_store_threshold_runtime<float>() equals (2 * L1 / sizeof(float)).
  - Why: Ensure runtime threshold relation matches the strategy formula used elsewhere.

- Strategy.compile_time_constant_is_nonzero
  - What: Confirm compile-time L1 constant is non-zero.
  - Why: Protect users relying on compile-time constants and static_asserts.

- Strategy.repeated_calls_are_identical
  - What: Verify that repeated calls to l1_data_bytes() return identical values.
  - Why: Ensure idempotence and safe caching behavior of the query function.

Redundant tests (can be combined or parameterized):
- The explicit AlreadyAlignedReturnsZero case is covered by AlignmentAndMinimality's iteration but is useful as a focused unit test.
- UpperBound repeats a bound-check that AlignmentAndMinimality implies; kept for focused assertion.
- AlignedAdd and UnalignedAdd plus PrologueOnly and WithPrologueAndEpilogue overlap in exercising add/scale paths across alignment scenarios; these can be parameterized but are left separate for clarity.
*/

/*
   ALIGNMENT TESTS
*/

/*
- What: Ensure that when the pointer is already aligned to the requested alignment, samples_to_alignment returns 0.
- Why: Aligned pointers should require zero samples to reach an alignment boundary.
*/
TEST(Strategy, alignment_AlreadyAlignedReturnsZero)
{
    /* Arrange: create a buffer aligned to 32 bytes */
    alignas(32) float buffer[8];

    /* Act: compute required samples to reach alignment */
    const std::size_t s =
        CASPI::SIMD::Strategy::samples_to_alignment<32>(buffer);

    /* Assert: no additional samples are required for an already-aligned ptr */
    EXPECT_EQ(s, 0u);
}

/*
- What: Verify that a pointer misaligned by one byte will require exactly one sample.
- Why: Confirms the function counts samples correctly for small misalignments.
*/
TEST(Strategy, alignment_OneSampleMisalignedReturnsOne)
{
    /* Arrange: raw storage aligned to 32 bytes, force a one-byte misaligned float* */
    alignas(32) std::uint8_t raw[64];
    auto ptr = reinterpret_cast<float*>(raw + 31);

    /* Act: compute required samples to reach 32-byte alignment */
    const std::size_t s =
        CASPI::SIMD::Strategy::samples_to_alignment<32>(ptr);

    /* Assert: exactly one sample should be required to reach alignment */
    EXPECT_EQ(s, 1u);
}

/*
- What: Exhaustively check offsets to ensure returned shift aligns pointer and is minimal.
- Why: Property-style verification across many offsets to catch edge cases.
*/
TEST(Strategy, alignment_AlignmentAndMinimality)
{
    /* Arrange: allocate a larger raw buffer aligned to 64 bytes */
    alignas(64) std::uint8_t raw[128];

    for (std::size_t offset = 0; offset < 32; ++offset)
    {
        auto ptr = reinterpret_cast<float*>(raw + offset);

        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);

        /* Skip impossible cases where ptr isn't aligned to float's size */
        if ((addr % sizeof(float)) != 0)
        {
            continue;
        }

        /* Act: compute the number of samples to align */
        const std::size_t s =
            CASPI::SIMD::Strategy::samples_to_alignment<32>(ptr);

        const float* aligned = ptr + s;

        /* Assert: computed pointer must be aligned */
        EXPECT_TRUE (CASPI::SIMD::Strategy::is_aligned<32>(aligned))
            << "offset=" << offset;

        /* If s>0, ensure previous element is not aligned */
        if (s > 0)
        {
            const float* prev = ptr + (s - 1);

            EXPECT_FALSE (CASPI::SIMD::Strategy::is_aligned<32>(prev))
                << "offset=" << offset;
        }
    }
}

/*
- What: Ensure samples_to_alignment returns a value bounded by alignment/sizeof(float).
- Why: Enforce theoretical upper bound on required samples.
*/
TEST(Strategy, alignment_UpperBound)
{
    /* Arrange: buffer aligned to 64 bytes */
    alignas(64) std::uint8_t raw[128];

    for (std::size_t offset = 0; offset < 32; ++offset)
    {
        float* ptr = reinterpret_cast<float*>(raw + offset);

        /* Act: compute number of samples to align */
        const std::size_t s =
            CASPI::SIMD::Strategy::samples_to_alignment<32>(ptr);

        /* Assert: s must be no more than alignment / sizeof(float) */
        EXPECT_LE(s, 32u / sizeof(float));
    }
}

/*
   SIMD operation correctness with alignment scenarios
*/

/*
- Strategy.alignment_AlignedAdd
  - What: Validate element-wise add when arrays are 16-byte aligned.
  - Why: Ensure vectorized aligned path produces correct arithmetic.
*/
TEST (Strategy, alignment_AlignedAdd)
{
    /* Arrange: two arrays aligned to 16 bytes */
    constexpr std::size_t N = 32;
    alignas (16) float dst[N];
    alignas (16) float src[N];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<float> (i);
        src[i] = 10.0f;
    }

    /* Act: perform SIMD-aware add */
    CASPI::SIMD::ops::add (dst, src, N);

    /* Assert: each element was incremented by 10 (approximate) */
    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], static_cast<float> (i) + 10.0f, EPSILON_F32);
    }
}

/*
- Strategy.alignment_UnalignedAdd
  - What: Validate add when buffers are misaligned (offset start).
  - Why: Ensure prologue/epilogue and unaligned code paths work correctly.
*/
TEST (Strategy, alignment_UnalignedAdd)
{
    /* Arrange: allocate buffers with an extra element and start at offset 1 */
    constexpr std::size_t N = 32;
    float buffer_dst[N + 1];
    float buffer_src[N + 1];

    /* Start at offset 1 to ensure misalignment */
    float* dst = &buffer_dst[1];
    float* src = &buffer_src[1];

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<float> (i);
        src[i] = 10.0f;
    }

    /* Act: perform add on misaligned buffers */
    CASPI::SIMD::ops::add (dst, src, N);

    /* Assert: verify results are correct regardless of alignment */
    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], static_cast<float> (i) + 10.0f, EPSILON_F32);
    }
}

/*
- Strategy.alignment_PrologueOnly
  - What: Test add for arrays that fit entirely in the scalar prologue (no SIMD body).
  - Why: Ensure scalar-only path correctness for small arrays.
*/
TEST (Strategy, alignment_PrologueOnly)
{
    /* Arrange: small arrays smaller than typical SIMD width */
    constexpr std::size_t N = 3;
    float dst[N]            = { 1.0f, 2.0f, 3.0f };
    float src[N]            = { 10.0f, 20.0f, 30.0f };

    /* Act: perform add which should be handled by scalar prologue code */
    CASPI::SIMD::ops::add (dst, src, N);

    /* Assert: results computed element-wise */
    EXPECT_NEAR (dst[0], 11.0f, EPSILON_F32);
    EXPECT_NEAR (dst[1], 22.0f, EPSILON_F32);
    EXPECT_NEAR (dst[2], 33.0f, EPSILON_F32);
}

/*
- Strategy.alignment_WithPrologueAndEpilogue
  - What: Test scale with a misaligned start producing prologue, SIMD body, and epilogue.
  - Why: Validate correct handling across all phases of a vectorized loop.
*/
TEST (Strategy, alignment_WithPrologueAndEpilogue)
{
    /* Arrange: size constructed to include prologue, SIMD body and epilogue */
    constexpr std::size_t N = 17;
    float buffer[N + 4]; /* Extra space for alignment testing */
    float* dst = &buffer[1]; /* Start at misaligned position */

    for (std::size_t i = 0; i < N; ++i)
    {
        dst[i] = static_cast<float> (i);
    }

    /* Act: apply a scale operation which should vectorize the middle region */
    CASPI::SIMD::ops::scale (dst, N, 2.0f);

    /* Assert: every element scaled by 2 */
    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_NEAR (dst[i], static_cast<float> (i) * 2.0f, EPSILON_F32);
    }
}

/*
   Strategy (runtime/compile-time policy) tests
*/

/*
- Strategy.l1_data_bytes_is_positive_and_power_of_two
  - What: l1_data_bytes() > 0 and is a power of two.
  - Why: Many cache-aware heuristics depend on L1 being power-of-two and positive.
*/
TEST (Strategy, l1_data_bytes_is_positive_and_power_of_two)
{
    /* Arrange/Act: query L1 size */
    const std::size_t l1 = CASPI::SIMD::Strategy::l1_data_bytes();

    /* Assert: positive */
    EXPECT_GT (l1, 0u);

    /* Assert: power of two */
    EXPECT_EQ (l1 & (l1 - 1), 0u) << "L1=" << l1 << " is not a power of 2";
}

/*
- Strategy.l1_data_bytes_in_plausible_range
  - What: l1_data_bytes() within 1 KB – 2 MB.
  - Why: Catch mis-detections returning absurd values.
*/
TEST (Strategy, l1_data_bytes_in_plausible_range)
{
    /* Arrange/Act: query L1 */
    const std::size_t l1 = CASPI::SIMD::Strategy::l1_data_bytes();

    /* Assert: plausible range */
    EXPECT_GE (l1, 1024u);
    EXPECT_LE (l1, 2 * 1024 * 1024u);
}

/*
- Strategy.nt_threshold_is_consistent
  - What: nt_store_threshold_runtime<float>() equals (2 * L1 / sizeof(float)).
  - Why: Verify the expected relation between threshold and L1 size.
*/
TEST (Strategy, nt_threshold_is_consistent)
{
    /* Arrange/Act: query threshold and L1 */
    const std::size_t thresh = CASPI::SIMD::Strategy::nt_store_threshold_runtime<float>();
    const std::size_t l1     = CASPI::SIMD::Strategy::l1_data_bytes();
    std::cout << "L1 data cache size: " << l1 << " bytes" << std::endl;

    /* Assert: threshold formula */
    EXPECT_EQ (thresh, (2 * l1) / 4);
}

/*
- Strategy.compile_time_constant_is_nonzero
  - What: compile-time L1 constant > 0
  - Why: static_assert and compile-time assumptions must hold.
*/
TEST (Strategy, compile_time_constant_is_nonzero)
{
    /* Arrange/Act: evaluate compile-time const */
    constexpr std::size_t ct = CASPI::SIMD::Strategy::l1_data_bytes_compile_time();

    /* Compile-time assert already present; also runtime assert for test harness */
    static_assert (ct > 0, "compile-time L1 must be positive");
    EXPECT_GT (ct, 0u);
}

/*
- Strategy.repeated_calls_are_identical
  - What: repeated calls to l1_data_bytes() return identical values.
  - Why: ensure idempotence and safe caching.
*/
TEST (Strategy, repeated_calls_are_identical)
{
    /* Arrange/Act: call twice */
    const std::size_t first = CASPI::SIMD::Strategy::l1_data_bytes();
    std::cout << "L1 data cache size: " << first << " bytes" << std::endl;
    const std::size_t second = CASPI::SIMD::Strategy::l1_data_bytes();

    /* Assert: identical results */
    EXPECT_EQ (first, second);
}