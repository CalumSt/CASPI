/**
 * @file SIMD_Buffer_bm.cpp
 * @brief Comprehensive benchmarks for SIMD-accelerated buffer operations
 *
 * Benchmarks cover:
 * - Span operations (fill, scale, copy, add)
 * - Buffer operations (channel-major vs interleaved)
 * - SIMD vs scalar comparison
 * - Alignment impact
 * - Different buffer sizes
 */

#include "base/caspi_SIMD.h"
#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Span.h"

#include <benchmark/benchmark.h>
#include <cmath>
#include <random>
#include <vector>

using namespace CASPI;
using namespace CASPI::SIMD;
using namespace CASPI::Core;

// ============================================================================
// Benchmark Configuration
// ============================================================================

// Common buffer sizes for audio processing
constexpr std::size_t TINY_SIZE   = 64; // Very small block
constexpr std::size_t SMALL_SIZE  = 256; // Typical real-time block
constexpr std::size_t MEDIUM_SIZE = 512; // Common buffer size
constexpr std::size_t LARGE_SIZE  = 2048; // Larger buffer
constexpr std::size_t HUGE_SIZE   = 8192; // Very large buffer

// ============================================================================
// Low-Level SIMD Primitives Benchmarks
// ============================================================================

// Benchmark: SIMD add operation
static void BM_SIMD_Add_Primitive (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> a (size, 1.5f);
    std::vector<float> b (size, 2.5f);
    std::vector<float> result (size);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; i += 4)
        {
            auto va = load<float> (&a[i]);
            auto vb = load<float> (&b[i]);
            auto vr = add (va, vb);
            store<float> (&result[i], vr);
        }
        benchmark::DoNotOptimize (result.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Add_Primitive)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: SIMD mul_add operation (FMA)
static void BM_SIMD_MulAdd_Primitive (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> a (size, 1.5f);
    std::vector<float> b (size, 2.0f);
    std::vector<float> c (size, 0.5f);
    std::vector<float> result (size);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; i += 4)
        {
            auto va = load<float> (&a[i]);
            auto vb = load<float> (&b[i]);
            auto vc = load<float> (&c[i]);
            auto vr = mul_add (va, vb, vc);
            store<float> (&result[i], vr);
        }
        benchmark::DoNotOptimize (result.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 4);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_MulAdd_Primitive)->Range (TINY_SIZE, HUGE_SIZE);

// ============================================================================
// Block Operations Benchmarks (ops namespace)
// ============================================================================

// Benchmark: ops::fill
static void BM_SIMD_Fill (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size);

    for (auto _ : state)
    {
        ops::fill (data.data(), size, 1.5f);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Fill)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_Fill(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            data[i] = 1.5f;
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float));
}
BENCHMARK(BM_AutoVec_Fill)->Range(TINY_SIZE, HUGE_SIZE);


static void BM_Scalar_Fill(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            data[i] = 1.5f;
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float));
}
BENCHMARK(BM_Scalar_Fill)->Range(TINY_SIZE, HUGE_SIZE);


// Benchmark: ops::scale
static void BM_SIMD_Scale (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);

    for (auto _ : state)
    {
        ops::scale (data.data(), size, 0.5f);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Scale)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar implementation for comparison
static void BM_AutoVec_Scale (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            data[i] *= 0.5f;
        }
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_AutoVec_Scale)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar implementation for comparison
static void BM_Scalar_Scale (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            data[i] *= 0.5f;
        }
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Scalar_Scale)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: ops::add
static void BM_SIMD_Add (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> dst (size, 1.0f);
    std::vector<float> src (size, 2.0f);

    for (auto _ : state)
    {
        ops::add (dst.data(), src.data(), size);
        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Add)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_Add (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> dst (size, 1.0f);
    std::vector<float> src (size, 2.0f);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] += src[i];
        }

        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 3);
}
BENCHMARK (BM_AutoVec_Add)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_Scalar_Add (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> dst (size, 1.0f);
    std::vector<float> src (size, 2.0f);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] += src[i];
        }

        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 3);
}
BENCHMARK (BM_Scalar_Add)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: ops::copy
static void BM_SIMD_Copy (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> src (size, 1.5f);
    std::vector<float> dst (size);

    for (auto _ : state)
    {
        ops::copy (dst.data(), src.data(), size);
        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Copy)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_Copy (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> src (size, 1.0f);
    std::vector<float> dst (size);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] = src[i];
        }

        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
}
BENCHMARK (BM_AutoVec_Copy)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_Scalar_Copy (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> src (size, 1.0f);
    std::vector<float> dst (size);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] = src[i];
        }

        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
}
BENCHMARK (BM_Scalar_Copy)->Range (TINY_SIZE, HUGE_SIZE);


// Benchmark: ops::mac (multiply-accumulate)
static void BM_SIMD_MAC (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> dst (size, 0.0f);
    std::vector<float> src1 (size, 2.0f);
    std::vector<float> src2 (size, 3.0f);

    for (auto _ : state)
    {
        ops::mac (dst.data(), src1.data(), src2.data(), size);
        benchmark::DoNotOptimize (dst.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 4);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_MAC)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_MAC(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> dst(size, 0.0f);
    std::vector<float> a(size, 2.0f);
    std::vector<float> b(size, 3.0f);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] += a[i] * b[i];
        }

        benchmark::DoNotOptimize(dst.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float) * 4);
}
BENCHMARK(BM_AutoVec_MAC)->Range(TINY_SIZE, HUGE_SIZE);


static void BM_Scalar_MAC(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> dst(size, 0.0f);
    std::vector<float> a(size, 2.0f);
    std::vector<float> b(size, 3.0f);

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            dst[i] += a[i] * b[i];
        }

        benchmark::DoNotOptimize(dst.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float) * 4);
}
BENCHMARK(BM_Scalar_MAC)->Range(TINY_SIZE, HUGE_SIZE);


// Benchmark: ops::clamp
static void BM_SIMD_Clamp (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size);

    // Initialize with random values outside clamp range
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
    for (auto& v : data)
        v = dist (rng);

    for (auto _ : state)
    {
        ops::clamp (data.data(), -1.0f, 1.0f, size);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Clamp)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_Clamp(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);
    for (auto& v : data)
    {
        v = dist(rng);
    }

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            if (data[i] < -1.0f)
            {
                data[i] = -1.0f;
            }
            else if (data[i] > 1.0f)
            {
                data[i] = 1.0f;
            }
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float) * 2);
}
BENCHMARK(BM_AutoVec_Clamp)->Range(TINY_SIZE, HUGE_SIZE);


static void BM_Scalar_Clamp(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);
    for (auto& v : data)
    {
        v = dist(rng);
    }

    for (auto _ : state)
    {
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            if (data[i] < -1.0f)
            {
                data[i] = -1.0f;
            }
            else if (data[i] > 1.0f)
            {
                data[i] = 1.0f;
            }
        }

        benchmark::DoNotOptimize(data.data());
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float) * 2);
}
BENCHMARK(BM_Scalar_Clamp)->Range(TINY_SIZE, HUGE_SIZE);


// ============================================================================
// Reduction Operations Benchmarks
// ============================================================================

// Benchmark: ops::sum
static void BM_SIMD_Sum (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::sum (data.data(), size);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_Sum)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar sum for comparison
static void BM_AutoVec_Sum (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < size; ++i)
        {
            result += data[i];
        }
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_AutoVec_Sum)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar sum for comparison
static void BM_Scalar_Sum (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;

#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            result += data[i];
        }
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Scalar_Sum)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: ops::find_max
static void BM_SIMD_FindMax (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size);

    // Random data
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (auto& v : data)
        v = dist (rng);

    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::find_max (data.data(), size);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_FindMax)->Range (TINY_SIZE, HUGE_SIZE);

static void BM_AutoVec_FindMax(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& v : data)
    {
        v = dist(rng);
    }

    float result = 0.0f;

    for (auto _ : state)
    {
        float max_val = data[0];

#pragma loop(no_vector)
        for (std::size_t i = 1; i < size; ++i)
        {
            if (data[i] > max_val)
            {
                max_val = data[i];
            }
        }

        result = max_val;
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float));
}
BENCHMARK(BM_AutoVec_FindMax)->Range(TINY_SIZE, HUGE_SIZE);


static void BM_Scalar_FindMax(benchmark::State& state)
{
    const std::size_t size = state.range(0);
    std::vector<float> data(size);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& v : data)
    {
        v = dist(rng);
    }

    float result = 0.0f;

    for (auto _ : state)
    {
        float max_val = data[0];

#pragma loop(no_vector)
        for (std::size_t i = 1; i < size; ++i)
        {
            if (data[i] > max_val)
            {
                max_val = data[i];
            }
        }

        result = max_val;
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(state.iterations() * size * sizeof(float));
}
BENCHMARK(BM_Scalar_FindMax)->Range(TINY_SIZE, HUGE_SIZE);


// Benchmark: ops::dot_product
static void BM_SIMD_DotProduct (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> a (size, 1.0f);
    std::vector<float> b (size, 2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = ops::dot_product (a.data(), b.data(), size);
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_SIMD_DotProduct)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar dot product for comparison
static void BM_AutoVec_DotProduct (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> a (size, 1.0f);
    std::vector<float> b (size, 2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
        for (std::size_t i = 0; i < size; ++i)
        {
            result += a[i] * b[i];
        }
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_AutoVec_DotProduct)->Range (TINY_SIZE, HUGE_SIZE);

// Scalar dot product for comparison
static void BM_Scalar_DotProduct (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> a (size, 1.0f);
    std::vector<float> b (size, 2.0f);
    float result = 0.0f;

    for (auto _ : state)
    {
        result = 0.0f;
#pragma loop(no_vector)
        for (std::size_t i = 0; i < size; ++i)
        {
            result += a[i] * b[i];
        }
        benchmark::DoNotOptimize (result);
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Scalar_DotProduct)->Range (TINY_SIZE, HUGE_SIZE);

// ============================================================================
// Span Operations Benchmarks
// ============================================================================

// Benchmark: Span fill (contiguous)
static void BM_Span_Fill_Contiguous (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size);
    Span<float> span (data.data(), size);

    for (auto _ : state)
    {
        Core::fill (span, 1.5f);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Span_Fill_Contiguous)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Span scale (contiguous)
static void BM_Span_Scale_Contiguous (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size, 1.0f);
    Span<float> span (data.data(), size);

    for (auto _ : state)
    {
        Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Span_Scale_Contiguous)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Span add (contiguous)
static void BM_Span_Add_Contiguous (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> dst_data (size, 1.0f);
    std::vector<float> src_data (size, 2.0f);
    Span<float> dst (dst_data.data(), size);
    Span<const float> src (src_data.data(), size);

    for (auto _ : state)
    {
        Core::add (dst, src);
        benchmark::DoNotOptimize (dst_data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float) * 3);
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Span_Add_Contiguous)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: StridedSpan fill (scalar-only)
static void BM_Span_Fill_Strided (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> data (size * 2); // 2x size for stride
    StridedSpan<float> span (data.data(), size, 2);

    for (auto _ : state)
    {
        Core::fill (span, 1.5f);
        benchmark::DoNotOptimize (data.data());
    }

    state.SetBytesProcessed (state.iterations() * size * sizeof (float));
    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Span_Fill_Strided)->Range (TINY_SIZE, HUGE_SIZE);

// ============================================================================
// AudioBuffer Benchmarks
// ============================================================================

// Benchmark: Channel-major buffer fill (SIMD)
static void BM_AudioBuffer_Fill_ChannelMajor (benchmark::State& state)
{
    const std::size_t frames = state.range (0);
    AudioBuffer<float, ChannelMajorLayout> buffer (2, frames);

    for (auto _ : state)
    {
        block::fill_buffer (buffer, 0.5f);
        benchmark::DoNotOptimize (buffer.data());
    }

    state.SetBytesProcessed (state.iterations() * frames * 2 * sizeof (float));
    state.SetItemsProcessed (state.iterations() * frames * 2);
}
BENCHMARK (BM_AudioBuffer_Fill_ChannelMajor)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Interleaved buffer fill (SIMD)
static void BM_AudioBuffer_Fill_Interleaved (benchmark::State& state)
{
    const std::size_t frames = state.range (0);
    AudioBuffer<float, InterleavedLayout> buffer (2, frames);

    for (auto _ : state)
    {
        block::fill_buffer (buffer, 0.5f);
        benchmark::DoNotOptimize (buffer.data());
    }

    state.SetBytesProcessed (state.iterations() * frames * 2 * sizeof (float));
    state.SetItemsProcessed (state.iterations() * frames * 2);
}
BENCHMARK (BM_AudioBuffer_Fill_Interleaved)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Channel-major per-channel scale (SIMD)
static void BM_AudioBuffer_ScaleChannel_ChannelMajor (benchmark::State& state)
{
    const std::size_t frames = state.range (0);
    AudioBuffer<float, ChannelMajorLayout> buffer (2, frames);
    buffer.fill (1.0f);

    for (auto _ : state)
    {
        block::scale_channel (buffer, 0, 0.5f);
        block::scale_channel (buffer, 1, 0.5f);
        benchmark::DoNotOptimize (buffer.data());
    }

    state.SetBytesProcessed (state.iterations() * frames * 2 * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * frames * 2);
}
BENCHMARK (BM_AudioBuffer_ScaleChannel_ChannelMajor)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Interleaved per-channel scale (scalar)
static void BM_AudioBuffer_ScaleChannel_Interleaved (benchmark::State& state)
{
    const std::size_t frames = state.range (0);
    AudioBuffer<float, InterleavedLayout> buffer (2, frames);
    buffer.fill (1.0f);

    for (auto _ : state)
    {
        block::scale_channel (buffer, 0, 0.5f);
        block::scale_channel (buffer, 1, 0.5f);
        benchmark::DoNotOptimize (buffer.data());
    }

    state.SetBytesProcessed (state.iterations() * frames * 2 * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * frames * 2);
}
BENCHMARK (BM_AudioBuffer_ScaleChannel_Interleaved)->Range (TINY_SIZE, HUGE_SIZE);

// ============================================================================
// SIMD vs Scalar Comparison Benchmarks
// ============================================================================

// ============================================================================
// Real-World Audio Processing Scenarios
// ============================================================================

// Benchmark: Stereo mixing with gain
static void BM_RealWorld_StereoMix (benchmark::State& state)
{
    const std::size_t frames = state.range (0);
    AudioBuffer<float, ChannelMajorLayout> track1 (2, frames);
    AudioBuffer<float, ChannelMajorLayout> track2 (2, frames);
    AudioBuffer<float, ChannelMajorLayout> master (2, frames);

    // Initialize with realistic audio
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    for (std::size_t i = 0; i < frames * 2; ++i)
    {
        track1.data()[i] = dist (rng);
        track2.data()[i] = dist (rng);
    }

    for (auto _ : state)
    {
        // Zero master
        block::fill_buffer (master, 0.0f);

        // Apply gains
        block::scale_buffer (track1, 0.7f);
        block::scale_buffer (track2, 0.5f);

        // Mix to master
        block::add_buffer (master, track1);
        block::add_buffer (master, track2);

        benchmark::DoNotOptimize (master.data());
    }

    state.SetItemsProcessed (state.iterations() * frames * 2);
}
BENCHMARK (BM_RealWorld_StereoMix)->Range (SMALL_SIZE, LARGE_SIZE);

// Benchmark: RMS calculation
static void BM_RealWorld_RMS (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> audio (size);

    // Sine wave
    for (std::size_t i = 0; i < size; ++i)
    {
        audio[i] = std::sin (2.0f * 3.14159f * i / 50.0f);
    }

    float rms = 0.0f;

    for (auto _ : state)
    {
        float sum_squares = ops::dot_product (audio.data(), audio.data(), size);
        rms               = std::sqrt (sum_squares / size);
        benchmark::DoNotOptimize (rms);
    }

    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_RealWorld_RMS)->Range (SMALL_SIZE, LARGE_SIZE);

// Benchmark: Soft clipping
static void BM_RealWorld_SoftClip (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> audio (size);

    // Generate audio with peaks
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.5f, 1.5f);
    for (auto& v : audio)
        v = dist (rng);

    for (auto _ : state)
    {
        ops::clamp (audio.data(), -1.0f, 1.0f, size);
        benchmark::DoNotOptimize (audio.data());
    }

    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_RealWorld_SoftClip)->Range (SMALL_SIZE, LARGE_SIZE);

// ============================================================================
// Alignment Impact Benchmarks
// ============================================================================

// Benchmark: Aligned data
static void BM_Alignment_Aligned (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    alignas (32) float data[8192]; // Stack-allocated, aligned

    for (std::size_t i = 0; i < size; ++i)
        data[i] = 1.0f;

    for (auto _ : state)
    {
        ops::scale (data, size, 0.5f);
        benchmark::DoNotOptimize (data);
    }

    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Alignment_Aligned)->Range (TINY_SIZE, HUGE_SIZE);

// Benchmark: Unaligned data
static void BM_Alignment_Unaligned (benchmark::State& state)
{
    const std::size_t size = state.range (0);
    std::vector<float> buffer (size + 1);
    float* data = &buffer[1]; // Intentionally misalign

    for (std::size_t i = 0; i < size; ++i)
        data[i] = 1.0f;

    for (auto _ : state)
    {
        ops::scale (data, size, 0.5f);
        benchmark::DoNotOptimize (data);
    }

    state.SetItemsProcessed (state.iterations() * size);
}
BENCHMARK (BM_Alignment_Unaligned)->Range (TINY_SIZE, HUGE_SIZE);