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
* @brief Cross-platform SIMD wrapper with kernel-based block operations
*
* ============================================================================
* ARCHITECTURE OVERVIEW
* ============================================================================
*
* This header provides a unified SIMD API that works across:
* - x86/x64: SSE, SSE2, AVX, FMA
* - ARM: NEON, NEON64
* - WebAssembly: WASM SIMD
* - Fallback: Scalar implementation
*
* The API is organized into three layers:
*
* 1. LOW-LEVEL PRIMITIVES (load, store, add, mul, etc.)
*    - Direct SIMD operations on vector types
*    - Platform-specific but with unified interface
*    - Example: add(v1, v2), mul(v1, v2)
*
* 2. KERNELS (AddKernel, ScaleKernel, etc.)
*    - Encapsulate operations with both SIMD and scalar implementations
*    - Automatically dispatch to appropriate code path
*    - Used internally by block operations
*
* 3. HIGH-LEVEL BLOCK OPERATIONS (ops::add, ops::scale, etc.)
*    - Process arrays with automatic prologue/SIMD/epilogue handling
*    - Handle alignment optimization
*    - Example: ops::add(dst, src, 1000)
*
* There is also a "Strategy" later - compile-time magic to determine optimal processing.
* ============================================================================
* QUICK START
* ============================================================================
*
* Processing Arrays:
* @code
* float audio[512];
*
* // Scale by constant
* SIMD::ops::scale(audio, 512, 0.5f);
*
* // Add two arrays
* float input[512], output[512];
* SIMD::ops::add(output, input, 512);
*
* // Clamp to range
* SIMD::ops::clamp(audio, -1.0f, 1.0f, 512);
*
* // Find maximum value
* float peak = SIMD::ops::find_max(audio, 512);
*
* // Compute dot product
* float energy = SIMD::ops::dot_product(audio, audio, 512);
* @endcode
*
* Low-Level SIMD:
* @code
* float32x4 a = set1<float>(2.0f);           // Broadcast
* float32x4 b = load<float>(data);           // Load from memory
* float32x4 c = mul_add(a, b, set1<float>(1.0f)); // a*b+1
* store<float>(output, c);                   // Store to memory
* @endcode
*
* ============================================================================
* OPERATIONS REFERENCE
* ============================================================================
*
* Block Operations (ops namespace):
* - add(dst, src, count)         - dst[i] += src[i]
* - sub(dst, src, count)         - dst[i] -= src[i]
* - mul(dst, src, count)         - dst[i] *= src[i]
* - scale(data, count, factor)   - data[i] *= factor
* - copy(dst, src, count)        - dst[i] = src[i]
* - fill(dst, count, value)      - dst[i] = value
* - mac(dst, s1, s2, count)      - dst[i] += s1[i] * s2[i]
* - lerp(dst, a, b, t, count)    - dst[i] = a[i] + t*(b[i]-a[i])
* - clamp(data, min, max, count) - Clamp to [min, max]
* - abs(data, count)             - Absolute value
*
* Reductions:
* - find_min(data, count)        - Minimum element
* - find_max(data, count)        - Maximum element
* - sum(data, count)             - Sum all elements
* - dot_product(a, b, count)     - Dot product
*
* SIMD Primitives:
* - load<T>(ptr)                 - Load vector (auto-detects alignment)
* - store<T>(ptr, vec)           - Store vector
* - set1<T>(value)               - Broadcast scalar
* - add(a, b), sub(a, b)         - Arithmetic
* - mul(a, b), div(a, b)
* - mul_add(a, b, c)             - Fused multiply-add (uses FMA if available)
* - min(a, b), max(a, b)         - Per-lane min/max
* - cmp_eq(a, b), cmp_lt(a, b)   - Comparisons (return masks)
* - blend(a, b, mask)            - Conditional select
* - abs(v), sqrt(v), negate(v)   - Math operations
* - rcp(v), rsqrt(v)             - Fast approximations
*
* Horizontal Operations:
* - hsum(v)                      - Sum all lanes
* - hmax(v), hmin(v)             - Max/min of all lanes
*
* ============================================================================
* ALIGNMENT NOTES
* ============================================================================
*
* - SSE/NEON prefer 16-byte alignment, AVX prefers 32-byte
* - Aligned operations are faster but require aligned pointers
* - load<T>()/store<T>() automatically choose aligned/unaligned
* - Block operations (ops::*) handle alignment internally:
*   1. Scalar prologue until aligned
*   2. SIMD main loop (aligned if possible)
*   3. Scalar epilogue for remainder
*
* For best performance:
* @code
* alignas(16) float buffer[512];  // SSE/NEON
* alignas(32) float buffer[512];  // AVX
* @endcode
*
* ============================================================================
* PERFORMANCE TIPS
* ============================================================================
*
* 1. Use block operations (ops::*) for array processing
*    - Automatic prologue/epilogue handling
*    - Alignment optimization
*    - Branch-free SIMD loops
*
* 2. Prefer mul_add() over separate mul() + add()
*    - Automatically uses FMA instruction when available
*    - ~2x faster on modern CPUs
*
* 3. Use fast approximations when accuracy allows:
*    - rcp(x) is ~4x faster than div(set1(1.f), x)
*    - rsqrt(x) is ~8x faster than div(set1(1.f), sqrt(x))
*    - Error is ~0.15%, acceptable for many DSP tasks
*
* 4. Horizontal operations (hsum, hmax) are slower than lane-wise
*    - Use sparingly
*    - Accumulate in SIMD registers when possible
*
* 5. Avoid branches in SIMD code
*    - Use blend() instead of if/else
*    - Use min()/max() instead of conditional assignment
*
* ============================================================================
* PLATFORM DETECTION
* ============================================================================
*
* The library automatically detects available SIMD features via:
* - CASPI_HAS_SSE, CASPI_HAS_SSE2, CASPI_HAS_AVX
* - CASPI_HAS_FMA
* - CASPI_HAS_NEON, CASPI_HAS_NEON64
* - CASPI_HAS_WASM_SIMD
*
* These are defined in caspi_Features.h based on compiler flags.
*
* Runtime check:
* @code
* if (SIMD::HAS_SIMD) {
*     // SIMD available
* }
* @endcode
*
************************************************************************/

#include "caspi_Assert.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "caspi_Features.h"
#include "caspi_Platform.h"

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
        /**
         * @brief Runtime SIMD availability flag
         *
         * Set to true if hardware SIMD is available, false for scalar fallback.
         * Use to gate SIMD-specific code paths if needed.
         */
#if defined(CASPI_HAS_SSE) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_WASM_SIMD)
        constexpr bool HAS_SIMD = true;
#else
        constexpr bool HAS_SIMD = false;
#endif

        /************************************************************************************************
          SIMD Vector Types
        ************************************************************************************************/

        /** @brief 4-lane float32 vector (128-bit) */
#if defined(CASPI_HAS_SSE)
        using float32x4 = __m128;
#elif defined(CASPI_HAS_NEON)
        using float32x4 = float32x4_t;
#elif defined(CASPI_HAS_WASM_SIMD)
        using float32x4 = v128_t;
#else
        struct float32x4
        {
                float data[4];
        };
#endif

        /** @brief 2-lane float64 vector (128-bit) */
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
        /** @brief 8-lane float32 vector (256-bit, AVX only) */
        using float32x8 = __m256;

        /** @brief 4-lane float64 vector (256-bit, AVX only) */
        using float64x4 = __m256d;
#endif

        /************************************************************************************************
          Strategy - Compile-Time SIMD Traits
        ************************************************************************************************/

        namespace Strategy
        {
            /**
             * @brief Maps scalar type and width to SIMD vector type
             * @tparam T Scalar type (float or double)
             * @tparam Width Number of lanes
             */
            template <typename T, std::size_t Width>
            struct simd_type;

            template <>
            struct simd_type<float, 4>
            {
                    using type = float32x4;
            };

            template <>
            struct simd_type<double, 2>
            {
                    using type = float64x2;
            };

#if defined(CASPI_HAS_AVX)
            template <>
            struct simd_type<float, 8>
            {
                    using type = float32x8;
            };

            template <>
            struct simd_type<double, 4>
            {
                    using type = float64x4;
            };
#endif

            /**
             * @brief Minimum SIMD width for a given scalar type
             * @tparam T Scalar type (float or double)
             */
            template <typename T>
            struct min_simd_width
            {
                    static constexpr std::size_t value = 1; ///< Scalar fallback
            };

            template <>
            struct min_simd_width<float>
            {
#if defined(CASPI_HAS_SSE) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_WASM_SIMD)
                    static constexpr std::size_t value = 4;
#else
                    static constexpr std::size_t value = 1;
#endif
            };

            template <>
            struct min_simd_width<double>
            {
#if defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_NEON64) || defined(CASPI_HAS_WASM_SIMD)
                    static constexpr std::size_t value = 2;
#else
                    static constexpr std::size_t value = 1;
#endif
            };

            /**
             * @brief Maximum SIMD width for a given scalar type
             * @tparam T Scalar type (float or double)
             *
             * Returns the widest vector available on current platform.
             * Use for selecting optimal loop unrolling.
             */
            template <typename T>
            struct max_simd_width
            {
                    static constexpr std::size_t value = min_simd_width<T>::value;
            };

#if defined(CASPI_HAS_AVX)
            template <>
            struct max_simd_width<float>
            {
                    static constexpr std::size_t value = 8;
            };

            template <>
            struct max_simd_width<double>
            {
                    static constexpr std::size_t value = 4;
            };
#endif

            /**
             * @brief Check if pointer is aligned to N bytes
             * @tparam N Alignment boundary (must be power of 2)
             * @param ptr Pointer to check
             * @return true if aligned, false otherwise
             */
            template <std::size_t N>
            inline bool is_aligned (const void* ptr) noexcept
            {
                return (reinterpret_cast<std::uintptr_t> (ptr) % N) == 0;
            }

            /**
             * @brief Calculate number of elements until next alignment boundary
             * @tparam Alignment Desired alignment in bytes
             * @tparam T Element type
             * @param ptr Pointer to check
             * @return Number of elements to skip to reach alignment
             *
             * Used internally for prologue calculations in block operations.
             */
            template <std::size_t Alignment, typename T>
            inline std::size_t samples_to_alignment (const T* ptr) noexcept
            {
                const auto addr                   = reinterpret_cast<std::uintptr_t> (ptr);
                const std::uintptr_t misalignment = addr % Alignment;

                if (misalignment == 0)
                    return 0;

                const std::uintptr_t bytes_to_align = Alignment - misalignment;
                return bytes_to_align / sizeof (T);
            }

            /**
             * @brief Get preferred SIMD alignment for a type
             * @tparam T Scalar type
             * @return Alignment in bytes (16 for SSE/NEON, 32 for AVX, natural for scalar)
             */
            template <typename T>
            constexpr std::size_t simd_alignment()
            {
#if defined(CASPI_HAS_AVX)
                return 32;
#elif defined(CASPI_HAS_SSE) || defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_NEON64) || defined(CASPI_HAS_WASM_SIMD)
                return 16;
#else
                return alignof (T);
#endif
            }
        } // namespace Strategy

        /************************************************************************************************
          Load/Store Operations
        ************************************************************************************************/

        /**
         * @brief Load vector from aligned memory
         * @tparam T Scalar type (float or double)
         * @param p Pointer to aligned memory (must be 16/32-byte aligned)
         * @return Loaded SIMD vector
         *
         * @warning Undefined behavior if pointer is not properly aligned
         * @note Prefer load<T>() which auto-detects alignment
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load_aligned (const T* p);

        template <>
        inline float32x4 load_aligned<float> (const float* p)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_load_ps (p);
#elif defined(CASPI_HAS_NEON)
            return vld1q_f32 (p);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_load (p);
#else
            float32x4 v;
            for (int i = 0; i < 4; i++)
                v.data[i] = p[i];
            return v;
#endif
        }

        template <>
        inline float64x2 load_aligned<double> (const double* p)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_load_pd (p);
#elif defined(CASPI_HAS_NEON64)
            return vld1q_f64 (p);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_load (p);
#else
            float64x2 v;
            v.data[0] = p[0];
            v.data[1] = p[1];
            return v;
#endif
        }

        /**
         * @brief Load vector from unaligned memory
         * @tparam T Scalar type (float or double)
         * @param p Pointer to memory (no alignment requirement)
         * @return Loaded SIMD vector
         *
         * @note Slower than load_aligned<T>() but works with any pointer
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load_unaligned (const T* p);

        template <>
        inline float32x4 load_unaligned<float> (const float* p)
        {
#if defined(CASPI_HAS_SSE)
            return _mm_loadu_ps (p);
#elif defined(CASPI_HAS_NEON)
            return vld1q_f32 (p);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_load (p);
#else
            float32x4 v;
            for (int i = 0; i < 4; i++)
                v.data[i] = p[i];
            return v;
#endif
        }

        template <>
        inline float64x2 load_unaligned<double> (const double* p)
        {
#if defined(CASPI_HAS_SSE2)
            return _mm_loadu_pd (p);
#elif defined(CASPI_HAS_NEON64)
            return vld1q_f64 (p);
#elif defined(CASPI_HAS_WASM_SIMD)
            return wasm_v128_load (p);
#else
            float64x2 v;
            v.data[0] = p[0];
            v.data[1] = p[1];
            return v;
#endif
        }

        /**
         * @brief Load vector with automatic alignment detection
         * @tparam T Scalar type (float or double)
         * @param p Pointer to memory
         * @return Loaded SIMD vector
         *
         * Automatically chooses aligned or unaligned load based on pointer alignment.
         * Alignment check is performed once, not per iteration.
         *
         * @note Preferred method for most use cases
         */
        template <typename T>
        inline typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load (const T* p)
        {
            constexpr std::size_t alignment = Strategy::simd_alignment<T>();
            if (Strategy::is_aligned<alignment> (p))
            {
                return load_aligned<T> (p);
            }
            return load_unaligned<T> (p);
        }

        /**
         * @brief Store vector to aligned memory
         * @tparam T Scalar type (float or double)
         * @tparam VecT Vector type (auto-deduced)
         * @param p Pointer to aligned memory (must be 16/32-byte aligned)
         * @param v Vector to store
         *
         * @warning Undefined behavior if pointer is not properly aligned
         */
        template <typename T, typename VecT>
        void store_aligned (T* p, VecT v);

        template <>
        inline void store_aligned<float, float32x4> (float* p, float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            _mm_store_ps (p, v);
#elif defined(CASPI_HAS_NEON)
            vst1q_f32 (p, v);
#elif defined(CASPI_HAS_WASM_SIMD)
            wasm_v128_store (p, v);
#else
            for (int i = 0; i < 4; i++)
                p[i] = v.data[i];
#endif
        }

        template <>
        inline void store_aligned<double, float64x2> (double* p, float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            _mm_store_pd (p, v);
#elif defined(CASPI_HAS_NEON64)
            vst1q_f64 (p, v);
#elif defined(CASPI_HAS_WASM_SIMD)
            wasm_v128_store (p, v);
#else
            p[0] = v.data[0];
            p[1] = v.data[1];
#endif
        }

        /**
         * @brief Store vector to unaligned memory
         * @tparam T Scalar type (float or double)
         * @tparam VecT Vector type (auto-deduced)
         * @param p Pointer to memory (no alignment requirement)
         * @param v Vector to store
         */
        template <typename T, typename VecT>
        void store_unaligned (T* p, VecT v);

        template <>
        inline void store_unaligned<float, float32x4> (float* p, float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            _mm_storeu_ps (p, v);
#elif defined(CASPI_HAS_NEON)
            vst1q_f32 (p, v);
#elif defined(CASPI_HAS_WASM_SIMD)
            wasm_v128_store (p, v);
#else
            for (int i = 0; i < 4; i++)
                p[i] = v.data[i];
#endif
        }

        template <>
        inline void store_unaligned<double, float64x2> (double* p, float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            _mm_storeu_pd (p, v);
#elif defined(CASPI_HAS_NEON64)
            vst1q_f64 (p, v);
#elif defined(CASPI_HAS_WASM_SIMD)
            wasm_v128_store (p, v);
#else
            p[0] = v.data[0];
            p[1] = v.data[1];
#endif
        }

        /**
         * @brief Store vector with automatic alignment detection
         * @tparam T Scalar type (float or double)
         * @tparam VecT Vector type (auto-deduced)
         * @param p Pointer to memory
         * @param v Vector to store
         *
         * Automatically chooses aligned or unaligned store based on pointer alignment.
         */
        template <typename T, typename VecT>
        void store (T* p, VecT v)
        {
            constexpr std::size_t alignment = Strategy::simd_alignment<T>();
            if (Strategy::is_aligned<alignment> (p))
            {
                store_aligned<T, VecT> (p, v);
            }
            else
            {
                store_unaligned<T, VecT> (p, v);
            }
        }

#if defined(CASPI_HAS_AVX)
        // AVX 256-bit load/store specializations

        template <>
        inline float32x8 load_aligned<float> (const float* p)
        {
            return _mm256_load_ps (p);
        }

        template <>
        inline float32x8 load_unaligned<float> (const float* p)
        {
            return _mm256_loadu_ps (p);
        }

        template <>
        inline float64x4 load_aligned<double> (const double* p)
        {
            return _mm256_load_pd (p);
        }

        template <>
        inline float64x4 load_unaligned<double> (const double* p)
        {
            return _mm256_loadu_pd (p);
        }

        template <>
        inline void store_aligned<float, float32x8> (float* p, float32x8 v)
        {
            _mm256_store_ps (p, v);
        }

        template <>
        inline void store_unaligned<float, float32x8> (float* p, float32x8 v)
        {
            _mm256_storeu_ps (p, v);
        }

        template <>
        inline void store_aligned<double, float64x4> (double* p, float64x4 v)
        {
            _mm256_store_pd (p, v);
        }

        template <>
        inline void store_unaligned<double, float64x4> (double* p, float64x4 v)
        {
            _mm256_storeu_pd (p, v);
        }
#endif // CASPI_HAS_AVX

        /************************************************************************************************
          Broadcast Operations
        ************************************************************************************************/

        /**
         * @brief Broadcast scalar to all vector lanes
         * @tparam T Scalar type (float or double)
         * @param x Scalar value to broadcast
         * @return Vector with all lanes set to x
         *
         * @code
         * float32x4 twos = set1<float>(2.0f);  // [2.0, 2.0, 2.0, 2.0]
         * @endcode
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            set1 (T x);

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

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Broadcast scalar to 8-lane float vector (AVX)
         * @param x Scalar value
         * @return 8-lane vector with all lanes set to x
         */
        inline float32x8 set1x8 (float x)
        {
            return _mm256_set1_ps (x);
        }

        /**
         * @brief Broadcast scalar to 4-lane double vector (AVX)
         * @param x Scalar value
         * @return 4-lane vector with all lanes set to x
         */
        inline float64x4 set1x4 (double x)
        {
            return _mm256_set1_pd (x);
        }
#endif

        /************************************************************************************************
          Arithmetic Operations
        ************************************************************************************************/

        /**
         * @brief Per-lane addition
         * @param a First vector
         * @param b Second vector
         * @return Result vector where result[i] = a[i] + b[i]
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

        /** @brief Per-lane addition (double precision) */
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
         * @brief Per-lane subtraction
         * @param a First vector
         * @param b Second vector
         * @return Result vector where result[i] = a[i] - b[i]
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

        /** @brief Per-lane subtraction (double precision) */
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
         * @brief Per-lane multiplication
         * @param a First vector
         * @param b Second vector
         * @return Result vector where result[i] = a[i] * b[i]
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

        /** @brief Per-lane multiplication (double precision) */
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
         * @brief Per-lane division
         * @param a Numerator vector
         * @param b Denominator vector
         * @return Result vector where result[i] = a[i] / b[i]
         *
         * @warning Division by zero produces platform-specific results (usually Inf or NaN)
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

        /** @brief Per-lane division (double precision) */
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

#if defined(CASPI_HAS_AVX)
        // AVX 256-bit arithmetic operations

        /** @brief Per-lane addition (AVX 8xfloat32) */
        inline float32x8 add (float32x8 a, float32x8 b)
        {
            return _mm256_add_ps (a, b);
        }

        /** @brief Per-lane subtraction (AVX 8xfloat32) */
        inline float32x8 sub (float32x8 a, float32x8 b)
        {
            return _mm256_sub_ps (a, b);
        }

        /** @brief Per-lane multiplication (AVX 8xfloat32) */
        inline float32x8 mul (float32x8 a, float32x8 b)
        {
            return _mm256_mul_ps (a, b);
        }

        /** @brief Per-lane division (AVX 8xfloat32) */
        inline float32x8 div (float32x8 a, float32x8 b)
        {
            return _mm256_div_ps (a, b);
        }

        /** @brief Per-lane addition (AVX 4xfloat64) */
        inline float64x4 add (float64x4 a, float64x4 b)
        {
            return _mm256_add_pd (a, b);
        }

        /** @brief Per-lane subtraction (AVX 4xfloat64) */
        inline float64x4 sub (float64x4 a, float64x4 b)
        {
            return _mm256_sub_pd (a, b);
        }

        /** @brief Per-lane multiplication (AVX 4xfloat64) */
        inline float64x4 mul (float64x4 a, float64x4 b)
        {
            return _mm256_mul_pd (a, b);
        }

        /** @brief Per-lane division (AVX 4xfloat64) */
        inline float64x4 div (float64x4 a, float64x4 b)
        {
            return _mm256_div_pd (a, b);
        }
#endif // CASPI_HAS_AVX

        /************************************************************************************************
          Fused Multiply-Add
        ************************************************************************************************/

        /**
         * @brief Fused multiply-add: a * b + c
         * @param a First multiplicand
         * @param b Second multiplicand
         * @param c Addend
         * @return Result where result[i] = a[i] * b[i] + c[i]
         *
         * Uses FMA instruction when available (CASPI_HAS_FMA), otherwise
         * falls back to separate multiply and add.
         *
         * FMA advantages:
         * - Single rounding error instead of two
         * - Often faster (single instruction)
         * - More accurate for accumulation
         *
         * @code
         * // Apply gain with DC offset
         * float32x4 result = mul_add(samples, gain, offset);
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

        /** @brief Fused multiply-add (double precision) */
        inline float64x2 mul_add (float64x2 a, float64x2 b, float64x2 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

#if defined(CASPI_HAS_AVX)
        /** @brief Fused multiply-add (AVX 8xfloat32) */
        inline float32x8 mul_add (float32x8 a, float32x8 b, float32x8 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm256_fmadd_ps (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

        /** @brief Fused multiply-add (AVX 4xfloat64) */
        inline float64x4 mul_add (float64x4 a, float64x4 b, float64x4 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm256_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }
#endif

        /************************************************************************************************
          Fast Approximations
        ************************************************************************************************/

        /**
         * @brief Fast approximate reciprocal (1/x)
         * @param x Input vector
         * @return Approximate reciprocal
         *
         * @note Performance: ~4x faster than div(set1(1.f), x)
         * @note Accuracy: Maximum relative error ~0.15% (1.5e-3)
         *
         * Use cases:
         * - Normalization where approximate results are acceptable
         * - Real-time audio where speed >> accuracy
         * - Initial guess for Newton-Raphson refinement
         *
         * @warning Not suitable for precise calculations
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
         * @brief Fast approximate reciprocal square root (1/√x)
         * @param x Input vector (must be positive)
         * @return Approximate reciprocal square root
         *
         * @note Performance: ~8x faster than div(set1(1.f), sqrt(x))
         * @note Accuracy: Maximum relative error ~0.15% (1.5e-3)
         *
         * Use cases:
         * - Vector normalization (graphics, physics)
         * - Distance calculations
         * - Lighting computations
         *
         * @warning Results undefined for x ≤ 0
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

        /************************************************************************************************
          Comparison Operations
        ************************************************************************************************/

        /**
         * @brief Per-lane equality comparison
         * @param a First vector
         * @param b Second vector
         * @return Mask where mask[i] = 0xFFFFFFFF if a[i] == b[i], else 0x00000000
         *
         * @note Use with blend() for conditional operations
         * @code
         * float32x4 mask = cmp_eq(a, b);
         * float32x4 result = blend(if_false, if_true, mask);
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
         * @brief Per-lane less-than comparison
         * @param a First vector
         * @param b Second vector
         * @return Mask where mask[i] = 0xFFFFFFFF if a[i] < b[i], else 0x00000000
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

        /** @brief Per-lane equality comparison (double precision) */
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

        /** @brief Per-lane less-than comparison (double precision) */
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

        /************************************************************************************************
          Min/Max Operations
        ************************************************************************************************/

        /**
         * @brief Per-lane minimum
         * @param a First vector
         * @param b Second vector
         * @return Result where result[i] = min(a[i], b[i])
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
         * @brief Per-lane maximum
         * @param a First vector
         * @param b Second vector
         * @return Result where result[i] = max(a[i], b[i])
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

        /** @brief Per-lane minimum (double precision) */
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

        /** @brief Per-lane maximum (double precision) */
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

#if defined(CASPI_HAS_AVX)
        /** @brief Per-lane minimum (AVX 8xfloat32) */
        inline float32x8 min (float32x8 a, float32x8 b)
        {
            return _mm256_min_ps (a, b);
        }

        /** @brief Per-lane maximum (AVX 8xfloat32) */
        inline float32x8 max (float32x8 a, float32x8 b)
        {
            return _mm256_max_ps (a, b);
        }

        /** @brief Per-lane minimum (AVX 4xfloat64) */
        inline float64x4 min (float64x4 a, float64x4 b)
        {
            return _mm256_min_pd (a, b);
        }

        /** @brief Per-lane maximum (AVX 4xfloat64) */
        inline float64x4 max (float64x4 a, float64x4 b)
        {
            return _mm256_max_pd (a, b);
        }
#endif

        /************************************************************************************************
          Horizontal Reductions
        ************************************************************************************************/

        /**
         * @brief Sum all lanes (horizontal sum)
         * @param v Input vector
         * @return Sum of all lanes
         *
         * @note Slower than per-lane operations, use sparingly
         * @note Implementation optimized to avoid slow hadd instruction
         *
         * @code
         * float32x4 v = load<float>(data);
         * float total = hsum(v);  // v[0] + v[1] + v[2] + v[3]
         * @endcode
         */
        inline float hsum (float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            // Optimized: avoid slow hadd instruction
            __m128 shuf = _mm_movehdup_ps (v); // [1, 1, 3, 3]
            __m128 sums = _mm_add_ps (v, shuf); // [0+1, 1+1, 2+3, 3+3]
            shuf        = _mm_movehl_ps (shuf, sums); // [2+3, 3+3, ?, ?]
            sums        = _mm_add_ss (sums, shuf); // [0+1+2+3, ...]
            return _mm_cvtss_f32 (sums);
#else
            float tmp[4];
            store<float, float32x4> (tmp, v);
            return tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }

        /** @brief Sum all lanes (double precision) */
        inline double hsum (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d shuf = _mm_shuffle_pd (v, v, 1);
            __m128d sums = _mm_add_sd (v, shuf);
            return _mm_cvtsd_f64 (sums);
#else
            double tmp[2];
            store<double, float64x2> (tmp, v);
            return tmp[0] + tmp[1];
#endif
        }

#if defined(CASPI_HAS_AVX)
        /** @brief Horizontal sum (AVX 8xfloat32) */
        inline float hsum (float32x8 v)
        {
            __m128 lo = _mm256_castps256_ps128 (v);
            __m128 hi = _mm256_extractf128_ps (v, 1);
            return hsum (add (lo, hi));
        }

        /** @brief Horizontal sum (AVX 4xfloat64) */
        inline double hsum (float64x4 v)
        {
            __m128d lo = _mm256_castpd256_pd128 (v);
            __m128d hi = _mm256_extractf128_pd (v, 1);
            return hsum (add (lo, hi));
        }
#endif

        /**
         * @brief Maximum of all lanes
         * @param v Input vector
         * @return Maximum lane value
         */
        inline float hmax (float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            __m128 t1 = _mm_max_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_max_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            return _mm_cvtss_f32 (t2);
#else
            float tmp[4];
            store<float, float32x4> (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
                if (tmp[i] > out)
                    out = tmp[i];
            return out;
#endif
        }

        /** @brief Maximum of all lanes (double precision) */
        inline double hmax (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_max_sd (v, _mm_shuffle_pd (v, v, 1));
            return _mm_cvtsd_f64 (t);
#else
            double tmp[2];
            store<double, float64x2> (tmp, v);
            return tmp[0] > tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /**
         * @brief Minimum of all lanes
         * @param v Input vector
         * @return Minimum lane value
         */
        inline float hmin (float32x4 v)
        {
#if defined(CASPI_HAS_SSE)
            __m128 t1 = _mm_min_ps (v, _mm_movehl_ps (v, v));
            __m128 t2 = _mm_min_ss (t1, _mm_shuffle_ps (t1, t1, 1));
            return _mm_cvtss_f32 (t2);
#else
            float tmp[4];
            store<float, float32x4> (tmp, v);
            float out = tmp[0];
            for (int i = 1; i < 4; i++)
                if (tmp[i] < out)
                    out = tmp[i];
            return out;
#endif
        }

        /** @brief Minimum of all lanes (double precision) */
        inline double hmin (float64x2 v)
        {
#if defined(CASPI_HAS_SSE2)
            __m128d t = _mm_min_sd (v, _mm_shuffle_pd (v, v, 1));
            return _mm_cvtsd_f64 (t);
#else
            double tmp[2];
            store<double, float64x2> (tmp, v);
            return tmp[0] < tmp[1] ? tmp[0] : tmp[1];
#endif
        }

        /************************************************************************************************
          Blend/Select
        ************************************************************************************************/

        /**
         * @brief Conditional per-lane select
         * @param a Value if mask lane is false (all zeros)
         * @param b Value if mask lane is true (all ones)
         * @param mask Selection mask from comparison
         * @return result[i] = mask[i] ? b[i] : a[i]
         *
         * @note Mask values must be from comparison operations (0x00000000 or 0xFFFFFFFF)
         *
         * @code
         * // Clamp to [0, 1] using blend
         * float32x4 zero = set1<float>(0.0f);
         * float32x4 one = set1<float>(1.0f);
         * float32x4 too_low = cmp_lt(v, zero);
         * float32x4 too_high = cmp_lt(one, v);
         * v = blend(v, zero, too_low);
         * v = blend(v, one, too_high);
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

        /** @brief Conditional per-lane select (double precision) */
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

        /************************************************************************************************
          Math Operations
        ************************************************************************************************/

        /**
         * @brief Per-lane negation
         * @param a Input vector
         * @return Result where result[i] = -a[i]
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

        /** @brief Per-lane negation (double precision) */
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
         * @brief Per-lane absolute value
         * @param a Input vector
         * @return Result where result[i] = |a[i]|
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

        /** @brief Per-lane absolute value (double precision) */
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
         * @brief Per-lane square root
         * @param a Input vector (must be non-negative)
         * @return Result where result[i] = √a[i]
         *
         * @warning Results undefined for negative inputs
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

        /** @brief Per-lane square root (double precision) */
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

        /************************************************************************************************
          Kernels - Operation Encapsulation
        ************************************************************************************************

        Kernels encapsulate operations with both SIMD and scalar implementations.
        Used internally by block operations for automatic dispatch.
        ************************************************************************************************/

        namespace kernels
        {
            /**
             * @brief Addition kernel
             *
             * Provides both SIMD and scalar addition: result = a + b
             */
            template <typename T>
            struct AddKernel
            {
                    CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
                      "SIMD kernels only support floating-point types");

                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type a, simd_type b) const
                    {
                        return add (a, b);
                    }

                    T operator() (T a, T b) const
                    {
                        return a + b;
                    }
            };

            /**
             * @brief Subtraction kernel
             *
             * Provides both SIMD and scalar subtraction: result = a - b
             */
            template <typename T>
            struct SubKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type a, simd_type b) const
                    {
                        return sub (a, b);
                    }

                    T operator() (T a, T b) const
                    {
                        return a - b;
                    }
            };

            /**
             * @brief Multiplication kernel
             *
             * Provides both SIMD and scalar multiplication: result = a * b
             */
            template <typename T>
            struct MulKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type a, simd_type b) const
                    {
                        return mul (a, b);
                    }

                    T operator() (T a, T b) const
                    {
                        return a * b;
                    }
            };

            /**
             * @brief Scaling kernel
             *
             * Multiplies by constant factor: result = a * factor
             * Factor is stored as both SIMD vector and scalar for efficiency.
             */
            template <typename T>
            struct ScaleKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type vec;
                    T scalar;

                    explicit ScaleKernel (T factor) : vec (set1<T> (factor)), scalar (factor) {}

                    simd_type operator() (simd_type a) const
                    {
                        return mul (a, vec);
                    }

                    T operator() (T a) const
                    {
                        return a * scalar;
                    }
            };

            /**
             * @brief Copy kernel
             *
             * Passthrough operation: result = a
             */
            template <typename T>
            struct CopyKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type a) const
                    {
                        return a;
                    }

                    T operator() (T a) const
                    {
                        return a;
                    }
            };

            /**
             * @brief Fill kernel
             *
             * Constant value fill: result = value
             */
            template <typename T>
            struct FillKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type vec;
                    T scalar;

                    explicit FillKernel (T value) : vec (set1<T> (value)), scalar (value) {}

                    simd_type simd_value() const { return vec; }
                    T scalar_value() const { return scalar; }
            };

            /**
             * @brief Multiply-accumulate kernel
             *
             * Fused multiply-add: result = acc + a * b
             * Uses FMA instruction when available for best performance.
             */
            template <typename T>
            struct MACKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type acc, simd_type a, simd_type b) const
                    {
                        return mul_add (a, b, acc);
                    }

                    T operator() (T acc, T a, T b) const
                    {
                        return acc + a * b;
                    }
            };

            /**
             * @brief Linear interpolation kernel
             *
             * Computes: result = a + t * (b - a)
             *
             * @param t Interpolation factor (typically in [0, 1])
             *          t=0 returns a, t=1 returns b
             */
            template <typename T>
            struct LerpKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type t_vec;
                    T t_scalar;

                    explicit LerpKernel (T t) : t_vec (set1<T> (t)), t_scalar (t) {}

                    simd_type operator() (simd_type a, simd_type b) const
                    {
                        simd_type diff = sub (b, a);
                        return mul_add (diff, t_vec, a);
                    }

                    T operator() (T a, T b) const
                    {
                        return a + t_scalar * (b - a);
                    }
            };

            /**
             * @brief Clamp kernel
             *
             * Restricts values to [min_val, max_val] range
             */
            template <typename T>
            struct ClampKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type min_vec;
                    simd_type max_vec;
                    T min_scalar;
                    T max_scalar;

                    ClampKernel (T min_val, T max_val)
                        : min_vec (set1<T> (min_val)), max_vec (set1<T> (max_val)), min_scalar (min_val), max_scalar (max_val)
                    {
                    }

                    simd_type operator() (simd_type v) const
                    {
                        v = max (v, min_vec);
                        v = min (v, max_vec);
                        return v;
                    }

                    T operator() (T v) const
                    {
                        if (v < min_scalar)
                            v = min_scalar;
                        if (v > max_scalar)
                            v = max_scalar;
                        return v;
                    }
            };

            /**
             * @brief Absolute value kernel
             *
             * Computes: result = |a|
             */
            template <typename T>
            struct AbsKernel
            {
                CASPI_STATIC_ASSERT(std::is_floating_point<T>::value,
  "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type operator() (simd_type a) const
                    {
                        return abs (a);
                    }

                    T operator() (T a) const
                    {
                        return std::fabs (a);
                    }
            };
        } // namespace kernels

        /************************************************************************************************
          Block Operation Processors
        ************************************************************************************************

        Generic processors that handle prologue/SIMD/epilogue for different operation patterns.
        These are used internally by the high-level ops:: API.
        ************************************************************************************************/

        /**
         * @brief Binary operation processor: dst[i] = kernel(dst[i], src[i])
         *
         * Processes array with automatic alignment handling:
         * 1. Scalar prologue until dst is aligned
         * 2. SIMD loop (aligned if both dst and src are aligned)
         * 3. Scalar epilogue for remaining elements
         *
         * @tparam T Element type (float or double)
         * @tparam Kernel Operation kernel type
         * @param dst Destination array (modified in-place)
         * @param src Source array
         * @param count Number of elements
         * @param kernel Kernel instance defining the operation
         */
        template <typename T, typename Kernel>
        inline void block_op_binary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

            std::size_t i = 0;

            // Prologue: align dst pointer
            const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (dst);
            const std::size_t prologue_count = (prologue < count) ? prologue : count;

            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (dst[i], src[i]);
            }

            // SIMD main loop
            const std::size_t remaining  = count - i;
            const std::size_t simd_count = (remaining / Width) * Width;
            const std::size_t simd_end   = i + simd_count;

            // Check alignment once (hoisted out of loop)
            const bool dst_aligned = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned = Strategy::is_aligned<Alignment> (src + i);

            if (dst_aligned && src_aligned)
            {
                for (; i < simd_end; i += Width)
                {
                    auto va = load_aligned<T> (src + i);
                    auto vd = load_aligned<T> (dst + i);
                    auto vr = kernel (vd, va);
                    store_aligned<T> (dst + i, vr);
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    auto va = load_unaligned<T> (src + i);
                    auto vd = load_unaligned<T> (dst + i);
                    auto vr = kernel (vd, va);
                    store_unaligned<T> (dst + i, vr);
                }
            }

            // Epilogue
            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src[i]);
            }
        }

        /**
         * @brief Unary operation processor: dst[i] = kernel(src[i])
         *
         * Same as block_op_binary but for operations with single source.
         */
        template <typename T, typename Kernel>
        inline void block_op_unary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

            std::size_t i = 0;

            const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (dst);
            const std::size_t prologue_count = (prologue < count) ? prologue : count;

            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (src[i]);
            }

            const std::size_t remaining  = count - i;
            const std::size_t simd_count = (remaining / Width) * Width;
            const std::size_t simd_end   = i + simd_count;

            const bool dst_aligned = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned = Strategy::is_aligned<Alignment> (src + i);

            if (dst_aligned && src_aligned)
            {
                for (; i < simd_end; i += Width)
                {
                    auto va = load_aligned<T> (src + i);
                    auto vr = kernel (va);
                    store_aligned<T> (dst + i, vr);
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    auto va = load_unaligned<T> (src + i);
                    auto vr = kernel (va);
                    store_unaligned<T> (dst + i, vr);
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (src[i]);
            }
        }

        /**
         * @brief In-place unary operation processor: data[i] = kernel(data[i])
         *
         * Optimized for in-place operations (e.g., scale, clamp, abs).
         */
        template <typename T, typename Kernel>
        inline void block_op_inplace (T* CASPI_RESTRICT data, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

            std::size_t i = 0;

            const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (data);
            const std::size_t prologue_count = (prologue < count) ? prologue : count;

            for (; i < prologue_count; ++i)
            {
                data[i] = kernel (data[i]);
            }

            const std::size_t remaining  = count - i;
            const std::size_t simd_count = (remaining / Width) * Width;
            const std::size_t simd_end   = i + simd_count;

            const bool aligned = Strategy::is_aligned<Alignment> (data + i);

            if (aligned)
            {
                for (; i < simd_end; i += Width)
                {
                    auto v = load_aligned<T> (data + i);
                    v      = kernel (v);
                    store_aligned<T> (data + i, v);
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    auto v = load_unaligned<T> (data + i);
                    v      = kernel (v);
                    store_unaligned<T> (data + i, v);
                }
            }

            for (; i < count; ++i)
            {
                data[i] = kernel (data[i]);
            }
        }

        /**
         * @brief Fill operation processor: dst[i] = value
         *
         * Optimized constant fill operation.
         */
        template <typename T, typename Kernel>
        inline void block_op_fill (T* CASPI_RESTRICT dst, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

            std::size_t i = 0;

            const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (dst);
            const std::size_t prologue_count = (prologue < count) ? prologue : count;

            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }

            const std::size_t remaining  = count - i;
            const std::size_t simd_count = (remaining / Width) * Width;
            const std::size_t simd_end   = i + simd_count;

            const bool aligned = Strategy::is_aligned<Alignment> (dst + i);
            auto vec           = kernel.simd_value();

            if (aligned)
            {
                for (; i < simd_end; i += Width)
                {
                    store_aligned<T> (dst + i, vec);
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned<T> (dst + i, vec);
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }
        }

        /**
         * @brief Ternary operation processor: dst[i] = kernel(dst[i], src1[i], src2[i])
         *
         * Used for multiply-accumulate and similar three-operand operations.
         */
        template <typename T, typename Kernel>
        inline void block_op_ternary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

            std::size_t i = 0;

            const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (dst);
            const std::size_t prologue_count = (prologue < count) ? prologue : count;

            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }

            const std::size_t remaining  = count - i;
            const std::size_t simd_count = (remaining / Width) * Width;
            const std::size_t simd_end   = i + simd_count;

            const bool dst_aligned  = Strategy::is_aligned<Alignment> (dst + i);
            const bool src1_aligned = Strategy::is_aligned<Alignment> (src1 + i);
            const bool src2_aligned = Strategy::is_aligned<Alignment> (src2 + i);

            if (dst_aligned && src1_aligned && src2_aligned)
            {
                for (; i < simd_end; i += Width)
                {
                    auto vd = load_aligned<T> (dst + i);
                    auto v1 = load_aligned<T> (src1 + i);
                    auto v2 = load_aligned<T> (src2 + i);
                    auto vr = kernel (vd, v1, v2);
                    store_aligned<T> (dst + i, vr);
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    auto vd = load_unaligned<T> (dst + i);
                    auto v1 = load_unaligned<T> (src1 + i);
                    auto v2 = load_unaligned<T> (src2 + i);
                    auto vr = kernel (vd, v1, v2);
                    store_unaligned<T> (dst + i, vr);
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }
        }

        /************************************************************************************************
          High-Level Block Operations API
        ************************************************************************************************

        User-facing API for array processing. All operations handle alignment automatically.
        ************************************************************************************************/

        namespace ops
        {
            /**
             * @brief Add source array to destination: dst[i] += src[i]
             * @tparam T Element type (float or double)
             * @param dst Destination array (modified in-place)
             * @param src Source array
             * @param count Number of elements
             *
             * @code
             * float buffer[512], input[512];
             * ops::add(buffer, input, 512);  // buffer += input
             * @endcode
             */
            template <typename T>
            void add (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::AddKernel<T>());
            }

            /**
             * @brief Subtract source array from destination: dst[i] -= src[i]
             * @tparam T Element type (float or double)
             * @param dst Destination array (modified in-place)
             * @param src Source array
             * @param count Number of elements
             */
            template <typename T>
            void sub (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::SubKernel<T>());
            }

            /**
             * @brief Element-wise multiply: dst[i] *= src[i]
             * @tparam T Element type (float or double)
             * @param dst Destination array (modified in-place)
             * @param src Source array
             * @param count Number of elements
             */
            template <typename T>
            void mul (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::MulKernel<T>());
            }

            /**
             * @brief Scale array by constant: data[i] *= factor
             * @tparam T Element type (float or double)
             * @param data Array to scale (modified in-place)
             * @param count Number of elements
             * @param factor Scaling factor
             *
             * @code
             * float audio[512];
             * ops::scale(audio, 512, 0.5f);  // Halve volume
             * @endcode
             */
            template <typename T>
            void scale (T* CASPI_RESTRICT data, std::size_t count, T factor)
            {
                block_op_inplace (data, count, kernels::ScaleKernel<T> (factor));
            }

            /**
             * @brief Copy array: dst[i] = src[i]
             * @tparam T Element type (float or double)
             * @param dst Destination array
             * @param src Source array
             * @param count Number of elements
             */
            template <typename T>
            void copy (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_unary (dst, src, count, kernels::CopyKernel<T>());
            }

            /**
             * @brief Fill array with constant: dst[i] = value
             * @tparam T Element type (float or double)
             * @param dst Destination array
             * @param count Number of elements
             * @param value Fill value
             *
             * @code
             * float buffer[512];
             * ops::fill(buffer, 512, 0.0f);  // Zero buffer
             * @endcode
             */
            template <typename T>
            void fill (T* CASPI_RESTRICT dst, std::size_t count, T value)
            {
                block_op_fill (dst, count, kernels::FillKernel<T> (value));
            }

            /**
             * @brief Multiply-accumulate: dst[i] += src1[i] * src2[i]
             * @tparam T Element type (float or double)
             * @param dst Accumulator array (modified in-place)
             * @param src1 First multiplicand array
             * @param src2 Second multiplicand array
             * @param count Number of elements
             *
             * Uses FMA instruction when available for optimal performance.
             *
             * @code
             * float output[512], signal[512], kernel[512];
             * ops::fill(output, 512, 0.0f);
             * ops::mac(output, signal, kernel, 512);  // Convolution step
             * @endcode
             */
            template <typename T>
            void mac (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count)
            {
                block_op_ternary (dst, src1, src2, count, kernels::MACKernel<T>());
            }

            /**
             * @brief Linear interpolation: dst[i] = a[i] + t * (b[i] - a[i])
             * @tparam T Element type (float or double)
             * @param dst Destination array
             * @param a First source array (returned when t=0)
             * @param b Second source array (returned when t=1)
             * @param t Interpolation factor
             * @param count Number of elements
             *
             * @code
             * float crossfade[512], track_a[512], track_b[512];
             * ops::lerp(crossfade, track_a, track_b, 0.5f, 512);  // 50% mix
             * @endcode
             */
            template <typename T>
            void lerp (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, T t, std::size_t count)
            {
                kernels::LerpKernel<T> kernel (t);
                constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<T>();

                std::size_t i                    = 0;
                const std::size_t prologue       = Strategy::samples_to_alignment<Alignment> (dst);
                const std::size_t prologue_count = (prologue < count) ? prologue : count;

                for (; i < prologue_count; ++i)
                {
                    dst[i] = kernel (a[i], b[i]);
                }

                const std::size_t remaining  = count - i;
                const std::size_t simd_count = (remaining / Width) * Width;
                const std::size_t simd_end   = i + simd_count;

                const bool dst_aligned = Strategy::is_aligned<Alignment> (dst + i);
                const bool a_aligned   = Strategy::is_aligned<Alignment> (a + i);
                const bool b_aligned   = Strategy::is_aligned<Alignment> (b + i);

                if (dst_aligned && a_aligned && b_aligned)
                {
                    for (; i < simd_end; i += Width)
                    {
                        auto va = load_aligned<T> (a + i);
                        auto vb = load_aligned<T> (b + i);
                        auto vr = kernel (va, vb);
                        store_aligned<T> (dst + i, vr);
                    }
                }
                else
                {
                    for (; i < simd_end; i += Width)
                    {
                        auto va = load_unaligned<T> (a + i);
                        auto vb = load_unaligned<T> (b + i);
                        auto vr = kernel (va, vb);
                        store_unaligned<T> (dst + i, vr);
                    }
                }

                for (; i < count; ++i)
                {
                    dst[i] = kernel (a[i], b[i]);
                }
            }

            /**
             * @brief Clamp array to range: data[i] = clamp(data[i], min_val, max_val)
             * @tparam T Element type (float or double)
             * @param data Array to clamp (modified in-place)
             * @param min_val Minimum value
             * @param max_val Maximum value
             * @param count Number of elements
             *
             * @code
             * float audio[512];
             * ops::clamp(audio, -1.0f, 1.0f, 512);  // Limit to valid range
             * @endcode
             */
            template <typename T>
            void clamp (T* CASPI_RESTRICT data, T min_val, T max_val, std::size_t count)
            {
                block_op_inplace (data, count, kernels::ClampKernel<T> (min_val, max_val));
            }

            /**
             * @brief Absolute value: data[i] = |data[i]|
             * @tparam T Element type (float or double)
             * @param data Array to process (modified in-place)
             * @param count Number of elements
             *
             * @code
             * float signal[512];
             * ops::abs(signal, 512);  // Rectify signal
             * @endcode
             */
            template <typename T>
            void abs (T* CASPI_RESTRICT data, std::size_t count)
            {
                block_op_inplace (data, count, kernels::AbsKernel<T>());
            }

            /**
             * @brief Find minimum element in array
             * @tparam T Element type (float or double)
             * @param data Array to search
             * @param count Number of elements
             * @return Minimum value (or T(0) if count==0)
             *
             * @code
             * float audio[512];
             * float min_sample = ops::find_min(audio, 512);
             * @endcode
             */
            template <typename T>
            T find_min (const T* CASPI_RESTRICT data, std::size_t count)
            {
                if (count == 0)
                    return T (0);

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                const std::size_t prologue     = Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data + i);
                const std::size_t prologue_end = (i + prologue < count) ? i + prologue : count;

                for (; i < prologue_end; ++i)
                {
                    if (data[i] < result)
                        result = data[i];
                }

                if (i + Width <= count)
                {
                    simd_t vmin = set1<T> (result);

                    for (; i + Width <= count; i += Width)
                    {
                        simd_t v = load<T> (data + i);
                        vmin     = min (vmin, v);
                    }

                    result = hmin (vmin);
                }

                for (; i < count; ++i)
                {
                    if (data[i] < result)
                        result = data[i];
                }

                return result;
            }

            /**
             * @brief Find maximum element in array
             * @tparam T Element type (float or double)
             * @param data Array to search
             * @param count Number of elements
             * @return Maximum value (or T(0) if count==0)
             *
             * @code
             * float audio[512];
             * float peak = ops::find_max(audio, 512);
             * @endcode
             */
            template <typename T>
            T find_max (const T* CASPI_RESTRICT data, std::size_t count)
            {
                if (count == 0)
                    return T (0);

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                const std::size_t prologue     = Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data + i);
                const std::size_t prologue_end = (i + prologue < count) ? i + prologue : count;

                for (; i < prologue_end; ++i)
                {
                    if (data[i] > result)
                        result = data[i];
                }

                if (i + Width <= count)
                {
                    simd_t vmax = set1<T> (result);

                    for (; i + Width <= count; i += Width)
                    {
                        simd_t v = load<T> (data + i);
                        vmax     = max (vmax, v);
                    }

                    result = hmax (vmax);
                }

                for (; i < count; ++i)
                {
                    if (data[i] > result)
                        result = data[i];
                }

                return result;
            }

            /**
             * @brief Sum all elements in array
             * @tparam T Element type (float or double)
             * @param data Array to sum
             * @param count Number of elements
             * @return Sum of all elements
             *
             * @code
             * float samples[512];
             * float dc_offset = ops::sum(samples, 512) / 512.0f;
             * @endcode
             */
            template <typename T>
            T sum (const T* CASPI_RESTRICT data, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue     = Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data);
                const std::size_t prologue_end = (prologue < count) ? prologue : count;

                for (; i < prologue_end; ++i)
                {
                    result += data[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));

                    for (; i + Width <= count; i += Width)
                    {
                        simd_t v = load<T> (data + i);
                        vsum     = SIMD::add (vsum, v);
                    }

                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                {
                    result += data[i];
                }

                return result;
            }

            /**
             * @brief Compute dot product of two arrays
             * @tparam T Element type (float or double)
             * @param a First array
             * @param b Second array
             * @param count Number of elements
             * @return Dot product sum(a[i] * b[i])
             *
             * @code
             * float signal[512];
             * float rms = std::sqrt(ops::dot_product(signal, signal, 512) / 512.0f);
             * @endcode
             */
            template <typename T>
            T dot_product (const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue     = Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (a);
                const std::size_t prologue_end = (prologue < count) ? prologue : count;

                for (; i < prologue_end; ++i)
                {
                    result += a[i] * b[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));

                    for (; i + Width <= count; i += Width)
                    {
                        simd_t va = load<T> (a + i);
                        simd_t vb = load<T> (b + i);
                        vsum      = mul_add (va, vb, vsum);
                    }

                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                {
                    result += a[i] * b[i];
                }

                return result;
            }

        } // namespace ops
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_SIMD_H