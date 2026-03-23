/**
 * @file PolyKernel_bm.cpp
 * @brief Benchmarks for PolyKernel approximation block operations.
 *
 * WHAT IS MEASURED
 * ================
 * Three variants per function:
 *
 *   _SIMD      ops:: path: block_op_unary with SIMD kernel
 *   _AutoVec   plain loop calling std:: function, compiler free to vectorise
 *   _Scalar    plain loop with volatile reads to defeat auto-vectorisation
 *
 * The AutoVec baseline is the most meaningful comparison for SIMD: it shows
 * what a modern compiler can achieve on the same algorithm without explicit
 * intrinsics. Scalar shows the single-thread no-vector floor.
 *
 * FUNCTIONS BENCHMARKED
 * =====================
 *   sin_block<float>    — x * P(x²), degree-5, input ∈ [-π/2, π/2]
 *   sin_block<double>   — same, double precision
 *   cos_block<float>    — P(x²), degree-5
 *   cos_block<double>
 *   tanh_block<float>   — x * P(x²), degree-3, input ∈ [-0.65, 0.65]
 *   tanh_block<double>  — x * P(x²), degree-7, input ∈ [-0.60, 0.60]
 *   PolyKernel raw      — raw block_op_unary(poly) without outer multiply
 *                         used to isolate the Horner loop from x*P(x²) overhead
 *
 * SCALAR DEFEAT STRATEGY
 * ======================
 * We read each src element via a volatile pointer before passing to std::sin
 * etc. This creates a per-element load dependency the compiler cannot fuse
 * into a SIMD gather, while still calling the correct libm function.
 *
 * NOTE: std::sin/cos/tanh in the AutoVec/Scalar baselines use libm, which is
 * much slower and more accurate than our degree-5/7 approximations. The
 * comparison is therefore "our SIMD approx vs libm + auto-vec". To compare
 * throughput fairly against an equivalent-quality autovec approximation, see
 * the _PolyLoop variants which manually inline the same Horner coefficients.
 *
 * BUFFER SIZES
 * ============
 *    64  — L1-hot, overhead-dominated
 *   512  — typical real-time audio block (128 frames × 4 channels)
 *  2048  — large audio block
 * 16384  — L2/L3 boundary
 *
 * Sizes beyond 16384 are less useful here because:
 *   - These functions are compute-bound, not memory-bound at audio block sizes
 *   - Real DSP usage is always inside an audio callback with small N
 *
 * METRICS
 * =======
 * SetBytesProcessed: read + write bytes → GB/s in output
 * SetItemsProcessed: elements/s (most useful for GFLOP/s mental model)
 */

#include "base/caspi_SIMD.h"

#include <benchmark/benchmark.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace CASPI::SIMD;

// ============================================================================
// Constants and helpers
// ============================================================================

static const std::vector<int64_t> kSizes = { 64, 512, 2048, 16384 };

static constexpr double kPi2 = 1.5707963267948966;

// Scalar defeat: volatile load prevents auto-vectorisation of the loop.
template <typename T>
CASPI_NOINLINE static T volatile_read (const T* p, std::size_t i) noexcept
{
    const volatile T* vp = p;
    return vp[i];
}

// Platform-agnostic aligned allocation (matches existing benchmark pattern)
static float* alloc_f32 (std::size_t n, std::size_t align = 32)
{
#if defined(_MSC_VER)
    return static_cast<float*> (_aligned_malloc (n * sizeof (float), align));
#else
    void* p = nullptr;
    ::posix_memalign (&p, align, n * sizeof (float));
    return static_cast<float*> (p);
#endif
}
static void free_f32 (float* p)
{
#if defined(_MSC_VER)
    _aligned_free (p);
#else
    ::free (p);
#endif
}

struct FloatBuf
{
    float*      ptr;
    std::size_t size;
    explicit FloatBuf (std::size_t n) : ptr (alloc_f32 (n)), size (n) {}
    ~FloatBuf() { free_f32 (ptr); }
    FloatBuf (const FloatBuf&)            = delete;
    FloatBuf& operator= (const FloatBuf&) = delete;
};

// ============================================================================
// sin_block<float>
// ============================================================================

// SIMD: ops::sin_block uses block_op_unary with SinKernel<float,5>
static void BM_Sin_Float_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        ops::sin_block<float> (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

// AutoVec: std::sinf — libm, slow but auto-vectorisable
static void BM_Sin_Float_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = std::sin (src.ptr[i]);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

// PolyLoop: manually inlined Horner with same coefficients as sin_poly<float>
// — equivalent quality to _SIMD but written as a scalar loop (autovec-able)
static void BM_Sin_Float_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    // Same coefficients as approx::coeffs::sin_d5, cast to float
    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -1.6666667163e-1f;
    static constexpr float c2 =  8.3333337680e-3f;
    static constexpr float c3 = -1.9841270114e-4f;
    static constexpr float c4 =  2.7557314297e-6f;
    static constexpr float c5 = -2.5052248440e-8f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = src.ptr[i];
            const float u = x * x;
            // Horner: c0 + u*(c1 + u*(c2 + u*(c3 + u*(c4 + u*c5))))
            const float p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
            dst.ptr[i] = x * p;
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

// Scalar: volatile reads to defeat vectorisation
static void BM_Sin_Float_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -1.6666667163e-1f;
    static constexpr float c2 =  8.3333337680e-3f;
    static constexpr float c3 = -1.9841270114e-4f;
    static constexpr float c4 =  2.7557314297e-6f;
    static constexpr float c5 = -2.5052248440e-8f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = volatile_read (src.ptr, i);
            const float u = x * x;
            const float p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
            dst.ptr[i] = x * p;
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Sin_Float_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Float_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Float_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Float_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// sin_block<double>
// ============================================================================

static void BM_Sin_Double_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    for (auto _ : state)
    {
        ops::sin_block<double> (dst.data(), src.data(), n);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sin_Double_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = std::sin (src[i]);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sin_Double_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -1.6666666666666665052e-1;
    static constexpr double c2 =  8.3333333333331650314e-3;
    static constexpr double c3 = -1.9841269841201840457e-4;
    static constexpr double c4 =  2.7557319223985880784e-6;
    static constexpr double c5 = -2.5052106798274584544e-8;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = src[i];
            const double u = x * x;
            const double p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
            dst[i] = x * p;
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sin_Double_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -1.6666666666666665052e-1;
    static constexpr double c2 =  8.3333333333331650314e-3;
    static constexpr double c3 = -1.9841269841201840457e-4;
    static constexpr double c4 =  2.7557319223985880784e-6;
    static constexpr double c5 = -2.5052106798274584544e-8;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = volatile_read (src.data(), i);
            const double u = x * x;
            const double p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
            dst[i] = x * p;
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Sin_Double_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Double_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Double_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Double_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// cos_block<float>
// ============================================================================

static void BM_Cos_Float_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        ops::cos_block<float> (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Float_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = std::cos (src.ptr[i]);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Float_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -5.0000001292e-1f;
    static constexpr float c2 =  4.1666667908e-2f;
    static constexpr float c3 = -1.3888889225e-3f;
    static constexpr float c4 =  2.4801587642e-5f;
    static constexpr float c5 = -2.7557314297e-7f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = src.ptr[i];
            const float u = x * x;
            dst.ptr[i] = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Float_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -5.0000001292e-1f;
    static constexpr float c2 =  4.1666667908e-2f;
    static constexpr float c3 = -1.3888889225e-3f;
    static constexpr float c4 =  2.4801587642e-5f;
    static constexpr float c5 = -2.7557314297e-7f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = volatile_read (src.ptr, i);
            const float u = x * x;
            dst.ptr[i] = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Cos_Float_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Float_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Float_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Float_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// cos_block<double>
// ============================================================================

static void BM_Cos_Double_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    for (auto _ : state)
    {
        ops::cos_block<double> (dst.data(), src.data(), n);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Double_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = std::cos (src[i]);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Double_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -4.9999999999999999759e-1;
    static constexpr double c2 =  4.1666666666666664811e-2;
    static constexpr double c3 = -1.3888888888888872993e-3;
    static constexpr double c4 =  2.4801587301585605359e-5;
    static constexpr double c5 = -2.7557319223472284322e-7;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = src[i];
            const double u = x * x;
            dst[i] = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Cos_Double_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = static_cast<double> (i) * kPi2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -4.9999999999999999759e-1;
    static constexpr double c2 =  4.1666666666666664811e-2;
    static constexpr double c3 = -1.3888888888888872993e-3;
    static constexpr double c4 =  2.4801587301585605359e-5;
    static constexpr double c5 = -2.7557319223472284322e-7;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = volatile_read (src.data(), i);
            const double u = x * x;
            dst[i] = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Cos_Double_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Double_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Double_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Cos_Double_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// tanh_block<float>  — degree-3, valid |x| <= 0.65
// ============================================================================

static void BM_Tanh_Float_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    // Stay within the valid domain [-0.65, 0.65]
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = -0.65f + static_cast<float> (i) * 1.3f / n;

    for (auto _ : state)
    {
        ops::tanh_block<float> (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Float_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = -0.65f + static_cast<float> (i) * 1.3f / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst.ptr[i] = std::tanh (src.ptr[i]);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Float_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = -0.65f + static_cast<float> (i) * 1.3f / n;

    // Same coefficients as tanh_poly<float>() deg-3
    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -3.3333334327e-1f;
    static constexpr float c2 =  1.3333333820e-1f;
    static constexpr float c3 = -5.3968254335e-2f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = src.ptr[i];
            const float u = x * x;
            dst.ptr[i] = x * (c0 + u * (c1 + u * (c2 + u * c3)));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Float_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = -0.65f + static_cast<float> (i) * 1.3f / n;

    static constexpr float c0 =  1.0f;
    static constexpr float c1 = -3.3333334327e-1f;
    static constexpr float c2 =  1.3333333820e-1f;
    static constexpr float c3 = -5.3968254335e-2f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = volatile_read (src.ptr, i);
            const float u = x * x;
            dst.ptr[i] = x * (c0 + u * (c1 + u * (c2 + u * c3)));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Tanh_Float_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Float_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Float_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Float_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// tanh_block<double>  — degree-7, valid |x| <= 0.60
// ============================================================================

static void BM_Tanh_Double_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = -0.6 + static_cast<double> (i) * 1.2 / n;

    for (auto _ : state)
    {
        ops::tanh_block<double> (dst.data(), src.data(), n);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Double_AutoVec (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = -0.6 + static_cast<double> (i) * 1.2 / n;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = std::tanh (src[i]);
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Double_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = -0.6 + static_cast<double> (i) * 1.2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -3.3333333333333337034e-1;
    static constexpr double c2 =  1.3333333333333252036e-1;
    static constexpr double c3 = -5.3968253968245345830e-2;
    static constexpr double c4 =  2.1869488536155748960e-2;
    static constexpr double c5 = -8.8632369985788490983e-3;
    static constexpr double c6 =  3.5921924242902374958e-3;
    static constexpr double c7 = -1.4558343870769948509e-3;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = src[i];
            const double u = x * x;
            const double p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * (c5 + u * (c6 + u * c7))))));
            dst[i] = x * p;
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Tanh_Double_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<double> src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src[i] = -0.6 + static_cast<double> (i) * 1.2 / n;

    static constexpr double c0 =  1.0;
    static constexpr double c1 = -3.3333333333333337034e-1;
    static constexpr double c2 =  1.3333333333333252036e-1;
    static constexpr double c3 = -5.3968253968245345830e-2;
    static constexpr double c4 =  2.1869488536155748960e-2;
    static constexpr double c5 = -8.8632369985788490983e-3;
    static constexpr double c6 =  3.5921924242902374958e-3;
    static constexpr double c7 = -1.4558343870769948509e-3;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = volatile_read (src.data(), i);
            const double u = x * x;
            const double p = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * (c5 + u * (c6 + u * c7))))));
            dst[i] = x * p;
        }
        benchmark::DoNotOptimize (dst[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (double) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Tanh_Double_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Double_AutoVec)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Double_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Tanh_Double_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// Raw PolyKernel via block_op_unary — isolates Horner cost without outer ops
// Degree-5 float: measures the pure block_op_unary + Horner overhead
// ============================================================================

static void BM_PolyKernel_Raw_Float_SIMD (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * 0.5f / n;

    // p(x) = degree-5 polynomial (arbitrary coefficients)
    kernels::PolyKernel<float, 5> poly ({1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f});

    for (auto _ : state)
    {
        block_op_unary (dst.ptr, src.ptr, n, poly);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_PolyKernel_Raw_Float_PolyLoop (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * 0.5f / n;

    static constexpr float c0 = 1.0f, c1 = 0.5f, c2 = 0.25f;
    static constexpr float c3 = 0.125f, c4 = 0.0625f, c5 = 0.03125f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = src.ptr[i];
            dst.ptr[i] = c0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * c5))));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_PolyKernel_Raw_Float_Scalar (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * 0.5f / n;

    static constexpr float c0 = 1.0f, c1 = 0.5f, c2 = 0.25f;
    static constexpr float c3 = 0.125f, c4 = 0.0625f, c5 = 0.03125f;

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = volatile_read (src.ptr, i);
            dst.ptr[i] = c0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * c5))));
        }
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_PolyKernel_Raw_Float_SIMD)->ArgsProduct ({ kSizes });
BENCHMARK (BM_PolyKernel_Raw_Float_PolyLoop)->ArgsProduct ({ kSizes });
BENCHMARK (BM_PolyKernel_Raw_Float_Scalar)->ArgsProduct ({ kSizes });

// ============================================================================
// In-place (dst == src) — confirm no aliasing penalty
// ============================================================================

static void BM_Sin_Float_SIMD_Inplace (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf buf (n), backup (n);
    for (std::size_t i = 0; i < n; ++i)
        backup.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        std::memcpy (buf.ptr, backup.ptr, n * sizeof (float)); // restore before each iter
        ops::sin_block<float> (buf.ptr, buf.ptr, n);           // in-place
        benchmark::DoNotOptimize (buf.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

static void BM_Sin_Float_SIMD_OutOfPlace (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    FloatBuf src (n), dst (n);
    for (std::size_t i = 0; i < n; ++i)
        src.ptr[i] = static_cast<float> (i) * static_cast<float> (kPi2) / n;

    for (auto _ : state)
    {
        ops::sin_block<float> (dst.ptr, src.ptr, n);
        benchmark::DoNotOptimize (dst.ptr[0]);
    }

    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * n);
}

BENCHMARK (BM_Sin_Float_SIMD_Inplace)->ArgsProduct ({ kSizes });
BENCHMARK (BM_Sin_Float_SIMD_OutOfPlace)->ArgsProduct ({ kSizes });