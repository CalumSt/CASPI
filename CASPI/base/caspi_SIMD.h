#ifndef CASPI_SIMD_H
#define CASPI_SIMD_H
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


* @file caspi_SIMD.h
* @author CS Islay
* @brief A header wrapping platform specific SIMD intrinsics into a common API.
* Provides basic SIMD types and operations for float32x4 and float64x2.
* For a more comprehensive SIMD abstraction, consider using a library like xsimd or simde,
* but this header provides a minimal set of operations for specific use cases like audio DSP.
*
* @note Alignment:
* - SSE/AVX prefer 16/32-byte alignment for best performance
* - Unaligned loads/stores are slower but more flexible
* - Consider using alignas(16) or alignas(32) for local buffers
* - load()/store() handle unaligned data correctly
*
* @note Performance:
* - rcp() is ~4x faster than div(set1(1.f), x) on x86, but less accurate
* - rsqrt() is ~8x faster than div(set1(1.f), sqrt(x)) on x86
* - mul_add() automatically uses FMA when available
* - Horizontal operations (hsum, hmax, etc.) are slower than lane-wise ops
*
************************************************************************/

#include "caspi_Constants.h"
#include "caspi_Platform.h"
#include <cstring> // for memcpy in fallback code

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

        /*=====================================================================
          Types
        =====================================================================*/

#if defined(CASPI_HAS_SSE)
        using float32x4 = __m128;
#elif defined(CASPI_HAS_NEON)
        using float32x4 = float32x4_t;
#elif defined(CASPI_ARCH_WASM)
        using float32x4 = v128_t;
#else
        struct float32x4
        {
                float data[4];
        };
#endif

#if defined(CASPI_HAS_SSE2)
        using float64x2 = __m128d;
#elif defined(CASPI_HAS_NEON64)
        using float64x2 = float64x2_t;
#elif defined(CASPI_HAS_WASM_SIMD)
        using float64x2 = v128_t;
#else
        struct float64x2
        {
                double data[2];
        };
#endif

#if defined(CASPI_HAS_AVX)
        using float32x8 = __m256;
        using float64x4 = __m256d;
#endif

        /*=====================================================================
          Load / Store
        =====================================================================*/

        /**
         * @brief Load 4 floats into a SIMD register (unaligned).
         */
        inline float32x4 load (const float* p)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_loadu_ps (p);
#elif defined(CASPI_HAS_NEON)
            return vld1q_f32 (p);
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_load (p);
#else
            float32x4 v;
            for (int i = 0; i < 4; i++)
            {
                v.data[i] = p[i];
            }
            return v;
#endif
        }

        /**
         * @brief Load 2 doubles into a SIMD register (unaligned).
         */
        inline float64x2 load (const double* p)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_loadu_pd (p);
#elif defined(CASPI_HAS_NEON64)
            return vld1q_f64 (p);
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_load (p);
#else
            float64x2 v;
            v.data[0] = p[0];
            v.data[1] = p[1];
            return v;
#endif
        }

        /**
         * @brief Store a SIMD float32x4 into memory (unaligned).
         */
        inline void store (float* p, float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            _mm_storeu_ps (p, v);
#elif defined(CASPI_HAS_NEON)
            vst1q_f32 (p, v);
#elif defined(CASPI_ARCH_WASM)
            wasm_v128_store (p, v);
#else
            for (int i = 0; i < 4; i++)
            {
                p[i] = v.data[i];
            }
#endif
        }

        /**
         * @brief Store a SIMD float64x2 into memory (unaligned).
         */
        inline void store (double* p, float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            _mm_storeu_pd (p, v);
#elif defined(CASPI_HAS_NEON64)
            vst1q_f64 (p, v);
#elif defined(CASPI_ARCH_WASM)
            wasm_v128_store (p, v);
#else
            p[0] = v.data[0];
            p[1] = v.data[1];
#endif
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Load 8 floats (AVX, unaligned).
         */
        inline float32x8 loadx8 (const float* p)
        {
            return _mm256_loadu_ps (p);
        }

        /**
         * @brief Store 8 floats (AVX, unaligned).
         */
        inline void storex8 (float* p, float32x8 v)
        {
            _mm256_storeu_ps (p, v);
        }

        /**
         * @brief Load 4 doubles (AVX, unaligned).
         */
        inline float64x4 loadx4 (const double* p)
        {
            return _mm256_loadu_pd (p);
        }

        /**
         * @brief Store 4 doubles (AVX, unaligned).
         */
        inline void storex4 (double* p, float64x4 v)
        {
            _mm256_storeu_pd (p, v);
        }
#endif // CASPI_HAS_AVX

        /*=====================================================================
          Alignment-aware load/store
        =====================================================================*/

        /**
         * @brief Load 4 floats (aligned, SSE/NEON/WASM), fallback uses unaligned.
         */
        inline float32x4 load_aligned (const float* p)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_load_ps (p);
#elif defined(CASPI_HAS_NEON)
            return vld1q_f32 (p); // NEON loads always aligned, user should align manually
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_load (p); // WASM assumes alignment handled
#else
            return load (p); // fallback
#endif
        }

        /**
         * @brief Store 4 floats (aligned, SSE/NEON/WASM), fallback uses unaligned.
         */
        inline void store_aligned (float* p, float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            _mm_store_ps (p, v);
#elif defined(CASPI_HAS_NEON)
            vst1q_f32 (p, v);
#elif defined(CASPI_ARCH_WASM)
            wasm_v128_store (p, v);
#else
            store (p, v);
#endif
        }

        /**
         * @brief Load 2 doubles (aligned, SSE2/NEON64/WASM), fallback uses unaligned.
         */
        inline float64x2 load_aligned (const double* p)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_load_pd (p);
#elif defined(CASPI_HAS_NEON64)
            return vld1q_f64 (p);
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_load (p);
#else
            return load (p);
#endif
        }

        /**
         * @brief Store 2 doubles (aligned, SSE2/NEON64/WASM), fallback uses unaligned.
         */
        inline void store_aligned (double* p, float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            _mm_store_pd (p, v);
#elif defined(CASPI_HAS_NEON64)
            vst1q_f64 (p, v);
#elif defined(CASPI_ARCH_WASM)
            wasm_v128_store (p, v);
#else
            store (p, v);
#endif
        }

        /*=====================================================================
      Construction helpers
    =====================================================================*/

        /**
         * @brief Construct float32x4 from 4 scalar values.
         */
        inline float32x4 make_float32x4 (float x0, float x1, float x2, float x3)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_set_ps (x3, x2, x1, x0); // Note: reversed order in _mm_set_ps
#elif defined(CASPI_HAS_NEON)
            const float data[4] = { x0, x1, x2, x3 };
            return vld1q_f32 (data);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_make (x0, x1, x2, x3);
#else
            float32x4 v;
            v.data[0] = x0;
            v.data[1] = x1;
            v.data[2] = x2;
            v.data[3] = x3;
            return v;
#endif
        }

        /**
         * @brief Construct float32x4 from array.
         */
        inline float32x4 make_float32x4_from_array (const float* arr)
        {
            return load (arr);
        }

        /**
         * @brief Construct float64x2 from 2 scalar values.
         */
        inline float64x2 make_float64x2 (double x0, double x1)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_set_pd (x1, x0); // Note: reversed order in _mm_set_pd
#elif defined(CASPI_HAS_NEON64)
            const double data[2] = { x0, x1 };
            return vld1q_f64 (data);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_make (x0, x1);
#else
            float64x2 v;
            v.data[0] = x0;
            v.data[1] = x1;
            return v;
#endif
        }

        /**
         * @brief Construct float64x2 from array.
         */
        inline float64x2 make_float64x2_from_array (const double* arr)
        {
            return load (arr);
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Construct float32x8 from 8 scalar values.
         */
        inline float32x8 make_float32x8 (float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7)
        {
            return _mm256_set_ps (x7, x6, x5, x4, x3, x2, x1, x0);
        }

        /**
         * @brief Construct float32x8 from array.
         */
        inline float32x8 make_float32x8_from_array (const float* arr)
        {
            return _mm256_loadu_ps (arr);
        }

        /**
         * @brief Construct float64x4 from 4 scalar values.
         */
        inline float64x4 make_float64x4 (double x0, double x1, double x2, double x3)
        {
            return _mm256_set_pd (x3, x2, x1, x0);
        }

        /**
         * @brief Construct float64x4 from array.
         */
        inline float64x4 make_float64x4_from_array (const double* arr)
        {
            return _mm256_loadu_pd (arr);
        }
#endif // CASPI_HAS_AVX

        /*=====================================================================
          Broadcast
        =====================================================================*/

        /**
         * @brief Broadcast a scalar float to all lanes.
         */
        inline float32x4 set1 (float x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_set1_ps (x);
#elif defined(CASPI_HAS_NEON)
            return vdupq_n_f32 (x);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_splat (x);
#else
            float32x4 v;
            for (int i = 0; i < 4; i++)
            {
                v.data[i] = x;
            }
            return v;
#endif
        }

        /**
         * @brief Broadcast a scalar double to all lanes.
         */
        inline float64x2 set1 (double x)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_set1_pd (x);
#elif defined(CASPI_HAS_NEON64)
            return vdupq_n_f64 (x);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_splat (x);
#else
            float64x2 v;
            v.data[0] = x;
            v.data[1] = x;
            return v;
#endif
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Broadcast scalar to all 8 lanes (AVX).
         */
        inline float32x8 set1x8 (float x)
        {
            return _mm256_set1_ps (x);
        }

        /**
         * @brief Broadcast scalar to all 4 lanes (AVX).
         */
        inline float64x4 set1x4 (double x)
        {
            return _mm256_set1_pd (x);
        }
#endif // CASPI_HAS_AVX

        /*=====================================================================
          Arithmetic
        =====================================================================*/

        /**
         * @brief Per-lane addition.
         */
        inline float32x4 add (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_add_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vaddq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_add (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = a.data[i] + b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane addition.
         */
        inline float64x2 add (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_add_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vaddq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_add (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] + b.data[0];
            r.data[1] = a.data[1] + b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane subtraction.
         */
        inline float32x4 sub (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_sub_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vsubq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_sub (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = a.data[i] - b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane subtraction.
         */
        inline float64x2 sub (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_sub_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vsubq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_sub (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] - b.data[0];
            r.data[1] = a.data[1] - b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane multiplication.
         */
        inline float32x4 mul (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_mul_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vmulq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_mul (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = a.data[i] * b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane multiplication.
         */
        inline float64x2 mul (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_mul_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vmulq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_mul (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] * b.data[0];
            r.data[1] = a.data[1] * b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane division.
         */
        inline float32x4 div (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_div_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vdivq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_div (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = a.data[i] / b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane division.
         */
        inline float64x2 div (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_div_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vdivq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_div (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] / b.data[0];
            r.data[1] = a.data[1] / b.data[1];
            return r;
#endif
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Per-lane addition (AVX, 8 floats).
         */
        inline float32x8 addx8 (float32x8 a, float32x8 b)
        {
            return _mm256_add_ps (a, b);
        }

        /**
         * @brief Per-lane subtraction (AVX, 8 floats).
         */
        inline float32x8 subx8 (float32x8 a, float32x8 b)
        {
            return _mm256_sub_ps (a, b);
        }

        /**
         * @brief Per-lane multiplication (AVX, 8 floats).
         */
        inline float32x8 mulx8 (float32x8 a, float32x8 b)
        {
            return _mm256_mul_ps (a, b);
        }

        /**
         * @brief Per-lane division (AVX, 8 floats).
         */
        inline float32x8 divx8 (float32x8 a, float32x8 b)
        {
            return _mm256_div_ps (a, b);
        }

        /**
         * @brief Per-lane addition (AVX, 4 doubles).
         */
        inline float64x4 addx4 (float64x4 a, float64x4 b)
        {
            return _mm256_add_pd (a, b);
        }

        /**
         * @brief Per-lane subtraction (AVX, 4 doubles).
         */
        inline float64x4 subx4 (float64x4 a, float64x4 b)
        {
            return _mm256_sub_pd (a, b);
        }

        /**
         * @brief Per-lane multiplication (AVX, 4 doubles).
         */
        inline float64x4 mulx4 (float64x4 a, float64x4 b)
        {
            return _mm256_mul_pd (a, b);
        }

        /**
         * @brief Per-lane division (AVX, 4 doubles).
         */
        inline float64x4 divx4 (float64x4 a, float64x4 b)
        {
            return _mm256_div_pd (a, b);
        }
#endif // CASPI_HAS_AVX

        /*=====================================================================
          FMA and multiply-add wrappers
        =====================================================================*/

#if defined(CASPI_HAS_FMA)
        /**
         * @brief Fused multiply-add: a * b + c.
         */
        inline float32x4 fma (float32x4 a, float32x4 b, float32x4 c)
        {
            return _mm_fmadd_ps (a, b, c);
        }

        /**
         * @brief Fused multiply-add: a * b + c.
         */
        inline float64x2 fma (float64x2 a, float64x2 b, float64x2 c)
        {
            return _mm_fmadd_pd (a, b, c);
        }
#endif

        /**
         * @brief Multiply-add: a * b + c.
         * Uses FMA instruction if available, falls back to separate mul+add otherwise.
         * @example Apply gain with DC offset: mul_add(samples, gain, offset)
         */
        inline float32x4 mul_add (float32x4 a, float32x4 b, float32x4 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_ps (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

        /**
         * @brief Multiply-add: a * b + c (float64x2).
         * Uses FMA instruction if available, falls back to separate mul+add otherwise.
         */
        inline float64x2 mul_add (float64x2 a, float64x2 b, float64x2 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

        /*=====================================================================
          Fast approximations
        =====================================================================*/

        /**
         * @brief Fast approximate reciprocal (1/x).
         * @note Less accurate than div(set1(1.f), x) but much faster (~4x on x86).
         *       Maximum relative error is about 1.5e-3. Use for performance-critical code
         *       where approximate results are acceptable (e.g., normalization, rough division).
         */
        inline float32x4 rcp (float32x4 x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_rcp_ps (x);
#elif defined(CASPI_HAS_NEON)
            // NEON has vrecpeq_f32 for estimate, but for consistency use division
            return vdivq_f32 (vdupq_n_f32 (1.0f), x);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_div (wasm_f32x4_splat (1.0f), x);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = 1.0f / x.data[i];
            return r;
#endif
        }

        /**
         * @brief Fast approximate reciprocal square root (1/√x).
         * @note Less accurate than div(set1(1.f), sqrt(x)) but much faster (~8x on x86).
         *       Maximum relative error is about 1.5e-3. Useful for vector normalization,
         *       distance calculations, and other geometric operations.
         */
        inline float32x4 rsqrt (float32x4 x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_rsqrt_ps (x);
#elif defined(CASPI_HAS_NEON)
            // NEON has vrsqrteq_f32 for estimate
            return vrsqrteq_f32 (x);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_div (wasm_f32x4_splat (1.0f), wasm_f32x4_sqrt (x));
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = 1.0f / std::sqrt (x.data[i]);
            return r;
#endif
        }

        /*=====================================================================
          Comparisons
        =====================================================================*/

        /**
         * @brief Per-lane equality comparison.
         * @return Mask with 0xFFFFFFFF for true lanes, 0x00000000 for false.
         */
        inline float32x4 cmp_eq (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_cmpeq_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vreinterpretq_f32_u32 (vceqq_f32 (a, b));
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_eq (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                uint32_t mask = (a.data[i] == b.data[i]) ? 0xFFFFFFFF : 0x00000000;
                std::memcpy (&r.data[i], &mask, sizeof (float));
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane less-than comparison.
         * @return Mask with 0xFFFFFFFF for true lanes, 0x00000000 for false.
         */
        inline float32x4 cmp_lt (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_cmplt_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vreinterpretq_f32_u32 (vcltq_f32 (a, b));
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_lt (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                uint32_t mask = (a.data[i] < b.data[i]) ? 0xFFFFFFFF : 0x00000000;
                std::memcpy (&r.data[i], &mask, sizeof (float));
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane equality comparison (float64x2).
         */
        inline float64x2 cmp_eq (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_cmpeq_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vreinterpretq_f64_u64 (vceqq_f64 (a, b));
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_eq (a, b);
#else
            float64x2 r;
            for (int i = 0; i < 2; i++)
            {
                uint64_t mask = (a.data[i] == b.data[i]) ? 0xFFFFFFFFFFFFFFFFULL : 0x0ULL;
                std::memcpy (&r.data[i], &mask, sizeof (double));
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane less-than comparison (float64x2).
         */
        inline float64x2 cmp_lt (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_cmplt_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vreinterpretq_f64_u64 (vcltq_f64 (a, b));
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_lt (a, b);
#else
            float64x2 r;
            for (int i = 0; i < 2; i++)
            {
                uint64_t mask = (a.data[i] < b.data[i]) ? 0xFFFFFFFFFFFFFFFFULL : 0x0ULL;
                std::memcpy (&r.data[i], &mask, sizeof (double));
            }
            return r;
#endif
        }

        /*=====================================================================
          Min/Max (per-lane, not horizontal)
        =====================================================================*/

        /**
         * @brief Per-lane minimum.
         */
        inline float32x4 min (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_min_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vminq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_min (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = (a.data[i] < b.data[i]) ? a.data[i] : b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane maximum.
         */
        inline float32x4 max (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_max_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vmaxq_f32 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f32x4_max (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
            {
                r.data[i] = (a.data[i] > b.data[i]) ? a.data[i] : b.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane minimum (float64x2).
         */
        inline float64x2 min (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_min_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vminq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_min (a, b);
#else
            float64x2 r;
            r.data[0] = (a.data[0] < b.data[0]) ? a.data[0] : b.data[0];
            r.data[1] = (a.data[1] < b.data[1]) ? a.data[1] : b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane maximum (float64x2).
         */
        inline float64x2 max (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_max_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vmaxq_f64 (a, b);
#elif defined(CASPI_ARCH_WASM)
            return wasm_f64x2_max (a, b);
#else
            float64x2 r;
            r.data[0] = (a.data[0] > b.data[0]) ? a.data[0] : b.data[0];
            r.data[1] = (a.data[1] > b.data[1]) ? a.data[1] : b.data[1];
            return r;
#endif
        }

        /*=====================================================================
          Horizontal reductions
        =====================================================================*/

        /**
         * @brief Sum all lanes of a float32x4 vector.
         */
        inline float hsum (float32x4 v)
        {
#if defined(CASPI_HAS_SSE3)
            __m128 t = _mm_hadd_ps (v, v);
            t        = _mm_hadd_ps (t, t);
            float out;
            _mm_store_ss (&out, t);
            return out;
#else
            float tmp[4];
            store (tmp, v);
            return tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }

        /**
         * @brief Sum all lanes of a float64x2 vector.
         */
        inline double hsum (float64x2 v)
        {
#if defined(CASPI_HAS_SSE3)
            __m128d t = _mm_hadd_pd (v, v);
            double out;
            _mm_store_sd (&out, t);
            return out;
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] + tmp[1];
#endif
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Horizontal sum of float32x8 (AVX).
         */
        inline float hsum (float32x8 v)
        {
            __m128 lo = _mm256_castps256_ps128 (v);
            __m128 hi = _mm256_extractf128_ps (v, 1);
            return hsum (add (lo, hi));
        }

        /**
         * @brief Horizontal sum of float64x4 (AVX).
         */
        inline double hsum (float64x4 v)
        {
            __m128d lo = _mm256_castpd256_pd128 (v);
            __m128d hi = _mm256_extractf128_pd (v, 1);
            return hsum (add (lo, hi));
        }
#endif

        /**
         * @brief Maximum of all lanes of a float32x4 vector.
         */
        inline float hmax (float32x4 v)
        {
#if defined(CASPI_HAS_SSE3)
            __m128 t1 = _mm_max_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_max_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            float out;
            _mm_store_ss (&out, t2);
            return out;
#else
            float tmp[4];
            store (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
            {
                if (tmp[i] > out)
                    out = tmp[i];
            }
            return out;
#endif
        }

        /**
         * @brief Maximum of all lanes of a float64x2 vector.
         */
        inline double hmax (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_max_sd (v, _mm_shuffle_pd (v, v, 1));
            double out;
            _mm_store_sd (&out, t);
            return out;
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] > tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /**
         * @brief Minimum of all lanes of a float32x4 vector.
         */
        inline float hmin (float32x4 v)
        {
#if defined(CASPI_HAS_SSE3)
            __m128 t1 = _mm_min_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_min_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            float out;
            _mm_store_ss (&out, t2);
            return out;
#else
            float tmp[4];
            store (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
            {
                if (tmp[i] < out)
                    out = tmp[i];
            }
            return out;
#endif
        }

        /**
         * @brief Minimum of all lanes of a float64x2 vector.
         */
        inline double hmin (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_min_sd (v, _mm_shuffle_pd (v, v, 1));
            double out;
            _mm_store_sd (&out, t);
            return out;
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] < tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /*---------------------------------
          Blend / Select
        ---------------------------------*/

        /**
         * @brief Select per-lane: mask ? b : a (float32x4).
         *
         * Each lane selects from @p b if the corresponding mask lane is non-zero,
         * otherwise from @p a.
         */
        inline float32x4 blend (float32x4 a, float32x4 b, float32x4 mask)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_or_ps (_mm_and_ps (mask, b),
                              _mm_andnot_ps (mask, a));
#elif defined(CASPI_HAS_NEON)
            return vbslq_f32 (vreinterpretq_u32_f32 (mask), b, a);
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_bitselect (b, a, mask);
#else
            float64x2 r;
            for (int i = 0; i < 2; ++i)
                r.data[i] = (mask.data[i] != 0.0) ? b.data[i] : a.data[i];
            return r;
#endif
        }

        /**
         * @brief Select per-lane: mask ? b : a (float64x2).
         *
         * Each lane selects from @p b if the corresponding mask lane is non-zero,
         * otherwise from @p a.
         */
        inline float64x2 blend (float64x2 a, float64x2 b, float64x2 mask)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_or_pd (_mm_and_pd (mask, b),
                              _mm_andnot_pd (mask, a));
#elif defined(CASPI_HAS_NEON64)
            return vbslq_f64 (vreinterpretq_u64_f64 (mask), b, a);
#elif defined(CASPI_ARCH_WASM)
            return wasm_v128_bitselect (b, a, mask);
#else
            float32x4 r;
            for (int i = 0; i < 2; ++i)
                r.data[i] = (mask.data[i] != 0.f) ? b.data[i] : a.data[i];
            return r;
#endif
        }

        /*=====================================================================
          Lane-wise math
        =====================================================================*/

        /**
         * @brief Negate all lanes of a float32x4 vector.
         */
        inline float32x4 negate (float32x4 a)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_sub_ps (_mm_setzero_ps(), a);
#elif defined(CASPI_HAS_NEON)
            return vnegq_f32 (a);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_neg (a);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = -a.data[i];
            return r;
#endif
        }

        /**
         * @brief Negate all lanes of a float64x2 vector.
         */
        inline float64x2 negate (float64x2 a)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_sub_pd (_mm_setzero_pd(), a);
#elif defined(CASPI_HAS_NEON64)
            return vnegq_f64 (a);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_neg (a);
#else
            float64x2 r;
            r.data[0] = -a.data[0];
            r.data[1] = -a.data[1];
            return r;
#endif
        }

        /**
         * @brief Absolute value of all lanes of a float32x4 vector.
         */
        inline float32x4 abs (float32x4 a)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_andnot_ps (_mm_set1_ps (-0.f), a);
#elif defined(CASPI_HAS_NEON)
            return vabsq_f32 (a);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = std::fabs (a.data[i]);
            return r;
#endif
        }

        /**
         * @brief Absolute value of all lanes of a float64x2 vector.
         */
        inline float64x2 abs (float64x2 a)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_andnot_pd (_mm_set1_pd (-0.0), a);
#elif defined(CASPI_HAS_NEON64)
            return vabsq_f64 (a);
#else
            float64x2 r;
            r.data[0] = std::fabs (a.data[0]);
            r.data[1] = std::fabs (a.data[1]);
            return r;
#endif
        }

        /**
         * @brief Square root of all lanes of a float32x4 vector.
         */
        inline float32x4 sqrt (float32x4 a)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_sqrt_ps (a);
#elif defined(CASPI_HAS_NEON)
            return vsqrtq_f32 (a);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = std::sqrt (a.data[i]);
            return r;
#endif
        }

        /**
         * @brief Square root of all lanes of a float64x2 vector.
         */
        inline float64x2 sqrt (float64x2 a)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_sqrt_pd (a);
#elif defined(CASPI_HAS_NEON64)
            return vsqrtq_f64 (a);
#else
            float64x2 r;
            r.data[0] = std::sqrt (a.data[0]);
            r.data[1] = std::sqrt (a.data[1]);
            return r;
#endif
        }

        /*=====================================================================
  Utility helpers
=====================================================================*/

        /**
         * @brief Vector with all zeros.
         */
        template <typename SIMDFloat>
        SIMDFloat zero()
        {
            return set1 (0.0);
        }

        /**
         * @brief Vector with all ones.
         */
        template <typename SIMDFloat>
        SIMDFloat ones()
        {
            return set1 (1.0);
        }

        /**
         * @brief Vector with all -1.
         */
        template <typename SIMDFloat>
        SIMDFloat neg_ones()
        {
            return set1 (-1.0);
        }

        /*=====================================================================
  Stack allocation helpers
=====================================================================*/

        // Example: aligned stack vector
#define CASPI_ALIGNED_FLOAT32x4(name) alignas (16) float name[4]
#define CASPI_ALIGNED_FLOAT64x2(name) alignas (16) double name[2]

    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_SIMD_H