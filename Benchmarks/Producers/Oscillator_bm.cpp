/*******************************************************************************
 * Benchmarks
 ******************************************************************************/

#include <benchmark/benchmark.h>
#include "oscillators/caspi_BlepOscillator.h"

static constexpr float kSR        = 44100.f;
static constexpr float kFreq      = 440.f;
static constexpr int   kBlock     = 512;

static void BM_Sine_renderSample (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Sine, kSR, kFreq);
    for (auto _ : state) { benchmark::DoNotOptimize (osc.renderSample()); }
}
BENCHMARK (BM_Sine_renderSample);

static void BM_Saw_renderSample (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Saw, kSR, kFreq);
    for (auto _ : state) { benchmark::DoNotOptimize (osc.renderSample()); }
}
BENCHMARK (BM_Saw_renderSample);

static void BM_Square_renderSample (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Square, kSR, kFreq);
    for (auto _ : state) { benchmark::DoNotOptimize (osc.renderSample()); }
}
BENCHMARK (BM_Square_renderSample);

static void BM_Triangle_renderSample (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Triangle, kSR, kFreq);
    for (auto _ : state) { benchmark::DoNotOptimize (osc.renderSample()); }
}
BENCHMARK (BM_Triangle_renderSample);

static void BM_Saw_renderBlock512 (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Saw, kSR, kFreq);
    std::vector<float> buf (kBlock);
    for (auto _ : state)
    {
        osc.renderBlock (buf.data(), kBlock);
        benchmark::DoNotOptimize (buf.data());
    }
}
BENCHMARK (BM_Saw_renderBlock512);

static void BM_Square_renderBlock512 (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Square, kSR, kFreq);
    std::vector<float> buf (kBlock);
    for (auto _ : state)
    {
        osc.renderBlock (buf.data(), kBlock);
        benchmark::DoNotOptimize (buf.data());
    }
}
BENCHMARK (BM_Square_renderBlock512);

static void BM_Triangle_renderBlock512 (benchmark::State& state)
{
    auto osc = CASPI::Oscillators::BlepOscillator<float> (CASPI::Oscillators::WaveShape::Triangle, kSR, kFreq);
    std::vector<float> buf (kBlock);
    for (auto _ : state)
    {
        osc.renderBlock (buf.data(), kBlock);
        benchmark::DoNotOptimize (buf.data());
    }
}
BENCHMARK (BM_Triangle_renderBlock512);

/* Naive baselines for throughput comparison */

static void BM_NaiveSaw_renderBlock512 (benchmark::State& state)
{
    const float dt = kFreq / kSR;
    float p = 0.f;
    std::vector<float> buf (kBlock);
    for (auto _ : state)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buf[static_cast<std::size_t> (i)] = 2.f * p - 1.f;
            p += dt;
            if (p >= 1.f) { p -= 1.f; }
        }
        benchmark::DoNotOptimize (buf.data());
    }
}
BENCHMARK (BM_NaiveSaw_renderBlock512);

static void BM_NaiveSquare_renderBlock512 (benchmark::State& state)
{
    const float dt = kFreq / kSR;
    float p = 0.f;
    std::vector<float> buf (kBlock);
    for (auto _ : state)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            buf[static_cast<std::size_t> (i)] = (p < 0.5f) ? -1.f : 1.f;
            p += dt;
            if (p >= 1.f) { p -= 1.f; }
        }
        benchmark::DoNotOptimize (buf.data());
    }
}
BENCHMARK (BM_NaiveSquare_renderBlock512);