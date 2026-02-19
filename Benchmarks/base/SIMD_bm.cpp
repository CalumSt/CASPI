/**
 * @file SIMD_Buffer_bm.cpp
 * @brief SIMD block-operation benchmarks
 *
 * Design notes:
 *
 * Baselines
 * ---------
 * Each operation has three variants:
 *   _SIMD      - ops:: path (this library)
 *   _AutoVec   - plain loop, compiler free to auto-vectorise
 *   _Scalar    - plain loop with volatile sink to prevent vectorisation
 *                (replaces the previous #pragma loop(no_vector) which is
 *                MSVC-only and silently ignored elsewhere)
 *
 * Preventing scalar optimisation without pragmas
 * -----------------------------------------------
 * We read each element through a volatile pointer before writing back.
 * This creates a dependency chain the compiler cannot vectorise while
 * still producing the correct result for the benchmark.
 *
 * Drift prevention for in-place ops
 * -----------------------------------
 * Operations like scale() that mutate a buffer in-place would otherwise
 * converge to zero (or infinity) across iterations.  We use
 * benchmark::DoNotOptimize plus a periodic state.KeepRunning() reset,
 * or we just reinitialise the buffer in SetUp-style lambdas.
 * The simplest portable fix: store a fresh copy and restore it inside
 * the iteration loop.  This adds memcpy cost to both SIMD and scalar
 * variants equally, so comparisons remain fair.
 *
 * Alignment
 * ----------
 * Aligned buffers: std::aligned_alloc(32, …) on POSIX / _aligned_malloc on MSVC.
 * Misaligned buffers: guaranteed +1 float offset from a 32-byte-aligned base,
 * so pointer is always 4 bytes past a 32-byte boundary regardless of allocator.
 *
 * Sizes
 * -----
 *   64   - L1-hot, overhead dominated
 *  512   - typical real-time audio block
 * 2048   - large block (still L1/L2)
 * 16384  - L3 working set (real throughput limit)
 * 65536  - beyond L3 on most desktop CPUs (memory bandwidth limit)
 *
 * Metrics
 * -------
 * SetBytesProcessed: enables GB/s in benchmark output.
 * SetItemsProcessed: enables items/s.
 * Both are reported so the runner can choose the most meaningful one.
 */

#include "base/caspi_SIMD.h"

#include <benchmark/benchmark.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace CASPI::SIMD;

// ============================================================================
// Helpers
// ============================================================================

// Sizes that span L1 → beyond-L3 on typical desktop x86
static const std::vector<int64_t> kSizes = { 64, 512, 2048, 16384, 65536 };

// ---------------------------------------------------------------------------
// Platform-agnostic aligned allocation
// ---------------------------------------------------------------------------
static float* alloc_aligned (std::size_t count, std::size_t alignment = 32)
{
#if defined(_MSC_VER)
    return static_cast<float*> (_aligned_malloc (count * sizeof (float), alignment));
#else
    void* p = nullptr;
    ::posix_memalign (&p, alignment, count * sizeof (float));
    return static_cast<float*> (p);
#endif
}

static void free_aligned (float* p)
{
#if defined(_MSC_VER)
    _aligned_free (p);
#else
    ::free (p);
#endif
}

// RAII wrapper
struct AlignedBuffer
{
    float* ptr;
    std::size_t size;

    explicit AlignedBuffer (std::size_t n, std::size_t align = 32)
        : ptr (alloc_aligned (n, align)), size (n) {}

    ~AlignedBuffer() { free_aligned (ptr); }

    void fill (float v) const { std::fill (ptr, ptr + size, v); }

    // disallow copy
    AlignedBuffer (const AlignedBuffer&)            = delete;
    AlignedBuffer& operator= (const AlignedBuffer&) = delete;
};

// ---------------------------------------------------------------------------
// Scalar-force sink: reads through volatile to defeat vectorisation
// ---------------------------------------------------------------------------
// Usage: write volatile_read(src[i]) into dst[i].
// The volatile load prevents the compiler from treating the loop as SIMD-able.
template <typename T>
CASPI_NOINLINE static T volatile_read (const T* p, std::size_t i) noexcept
{
    const volatile T* vp = p;
    return vp[i];
}

// ============================================================================
// BM_Scale — data[i] *= factor
//
// Drift fix: keep a fresh copy and restore before each outer iteration.
// ============================================================================

static void BM_Scale_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);
    data.fill (1.0f);
    backup.fill (1.0f);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float)); // restore
        ops::scale (data.ptr, n, 0.5f);
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2); // read + write
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Scale_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);
    data.fill (1.0f);
    backup.fill (1.0f);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
            data.ptr[i] *= 0.5f;
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Scale_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);
    data.fill (1.0f);
    backup.fill (1.0f);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
            data.ptr[i] = volatile_read (data.ptr, i) * 0.5f; // defeats vectorisation
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Scale_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Scale_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Scale_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Add — dst[i] += src[i]
// ============================================================================

static void BM_Add_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    dst.fill (1.0f);
    src.fill (2.0f);

    for (auto _ : state)
    {
        ops::add (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3); // 2 reads + 1 write
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Add_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    dst.fill (1.0f);
    src.fill (2.0f);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] += src.ptr[i];
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Add_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    dst.fill (1.0f);
    src.fill (2.0f);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = volatile_read (dst.ptr, i) + volatile_read (src.ptr, i);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Add_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Add_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Add_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Fill — dst[i] = value
// ============================================================================

static void BM_Fill_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n);

    for (auto _ : state)
    {
        ops::fill (dst.ptr, n, 1.5f);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Fill_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n);

    for (auto _ : state)
    {
        std::fill (dst.ptr, dst.ptr + n, 1.5f);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Fill_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n);
    const volatile float val = 1.5f; // volatile constant prevents constant-prop

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = val;
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Fill_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Fill_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Fill_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Copy — dst[i] = src[i]
// ops::copy now delegates to memcpy — verify it is not slower
// ============================================================================

static void BM_Copy_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    src.fill (1.5f);

    for (auto _ : state)
    {
        ops::copy (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Copy_Memcpy (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    src.fill (1.5f);

    for (auto _ : state)
    {
        std::memcpy (dst.ptr, src.ptr, n * sizeof (float));
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Copy_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), src (n);
    src.fill (1.5f);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = volatile_read (src.ptr, i);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Copy_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Copy_Memcpy)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Copy_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_MAC — dst[i] += a[i] * b[i]
// ============================================================================

static void BM_MAC_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n), backup_dst (n);
    dst.fill (0.0f);
    a.fill (2.0f);
    b.fill (3.0f);
    backup_dst.fill (0.0f);

    for (auto _ : state)
    {
        std::memcpy (dst.ptr, backup_dst.ptr, n * sizeof (float)); // prevent unbounded growth
        ops::mac (dst.ptr, a.ptr, b.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 4); // 3 reads + 1 write
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_MAC_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n), backup_dst (n);
    dst.fill (0.0f);
    a.fill (2.0f);
    b.fill (3.0f);
    backup_dst.fill (0.0f);

    for (auto _ : state)
    {
        std::memcpy (dst.ptr, backup_dst.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] += a.ptr[i] * b.ptr[i];
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 4);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_MAC_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n), backup_dst (n);
    dst.fill (0.0f);
    a.fill (2.0f);
    b.fill (3.0f);
    backup_dst.fill (0.0f);

    for (auto _ : state)
    {
        std::memcpy (dst.ptr, backup_dst.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = volatile_read (dst.ptr, i) + volatile_read (a.ptr, i) * volatile_read (b.ptr, i);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 4);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_MAC_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_MAC_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_MAC_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Lerp — dst[i] = a[i] + t*(b[i]-a[i])
// (Previously missing from the benchmark suite entirely)
// ============================================================================

static void BM_Lerp_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n);
    a.fill (0.0f);
    b.fill (1.0f);

    for (auto _ : state)
    {
        ops::lerp (dst.ptr, a.ptr, b.ptr, 0.5f, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3); // 2 reads + 1 write
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Lerp_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n);
    a.fill (0.0f);
    b.fill (1.0f);
    const float t = 0.5f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = a.ptr[i] + t * (b.ptr[i] - a.ptr[i]);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Lerp_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer dst (n), a (n), b (n);
    a.fill (0.0f);
    b.fill (1.0f);
    const float t = 0.5f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = volatile_read (a.ptr, i) + t * (volatile_read (b.ptr, i) - volatile_read (a.ptr, i));
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Lerp_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Lerp_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Lerp_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Clamp — data[i] = clamp(data[i], -1, 1)
// Uses random data that won't collapse after multiple iterations
// ============================================================================

static void BM_Clamp_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        ops::clamp (data.ptr, -1.0f, 1.0f, n);
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Clamp_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
        {
            if (data.ptr[i] < -1.0f) data.ptr[i] = -1.0f;
            if (data.ptr[i] >  1.0f) data.ptr[i] =  1.0f;
        }
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Clamp_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
        {
            float v = volatile_read (data.ptr, i);
            if (v < -1.0f) v = -1.0f;
            if (v >  1.0f) v =  1.0f;
            data.ptr[i] = v;
        }
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Clamp_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Clamp_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Clamp_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Abs — data[i] = |data[i]|
// (Previously missing)
// ============================================================================

static void BM_Abs_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        ops::abs (data.ptr, n);
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Abs_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
            data.ptr[i] = std::abs (data.ptr[i]);
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Abs_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n), backup (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = dist (rng);

    for (auto _ : state)
    {
        std::memcpy (data.ptr, backup.ptr, n * sizeof (float));
        for (std::size_t i = 0; i < n; ++i)
        {
            float v = volatile_read (data.ptr, i);
            data.ptr[i] = v < 0.0f ? -v : v;
        }
        benchmark::DoNotOptimize (data.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Abs_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Abs_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Abs_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Sum — sum all elements (reduction)
// ============================================================================

static void BM_Sum_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);
    data.fill (1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::sum (data.ptr, n);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sum_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);
    data.fill (1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            result += data.ptr[i];
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sum_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);
    data.fill (1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            result += volatile_read (data.ptr, i);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Sum_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sum_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sum_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_DotProduct — sum(a[i]*b[i])
// ============================================================================

static void BM_DotProduct_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer a (n), b (n);
    a.fill (1.0f);
    b.fill (2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::dot_product (a.ptr, b.ptr, n);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_DotProduct_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer a (n), b (n);
    a.fill (1.0f);
    b.fill (2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            result += a.ptr[i] * b.ptr[i];
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_DotProduct_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer a (n), b (n);
    a.fill (1.0f);
    b.fill (2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            result += volatile_read (a.ptr, i) * volatile_read (b.ptr, i);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_DotProduct_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_DotProduct_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_DotProduct_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_FindMax — find maximum element
// ============================================================================

static void BM_FindMax_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::find_max (data.ptr, n);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_FindMax_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        float m = data.ptr[0];
        for (std::size_t i = 1; i < n; ++i)
            if (data.ptr[i] > m) m = data.ptr[i];
        result = m;
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_FindMax_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        float m = volatile_read (data.ptr, 0);
        for (std::size_t i = 1; i < n; ++i)
        {
            float v = volatile_read (data.ptr, i);
            if (v > m) m = v;
        }
        result = m;
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_FindMax_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_FindMax_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_FindMax_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_FindMin — find minimum element (previously missing)
// ============================================================================

static void BM_FindMin_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::find_min (data.ptr, n);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_FindMin_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        float m = data.ptr[0];
        for (std::size_t i = 1; i < n; ++i)
            if (data.ptr[i] < m) m = data.ptr[i];
        result = m;
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_FindMin_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n);

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        data.ptr[i] = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        float m = volatile_read (data.ptr, 0);
        for (std::size_t i = 1; i < n; ++i)
        {
            float v = volatile_read (data.ptr, i);
            if (v < m) m = v;
        }
        result = m;
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_FindMin_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_FindMin_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_FindMin_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Alignment — quantify aligned vs misaligned cost for scale()
//
// Misalignment: guaranteed +1 float (4 bytes) from a 32-byte-aligned base.
// The previous version used &vector[1] which may still be 16-byte aligned
// depending on allocator.
// ============================================================================

static void BM_Alignment_Aligned (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    // +1 for the guard element; ptr itself is 32-byte aligned
    AlignedBuffer data (n + 1);
    float* p = data.ptr; // guaranteed 32-byte aligned
    std::fill (p, p + n, 1.0f);

    for (auto _ : state)
    {
        ops::scale (p, n, 0.5f);
        benchmark::DoNotOptimize (p[0]);
        // restore so scale doesn't converge to 0
        std::fill (p, p + n, 1.0f);
    }

    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Alignment_Misaligned (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    AlignedBuffer data (n + 1);
    float* p = data.ptr + 1; // guaranteed +4 bytes from 32-byte boundary → misaligned
    std::fill (p, p + n, 1.0f);

    for (auto _ : state)
    {
        ops::scale (p, n, 0.5f);
        benchmark::DoNotOptimize (p[0]);
        std::fill (p, p + n, 1.0f);
    }

    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Alignment_Aligned)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Alignment_Misaligned)->ArgsProduct ({ kSizes });

// ============================================================================
// BM_Double — double precision equivalents for float32 ops
// (Previously zero double benchmarks despite float64x2 support)
// ============================================================================

static void BM_Scale_Double_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> data (n, 1.0), backup (n, 1.0);

    for (auto _ : state)
    {
        std::copy (backup.begin(), backup.end(), data.begin());
        ops::scale (data.data(), n, 0.5);
        benchmark::DoNotOptimize (data[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_DotProduct_Double_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> a (n, 1.0), b (n, 2.0);
    double result = 0.0;

    for (auto _ : state)
    {
        result = ops::dot_product (a.data(), b.data(), n);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Scale_Double_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_DotProduct_Double_SIMD)->ArgsProduct ({ kSizes });