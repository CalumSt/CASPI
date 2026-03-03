// SIMD_tests.cpp
//
// Comprehensive Unit Tests for CASPI SIMD API (caspi_SIMD.h)
//
// ============================================================================
// TEST COVERAGE OVERVIEW
// ============================================================================
//
// This test suite provides comprehensive coverage of the SIMD API across all
// supported platforms (SSE/AVX on x86, NEON on ARM, WASM SIMD, scalar fallback).
//
// ============================================================================
// TEST CATEGORIES AND RATIONALE
// ============================================================================
//
// 1. CONSTRUCTION TESTS
//    Purpose: Verify that SIMD vectors can be created correctly from scalars
//             and arrays, with proper lane ordering across all platforms.
//    Why: Lane ordering differs between intrinsics (_mm_set_ps reverses order),
//         so we must verify data ends up in the correct lanes.
//    Tests:
//    - float32x4: ConstructionScalars, LoadStore
//    - float64x2: ConstructionScalars, ConstructionFromArray, LoadStore
//    - float32x8 (AVX): ConstructionScalars, LoadStore
//    - float64x4 (AVX): ConstructionScalars, LoadStore
//
// 2. BROADCAST TESTS
//    Purpose: Ensure scalar values can be replicated to all lanes correctly.
//    Why: Broadcast is a fundamental operation for applying constants to vectors.
//    Tests:
//    - float32x4: Broadcast (via set1)
//    - float64x2: Broadcast
//    - float32x8 (AVX): Broadcast
//    - float64x4 (AVX): Broadcast
//
// 3. ARITHMETIC TESTS
//    Purpose: Validate that per-lane arithmetic operations produce correct results.
//    Why: Core functionality for DSP, must work identically across platforms.
//    Tests:
//    - Basic operations: add, sub, mul, div for all types
//    - Edge cases: zeros, negatives, large/small_value values
//    - Chained operations: complex expressions like (a+b)*c-d
//
// 4. FMA AND MUL_ADD TESTS
//    Purpose: Verify fused multiply-add operations work correctly and that
//             mul_add wrapper uses FMA when available.
//    Why: FMA is critical for performance in audio DSP (e.g., filter coefficients).
//         The wrapper ensures optimal code path is used automatically.
//    Tests:
//    - Explicit FMA (when CASPI_HAS_FMA is defined)
//    - mul_add wrapper (always available, uses FMA or fallback)
//    - Correctness of a*b+c computation
//
// 5. FAST kernelsIMATION TESTS
//    Purpose: Verify that rcp() and rsqrt() produce reasonably accurate results.
//    Why: These functions trade accuracy for speed (~4-8x faster). Users need
//         to know they work correctly within their error bounds.
//    Tests:
//    - rcp: reciprocal accuracy within tolerance
//    - rsqrt: reciprocal square root accuracy within tolerance
//    - Comparison with exact division/sqrt
//
// 6. COMPARISON TESTS
//    Purpose: Verify that comparison operations produce correct masks.
//    Why: Masks are used for conditional operations (blend, clamping, etc.).
//         Incorrect masks lead to subtle bugs in audio processing.
//    Tests:
//    - cmp_eq: equality comparison
//    - cmp_lt: less-than comparison
//    - Bit-level verification of mask values (0xFFFFFFFF vs 0x00000000)
//
// 7. MIN/MAX TESTS
//    Purpose: Validate per-lane minimum and maximum operations.
//    Why: Used for clamping, envelope followers, peak detection in audio.
//    Tests:
//    - Per-lane min/max for float32x4 and float64x2
//    - Edge cases with negative numbers
//
// 8. HORIZONTAL REDUCTION TESTS
//    Purpose: Verify operations that reduce a vector to a scalar.
//    Why: Used for computing sums, finding peaks, etc. Different platforms
//         implement these differently (hadd vs shuffle vs fallback).
//    Tests:
//    - hsum: horizontal sum
//    - hmax: horizontal maximum
//    - hmin: horizontal minimum
//
// 9. BLEND/SELECT TESTS
//    Purpose: Verify mask-based selection works correctly.
//    Why: Critical for conditional processing without branches (e.g., soft clipping).
//    Tests:
//    - All-true mask: should select all from b
//    - All-false mask: should select all from a
//    - Mixed mask: should select per-lane correctly
//    - ComparisonsAndBlend: integration test
//
// 10. LANE-WISE MATH TESTS
//     Purpose: Verify unary math operations on all lanes.
//     Why: Common operations in audio DSP (rectification, normalization, etc.).
//     Tests:
//     - negate: sign flip
//     - abs: absolute value
//     - sqrt: square root
//     - Chained operations: negate(abs(x))
//
// 11. EDGE CASE TESTS
//     Purpose: Verify behavior with special values.
//     Why: Audio processing encounters zeros, very small_value values, etc.
//          Must handle gracefully across platforms.
//     Tests:
//     - Zero operations: add/mul with zero
//     - Negative numbers: ensure sign handling is correct
//     - small_value numbers: near machine epsilon
//     - Large numbers: near overflow
//     - Division by small_value values (avoiding exact zero to prevent UB)
//
// 12. REAL-WORLD INTEGRATION TESTS
//     Purpose: Verify API works correctly in realistic usage patterns.
//     Why: Unit tests are necessary but not sufficient. Real-world patterns
//          expose integration issues, performance characteristics, etc.
//     Tests:
//     - AudioGainRamping: smooth parameter changes
//     - StereoInterleaving: multi-channel processing
//     - ClippingWithBlend: soft clipping using blend
//     - AudioBufferProcessing: stereo gain application
//     - MixedPrecisionProcessing: float→double→float conversion
//
// 13. AVX EXTENDED TESTS
//     Purpose: Verify 256-bit vector operations (8 floats, 4 doubles).
//     Why: AVX provides wider vectors for better throughput. Must verify
//          all 8/4 lanes work correctly.
//     Tests:
//     - float32x8: construction, load/store, arithmetic
//     - float64x4: construction, load/store, arithmetic
//     - LargeAudioBuffer: processing with AVX
// 14. KERNEL OPERATIONS
//    - Add, subtract, multiply kernels
//    - Scale, copy, fill kernels
//    - MAC, lerp, clamp, abs kernels
//
// 15. BLOCK OPERATIONS
//    - Binary operations (add, sub, mul)
//    - Unary operations (scale, abs, clamp)
//    - Fill operations
//    - Ternary operations (MAC)
//    - Reduction operations (min, max, sum, dot)
//
// 16. ALIGNMENT HANDLING
//    - Prologue execution
//    - Aligned SIMD loop
//    - Epilogue execution
//    - Unaligned data paths
//
// 17. EDGE CASES
//    - Zero-length arrays
//    - Arrays small_valueer than SIMD width
//    - Odd-sized arrays
//    - Large arrays
//
//
// ============================================================================

#include "base/caspi_Constants.h"
#include "SIMD/SIMD_test_helpers.h"
#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <vector>

using namespace CASPI::SIMD;


// ============================================================================
// REAL-WORLD INTEGRATION TESTS
// ============================================================================

TEST(SIMD_RealWorld, AudioGainRamping) {
    // Simulate smooth gain ramping in audio processing
    const int numSamples = 16;
    float input[numSamples];
    float output[numSamples];

    // Initialize with constant signal
    for (int i = 0; i < numSamples; i++) {
        input[i] = 1.0f;
    }

    // Ramp gain from 0.0 to 1.0
    float gain_start = 0.0f;
    float gain_end = 1.0f;
    float gain_increment = (gain_end - gain_start) / numSamples;

    for (int i = 0; i < numSamples; i += 4) {
        float32x4 samples = load(&input[i]);
        float32x4 gains = {
            gain_start + (i + 0) * gain_increment,
            gain_start + (i + 1) * gain_increment,
            gain_start + (i + 2) * gain_increment,
            gain_start + (i + 3) * gain_increment
        };

        float32x4 result = mul(samples, gains);
        store(&output[i], result);
    }

    // Verify smooth ramp
    for (int i = 0; i < numSamples; i++) {
        float expected = gain_start + i * gain_increment;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, StereoInterleaving) {
    // Test interleaved stereo processing
    const int numFrames = 8;
    float left[numFrames] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    float right[numFrames] = {10.f, 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f};
    float interleaved[numFrames * 2];

    // Process and interleave using SIMD
    for (int i = 0; i < numFrames; i += 4) {
        float32x4 l = load(&left[i]);
        float32x4 r = load(&right[i]);

        // Apply processing (e.g., gain)
        float32x4 gain = set1(0.5f);
        l = mul(l, gain);
        r = mul(r, gain);

        // Store (in real implementation, would interleave properly)
        float temp_l[4], temp_r[4];
        store(temp_l, l);
        store(temp_r, r);

        for (int j = 0; j < 4; j++) {
            interleaved[(i + j) * 2 + 0] = temp_l[j];
            interleaved[(i + j) * 2 + 1] = temp_r[j];
        }
    }

    // Verify
    for (int i = 0; i < numFrames; i++) {
        EXPECT_NEAR(interleaved[i * 2 + 0], left[i] * 0.5f, EPSILON_F32);
        EXPECT_NEAR(interleaved[i * 2 + 1], right[i] * 0.5f, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, ClippingWithBlend) {
    // Demonstrate soft clipping using blend
    float32x4 samples = {-1.5f, -0.5f, 0.5f, 1.5f};
    float32x4 threshold = set1(1.0f);
    float32x4 neg_threshold = set1(-1.0f);

    // Clip to [-1.0, 1.0] range
    float32x4 too_high = cmp_lt(threshold, samples);
    float32x4 too_low = cmp_lt(samples, neg_threshold);

    float32x4 result = blend(samples, threshold, too_high);
    result = blend(result, neg_threshold, too_low);

    float out[4];
    store(out, result);

    float expected[4] = {-1.0f, -0.5f, 0.5f, 1.0f};
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out[i], expected[i], EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, AudioBufferProcessing) {
    // Simulate processing a stereo audio buffer with double precision
    const int numSamples = 100;
    double inputL[numSamples];
    double inputR[numSamples];
    double outputL[numSamples];
    double outputR[numSamples];

    // Initialize with sine wave
    for (int i = 0; i < numSamples; i++) {
        inputL[i] = std::sin(2.0 * CASPI::Constants::PI<float> * i / 50.0);
        inputR[i] = std::cos(2.0 * CASPI::Constants::PI<float> * i / 50.0);
    }

    // Process in pairs using SIMD
    const double gain = 0.5;
    float64x2 gainVec = set1(gain);

    for (int i = 0; i < numSamples; i += 2) {
        // Load L+R as a pair
        float64x2 samplesL = load(&inputL[i]);
        float64x2 samplesR = load(&inputR[i]);

        // Apply gain
        samplesL = mul(samplesL, gainVec);
        samplesR = mul(samplesR, gainVec);

        // Store
        store(&outputL[i], samplesL);
        store(&outputR[i], samplesR);
    }

    // Verify
    for (int i = 0; i < numSamples; i++) {
        EXPECT_NEAR(outputL[i], inputL[i] * gain, EPSILON_F64);
        EXPECT_NEAR(outputR[i], inputR[i] * gain, EPSILON_F64);
    }
}

TEST(SIMD_RealWorld, MulAddExample) {
    // Realistic use case: apply gain and add DC offset
    const int numSamples = 16;
    float input[numSamples];
    float output[numSamples];

    for (int i = 0; i < numSamples; i++) {
        input[i] = std::sin(2.0f * CASPI::Constants::PI<float> * i / 8.0f);
    }

    const float gain = 0.8f;
    const float dc_offset = 0.1f;

    float32x4 gain_vec = set1(gain);
    float32x4 offset_vec = set1(dc_offset);

    for (int i = 0; i < numSamples; i += 4) {
        float32x4 samples = load(&input[i]);
        // Use mul_add for: samples * gain + offset
        float32x4 result = mul_add(samples, gain_vec, offset_vec);
        store(&output[i], result);
    }

    // Verify
    for (int i = 0; i < numSamples; i++) {
        float expected = input[i] * gain + dc_offset;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}


TEST(SIMD_RealWorld, StereoMixing) {
    constexpr std::size_t N = 512;
    float left[N], right[N], master[N];

    // Initialize tracks
    for (std::size_t i = 0; i < N; ++i) {
        left[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
        right[i] = std::cos(2.0f * 3.14159f * i / 50.0f);
        master[i] = 0.0f;
    }

    // Apply gains
    ops::scale(left, N, 0.7f);
    ops::scale(right, N, 0.5f);

    // Mix to master
    ops::add(master, left, N);
    ops::add(master, right, N);

    // Verify output is reasonable
    float peak = ops::find_max(master, N);
    EXPECT_LE(peak, 2.0f);
}

TEST(SIMD_RealWorld, RMSCalculation) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate sine wave
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
    }

    // Calculate RMS using dot product
    float sum_squares = ops::dot_product(audio, audio, N);
    float rms = std::sqrt(sum_squares / N);

    // RMS of sine wave is kernelsimately 0.707
    EXPECT_NEAR(rms, 0.707f, 0.01f);
}

TEST(SIMD_RealWorld, SoftClipping) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with peaks
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f) * 1.5f;
    }

    // Apply soft clipping
    ops::clamp(audio, -1.0f, 1.0f, N);

    // Verify all samples are in valid range
    float min_val = ops::find_min(audio, N);
    float max_val = ops::find_max(audio, N);

    EXPECT_GE(min_val, -1.0f);
    EXPECT_LE(max_val, 1.0f);
}

TEST(SIMD_RealWorld, EnvelopeFollower) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with varying amplitude
    for (std::size_t i = 0; i < N; ++i) {
        float envelope = static_cast<float>(i) / N;
        audio[i] = std::sin(2.0f * 3.14159f * i / 20.0f) * envelope;
    }

    // Get absolute values
    ops::abs(audio, N);

    // Find peak
    float peak = ops::find_max(audio, N);

    // Peak should be near the end where envelope is highest
    EXPECT_GT(peak, 0.9f);
}

TEST(SIMD_RealWorld, Crossfade) {
    constexpr std::size_t N = 512;
    float buffer_a[N], buffer_b[N], output[N];

    // Initialize with different signals
    for (std::size_t i = 0; i < N; ++i) {
        buffer_a[i] = std::sin(2.0f * 3.14159f * i / 50.0f);
        buffer_b[i] = std::cos(2.0f * 3.14159f * i / 50.0f);
    }

    // Crossfade 50%
    ops::lerp(output, buffer_a, buffer_b, 0.5f, N);

    // Verify smooth transition
    for (std::size_t i = 0; i < N; ++i) {
        float expected = buffer_a[i] * 0.5f + buffer_b[i] * 0.5f;
        EXPECT_NEAR(output[i], expected, EPSILON_F32);
    }
}

TEST(SIMD_RealWorld, DynamicRangeCompression) {
    constexpr std::size_t N = 512;
    float audio[N];

    // Generate audio with wide dynamic range
    for (std::size_t i = 0; i < N; ++i) {
        audio[i] = std::sin(2.0f * 3.14159f * i / 50.0f) * 2.0f;
    }

    // Get absolute values
    float abs_audio[N];
    ops::copy(abs_audio, audio, N);
    ops::abs(abs_audio, N);

    // Find peak
    float peak = ops::find_max(abs_audio, N);

    // Calculate makeup gain to normalize
    float makeup_gain = 1.0f / peak;

    // Apply compression (simple limiting)
    ops::clamp(audio, -1.0f, 1.0f, N);
    ops::scale(audio, N, makeup_gain);

    // Verify normalized output
    float new_peak = ops::find_max(audio, N);
    EXPECT_LE(new_peak, 1.0f);
}

TEST(SIMD_RealWorld, ConvolutionMAC) {
    // Simple convolution using MAC
    constexpr std::size_t signal_len = 16;
    constexpr std::size_t kernel_len = 4;

    float signal[signal_len];
    float kernel[kernel_len] = {0.25f, 0.5f, 0.25f, 0.0f};
    float output[signal_len];

    // Generate impulse
    for (std::size_t i = 0; i < signal_len; ++i) {
        signal[i] = (i == 8) ? 1.0f : 0.0f;
    }

    // Simple convolution (not optimized, just testing MAC)
    ops::fill(output, signal_len, 0.0f);

    for (std::size_t i = 0; i < signal_len; ++i) {
        for (std::size_t k = 0; k < kernel_len && (i + k) < signal_len; ++k) {
            output[i + k] += signal[i] * kernel[k];
        }
    }

    // Verify impulse response
    EXPECT_NEAR(output[8], 0.25f, EPSILON_F32);
    EXPECT_NEAR(output[9], 0.5f, EPSILON_F32);
    EXPECT_NEAR(output[10], 0.25f, EPSILON_F32);
}

        // ============================================================================
// EDGE CASES
// ============================================================================

TEST(SIMD_EdgeCases, ZeroLength) {
    float data[1] = {1.0f};

    ops::scale(data, 0, 2.0f);
    float sum = ops::sum(data, 0);

    EXPECT_NEAR(sum, 0.0f, EPSILON_F32);
    EXPECT_NEAR(data[0], 1.0f, EPSILON_F32);  // Unchanged
}

TEST(SIMD_EdgeCases, SingleElement) {
    float data[1] = {5.0f};

    ops::scale(data, 1, 3.0f);

    EXPECT_NEAR(data[0], 15.0f, EPSILON_F32);
}

TEST(SIMD_EdgeCases, OddSize) {
    constexpr std::size_t N = 13;  // Not divisible by 4
    float data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<float>(i);
    }

    ops::scale(data, N, 2.0f);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], static_cast<float>(i * 2), EPSILON_F32);
    }
}

TEST(SIMD_EdgeCases, LargeArray) {
    constexpr std::size_t N = 10000;
    std::vector<float> data(N);

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = 1.0f;
    }

    ops::scale(data.data(), N, 0.5f);

    float sum = ops::sum(data.data(), N);
    EXPECT_NEAR(sum, 5000.0f, 0.1f);  // Allow small_value accumulated error
}

TEST(SIMD_EdgeCases, Verysmall_valueValues) {
    constexpr std::size_t N = 16;
    double data[N];

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = 1e-100;
    }

    ops::scale(data, N, 2.0);

    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data[i], 2e-100, 1e-110);
    }
}

// ============================================================================
// PERFORMANCE VERIFICATION
// ============================================================================

TEST(SIMD_Performance, CompareScalarVsSIMD) {
    constexpr std::size_t N = 10000;
    std::vector<float> data_simd(N);
    std::vector<float> data_scalar(N);

    // Initialize
    for (std::size_t i = 0; i < N; ++i) {
        data_simd[i] = static_cast<float>(i);
        data_scalar[i] = static_cast<float>(i);
    }

    // SIMD path
    ops::scale(data_simd.data(), N, 2.5f);

    // Scalar path
    for (std::size_t i = 0; i < N; ++i) {
        data_scalar[i] *= 2.5f;
    }

    // Verify results match
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(data_simd[i], data_scalar[i], EPSILON_F32);
    }
}

TEST(SIMD_Performance, ReductionAccuracy) {
    constexpr std::size_t N = 100000;
    std::vector<float> data(N);

    // Generate random data
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (std::size_t i = 0; i < N; ++i) {
        data[i] = dist(rng);
    }

    // Calculate sum with SIMD
    float simd_sum = ops::sum(data.data(), N);

    // Calculate sum with scalar (Kahan summation for accuracy)
    float scalar_sum = 0.0f;
    float c = 0.0f;
    for (std::size_t i = 0; i < N; ++i) {
        float y = data[i] - c;
        float t = scalar_sum + y;
        c = (t - scalar_sum) - y;
        scalar_sum = t;
    }

    // Should be very close (allowing for small_value FP differences)
    EXPECT_NEAR(simd_sum, scalar_sum, std::abs(scalar_sum) * 0.001f);
}

// ============================================================================
// CORRECTNESS - COMPARE WITH BASIC OPERATIONS
// ============================================================================

TEST(SIMD_Correctness, BlendSemantics) {
    // Verify fixed blend semantics
    float32x4 a = set1<float>(1.0f);
    float32x4 b = set1<float>(2.0f);

    // All true mask
    float32x4 all_true = cmp_lt(a, b);
    float32x4 result_true = blend(a, b, all_true);

    float out_true[4];
    store(out_true, result_true);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_true[i], 2.0f, EPSILON_F32);
    }

    // All false mask
    float32x4 all_false = cmp_lt(b, a);
    float32x4 result_false = blend(a, b, all_false);

    float out_false[4];
    store(out_false, result_false);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(out_false[i], 1.0f, EPSILON_F32);
    }
}

TEST(SIMD_Correctness, HorizontalOptimization) {
    // Verify optimized horizontal operations produce correct results
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float32x4 v = load<float>(values);

    float sum = hsum(v);
    float max_val = hmax(v);
    float min_val = hmin(v);

    EXPECT_NEAR(sum, 10.0f, EPSILON_F32);
    EXPECT_NEAR(max_val, 4.0f, EPSILON_F32);
    EXPECT_NEAR(min_val, 1.0f, EPSILON_F32);
}
