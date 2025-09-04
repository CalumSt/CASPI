#include <benchmark/benchmark.h>
#include <vector>
#include <array>
#include <iostream>
#include <numeric>
#include "core/caspi_AudioBuffer.h"

constexpr size_t kNumChannels = 8;
constexpr size_t kNumSamples = 1024;

using AudioBufferInterleaved = std::vector<float>;
using AudioBufferChannelMajor = std::array<std::vector<float>, kNumChannels>;

static void BM_CStyleArray(benchmark::State &state) {
    float buffer[kNumChannels][kNumSamples];
    for (auto _: state) {
        for (size_t c = 0; c < kNumChannels; ++c) {
            for (size_t s = 0; s < kNumSamples; ++s) {
                buffer[c][s] = static_cast<float>(s);
                benchmark::DoNotOptimize(buffer);
            }
        }
    }
}

BENCHMARK(BM_CStyleArray);

static void BM_StdVector(benchmark::State &state) {
    AudioBufferInterleaved buffer(kNumChannels * kNumSamples);
    for (auto _: state) {
        for (size_t i = 0; i < kNumChannels * kNumSamples; ++i) {
            buffer[i] = static_cast<float>(i);
            benchmark::DoNotOptimize(buffer);
        }
    }
}

BENCHMARK(BM_StdVector);

static void BM_AudioBufferInterleaved(benchmark::State &state) {
    AudioBufferInterleaved buffer(kNumChannels * kNumSamples);
    for (auto _: state) {
        for (size_t i = 0; i < kNumChannels * kNumSamples; ++i) {
            buffer[i] = static_cast<float>(i);
            benchmark::DoNotOptimize(buffer);
        }
    }
}

BENCHMARK(BM_AudioBufferInterleaved);

static void BM_AudioBufferChannelMajor(benchmark::State &state) {
    AudioBufferChannelMajor buffer;
    for (auto &channel: buffer) {
        channel.resize(kNumSamples);
    }
    for (auto _: state) {
        for (size_t c = 0; c < kNumChannels; ++c) {
            for (size_t s = 0; s < kNumSamples; ++s) {
                buffer[c][s] = static_cast<float>(s);
                benchmark::DoNotOptimize(buffer);
            }
        }
    }
}

BENCHMARK(BM_AudioBufferChannelMajor);

static void BM_CStyleArrayOptimised(benchmark::State &state) {
    volatile float buffer[kNumChannels][kNumSamples];
    for (auto _: state) {
        for (size_t c = 0; c < kNumChannels; ++c) {
            for (size_t s = 0; s < kNumSamples; ++s) {
                buffer[c][s] = static_cast<float>(s);
            }
        }
    }
}

BENCHMARK(BM_CStyleArrayOptimised);

static void BM_StdVectorOptimised(benchmark::State &state) {
    AudioBufferInterleaved buffer(kNumChannels * kNumSamples);
    for (auto _: state) {
        for (size_t i = 0; i < kNumChannels * kNumSamples; ++i) {
            buffer[i] = static_cast<float>(i);
        }
    }
}

BENCHMARK(BM_StdVectorOptimised);

static void BM_AudioBufferInterleavedOptimised(benchmark::State &state) {
    AudioBufferInterleaved buffer(kNumChannels * kNumSamples);
    for (auto _: state) {
        for (size_t i = 0; i < kNumChannels * kNumSamples; ++i) {
            buffer[i] = static_cast<float>(i);
        }
    }
}

BENCHMARK(BM_AudioBufferInterleavedOptimised);

static void BM_AudioBufferChannelMajorOptimised(benchmark::State &state) {
    AudioBufferChannelMajor buffer;
    for (auto &channel: buffer) {
        channel.resize(kNumSamples);
    }
    for (auto _: state) {
        for (size_t c = 0; c < kNumChannels; ++c) {
            for (size_t s = 0; s < kNumSamples; ++s) {
                buffer[c][s] = static_cast<float>(s);
            }
        }
    }
}

BENCHMARK(BM_AudioBufferChannelMajorOptimised);
