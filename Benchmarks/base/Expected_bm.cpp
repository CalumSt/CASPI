#include <benchmark/benchmark.h>
#include "core/caspi_Expected.h"  // your expected implementation

#include <string>
#include <stdexcept>

// A dummy function using exceptions
int func_throw(bool fail) {
    if (fail) throw std::runtime_error("error");
    return 42;
}

// A dummy function using expected
CASPI::expected<int, std::string> func_expected(bool fail) {
    if (fail) return CASPI::expected<int, std::string>(CASPI::unexpect, "error");
    return CASPI::expected<int, std::string>(42);
}

// Benchmark for success case with exceptions (no throw)
static void BM_Exception_Success(benchmark::State& state) {
    for (auto _ : state) {
        try {
            benchmark::DoNotOptimize(func_throw(false));
        } catch(...) {
            benchmark::DoNotOptimize(0);
        }
    }
}
BENCHMARK(BM_Exception_Success);

// Benchmark for failure case with exceptions (throw)
static void BM_Exception_Fail(benchmark::State& state) {
    for (auto _ : state) {
        try {
            benchmark::DoNotOptimize(func_throw(true));
        } catch(...) {
            benchmark::DoNotOptimize(0);
        }
    }
}
BENCHMARK(BM_Exception_Fail);

// Benchmark for success case with expected
static void BM_Expected_Success(benchmark::State& state) {
    for (auto _ : state) {
        auto res = func_expected(false);
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_Expected_Success);

// Benchmark for failure case with expected
static void BM_Expected_Fail(benchmark::State& state) {
    for (auto _ : state) {
        auto res = func_expected(true);
        benchmark::DoNotOptimize(res);
    }
}
BENCHMARK(BM_Expected_Fail);
