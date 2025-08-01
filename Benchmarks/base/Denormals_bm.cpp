#include <benchmark/benchmark.h>
#include <vector>
#include <cmath>
#include <limits>
#include "core/caspi_Core.h"

#if defined(CASPI_FEATURES_HAS_FLUSH_DENORMALS)
    #include <xmmintrin.h>  // for SSE flush-zero intrinsics
#else
    #pragma message("CASPI_FEATURES_HAS_FLUSH_DENORMALS is not defined — hardware flush-to-zero will not be benchmarked.")
#endif

constexpr int kSize = 1024 * 8;

// Generate an array of denormal floats
template <typename FloatType>
std::vector<FloatType> generateDenormals() {
    std::vector<FloatType> data(kSize, std::numeric_limits<FloatType>::denorm_min());
    return data;
}

// No FTZ, no manual flush
static void BM_DenormalProcessing_Normal(benchmark::State& state) {
    auto data = generateDenormals<float>();
    volatile float sum = 0.0f;

    for (auto _ : state) {
        for (float x : data)
            sum += x * 0.5f;
    }

    benchmark::DoNotOptimize(sum);
}
BENCHMARK(BM_DenormalProcessing_Normal);

// Manual flush using flushToZero()
static void BM_DenormalProcessing_ManualFlush(benchmark::State& state) {
    auto data = generateDenormals<float>();
    volatile float sum = 0.0f;

    for (auto _ : state) {
        for (float x : data)
            sum += CASPI::Core::flushToZero(x * 0.5f);
    }

    benchmark::DoNotOptimize(sum);
}
BENCHMARK(BM_DenormalProcessing_ManualFlush);

/// Benchmark: Hardware flush-to-zero (SSE2+)
#if defined(CASPI_FEATURES_HAS_FLUSH_DENORMALS)
static void BM_Denormal_HWFlush(benchmark::State& state) {
    // Enable FTZ mode
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    auto data = generateDenormals<float>();
    volatile float sum = 0.0f;
    for (auto _ : state) {
        for (float x : data)
            sum += x * 0.5f;
    }

    benchmark::DoNotOptimize(sum);

    // Restore modes (optional, polite for tests)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);
}
BENCHMARK(BM_Denormal_HWFlush);
#else
// Optional dummy benchmark to avoid "missing symbol" when listed
static void BM_Denormal_HWFlushUnavailable(benchmark::State& state) {
    state.SkipWithError("Flush-to-zero not supported on this platform.");
}
BENCHMARK(BM_Denormal_HWFlushUnavailable);
#endif // CASPI_FEATURES_HAS_FLUSH_DENORMALS