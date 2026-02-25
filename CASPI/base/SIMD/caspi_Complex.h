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

* @file caspi_Complex.h
* @author CS Islay
* @brief Complex arithmetic primitives for SIMD vector operations.
*
* Provides shuffle and complex-multiply primitives for two-lane double vectors.
* These operations exploit the two-lane geometry specific to complex arithmetic
* and are essential for FFT, convolution, and other complex DSP operations.
*
* +-------------------+-------------+----------------------------------------+
* | Operation         | Input       | Output                                |
* +-------------------+-------------+----------------------------------------+
* | swap_lanes(v)     | [a,b]       | [b,a]                                 |
* | broadcast_lo(v)   | [a,b]       | [a,a]                                 |
* | broadcast_hi(v)   | [a,b]       | [b,b]                                 |
* | interleave_lo(a,b)| [a0,a1]     | [a0,b0]                               |
* | interleave_hi(a,b)| [b0,b1]     | [a1,b1]                               |
* | negate_imag(v)   | [re,im]     | [re,-im]                              |
* | complex_mul(a,b)  | a*b         | (ar*br-ai*bi) + i(ar*bi+ai*br)       |
* +-------------------+-------------+----------------------------------------+
*
* USAGE EXAMPLES
* @code
* // Complex number representation: lane0=real, lane1=imaginary
* float64x2 a = ...;  // [ar, ai]
* float64x2 b = ...;  // [br, bi]
*
* // Complex multiplication
* float64x2 result = complex_mul(a, b);
*
* // Conjugate (negate imaginary part)
* float64x2 conjugated = negate_imag(a);
*
* // Build twiddle factor [wr, wi] from scalars
* float64x2 twiddle = interleave_lo(set1<double>(wr), set1<double>(wi));
* @endcode
*
* COMPLEX MULTIPLY ALGORITHM
* Uses the 3-multiply form (Knuth TAOCP Vol.2 §4.6.4):
*   t1 = broadcast_lo(a) * b        = [ar*br, ar*bi]
*   t2 = broadcast_hi(a) * swap(b)  = [ai*bi, ai*br]
*   result[0] = t1[0] - t2[0] = ar*br - ai*bi
*   result[1] = t1[1] + t2[1] = ar*bi + ai*br
*
* Platform optimization:
*   - SSE3: _mm_addsub_pd(t1, t2) - both lanes in one instruction
*   - SSE2: shuffle blend sub[0] with add[1]
*   - Others: add(t1, negate_imag(t2))
*
* PLATFORM COVERAGE
* +-------------+--------------------------------------------------+
* | Operation   | Platform Implementation                        |
* +-------------+--------------------------------------------------+
* | swap_lanes  | SSE2: shuffle_pd, NEON64: vcombine, WASM    |
* | broadcast_lo| SSE2: unpacklo, NEON64: vdupq_laneq, WASM   |
* | broadcast_hi| SSE2: unpackhi, NEON64: vdupq_laneq, WASM   |
* | interleave_lo| SSE2: unpacklo, NEON64: vzip1q, WASM        |
* | interleave_hi| SSE2: unpackhi, NEON64: vzip2q, WASM        |
* | negate_imag | SSE2: xor mask, NEON64: veorq, WASM         |
* | complex_mul | SSE3: addsub, SSE2: blend, Others: add+neg   |
* +-------------+--------------------------------------------------+
*
 ************************************************************************/

#ifndef CASPI_SIMDCOMPLEX_H
#define CASPI_SIMDCOMPLEX_H

#include "caspi_Intrinsics.h"
#include "caspi_Operations.h"

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Swap lanes: [a,b] → [b,a]
         *
         * Used inside complex_mul to form the cross-multiply term.
         *
         * @param v         Input vector [a, b]
         * @return          Vector with lanes swapped [b, a]
         *
         * @code
         * float64x2 b = set1<double>(2.0);  // [2, 2]
         * float64x2 swapped = swap_lanes(b); // [2, 2]
         * @endcode
         */
        inline float64x2 swap_lanes (const float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_shuffle_pd (v, v, _MM_SHUFFLE2 (0, 1));
#elif defined(CASPI_HAS_NEON64)
            return vcombine_f64 (vget_high_f64 (v), vget_low_f64 (v));
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_i64x2_shuffle (v, v, 1, 0);
#else
            float64x2 r;
            r.data[0] = v.data[1];
            r.data[1] = v.data[0];
            return r;
#endif
        }

        /**
         * @brief Broadcast lower lane: [a,b] → [a,a]
         *
         * Used to replicate the real part for complex multiplication.
         *
         * @param v         Input vector [a, b]
         * @return          Vector with lower lane broadcast [a, a]
         *
         * @code
         * float64x2 a = set1<double>(3.0);  // [3, 3]
         * float64x2 broadcast = broadcast_lo(a); // [3, 3]
         * // broadcast_lo([ar,ai]) * [br,bi] = [ar*br, ar*bi]
         * @endcode
         */
        inline float64x2 broadcast_lo (const float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_unpacklo_pd (v, v);
#elif defined(CASPI_HAS_NEON64)
            return vdupq_laneq_f64 (v, 0);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_i64x2_shuffle (v, v, 0, 0);
#else
            float64x2 r;
            r.data[0] = v.data[0];
            r.data[1] = v.data[0];
            return r;
#endif
        }

        /**
         * @brief Broadcast upper lane: [a,b] → [b,b]
         *
         * Used to replicate the imaginary part for complex multiplication.
         *
         * @param v         Input vector [a, b]
         * @return          Vector with upper lane broadcast [b, b]
         *
         * @code
         * float64x2 a = set1<double>(3.0);  // [3, 3]
         * float64x2 broadcast = broadcast_hi(a); // [3, 3]
         * // broadcast_hi([ar,ai]) * swap([br,bi]) = [ai*bi, ai*br]
         * @endcode
         */
        inline float64x2 broadcast_hi (const float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_unpackhi_pd (v, v);
#elif defined(CASPI_HAS_NEON64)
            return vdupq_laneq_f64 (v, 1);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_i64x2_shuffle (v, v, 1, 1);
#else
            float64x2 r;
            r.data[0] = v.data[1];
            r.data[1] = v.data[1];
            return r;
#endif
        }

        /**
         * @brief Interleave lower lanes: [a0,a1],[b0,b1] → [a0,b0]
         *
         * Primary use: building twiddle registers from two scalars.
         *
         * @param a         First vector [a0, a1]
         * @param b         Second vector [b0, b1]
         * @return          Interleaved result [a0, b0]
         *
         * @code
         * // Create twiddle [wr, wi] without memcpy
         * float64x2 twiddle = interleave_lo(set1<double>(wr), set1<double>(wi));
         * // Result: [wr, wi]
         * @endcode
         */
        inline float64x2 interleave_lo (const float64x2 a, const float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_unpacklo_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vzip1q_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_i64x2_shuffle (a, b, 0, 2);
#else
            float64x2 r;
            r.data[0] = a.data[0];
            r.data[1] = b.data[0];
            return r;
#endif
        }

        /**
         * @brief Interleave upper lanes: [a0,a1],[b0,b1] → [a1,b1]
         *
         * @param a         First vector [a0, a1]
         * @param b         Second vector [b0, b1]
         * @return          Interleaved result [a1, b1]
         */
        inline float64x2 interleave_hi (const float64x2 a, const float64x2 b)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_unpackhi_pd (a, b);
#elif defined(CASPI_HAS_NEON64)
            return vzip2q_f64 (a, b);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_i64x2_shuffle (a, b, 1, 3);
#else
            float64x2 r;
            r.data[0] = a.data[1];
            r.data[1] = b.data[1];
            return r;
#endif
        }

        /**
         * @brief Negate imaginary component: [re,im] → [re,-im]
         *
         * Used for complex conjugate (branchless). Essential for IFFT when
         * converting forward twiddles to inverse twiddles.
         *
         * @param v         Complex vector [real, imaginary]
         * @return          Complex vector with negated imaginary [real, -imag]
         *
         * @code
         * // Forward FFT twiddle: [wr, wi]
         * // IFFT needs conjugate: [wr, -wi]
         * float64x2 twiddle_ifft = negate_imag(twiddle_forward);
         * @endcode
         */
        inline float64x2 negate_imag (const float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            const float64x2 sign_mask = _mm_set_pd (-0.0, 0.0);
            return _mm_xor_pd (v, sign_mask);
#elif defined(CASPI_HAS_NEON64)
            const uint64x2_t sign_mask = vcombine_u64 (
                vcreate_u64 (0ULL),
                vcreate_u64 (0x8000000000000000ULL));
            return vreinterpretq_f64_u64 (veorq_u64 (vreinterpretq_u64_f64 (v), sign_mask));
#elif defined(CASPI_HAS_WASM_SIMD)
            const v128_t sign_mask = wasm_i64x2_const (0LL, static_cast<int64_t> (0x8000000000000000ULL));
            return wasm_v128_xor (v, sign_mask);
#else
            float64x2 r;
            r.data[0] = v.data[0];
            r.data[1] = -v.data[1];
            return r;
#endif
        }

        /**
         * @brief Complex multiplication: (ar+i*ai) * (br+i*bi)
         *
         * Computes the complex product using the 3-multiply algorithm.
         * Each register holds one complex number: lane0=real, lane1=imaginary.
         *
         * @param a         First complex number [ar, ai]
         * @param b         Second complex number [br, bi]
         * @return          Complex product [(ar*br-ai*bi), (ar*bi+ai*br)]
         *
         * @code
         * // Complex multiplication: (3+2i) * (1+4i) = -5 + 14i
         * float64x2 a = interleave_lo(set1<double>(3.0), set1<double>(2.0)); // [3,2]
         * float64x2 b = interleave_lo(set1<double>(1.0), set1<double>(4.0)); // [1,4]
         * float64x2 product = complex_mul(a, b); // [-5, 14]
         * @endcode
         */
        inline float64x2 complex_mul (const float64x2 a, const float64x2 b)
        {
            // t1 = [ar*br, ar*bi]
            // t2 = [ai*bi, ai*br]
            // result = [t1[0]-t2[0], t1[1]+t2[1]]
            //        = [ar*br - ai*bi, ar*bi + ai*br]
            const float64x2 t1 = mul (broadcast_lo (a), b);
            const float64x2 t2 = mul (broadcast_hi (a), swap_lanes (b));

        #if defined(CASPI_HAS_SSE3)
            // addsub: result[i] = t1[i] + ((-1)^i * t2[i])
            // lane 0: t1[0] - t2[0]
            // lane 1: t1[1] + t2[1]
            return _mm_addsub_pd (t1, t2);

        #elif defined(CASPI_HAS_SSE2)
            // Replicate addsub with sub + add + shuffle.
            // s = t1 - t2 = [ar*br - ai*bi,  ar*bi - ai*br]  ← need lane 0 from here
            // d = t1 + t2 = [ar*br + ai*bi,  ar*bi + ai*br]  ← need lane 1 from here
            // _MM_SHUFFLE2(1, 0): dst[0] = s[0], dst[1] = d[1]
            const float64x2 s = sub (t1, t2);
            const float64x2 d = add (t1, t2);
            return _mm_shuffle_pd (s, d, _MM_SHUFFLE2 (1, 0));

        #elif defined(CASPI_HAS_NEON64)
            // vzip1q picks lane 0 from each: [s[0], d[0]] — wrong, need [s[0], d[1]]
            // Use vcombine: take low lane of s, high lane of d.
            // vget_low_f64(s)  = s[0] = ar*br - ai*bi  (a 64-bit scalar register)
            // vget_high_f64(d) = d[1] = ar*bi + ai*br  (a 64-bit scalar register)
            const float64x2 s = sub (t1, t2);
            const float64x2 d = add (t1, t2);
            return vcombine_f64 (vget_low_f64 (s), vget_high_f64 (d));

        #elif defined(CASPI_HAS_WASM_SIMD)
            // wasm_i64x2_shuffle(a, b, l0, l1):
            //   lane indices 0-1 refer to a, 2-3 refer to b.
            // We want [s[0], d[1]] → s=a, d=b → indices [0, 3].
            const float64x2 s = sub (t1, t2);
            const float64x2 d = add (t1, t2);
            return wasm_i64x2_shuffle (s, d, 0, 3);

        #else
            // Scalar fallback: direct lane arithmetic, no helper dependencies.
            double buf_t1[2], buf_t2[2], res[2];
            std::memcpy (buf_t1, &t1, sizeof (buf_t1));
            std::memcpy (buf_t2, &t2, sizeof (buf_t2));
            res[0] = buf_t1[0] - buf_t2[0];  // ar*br - ai*bi
            res[1] = buf_t1[1] + buf_t2[1];  // ar*bi + ai*br
            float64x2 r;
            std::memcpy (&r, res, sizeof (r));
            return r;
        #endif
        }
    } // namespace SIMD
} // namespace CASPI
#endif // CASPI_SIMDCOMPLEX_H
