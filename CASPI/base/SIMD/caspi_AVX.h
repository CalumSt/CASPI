/************************************************************************
 .d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888

* @file caspi_AVX.h
* @author CS Islay
* @brief AVX-specific SIMD operations (256-bit vectors).
*
*
* OVERVIEW
*
* Provides AVX (Advanced Vector Extensions) specific operations for 256-bit
* vectors. These provide twice the throughput of 128-bit operations on
* AVX-capable processors.
*
* This file is automatically included when AVX is enabled (CASPI_HAS_AVX).
* All operations here complement the base operations in caspi_Operations.h
* and caspi_LoadStore.h.
*
*
* VECTOR TYPES
*
* +---------------+--------+------+-------+--------------------------+
* | Type          | Width  | Bits | Lanes | Description              |
* +---------------+--------+------+-------+--------------------------+
* | float32x8    | 256    | 32   | 8     | 8x single-precision      |
* | float64x4    | 256    | 64   | 4     | 4x double-precision      |
* +---------------+--------+------+-------+--------------------------+
*
*
* PLATFORM DETECTION
*
* CASPI_HAS_AVX is defined when:
*   - Compiler supports AVX (-mavx or /arch:AVX)
*   - Target architecture is AVX-capable
*
* Combined with CASPI_HAS_FMA for fused multiply-add support.
*
 ************************************************************************/
#ifndef CASPI_AVX_H
#define CASPI_AVX_H

#include "base/caspi_Platform.h"
#include "base/caspi_Assert.h"

#include <cmath>
#include <cstring>

#if defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_AVX)
#include <immintrin.h>
#elif defined(CASPI_HAS_SSE1)
#include <xmmintrin.h>
#endif

#if defined(CASPI_HAS_NEON)
#include <arm_neon.h>
#endif

#if defined(CASPI_HAS_WASM_SIMD)
#include <wasm_simd128.h>
#endif

namespace CASPI
{
    namespace SIMD
    {

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Load 256-bit float vector from aligned memory (AVX).
         *
         * @param p          Pointer to 32-byte aligned memory
         * @return           Loaded 8-lane float vector
         */
        template <>
        inline float32x8 load_aligned<float> (const float* p)
        {
            return _mm256_load_ps (p);
        }
        /**
         * @brief Load 256-bit float vector from unaligned memory (AVX).
         *
         * @param p          Pointer to memory (any alignment)
         * @return           Loaded 8-lane float vector
         */
        template <>
        inline float32x8 load_unaligned<float> (const float* p)
        {
            return _mm256_loadu_ps (p);
        }
        /**
         * @brief Load 256-bit double vector from aligned memory (AVX).
         *
         * @param p          Pointer to 32-byte aligned memory
         * @return           Loaded 4-lane double vector
         */
        template <>
        inline float64x4 load_aligned<double> (const double* p)
        {
            return _mm256_load_pd (p);
        }
        /**
         * @brief Load 256-bit double vector from unaligned memory (AVX).
         *
         * @param p          Pointer to memory (any alignment)
         * @return           Loaded 4-lane double vector
         */
        template <>
        inline float64x4 load_unaligned<double> (const double* p)
        {
            return _mm256_loadu_pd (p);
        }


        /**
         * @brief Store 256-bit float vector to aligned memory (AVX).
         *
         * @param p          Pointer to 32-byte aligned memory
         * @param v          Vector to store
         */
        inline void store_aligned (float* p, float32x8 v) { _mm256_store_ps (p, v); }
        /**
         * @brief Store 256-bit float vector to unaligned memory (AVX).
         *
         * @param p          Pointer to memory (any alignment)
         * @param v          Vector to store
         */
        inline void store_unaligned (float* p, float32x8 v) { _mm256_storeu_ps (p, v); }
        /**
         * @brief Store 256-bit double vector to aligned memory (AVX).
         *
         * @param p          Pointer to 32-byte aligned memory
         * @param v          Vector to store
         */
        inline void store_aligned (double* p, float64x4 v) { _mm256_store_pd (p, v); }
        /**
         * @brief Store 256-bit double vector to unaligned memory (AVX).
         *
         * @param p          Pointer to memory (any alignment)
         * @param v          Vector to store
         */
        inline void store_unaligned (double* p, float64x4 v) { _mm256_storeu_pd (p, v); }

        /**
         * @brief Store 256-bit float vector with auto detection (AVX).
         *
         * @param p          Pointer to memory
         * @param v          Vector to store
         */
        inline void store (float* p, float32x8 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<float>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /**
         * @brief Store 256-bit double vector with auto detection (AVX).
         *
         * @param p          Pointer to memory
         * @param v          Vector to store
         */
        inline void store (double* p, float64x4 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<double>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /**
         * @brief Non-temporal (streaming) store for 256-bit float vector (AVX).
         *
         * Bypasses the cache for large buffer writes.
         *
         * @param p          Pointer to 32-byte aligned memory
         * @param v          Vector to store
         */
        inline void stream_store (float* p, float32x8 v) noexcept
        {
            _mm256_stream_ps (p, v);
        }

        /**
         * @brief Non-temporal (streaming) store for 256-bit double vector (AVX).
         *
         * @param p          Pointer to 32-byte aligned memory
         * @param v          Vector to store
         */
        inline void stream_store (double* p, float64x4 v) noexcept
        {
            _mm256_stream_pd (p, v);
        }

        /**
         * @brief Broadcast scalar to 256-bit vector lanes (AVX).
         *
         * Creates a 256-bit vector where all lanes contain the same value.
         *
         * @param x         Value to broadcast (float or double)
         * @return          Vector with all lanes set to x
         *
         * @note Named set1_256 to avoid conflicting with the 128-bit set1<T> template.
         *
         * @code
         * float32x8 eight = set1_256(8.0f);  // [8,8,8,8,8,8,8,8]
         * @endcode
         */

        inline float32x8 set1_256 (float x) { return _mm256_set1_ps (x); }
        inline float64x4 set1_256 (double x) { return _mm256_set1_pd (x); }

        /**
         * @brief Per-lane addition for 256-bit float vector (AVX).
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane sums
         */
        inline float32x8 add (float32x8 a, float32x8 b) { return _mm256_add_ps (a, b); }
        /**
         * @brief Per-lane subtraction for 256-bit float vector (AVX).
         */
        inline float32x8 sub (float32x8 a, float32x8 b) { return _mm256_sub_ps (a, b); }
        /**
         * @brief Per-lane multiplication for 256-bit float vector (AVX).
         */
        inline float32x8 mul (float32x8 a, float32x8 b) { return _mm256_mul_ps (a, b); }
        /**
         * @brief Per-lane division for 256-bit float vector (AVX).
         */
        inline float32x8 div (float32x8 a, float32x8 b) { return _mm256_div_ps (a, b); }
        /**
         * @brief Per-lane addition for 256-bit double vector (AVX).
         */
        inline float64x4 add (float64x4 a, float64x4 b) { return _mm256_add_pd (a, b); }
        /**
         * @brief Per-lane subtraction for 256-bit double vector (AVX).
         */
        inline float64x4 sub (float64x4 a, float64x4 b) { return _mm256_sub_pd (a, b); }
        /**
         * @brief Per-lane multiplication for 256-bit double vector (AVX).
         */
        inline float64x4 mul (float64x4 a, float64x4 b) { return _mm256_mul_pd (a, b); }
        /**
         * @brief Per-lane division for 256-bit double vector (AVX).
         */
        inline float64x4 div (float64x4 a, float64x4 b) { return _mm256_div_pd (a, b); }

        /**
         * @brief Fused multiply-add for 256-bit float vector.
         *
         * @param a         First input vector (multiplicand)
         * @param b         Second input vector (multiplicand)
         * @param c         Vector to add
         * @return          Fused multiply-add result
         */
        inline float32x8 mul_add (float32x8 a, float32x8 b, float32x8 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm256_fmadd_ps (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

        /**
         * @brief Fused multiply-add for 256-bit double vector.
         *
         * @param a         First input vector (multiplicand)
         * @param b         Second input vector (multiplicand)
         * @param c         Vector to add
         * @return          Fused multiply-add result
         */
        inline float64x4 mul_add (float64x4 a, float64x4 b, float64x4 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm256_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }


        /**
         * @brief Per-lane minimum for 256-bit float vector.
         */
        inline float32x8 min (float32x8 a, float32x8 b) { return _mm256_min_ps (a, b); }
        /**
         * @brief Per-lane maximum for 256-bit float vector.
         */
        inline float32x8 max (float32x8 a, float32x8 b) { return _mm256_max_ps (a, b); }
        /**
         * @brief Per-lane minimum for 256-bit double vector.
         */
        inline float64x4 min (float64x4 a, float64x4 b) { return _mm256_min_pd (a, b); }
        /**
         * @brief Per-lane maximum for 256-bit double vector.
         */
        inline float64x4 max (float64x4 a, float64x4 b) { return _mm256_max_pd (a, b); }


        /**
         * @brief Sum all lanes of a 256-bit float vector.
         *
         * @param v         8-lane input vector
         * @return          Sum of all 8 lanes
         */
        inline float hsum (float32x8 v)
        {
            __m128 lo = _mm256_castps256_ps128 (v);
            __m128 hi = _mm256_extractf128_ps (v, 1);
            return hsum (add (lo, hi));
        }

        /**
         * @brief Sum all lanes of a 256-bit double vector.
         *
         * @param v         4-lane input vector
         * @return          Sum of all 4 lanes
         */
        inline double hsum (float64x4 v)
        {
            __m128d lo = _mm256_castpd256_pd128 (v);
            __m128d hi = _mm256_extractf128_pd (v, 1);
            return hsum (add (lo, hi));
        }
#endif

    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_AVX_H

