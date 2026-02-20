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

* @file caspi_Operations.h
* @author CS Islay
* @brief Low-level SIMD arithmetic and math operations.
*
*
* OVERVIEW
*
* Provides fundamental SIMD operations including:
*   - Broadcast operations (set1)
*   - Arithmetic (add, sub, mul, div)
*   - Fused multiply-add (mul_add)
*   - Fast approximations (rcp, rsqrt)
*   - Comparisons (cmp_eq, cmp_lt)
*   - Min/Max operations
*   - Horizontal reductions (hsum, hmax, hmin)
*   - Blend/Select operations
*   - Math operations (abs, sqrt, negate)
*
* Each operation is implemented for:
*   - float32x4 (4-lane float)
*   - float64x2 (2-lane double)
*   - float32x8 (8-lane float, AVX only)
*   - float64x4 (4-lane double, AVX only)
*
*
* USAGE EXAMPLES
*
* @code
* // Basic arithmetic
* float32x4 a = set1<float>(1.0f);
* float32x4 b = set1<float>(2.0f);
* float32x4 c = add(a, b);    // [3,3,3,3]
* float32x4 d = mul(a, b);    // [2,2,2,2]
*
* // Fused multiply-add: a*b + c
* float32x4 result = mul_add(a, b, set1<float>(1.0f));
*
* // Comparisons and blending
* float32x4 mask = cmp_lt(a, b);  // [true,true,true,true]
* float32x4 selected = blend(set1<float>(0.0f), set1<float>(1.0f), mask);
*
* // Horizontal reductions
* float32x4 v = load_aligned(data);
* float total = hsum(v);  // Sum of all 4 lanes
* float max_val = hmax(v); // Maximum of all 4 lanes
* @endcode
*
*
* PERFORMANCE NOTES
*
* - mul_add() is preferred over separate mul() + add() when FMA is available
* - rcp() and rsqrt() provide ~4x and ~8x speedup respectively but have ~0.15% error
* - Horizontal operations (hsum, hmax) are slower than lane-wise operations
* - Use blend() instead of branches for conditional selection
*
 ************************************************************************/

#ifndef CASPI_SIMDOPERATIONS_H
#define CASPI_SIMDOPERATIONS_H

#include "caspi_Strategy.h"

#include <cstdint>

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Broadcast a scalar value to all lanes of a 128-bit vector.
         *
         * Creates a vector where all lanes contain the same value.
         * Used for constants, scaling factors, and initialization.
         *
         * @tparam T        Scalar type (float or double)
         * @param x         Value to broadcast
         * @return           Vector with all lanes set to x
         *
         * @code
         * float32x4 twos = set1<float>(2.0f);  // [2, 2, 2, 2]
         * float64x2 ones = set1<double>(1.0);  // [1, 1]
         * @endcode
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type set1 (T x);

        template <>
        inline float32x4 set1<float> (float x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_set1_ps (x);
#elif defined(CASPI_HAS_NEON)
            return vdupq_n_f32 (x);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_splat (x);
#else
            float32x4 v;
            for (int i = 0; i < 4; i++)
                v.data[i] = x;
            return v;
#endif
        }

        template <>
        inline float64x2 set1<double> (double x)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_set1_pd (x);
#elif defined(CASPI_HAS_NEON64)
            return vdupq_n_f64 (x);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_splat (x);
#else
            float64x2 v;
            v.data[0] = x;
            v.data[1] = x;
            return v;
#endif
        }


        /**
         * @brief Per-lane addition: result[i] = a[i] + b[i]
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane sums
         */
        inline float32x4 add (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_add_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vaddq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_add (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = a.data[i] + b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane addition: result[i] = a[i] + b[i] (double precision)
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane sums
         */
        inline float64x2 add (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_add_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vaddq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_add (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] + b.data[0];
            r.data[1] = a.data[1] + b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane subtraction: result[i] = a[i] - b[i]
         *
         * @param a         First input vector (minuend)
         * @param b         Second input vector (subtrahend)
         * @return          Vector with per-lane differences
         */
        inline float32x4 sub (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_sub_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vsubq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_sub (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = a.data[i] - b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane subtraction: result[i] = a[i] - b[i] (double precision)
         *
         * @param a         First input vector (minuend)
         * @param b         Second input vector (subtrahend)
         * @return          Vector with per-lane differences
         */
        inline float64x2 sub (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_sub_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vsubq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_sub (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] - b.data[0];
            r.data[1] = a.data[1] - b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane multiplication: result[i] = a[i] * b[i]
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane products
         */
        inline float32x4 mul (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_mul_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vmulq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_mul (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = a.data[i] * b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane multiplication: result[i] = a[i] * b[i] (double precision)
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane products
         */
        inline float64x2 mul (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_mul_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vmulq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_mul (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] * b.data[0];
            r.data[1] = a.data[1] * b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane division: result[i] = a[i] / b[i]
         *
         * @param a         Dividend vector
         * @param b         Divisor vector
         * @return          Vector with per-lane quotients
         *
         * @warning Division by zero produces platform-specific results (Inf or NaN).
         */
        inline float32x4 div (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_div_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vdivq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_div (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = a.data[i] / b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane division: result[i] = a[i] / b[i] (double precision)
         *
         * @param a         Dividend vector
         * @param b         Divisor vector
         * @return          Vector with per-lane quotients
         *
         * @warning Division by zero produces platform-specific results (Inf or NaN).
         */
        inline float64x2 div (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_div_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vdivq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_div (a, b);
#else
            float64x2 r;
            r.data[0] = a.data[0] / b.data[0];
            r.data[1] = a.data[1] / b.data[1];
            return r;
#endif
        }

        /**
         * @brief Fused multiply-add: result[i] = a[i] * b[i] + c[i]
         *
         * Computes the product of two vectors and adds a third vector in a single
         * operation. Uses FMA instruction when available for better performance
         * and accuracy (single rounding instead of two).
         *
         * @param a         First input vector (multiplicand)
         * @param b         Second input vector (multiplicand)
         * @param c         Vector to add
         * @return          Vector with fused multiply-add results
         *
         * @note When CASPI_HAS_FMA is defined, uses hardware FMA instruction
         *       which is typically faster and more accurate than separate mul+add.
         *
         * @code
         * float32x4 a = set1<float>(2.0f);
         * float32x4 b = set1<float>(3.0f);
         * float32x4 c = set1<float>(1.0f);
         * float32x4 result = mul_add(a, b, c);  // [7,7,7,7] = 2*3 + 1
         * @endcode
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
         * @brief Fused multiply-add: result[i] = a[i] * b[i] + c[i] (double precision)
         *
         * @param a         First input vector (multiplicand)
         * @param b         Second input vector (multiplicand)
         * @param c         Vector to add
         * @return          Vector with fused multiply-add results
         */
        inline float64x2 mul_add (float64x2 a, float64x2 b, float64x2 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }


        /**
         * @brief Fast approximate reciprocal (1/x).
         *
         * Provides approximately 4x speedup over division by 1.0.
         * Uses hardware reciprocal instruction where available.
         *
         * @param x         Input vector
         * @return          Approximate reciprocal vector
         *
         * @note Maximum relative error is approximately 0.15%.
         * @warning Not suitable for precision-critical paths.
         *
         * @code
         * float32x4 v = set1<float>(2.0f);
         * float32x4 reciprocal = rcp(v);  // ~[0.5, 0.5, 0.5, 0.5]
         * @endcode
         */
        inline float32x4 rcp (float32x4 x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_rcp_ps (x);
#elif defined(CASPI_HAS_NEON)
            return vdivq_f32 (vdupq_n_f32 (1.0f), x);
#elif defined(CASPI_HAS_WASM_SIMD)
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
         *
         * Provides approximately 8x speedup over computing sqrt explicitly.
         *
         * @param x         Input vector (must be positive)
         * @return          Approximate reciprocal square root vector
         *
         * @note Maximum relative error is approximately 0.15%.
         * @warning Results are undefined for x ≤ 0.
         *
         * @code
         * float32x4 v = set1<float>(4.0f);
         * float32x4 rsqrt_result = rsqrt(v);  // ~[0.5, 0.5, 0.5, 0.5] = 1/sqrt(4)
         * @endcode
         */
        inline float32x4 rsqrt (float32x4 x)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_rsqrt_ps (x);
#elif defined(CASPI_HAS_NEON)
            return vrsqrteq_f32 (x);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_div (wasm_f32x4_splat (1.0f), wasm_f32x4_sqrt (x));
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = 1.0f / std::sqrt (x.data[i]);
            return r;
#endif
        }

        /**
         * @brief Per-lane equality comparison.
         *
         * Returns a mask vector where each lane is all-ones if the corresponding
         * lanes of a and b are equal, or all-zeros otherwise.
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Mask vector (0xFFFFFFFF if equal, 0x00000000 if not)
         *
         * @note Use with blend() for branchless conditional selection.
         *
         * @code
         * float32x4 a = set1<float>(1.0f);
         * float32x4 b = set1<float>(1.0f);
         * float32x4 mask = cmp_eq(a, b);  // [true,true,true,true]
         * @endcode
         */
        inline float32x4 cmp_eq (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_cmpeq_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vreinterpretq_f32_u32 (vceqq_f32 (a, b));
#elif defined(CASPI_HAS_WASM_SIMD)
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
         *
         * Returns a mask vector where each lane is all-ones if the corresponding
         * lane of a is less than b, or all-zeros otherwise.
         *
         * @param a         First input vector (left side of comparison)
         * @param b         Second input vector (right side of comparison)
         * @return          Mask vector (0xFFFFFFFF if a < b, 0x00000000 otherwise)
         *
         * @code
         * float32x4 a = set1<float>(1.0f);
         * float32x4 b = set1<float>(2.0f);
         * float32x4 mask = cmp_lt(a, b);  // [true,true,true,true]
         * @endcode
         */
        inline float32x4 cmp_lt (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_cmplt_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vreinterpretq_f32_u32 (vcltq_f32 (a, b));
#elif defined(CASPI_HAS_WASM_SIMD)
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
         * @brief Per-lane equality comparison (double precision).
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Mask vector
         */
        inline float64x2 cmp_eq (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_cmpeq_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vreinterpretq_f64_u64 (vceqq_f64 (a, b));
#elif defined(CASPI_HAS_WASM_SIMD)
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
         * @brief Per-lane less-than comparison (double precision).
         *
         * @param a         First input vector (left side of comparison)
         * @param b         Second input vector (right side of comparison)
         * @return          Mask vector
         */
        inline float64x2 cmp_lt (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_cmplt_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vreinterpretq_f64_u64 (vcltq_f64 (a, b));
#elif defined(CASPI_HAS_WASM_SIMD)
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

        /**
         * @brief Per-lane minimum: result[i] = min(a[i], b[i])
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane minima
         */
        inline float32x4 min (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_min_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vminq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_min (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = (a.data[i] < b.data[i]) ? a.data[i] : b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane maximum: result[i] = max(a[i], b[i])
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane maxima
         */
        inline float32x4 max (float32x4 a, float32x4 b)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_max_ps (a, b);
#elif defined(CASPI_HAS_NEON)
            return vmaxq_f32 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f32x4_max (a, b);
#else
            float32x4 r;
            for (int i = 0; i < 4; i++)
                r.data[i] = (a.data[i] > b.data[i]) ? a.data[i] : b.data[i];
            return r;
#endif
        }

        /**
         * @brief Per-lane minimum: result[i] = min(a[i], b[i]) (double precision)
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane minima
         */
        inline float64x2 min (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_min_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vminq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_min (a, b);
#else
            float64x2 r;
            r.data[0] = (a.data[0] < b.data[0]) ? a.data[0] : b.data[0];
            r.data[1] = (a.data[1] < b.data[1]) ? a.data[1] : b.data[1];
            return r;
#endif
        }

        /**
         * @brief Per-lane maximum: result[i] = max(a[i], b[i]) (double precision)
         *
         * @param a         First input vector
         * @param b         Second input vector
         * @return          Vector with per-lane maxima
         */
        inline float64x2 max (float64x2 a, float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_max_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vmaxq_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_f64x2_max (a, b);
#else
            float64x2 r;
            r.data[0] = (a.data[0] > b.data[0]) ? a.data[0] : b.data[0];
            r.data[1] = (a.data[1] > b.data[1]) ? a.data[1] : b.data[1];
            return r;
#endif
        }


        /**
         * @brief Sum all lanes of a float32x4 vector.
         *
         * Reduces a 4-lane vector to a scalar by adding all lanes together.
         * Uses an efficient shuffle-based approach to avoid the slow hadd instruction.
         *
         * @param v         Input vector
         * @return          Sum of all four lanes
         *
         * @code
         * float32x4 v = load_aligned(data);
         * float sum = hsum(v);  // v[0] + v[1] + v[2] + v[3]
         * @endcode
         */
        inline float hsum (float32x4 v)
        {
#if defined(CASPI_HAS_SSE3)
            __m128 shuf = _mm_movehdup_ps (v);
            __m128 sums = _mm_add_ps (v, shuf);
            shuf        = _mm_movehl_ps (shuf, sums);
            sums        = _mm_add_ss (sums, shuf);
            return _mm_cvtss_f32 (sums);
#elif defined(CASPI_HAS_SSE)
            __m128 shuf = _mm_shuffle_ps (v, v, _MM_SHUFFLE (2, 3, 0, 1));
            __m128 sums = _mm_add_ps (v, shuf);
            shuf        = _mm_movehl_ps (shuf, sums);
            sums        = _mm_add_ss (sums, shuf);
            return _mm_cvtss_f32 (sums);
#else
            float tmp[4];
            store (tmp, v);
            return tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }

        /**
         * @brief Sum all lanes of a float64x2 vector.
         *
         * @param v         Input vector
         * @return          Sum of both lanes
         */
        inline double hsum (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d shuf = _mm_shuffle_pd (v, v, 1);
            __m128d sums = _mm_add_sd (v, shuf);
            return _mm_cvtsd_f64 (sums);
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] + tmp[1];
#endif
        }


        /**
         * @brief Maximum of all lanes in a float32x4 vector.
         *
         * @param v         Input vector
         * @return          Maximum value across all four lanes
         */
        inline float hmax (float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            __m128 t1 = _mm_max_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_max_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            return _mm_cvtss_f32 (t2);
#else
            float tmp[4];
            store (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
                if (tmp[i] > out)
                    out = tmp[i];
            return out;
#endif
        }

        /**
         * @brief Maximum of all lanes in a float64x2 vector.
         *
         * @param v         Input vector
         * @return          Maximum value across both lanes
         */
        inline double hmax (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_max_sd (v, _mm_shuffle_pd (v, v, 1));
            return _mm_cvtsd_f64 (t);
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] > tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /**
         * @brief Minimum of all lanes in a float32x4 vector.
         *
         * @param v         Input vector
         * @return          Minimum value across all four lanes
         */
        inline float hmin (float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            __m128 t1 = _mm_min_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_min_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            return _mm_cvtss_f32 (t2);
#else
            float tmp[4];
            store (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
                if (tmp[i] < out)
                    out = tmp[i];
            return out;
#endif
        }

        /**
         * @brief Minimum of all lanes in a float64x2 vector.
         *
         * @param v         Input vector
         * @return          Minimum value across both lanes
         */
        inline double hmin (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_min_sd (v, _mm_shuffle_pd (v, v, 1));
            return _mm_cvtsd_f64 (t);
#else
            double tmp[2];
            store (tmp, v);
            return tmp[0] < tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /**
         * @brief Conditional per-lane select (blend).
         *
         * For each lane i: result[i] = mask[i] ? b[i] : a[i].
         * The mask must come from a comparison operation (all-zeros or all-ones per lane).
         *
         * @param a         Default value vector (used when mask lane is zero)
         * @param b         Selected value vector (used when mask lane is all-ones)
         * @param mask      Selection mask from comparison
         * @return          Blended result vector
         *
         * @note Uses and/andnot rather than SSE4.1 blendv for broader compatibility.
         *
         * @code
         * float32x4 a = set1<float>(0.0f);
         * float32x4 b = set1<float>(1.0f);
         * float32x4 mask = cmp_lt(a, b);  // All ones
         * float32x4 result = blend(a, b, mask);  // [1,1,1,1]
         * @endcode
         */
        inline float32x4 blend (float32x4 a, float32x4 b, float32x4 mask)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_or_ps (_mm_and_ps (mask, b), _mm_andnot_ps (mask, a));
#elif defined(CASPI_HAS_NEON)
            return vbslq_f32 (vreinterpretq_u32_f32 (mask), b, a);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_bitselect (b, a, mask);
#else
            float32x4 r;
            for (int i = 0; i < 4; ++i)
            {
                uint32_t m;
                std::memcpy (&m, &mask.data[i], sizeof (m));
                r.data[i] = (m != 0) ? b.data[i] : a.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Conditional per-lane select (blend) for double precision.
         *
         * @param a         Default value vector (used when mask lane is zero)
         * @param b         Selected value vector (used when mask lane is all-ones)
         * @param mask      Selection mask from comparison
         * @return          Blended result vector
         */
        inline float64x2 blend (float64x2 a, float64x2 b, float64x2 mask)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_or_pd (_mm_and_pd (mask, b), _mm_andnot_pd (mask, a));
#elif defined(CASPI_HAS_NEON64)
            return vbslq_f64 (vreinterpretq_u64_f64 (mask), b, a);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_bitselect (b, a, mask);
#else
            float64x2 r;
            for (int i = 0; i < 2; ++i)
            {
                uint64_t m;
                std::memcpy (&m, &mask.data[i], sizeof (m));
                r.data[i] = (m != 0) ? b.data[i] : a.data[i];
            }
            return r;
#endif
        }

        /**
         * @brief Per-lane negation: result[i] = -a[i]
         *
         * @param a         Input vector
         * @return          Vector with all lanes negated
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
         * @brief Per-lane negation: result[i] = -a[i] (double precision)
         *
         * @param a         Input vector
         * @return          Vector with all lanes negated
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
         * @brief Per-lane absolute value: result[i] = |a[i]|
         *
         * @param a         Input vector
         * @return          Vector with absolute values
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
         * @brief Per-lane absolute value: result[i] = |a[i]| (double precision)
         *
         * @param a         Input vector
         * @return          Vector with absolute values
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
         * @brief Per-lane square root: result[i] = √a[i]
         *
         * @param a         Input vector (all elements must be non-negative)
         * @return          Vector with square roots
         *
         * @warning Results are undefined for negative inputs.
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
         * @brief Per-lane square root: result[i] = √a[i] (double precision)
         *
         * @param a         Input vector (all elements must be non-negative)
         * @return          Vector with square roots
         *
         * @warning Results are undefined for negative inputs.
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
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_SIMDOPERATIONS_H
