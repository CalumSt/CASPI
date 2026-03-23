/**
 * @file AudioBuffer_Span_bm.cpp
 * @brief Benchmarks for AudioBuffer and Span types and their SIMD interactions.
 *
 * ============================================================================
 * WHAT THIS FILE MEASURES
 * ============================================================================
 *
 * 1. LAYOUT CONTIGUITY IMPACT
 *    The key architectural question: does the memory layout produce contiguous
 *    or strided spans for channel/frame access, and what does that cost?
 *
 *    ChannelMajorLayout:
 *      channel_span(c) -> Span<T>        (contiguous, SIMD-eligible)
 *      frame_span(f)   -> StridedSpan<T> (strided, scalar-only)
 *
 *    InterleavedLayout:
 *      channel_span(c) -> StridedSpan<T> (strided, scalar-only)
 *      frame_span(f)   -> Span<T>        (contiguous, SIMD-eligible)
 *
 * 2. SPAN OPERATION DISPATCH
 *    Core::fill/scale/copy/add dispatch to SIMD or scalar at compile time
 *    based on span type. Measures whether dispatch overhead is zero.
 *
 * 3. BLOCK:: VS DIRECT SIMD
 *    block:: functions call Core:: on all_span(). Measures overhead of
 *    the AudioBuffer wrapper vs calling ops:: directly on raw pointers.
 *
 * 4. STRIDED ACCESS COST
 *    Quantifies the penalty for strided channel access in interleaved buffers
 *    vs contiguous channel access in channel-major buffers.
 *
 * 5. MULTI-CHANNEL PROCESSING PATTERNS
 *    Per-channel vs whole-buffer operations, and their scaling with channel count.
 *
 * ============================================================================
 * SIZES
 * ============================================================================
 *
 * Frames: 64, 512, 2048, 16384 (mimics buffer sizes: small DSP block to offline)
 * Channels: 1, 2, 8 (mono, stereo, surround)
 *
 * "Frames" is the per-channel sample count. Total samples = frames * channels.
 *
 * ============================================================================
 * VOLATILE_READ SCALAR BASELINE
 * ============================================================================
 *
 * A portable scalar baseline is provided via volatile_read() which forces
 * serialised loads and prevents auto-vectorisation on all compilers.
 * See SIMD_Buffer_bm.cpp for rationale.
 */

#include <benchmark/benchmark.h>
#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Span.h"
#include "base/caspi_SIMD.h"

#include <cstring>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Forces a serialised scalar load. Compiler cannot vectorise a loop whose
 * loads depend on volatile pointer dereferences.
 */
template <typename T>
static inline T volatile_read (const T* p, std::size_t i) noexcept
{
    return *reinterpret_cast<const volatile T*> (p + i);
}

/**
 * Fills a vector with deterministic values to avoid denormal/zero pathologies.
 * Pattern: alternating positive/negative in [-0.9, 0.9].
 */
template <typename T>
static void init_data (T* data, std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        data[i] = static_cast<T> ((i % 2 == 0) ? 0.5 : -0.3);
}

// Backup + restore for in-place ops to prevent drift across iterations.
// Both SIMD and scalar variants call this, so comparison is fair.
template <typename T>
static void restore_from_backup (T* dst, const T* backup, std::size_t count) noexcept
{
    std::memcpy (dst, backup, count * sizeof (T));
}

// Sizes: small DSP block, medium, L2-resident, L3-resident
static const std::vector<int64_t> kFrameSizes = { 64, 512, 2048, 16384 };
static const std::vector<int64_t> kChannelCounts = { 1, 2, 8 };

// ---------------------------------------------------------------------------
// Section 1: Span operation dispatch overhead
//
// Measures: does Core::fill/scale via a contiguous Span<T> route to SIMD,
// and is there measurable dispatch overhead vs calling ops:: directly?
// ---------------------------------------------------------------------------

/**
 * Direct ops:: call on raw pointer — gold standard, no wrapper overhead.
 */
static void BM_RawPtr_Scale (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> buf (n, 1.0f);
    std::vector<float> backup (n, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), n);
        CASPI::SIMD::ops::scale (buf.data(), n, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_RawPtr_Scale)->ArgsProduct ({ kFrameSizes });

/**
 * Core::scale via Span<float> — should route to SIMD::ops::scale with
 * zero overhead (Span<T> is just a {ptr, size} struct).
 */
static void BM_Span_Scale (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> buf (n, 1.0f);
    std::vector<float> backup (n, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), n);
        CASPI::Core::Span<float> span (buf.data(), n);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_Span_Scale)->ArgsProduct ({ kFrameSizes });

/**
 * Core::scale via StridedSpan<float> — always scalar (data not contiguous).
 * Stride=2 simulates accessing one channel of a stereo interleaved buffer.
 */
static void BM_StridedSpan_Scale_Stride2 (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = 2;
    const std::size_t total    = frames * channels;
    std::vector<float> buf (total, 1.0f);
    std::vector<float> backup (total, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), total);
        // Channel 0 of interleaved stereo: stride=2
        CASPI::Core::StridedSpan<float> span (buf.data(), frames, channels);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Scale_Stride2)->ArgsProduct ({ kFrameSizes });

/**
 * Same as above but stride=8 (accessing one channel of 8-channel interleaved).
 * Higher stride = worse spatial locality.
 */
static void BM_StridedSpan_Scale_Stride8 (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = 8;
    const std::size_t total    = frames * channels;
    std::vector<float> buf (total, 1.0f);
    std::vector<float> backup (total, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), total);
        CASPI::Core::StridedSpan<float> span (buf.data(), frames, channels);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Scale_Stride8)->ArgsProduct ({ kFrameSizes });

// ---------------------------------------------------------------------------
// Section 2: AudioBuffer layout comparison — channel access
//
// Key question: how much does layout choice matter for per-channel processing?
//
// ChannelMajor:  channel_span(c) -> Span<T>        -> SIMD ops
// Interleaved:   channel_span(c) -> StridedSpan<T> -> scalar ops
// ---------------------------------------------------------------------------

/**
 * Scale channel 0 of a channel-major buffer.
 * channel_span(0) returns Span<float> → SIMD path.
 */
static void BM_AudioBuffer_ChannelMajor_ScaleChannel (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        // SIMD-eligible: contiguous channel in channel-major layout
        auto span = buf.channel_span (0);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_AudioBuffer_ChannelMajor_ScaleChannel)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * Scale channel 0 of an interleaved buffer.
 * channel_span(0) returns StridedSpan<float> → scalar path.
 * Direct apples-to-apples comparison with the above.
 */
static void BM_AudioBuffer_Interleaved_ScaleChannel (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::InterleavedLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        // Scalar-only: strided channel in interleaved layout
        auto span = buf.channel_span (0);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_AudioBuffer_Interleaved_ScaleChannel)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

// ---------------------------------------------------------------------------
// Section 3: AudioBuffer layout comparison — frame access
//
// Inverse of Section 2. Now frame access is the contiguous path in
// interleaved layout, and strided in channel-major.
// ---------------------------------------------------------------------------

/**
 * Scale frame 0 of an interleaved buffer.
 * frame_span(0) returns Span<float> → SIMD path.
 * Contiguous because all channels of one frame are adjacent in memory.
 */
static void BM_AudioBuffer_Interleaved_ScaleFrame (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::InterleavedLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        // SIMD-eligible: frame is contiguous in interleaved layout
        auto span = buf.frame_span (0);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    // Only channels elements accessed per frame
    state.SetBytesProcessed (state.iterations() * channels * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (channels));
}
BENCHMARK (BM_AudioBuffer_Interleaved_ScaleFrame)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * Scale frame 0 of a channel-major buffer.
 * frame_span(0) returns StridedSpan<float> → scalar path.
 */
static void BM_AudioBuffer_ChannelMajor_ScaleFrame (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        // Scalar-only: frame is strided in channel-major layout
        auto span = buf.frame_span (0);
        CASPI::Core::scale (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * channels * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (channels));
}
BENCHMARK (BM_AudioBuffer_ChannelMajor_ScaleFrame)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

// ---------------------------------------------------------------------------
// Section 4: Whole-buffer operations via block:: and all_span()
//
// Both layouts produce contiguous all_span() regardless of layout.
// Measures wrapper overhead: block:: → Core:: → ops:: → raw pointer.
// ---------------------------------------------------------------------------

/**
 * block::scale on channel-major buffer (whole buffer, all channels).
 * Routes: block::scale → all_span() → Core::scale → SIMD::ops::scale
 */
static void BM_AudioBuffer_ChannelMajor_BlockScale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        CASPI::block::scale (buf, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AudioBuffer_ChannelMajor_BlockScale)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * block::scale on interleaved buffer. Layout makes no difference for whole-buffer
 * ops since all_span() is always contiguous. Both should match BM_RawPtr_Scale
 * for the same total element count.
 */
static void BM_AudioBuffer_Interleaved_BlockScale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::InterleavedLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        CASPI::block::scale (buf, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AudioBuffer_Interleaved_BlockScale)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * Direct ops:: call on raw data() pointer for same element count.
 * This is the minimum achievable time: no Span, no wrapper.
 * Any gap between this and block::scale is pure wrapper overhead.
 */
static void BM_AudioBuffer_RawPtr_Scale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));
    const std::size_t total    = frames * channels;

    std::vector<float> buf (total, 1.0f);
    std::vector<float> backup (total, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), total);
        CASPI::SIMD::ops::scale (buf.data(), total, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * total * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (total));
}
BENCHMARK (BM_AudioBuffer_RawPtr_Scale)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

// ---------------------------------------------------------------------------
// Section 5: All-channels loop — per-channel vs whole-buffer strategy
//
// Real DSP often processes all channels. There are two strategies:
//   A) Loop over channels, call channel_span() on each
//   B) Call all_span() once (only valid when all channels need same op)
//
// This measures the cost difference and whether channel-major vs interleaved
// matters when doing strategy A.
// ---------------------------------------------------------------------------

/**
 * Strategy A: loop over channels, channel_span() each one.
 * Channel-major: each channel_span() → Span<T> → SIMD.
 */
static void BM_AllChannels_ChannelMajor_PerChannelScale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        for (std::size_t c = 0; c < channels; ++c)
        {
            auto span = buf.channel_span (c);
            CASPI::Core::scale (span, 0.5f);
        }
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AllChannels_ChannelMajor_PerChannelScale)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * Strategy A: loop over channels, channel_span() each one.
 * Interleaved: each channel_span() → StridedSpan<T> → scalar.
 * Expected: much slower than channel-major equivalent at large sizes.
 */
static void BM_AllChannels_Interleaved_PerChannelScale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::InterleavedLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        for (std::size_t c = 0; c < channels; ++c)
        {
            auto span = buf.channel_span (c);
            CASPI::Core::scale (span, 0.5f);
        }
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AllChannels_Interleaved_PerChannelScale)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * Strategy B: block::scale on all_span() — always contiguous, always SIMD.
 * This is the ceiling for whole-buffer ops. Per-channel strategies should
 * not be slower than this for channel-major layout (they process the same
 * memory in the same order). For interleaved, per-channel is strided so
 * will be slower.
 */
static void BM_AllChannels_BlockScale_AllSpan (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    std::vector<float> backup (buf.numSamples());
    std::memcpy (backup.data(), buf.data(), buf.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), buf.numSamples());
        CASPI::block::scale (buf, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AllChannels_BlockScale_AllSpan)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

// ---------------------------------------------------------------------------
// Section 6: Span operations beyond scale
//
// fill, copy, add — each tested on contiguous vs strided spans.
// ---------------------------------------------------------------------------

// --- Fill ---

static void BM_Span_Fill (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> buf (n);

    for (auto _ : state)
    {
        CASPI::Core::Span<float> span (buf.data(), n);
        CASPI::Core::fill (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_Span_Fill)->ArgsProduct ({ kFrameSizes });

static void BM_StridedSpan_Fill_Stride2 (benchmark::State& state)
{
    const std::size_t frames = static_cast<std::size_t> (state.range (0));
    const std::size_t stride = 2;
    std::vector<float> buf (frames * stride);

    for (auto _ : state)
    {
        CASPI::Core::StridedSpan<float> span (buf.data(), frames, stride);
        CASPI::Core::fill (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Fill_Stride2)->ArgsProduct ({ kFrameSizes });

static void BM_StridedSpan_Fill_Stride8 (benchmark::State& state)
{
    const std::size_t frames = static_cast<std::size_t> (state.range (0));
    const std::size_t stride = 8;
    std::vector<float> buf (frames * stride);

    for (auto _ : state)
    {
        CASPI::Core::StridedSpan<float> span (buf.data(), frames, stride);
        CASPI::Core::fill (span, 0.5f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Fill_Stride8)->ArgsProduct ({ kFrameSizes });

// --- Copy ---

static void BM_Span_Copy (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> src (n, 1.0f);
    std::vector<float> dst (n);

    for (auto _ : state)
    {
        CASPI::Core::Span<float> dst_span (dst.data(), n);
        CASPI::Core::Span<const float> src_span (src.data(), n);
        CASPI::Core::copy (dst_span, src_span);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2); // read + write
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_Span_Copy)->ArgsProduct ({ kFrameSizes });

static void BM_StridedSpan_Copy_Stride2 (benchmark::State& state)
{
    const std::size_t frames = static_cast<std::size_t> (state.range (0));
    const std::size_t stride = 2;
    std::vector<float> src (frames * stride, 1.0f);
    std::vector<float> dst (frames * stride);

    for (auto _ : state)
    {
        CASPI::Core::StridedSpan<float> dst_span (dst.data(), frames, stride);
        CASPI::Core::StridedSpan<const float> src_span (src.data(), frames, stride);
        CASPI::Core::copy (dst_span, src_span);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Copy_Stride2)->ArgsProduct ({ kFrameSizes });

// --- Add ---

static void BM_Span_Add (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> dst (n, 1.0f);
    std::vector<float> src (n, 0.5f);
    std::vector<float> backup (n, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (dst.data(), backup.data(), n);
        CASPI::Core::Span<float> dst_span (dst.data(), n);
        CASPI::Core::Span<const float> src_span (src.data(), n);
        CASPI::Core::add (dst_span, src_span);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_Span_Add)->ArgsProduct ({ kFrameSizes });

static void BM_StridedSpan_Add_Stride2 (benchmark::State& state)
{
    const std::size_t frames = static_cast<std::size_t> (state.range (0));
    const std::size_t stride = 2;
    std::vector<float> dst (frames * stride, 1.0f);
    std::vector<float> src (frames * stride, 0.5f);
    std::vector<float> backup (frames * stride, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (dst.data(), backup.data(), frames * stride);
        CASPI::Core::StridedSpan<float> dst_span (dst.data(), frames, stride);
        CASPI::Core::StridedSpan<const float> src_span (src.data(), frames, stride);
        CASPI::Core::add (dst_span, src_span);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Add_Stride2)->ArgsProduct ({ kFrameSizes });

// ---------------------------------------------------------------------------
// Section 7: AudioBuffer::block:: operations
//
// All of these go through all_span() which is always contiguous.
// Tests fill, copy, add via the block:: namespace.
// ---------------------------------------------------------------------------

static void BM_AudioBuffer_BlockFill (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);

    for (auto _ : state)
    {
        CASPI::block::fill (buf, 0.0f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AudioBuffer_BlockFill)->ArgsProduct ({ kFrameSizes, kChannelCounts });

static void BM_AudioBuffer_BlockCopy (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf src (channels, frames);
    Buf dst (channels, frames);
    init_data (src.data(), src.numSamples());

    for (auto _ : state)
    {
        CASPI::block::copy (dst, src);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * src.numSamples() * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (src.numSamples()));
}
BENCHMARK (BM_AudioBuffer_BlockCopy)->ArgsProduct ({ kFrameSizes, kChannelCounts });

static void BM_AudioBuffer_BlockAdd (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf src (channels, frames);
    Buf dst (channels, frames);
    init_data (src.data(), src.numSamples());
    init_data (dst.data(), dst.numSamples());

    std::vector<float> backup (dst.numSamples());
    std::memcpy (backup.data(), dst.data(), dst.numSamples() * sizeof (float));

    for (auto _ : state)
    {
        restore_from_backup (dst.data(), backup.data(), dst.numSamples());
        CASPI::block::add (dst, src);
        benchmark::DoNotOptimize (dst.data());
    }
    state.SetBytesProcessed (state.iterations() * src.numSamples() * sizeof (float) * 2);
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (src.numSamples()));
}
BENCHMARK (BM_AudioBuffer_BlockAdd)->ArgsProduct ({ kFrameSizes, kChannelCounts });

// ---------------------------------------------------------------------------
// Section 8: Scalar baseline for strided access
//
// Provides the minimum possible reference for strided iteration —
// a serialised volatile_read loop that the compiler cannot vectorise.
// Used to bound how much faster a scalar (non-volatile) loop is,
// and whether any auto-vectorisation occurs for strided access.
// ---------------------------------------------------------------------------

/**
 * Volatile-forced scalar scale on interleaved channel data.
 * Provides a lower bound on what strided scalar throughput looks like.
 */
static void BM_Strided_VolatileScalar_Scale (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = 2;
    const std::size_t stride   = channels;
    std::vector<float> buf (frames * channels, 1.0f);
    std::vector<float> backup (frames * channels, 1.0f);

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), frames * channels);
        for (std::size_t i = 0; i < frames; ++i)
        {
            // Volatile load, then write back — serialised
            float val = volatile_read (buf.data(), i * stride);
            buf[i * stride] = val * 0.5f;
        }
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_Strided_VolatileScalar_Scale)->ArgsProduct ({ kFrameSizes });

// ---------------------------------------------------------------------------
// Section 9: clamp via Span — validates contiguous path hits SIMD clamp
//
// SIMD clamp was the biggest win vs AutoVec in the core benchmarks.
// This checks that the benefit is preserved through the Span dispatch layer.
// ---------------------------------------------------------------------------

static void BM_Span_Clamp (benchmark::State& state)
{
    const std::size_t n = static_cast<std::size_t> (state.range (0));
    std::vector<float> buf (n);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = static_cast<float> (i % 200) / 100.0f - 1.0f; // range [-1, 1]
    std::vector<float> backup = buf;

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), n);
        CASPI::Core::Span<float> span (buf.data(), n);
        CASPI::Core::clamp (span, -0.9f, 0.9f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * n * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (n));
}
BENCHMARK (BM_Span_Clamp)->ArgsProduct ({ kFrameSizes });

static void BM_StridedSpan_Clamp_Stride2 (benchmark::State& state)
{
    const std::size_t frames = static_cast<std::size_t> (state.range (0));
    const std::size_t stride = 2;
    std::vector<float> buf (frames * stride);
    for (std::size_t i = 0; i < frames * stride; ++i)
        buf[i] = static_cast<float> (i % 200) / 100.0f - 1.0f;
    std::vector<float> backup = buf;

    for (auto _ : state)
    {
        restore_from_backup (buf.data(), backup.data(), frames * stride);
        CASPI::Core::StridedSpan<float> span (buf.data(), frames, stride);
        CASPI::Core::clamp (span, -0.9f, 0.9f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * frames * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (frames));
}
BENCHMARK (BM_StridedSpan_Clamp_Stride2)->ArgsProduct ({ kFrameSizes });

// ---------------------------------------------------------------------------
// Section 10: AudioBuffer construction and resize cost
//
// Measures: heap allocation cost (BLOCKING) vs processing cost.
// Not directly related to SIMD but important for understanding
// how expensive buffer setup is relative to processing.
// ---------------------------------------------------------------------------

static void BM_AudioBuffer_Construct_ChannelMajor (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    for (auto _ : state)
    {
        CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout> buf (channels, frames);
        benchmark::DoNotOptimize (buf.data());
    }
    // No bytes processed — this is allocation cost, not throughput
}
BENCHMARK (BM_AudioBuffer_Construct_ChannelMajor)
    ->ArgsProduct ({ kFrameSizes, kChannelCounts });

static void BM_AudioBuffer_Clear (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    for (auto _ : state)
    {
        buf.clear(); // std::fill to T{} — not SIMD, uses std::fill
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AudioBuffer_Clear)->ArgsProduct ({ kFrameSizes, kChannelCounts });

/**
 * block::fill(buf, 0.0f) — goes through SIMD::ops::fill.
 * Compare with BM_AudioBuffer_Clear which uses std::fill.
 * At large sizes, fill has NT stores; std::fill may use rep stosd.
 */
static void BM_AudioBuffer_BlockFill_Zero (benchmark::State& state)
{
    const std::size_t frames   = static_cast<std::size_t> (state.range (0));
    const std::size_t channels = static_cast<std::size_t> (state.range (1));

    using Buf = CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout>;
    Buf buf (channels, frames);
    init_data (buf.data(), buf.numSamples());

    for (auto _ : state)
    {
        CASPI::block::fill (buf, 0.0f);
        benchmark::DoNotOptimize (buf.data());
    }
    state.SetBytesProcessed (state.iterations() * buf.numSamples() * sizeof (float));
    state.SetItemsProcessed (state.iterations() * static_cast<int64_t> (buf.numSamples()));
}
BENCHMARK (BM_AudioBuffer_BlockFill_Zero)->ArgsProduct ({ kFrameSizes, kChannelCounts });