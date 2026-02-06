#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Phase.h"
#include <benchmark/benchmark.h>

static void BM_PhaseAdvanceAndWrap (benchmark::State& state)
{
    using FloatType = double;
    CASPI::Phase<FloatType> phase;
    // Set increment directly: frequency/sampleRate = 440.0/44100.0
    phase.increment = 440.0 / 44100.0;

    // Enable or disable FTZ/DAZ based on benchmark argument
    bool enableFlushToZero = state.range (0) != 0;
    CASPI::Core::configureFlushToZero (enableFlushToZero);

    constexpr FloatType wrapLimit = CASPI::Constants::TWO_PI<FloatType>;

    for (auto _ : state)
    {
        // simulate a buffer of 512 samples
        for (int i = 0; i < 512; ++i)
        {
            benchmark::DoNotOptimize (phase.advanceAndWrap (wrapLimit));
        }
    }
}

// Register benchmark: arg 0 = off, arg 1 = on
BENCHMARK (BM_PhaseAdvanceAndWrap)->Arg (0)->Arg (1);

BENCHMARK_MAIN();
