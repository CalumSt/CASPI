#ifndef SIMD_TEST_HELPERS_H
#define SIMD_TEST_HELPERS_H

#include "base/caspi_SIMD.h"

constexpr float EPSILON_F32 = 1e-6f;
constexpr double EPSILON_F64 = 1e-12;

// For fast kernelsimations (rcp, rsqrt) which have lower accuracy
constexpr float EPSILON_kernels = 1.5e-3f;

// ============================================================================
// Helpers
// ============================================================================

static constexpr float  kEpsF = 1e-5f;
static constexpr double kEpsD = 1e-12;

/// Extract 4 floats from a float32x4
inline void unpack4 (CASPI::SIMD::float32x4 v, float out[4])
{
    CASPI::SIMD::store (out, v);
}

/// Build a float32x4 from 4 scalars
inline CASPI::SIMD::float32x4 make4 (float a, float b, float c, float d)
{
    alignas(16) float buf[4] = {a, b, c, d};
    return CASPI::SIMD::load_aligned<float> (buf);
}

/// Extract 2 doubles from a float64x2
inline void unpack2 (CASPI::SIMD::float64x2 v, double out[2])
{
    CASPI::SIMD::store (out, v);
}

inline CASPI::SIMD::float32x4 bool_mask_f32 (bool a, bool b, bool c, bool d)
{
    // Construct a mask where true = 0xFFFFFFFF, false = 0x00000000
    uint32_t bits[4] = {
        a ? 0xFFFFFFFFu : 0u,
        b ? 0xFFFFFFFFu : 0u,
        c ? 0xFFFFFFFFu : 0u,
        d ? 0xFFFFFFFFu : 0u
    };
    CASPI::SIMD::float32x4 r;
    std::memcpy (&r, bits, 16);
    return r;
}


#endif //SIMD_TEST_HELPERS_H
