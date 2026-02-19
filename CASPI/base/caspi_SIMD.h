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
* There is also a "Strategy" layer - compile-time traits to determine optimal processing.
*
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
* store(output, c);                          // Store to memory
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
* - store(ptr, vec)              - Store vector (overloaded per type)
* - set1<T>(value)               - Broadcast scalar (128-bit)
* - set1_256(value)              - Broadcast scalar (256-bit AVX, float/double overloads)
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
* - load<T>()/store() automatically choose aligned/unaligned
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
        struct float32x4 { float data[4]; };
#endif

        /** @brief 2-lane float64 vector (128-bit) */
#if defined(CASPI_HAS_SSE2)
        using float64x2 = __m128d;
#elif defined(CASPI_HAS_NEON64)
        using float64x2 = float64x2_t;
#elif defined(CASPI_HAS_WASM_SIMD)
        using float64x2 = v128_t;
#else
        struct float64x2 { double data[2]; };
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

            template <> struct simd_type<float, 4>  { using type = float32x4; };
            template <> struct simd_type<double, 2> { using type = float64x2; };

#if defined(CASPI_HAS_AVX)
            template <> struct simd_type<float, 8>  { using type = float32x8; };
            template <> struct simd_type<double, 4> { using type = float64x4; };
#endif

            /**
             * @brief Minimum SIMD width for a given scalar type.
             *
             * Returns 4 for float and 2 for double when SIMD is available,
             * falling back to 1 (scalar) otherwise.
             */
            template <typename T>
            struct min_simd_width { static constexpr std::size_t value = 1; };

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

            // NOTE: max_simd_width removed - was unused dead code.

            /**
             * @brief Check if pointer is aligned to N bytes
             * @tparam N Alignment boundary (must be power of 2)
             */
            template <std::size_t N>
            inline bool is_aligned (const void* ptr) noexcept
            {
                return (reinterpret_cast<std::uintptr_t> (ptr) % N) == 0;
            }

            /**
             * @brief Calculate number of elements until next alignment boundary.
             *
             * Used internally for prologue calculations in block operations.
             *
             * @tparam Alignment Desired alignment in bytes
             * @tparam T Element type
             * @param ptr Pointer to check
             * @return Number of elements to advance to reach alignment
             */
            template <std::size_t Alignment, typename T>
            inline std::size_t samples_to_alignment (const T* ptr) noexcept
            {
                const auto addr = reinterpret_cast<std::uintptr_t>(ptr);
                const std::uintptr_t misalignment = addr % Alignment;

                if (misalignment == 0)
                {
                    return 0;
                }

                const std::uintptr_t bytes_to_align = Alignment - misalignment;

                // ceil(bytes / sizeof(T))
                return (bytes_to_align + sizeof(T) - 1) / sizeof(T);
            }

            /**
             * @brief Get preferred SIMD alignment for a type.
             * @return 32 (AVX), 16 (SSE/NEON/WASM), or alignof(T) (scalar)
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

            /**
             * @brief Byte size of L1 data cache for NT store threshold calculations.
             *
             * Injected at compile time via -DCASPI_L1_CACHE_BYTES=N.
             * Defaults to 32768 (32KB) — the smallest L1 found on modern x86/ARM targets.
             * The NT threshold is set to 2x L1 so that a working set that no longer fits
             * in L1 does not pollute it with streaming writes.
             *
             * To override in CMake:
             *   target_compile_definitions(your_target PRIVATE CASPI_L1_CACHE_BYTES=49152)
             */
#if ! defined(CASPI_L1_CACHE_BYTES)
#define CASPI_L1_CACHE_BYTES 32768
#endif

            static constexpr std::size_t L1_CACHE_BYTES = CASPI_L1_CACHE_BYTES;

            /**
             * @brief Number of elements of type T above which NT stores are preferred.
             *
             * Set to 2x L1 capacity for T. At this point the working set has
             * overflowed L1 and temporal stores cause unnecessary cache pollution.
             */
            template <typename T>
            constexpr std::size_t nt_store_threshold() noexcept
            {
                return (2 * L1_CACHE_BYTES) / sizeof (T);
            }
        } // namespace Strategy

        /************************************************************************************************
          Load Operations
        ************************************************************************************************/

        /**
         * @brief Load 128-bit vector from aligned memory.
         * @warning UB if pointer is not properly aligned.
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
            for (int i = 0; i < 4; i++) v.data[i] = p[i];
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
            float64x2 v; v.data[0] = p[0]; v.data[1] = p[1];
            return v;
#endif
        }

        /**
         * @brief Load 128-bit vector from unaligned memory.
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
            for (int i = 0; i < 4; i++) v.data[i] = p[i];
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
            float64x2 v; v.data[0] = p[0]; v.data[1] = p[1];
            return v;
#endif
        }

        /**
         * @brief Load vector with automatic alignment detection.
         *
         * Prefers load_aligned when the pointer is suitably aligned.
         * Use this for ad-hoc loads; block processors hoist the check.
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
        load (const T* p)
        {
            constexpr std::size_t alignment = Strategy::simd_alignment<T>();
            return Strategy::is_aligned<alignment> (p) ? load_aligned<T> (p)
                                                       : load_unaligned<T> (p);
        }

#if defined(CASPI_HAS_AVX)
        template <> inline float32x8 load_aligned<float>   (const float*  p) { return _mm256_load_ps  (p); }
        template <> inline float32x8 load_unaligned<float> (const float*  p) { return _mm256_loadu_ps (p); }
        template <> inline float64x4 load_aligned<double>  (const double* p) { return _mm256_load_pd  (p); }
        template <> inline float64x4 load_unaligned<double>(const double* p) { return _mm256_loadu_pd (p); }
#endif

        /************************************************************************************************
          Store Operations
          FIX: Replaced two-template-param store_aligned<T,VecT> with explicit overloads.
               The second type parameter was always fully determined by T and the call site
               vector type, making it a redundant deduction burden with awkward syntax.
        ************************************************************************************************/

        /** @brief Store float32x4 to aligned memory. @warning UB if unaligned. */
        inline void store_aligned (float* p, float32x4 v)
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

        /** @brief Store float64x2 to aligned memory. @warning UB if unaligned. */
        inline void store_aligned (double* p, float64x2 v)
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

        /** @brief Store float32x4 to unaligned memory. */
        inline void store_unaligned (float* p, float32x4 v)
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

        /** @brief Store float64x2 to unaligned memory. */
        inline void store_unaligned (double* p, float64x2 v)
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

        /** @brief Store float32x4 with automatic alignment detection. */
        inline void store (float* p, float32x4 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<float>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /** @brief Store float64x2 with automatic alignment detection. */
        inline void store (double* p, float64x2 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<double>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

#if defined(CASPI_HAS_AVX)
        /** @brief Store float32x8 to aligned memory (AVX). */
        inline void store_aligned (float* p, float32x8 v) { _mm256_store_ps (p, v); }
        /** @brief Store float32x8 to unaligned memory (AVX). */
        inline void store_unaligned (float* p, float32x8 v) { _mm256_storeu_ps (p, v); }
        /** @brief Store float64x4 to aligned memory (AVX). */
        inline void store_aligned (double* p, float64x4 v) { _mm256_store_pd (p, v); }
        /** @brief Store float64x4 to unaligned memory (AVX). */
        inline void store_unaligned (double* p, float64x4 v) { _mm256_storeu_pd (p, v); }

        /** @brief Store float32x8 with automatic alignment detection. */
        inline void store (float* p, float32x8 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<float>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /** @brief Store float64x4 with automatic alignment detection. */
        inline void store (double* p, float64x4 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<double>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }
#endif

        /**
         * @brief Write float32x4 to memory using a non-temporal (streaming) store.
         * @note Requires 16-byte alignment. No cache line allocation.
         */
        inline void stream_store (float* p, float32x4 v) noexcept
        {
#if defined(CASPI_HAS_SSE)
            _mm_stream_ps (p, v);
#else
            store_aligned (p, v); // fallback: no NT stores available
#endif
        }

        /**
         * @brief Write float64x2 to memory using a non-temporal store.
         * @note Requires 16-byte alignment.
         */
        inline void stream_store (double* p, float64x2 v) noexcept
        {
#if defined(CASPI_HAS_SSE2)
            _mm_stream_pd (p, v);
#else
            store_aligned (p, v);
#endif
        }

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Write float32x8 to memory using a non-temporal store.
         * @note Requires 32-byte alignment.
         */
        inline void stream_store (float* p, float32x8 v) noexcept
        {
            _mm256_stream_ps (p, v);
        }

        /**
         * @brief Write float64x4 to memory using a non-temporal store.
         * @note Requires 32-byte alignment.
         */
        inline void stream_store (double* p, float64x4 v) noexcept
        {
            _mm256_stream_pd (p, v);
        }
#endif

        /**
         * @brief Store fence: ensures all prior NT stores are globally visible.
         * Must be called after any sequence of NT stores before subsequent reads
         * of the written region by any thread.
         */
        inline void store_fence() noexcept
        {
#if defined(CASPI_HAS_SSE)
            _mm_sfence();
#endif
            // On non-SSE targets (NEON, WASM) there are no NT stores so no fence needed.
        }

        /************************************************************************************************
          Broadcast Operations
        ************************************************************************************************/

        /**
         * @brief Broadcast scalar to all 128-bit vector lanes.
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

#if defined(CASPI_HAS_AVX)
        /**
         * @brief Broadcast scalar to 256-bit vector lanes (AVX).
         *
         * Overloaded for float (float32x8) and double (float64x4).
         * Named set1_256 to avoid conflicting with the 128-bit set1<T> template.
         */
        inline float32x8 set1_256 (float x) { return _mm256_set1_ps (x); }
        inline float64x4 set1_256 (double x) { return _mm256_set1_pd (x); }
#endif

        /************************************************************************************************
          Arithmetic Operations
        ************************************************************************************************/

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

        /** @warning Division by zero produces platform-specific results (Inf or NaN). */
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
        inline float32x8 add (float32x8 a, float32x8 b) { return _mm256_add_ps (a, b); }
        inline float32x8 sub (float32x8 a, float32x8 b) { return _mm256_sub_ps (a, b); }
        inline float32x8 mul (float32x8 a, float32x8 b) { return _mm256_mul_ps (a, b); }
        inline float32x8 div (float32x8 a, float32x8 b) { return _mm256_div_ps (a, b); }
        inline float64x4 add (float64x4 a, float64x4 b) { return _mm256_add_pd (a, b); }
        inline float64x4 sub (float64x4 a, float64x4 b) { return _mm256_sub_pd (a, b); }
        inline float64x4 mul (float64x4 a, float64x4 b) { return _mm256_mul_pd (a, b); }
        inline float64x4 div (float64x4 a, float64x4 b) { return _mm256_div_pd (a, b); }
#endif

        /************************************************************************************************
          Fused Multiply-Add
        ************************************************************************************************/

        /**
         * @brief Fused multiply-add: result[i] = a[i] * b[i] + c[i]
         *
         * Uses FMA instruction when CASPI_HAS_FMA is defined (single rounding,
         * better accuracy, typically one cycle). Falls back to mul+add otherwise.
         */
        inline float32x4 mul_add (float32x4 a, float32x4 b, float32x4 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_ps (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

        inline float64x2 mul_add (float64x2 a, float64x2 b, float64x2 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm_fmadd_pd (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

#if defined(CASPI_HAS_AVX)
        inline float32x8 mul_add (float32x8 a, float32x8 b, float32x8 c)
        {
#if defined(CASPI_HAS_FMA)
            return _mm256_fmadd_ps (a, b, c);
#else
            return add (mul (a, b), c);
#endif
        }

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
          Fast Approximations (float only)
        ************************************************************************************************/

        /**
         * @brief Fast approximate reciprocal (1/x).
         * @note ~4x faster than div(set1(1.f), x). Max relative error ~0.15%.
         * @warning Not suitable for precision-critical paths.
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
         * @note ~8x faster than div(set1(1.f), sqrt(x)). Max relative error ~0.15%.
         * @warning Results undefined for x ≤ 0.
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
         * @brief Per-lane equality: mask[i] = 0xFFFFFFFF if a[i]==b[i], else 0.
         * Use with blend() for branchless conditional select.
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

        /** @brief Per-lane less-than: mask[i] = 0xFFFFFFFF if a[i] < b[i], else 0. */
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

        /** @brief Per-lane equality (double precision). */
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

        /** @brief Per-lane less-than (double precision). */
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
        inline float32x8 min (float32x8 a, float32x8 b) { return _mm256_min_ps (a, b); }
        inline float32x8 max (float32x8 a, float32x8 b) { return _mm256_max_ps (a, b); }
        inline float64x4 min (float64x4 a, float64x4 b) { return _mm256_min_pd (a, b); }
        inline float64x4 max (float64x4 a, float64x4 b) { return _mm256_max_pd (a, b); }
#endif

        /************************************************************************************************
          Horizontal Reductions
        ************************************************************************************************/

        /**
         * @brief Sum all lanes.
         * @note Uses shuffle-based approach to avoid the slow hadd instruction on SSE.
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

#if defined(CASPI_HAS_AVX)
        inline float hsum (float32x8 v)
        {
            __m128 lo = _mm256_castps256_ps128 (v);
            __m128 hi = _mm256_extractf128_ps (v, 1);
            return hsum (add (lo, hi));
        }

        inline double hsum (float64x4 v)
        {
            __m128d lo = _mm256_castpd256_pd128 (v);
            __m128d hi = _mm256_extractf128_pd (v, 1);
            return hsum (add (lo, hi));
        }
#endif

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

        /************************************************************************************************
          Blend/Select
        ************************************************************************************************/

        /**
         * @brief Conditional per-lane select: result[i] = mask[i] ? b[i] : a[i]
         *
         * Mask must come from a comparison op (all-zeros or all-ones per lane).
         * Uses and/andnot rather than SSE4.1 blendv for broader compatibility.
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

        /** @warning Results undefined for negative inputs. */
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

        Kernels provide paired SIMD and scalar operator() overloads.
        Used exclusively by the block operation processors below.
        ************************************************************************************************/

        namespace kernels
        {
            template <typename T>
            struct AddKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return add (a, b); }
                    T operator() (T a, T b) const { return a + b; }
            };

            template <typename T>
            struct SubKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return sub (a, b); }
                    T operator() (T a, T b) const { return a - b; }
            };

            template <typename T>
            struct MulKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return mul (a, b); }
                    T operator() (T a, T b) const { return a * b; }
            };

            template <typename T>
            struct ScaleKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type vec;
                    T scalar;
                    explicit ScaleKernel (T factor) : vec (set1<T> (factor)), scalar (factor) {}
                    simd_type operator() (simd_type a) const { return mul (a, vec); }
                    T operator() (T a) const { return a * scalar; }
            };

            template <typename T>
            struct FillKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type vec;
                    T scalar;
                    explicit FillKernel (T value) : vec (set1<T> (value)), scalar (value) {}
                    simd_type simd_value() const { return vec; }
                    T scalar_value() const { return scalar; }
            };

            template <typename T>
            struct MACKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type acc, simd_type a, simd_type b) const { return mul_add (a, b, acc); }
                    T operator() (T acc, T a, T b) const { return acc + a * b; }
            };

            /**
             * @brief Linear interpolation kernel: result = a + t*(b-a)
             * @param t Interpolation factor; t=0 returns a, t=1 returns b.
             */
            template <typename T>
            struct LerpKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type t_vec;
                    T t_scalar;
                    explicit LerpKernel (T t) : t_vec (set1<T> (t)), t_scalar (t) {}
                    simd_type operator() (simd_type a, simd_type b) const { return mul_add (sub (b, a), t_vec, a); }
                    T operator() (T a, T b) const { return a + t_scalar * (b - a); }
            };

            template <typename T>
            struct ClampKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type min_vec, max_vec;
                    T min_scalar, max_scalar;
                    ClampKernel (T lo, T hi) : min_vec (set1<T> (lo)), max_vec (set1<T> (hi)), min_scalar (lo), max_scalar (hi) {}
                    simd_type operator() (simd_type v) const { return min (max (v, min_vec), max_vec); }
                    T operator() (T v) const
                    {
                        if (v < min_scalar)
                            v = min_scalar;
                        if (v > max_scalar)
                            v = max_scalar;
                        return v;
                    }
            };

            template <typename T>
            struct AbsKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a) const { return abs (a); }
                    T operator() (T a) const { return std::fabs (a); }
            };
        } // namespace kernels

        /************************************************************************************************
          Block Operation Processors
        ************************************************************************************************

        Handle prologue (scalar until aligned) / SIMD main loop / epilogue (scalar remainder).
        Alignment check is hoisted once per call, not per iteration.
        ************************************************************************************************/

        /** @brief dst[i] = kernel(dst[i], src[i]) */
        template <typename T, typename Kernel>
        void block_op_binary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (src == nullptr) || (count == 0))
            {
                return;
            }
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
                dst[i] = kernel (dst[i], src[i]);

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned     = Strategy::is_aligned<Alignment> (src + i);

            // NOTE: binary ops (add, sub, mul) are latency-bound not bandwidth-bound —
            // NT stores rarely help since we read AND write the same cache lines.
            // No NT path here; unrolling gives the most benefit.
            if (dst_aligned && src_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (dst + i + Width), load_aligned<T> (src + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (dst + i + Width * 2), load_aligned<T> (src + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (dst + i + Width * 3), load_aligned<T> (src + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src + i)));
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (dst + i), load_unaligned<T> (src + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src[i]);
            }
        }

        /** @brief dst[i] = kernel(src[i]) */
        template <typename T, typename Kernel>
        void block_op_unary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (src == nullptr) || (count == 0))
            {
                return;
            }
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (src[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned     = Strategy::is_aligned<Alignment> (src + i);

            if (dst_aligned && src_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (src + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (src + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (src + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (src + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (src + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (src + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (src[i]);
            }
        }

        /**
         * @brief data[i] = kernel(data[i])
         *
         * Fix A: 4x manual unroll in the aligned SIMD loop.
         * Fix B: NT stores for large aligned buffers to avoid cache pollution.
         */
        template <typename T, typename Kernel>
        void block_op_inplace (T* CASPI_RESTRICT data, std::size_t count, const Kernel& kernel)
        {
            if ((data == nullptr) || (count == 0))
                return;

            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
            for (; i < prologue_count; ++i)
            {
                data[i] = kernel (data[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

            if (aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (data + i,           kernel (load_aligned<T> (data + i)));
                    store_aligned (data + i + Width,   kernel (load_aligned<T> (data + i + Width)));
                    store_aligned (data + i + Width*2, kernel (load_aligned<T> (data + i + Width*2)));
                    store_aligned (data + i + Width*3, kernel (load_aligned<T> (data + i + Width*3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (data + i, kernel (load_aligned<T> (data + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (data + i, kernel (load_unaligned<T> (data + i)));
                }
            }

            for (; i < count; ++i)
            {
                data[i] = kernel (data[i]);
            }
        }

        /**
         * @brief dst[i] = kernel.scalar_value()
         *
         * NT store path for large aligned buffers: fill is write-only so NT stores
         * are particularly effective (no read traffic at all on the written region).
         */
        template <typename T, typename Kernel>
        inline void block_op_fill (T* CASPI_RESTRICT dst, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (count == 0))
            {
                return;
            }
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }

            const std::size_t simd_end     = i + ((count - i) / Width) * Width;
            const bool aligned             = Strategy::is_aligned<Alignment> (dst + i);
            const auto vec                 = kernel.simd_value();
            const std::size_t nt_threshold = Strategy::nt_store_threshold<T>();

            if (aligned)
            {
#if defined(CASPI_HAS_SSE)
                // NT path: fill generates no reads so bypassing cache is always correct
                // above the threshold — the written data is not reused in L1.
                if ((simd_end - i) >= nt_threshold)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        stream_store (dst + i, vec);
                        stream_store (dst + i + Width, vec);
                        stream_store (dst + i + Width * 2, vec);
                        stream_store (dst + i + Width * 3, vec);
                    }
                    for (; i < simd_end; i += Width)
                    {
                        stream_store (dst + i, vec);
                    }

                    store_fence();
                }
                else
#endif
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (dst + i, vec);
                        store_aligned (dst + i + Width, vec);
                        store_aligned (dst + i + Width * 2, vec);
                        store_aligned (dst + i + Width * 3, vec);
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (dst + i, vec);
                    }
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, vec);
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }
        }

        /** @brief dst[i] = kernel(dst[i], src1[i], src2[i]) — unchanged, no NT benefit */
        template <typename T, typename Kernel>
        inline void block_op_ternary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src1_aligned    = Strategy::is_aligned<Alignment> (src1 + i);
            const bool src2_aligned    = Strategy::is_aligned<Alignment> (src2 + i);

            if (dst_aligned && src1_aligned && src2_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src1 + i), load_aligned<T> (src2 + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (dst + i + Width), load_aligned<T> (src1 + i + Width), load_aligned<T> (src2 + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (dst + i + Width * 2), load_aligned<T> (src1 + i + Width * 2), load_aligned<T> (src2 + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (dst + i + Width * 3), load_aligned<T> (src1 + i + Width * 3), load_aligned<T> (src2 + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src1 + i), load_aligned<T> (src2 + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (dst + i), load_unaligned<T> (src1 + i), load_unaligned<T> (src2 + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }
        }

        /**
         * @brief dst[i] = kernel(a[i], b[i]) — unchanged, no NT benefit
         * (lerp: 2 reads + 1 write, cache lines needed for reads anyway)
         */
        template <typename T, typename Kernel>
        inline void block_op_binary_out (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (a == nullptr) || (b == nullptr) || (count == 0))
            {
                return;
            }

            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (a[i], b[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool a_aligned       = Strategy::is_aligned<Alignment> (a + i);
            const bool b_aligned       = Strategy::is_aligned<Alignment> (b + i);

            if (dst_aligned && a_aligned && b_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (a + i), load_aligned<T> (b + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (a + i + Width), load_aligned<T> (b + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (a + i + Width * 2), load_aligned<T> (b + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (a + i + Width * 3), load_aligned<T> (b + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (a + i), load_aligned<T> (b + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (a + i), load_unaligned<T> (b + i)));
                }
            }

            for (; i < count; ++i)
                dst[i] = kernel (a[i], b[i]);
        }

        /************************************************************************************************
          High-Level Block Operations API
        ************************************************************************************************/

        namespace ops
        {
            /** @brief dst[i] += src[i] */
            template <typename T>
            void add (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::AddKernel<T>());
            }

            /** @brief dst[i] -= src[i] */
            template <typename T>
            void sub (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::SubKernel<T>());
            }

            /** @brief dst[i] *= src[i] */
            template <typename T>
            void mul (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::MulKernel<T>());
            }

            /** @brief data[i] *= factor */
            template <typename T>
            void scale (T* CASPI_RESTRICT data, std::size_t count, T factor)
            {
                block_op_inplace (data, count, kernels::ScaleKernel<T> (factor));
            }

            /**
             * @brief dst[i] = src[i]
             *
             * Delegates to std::memcpy, allowing the implementation to use
             * non-temporal stores for large buffers where appropriate.
             */
            template <typename T>
            void copy (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                std::memcpy (dst, src, count * sizeof (T));
            }

            /** @brief dst[i] = value */
            template <typename T>
            void fill (T* CASPI_RESTRICT dst, std::size_t count, T value)
            {
               // probably not beating this.
               std::fill_n (dst, count, value);
            }

            /** @brief dst[i] += src1[i] * src2[i]  (uses FMA when available) */
            template <typename T>
            void mac (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count)
            {
                block_op_ternary (dst, src1, src2, count, kernels::MACKernel<T>());
            }

            /**
             * @brief dst[i] = a[i] + t * (b[i] - a[i])
             *
             * FIX: Previously inlined its own prologue/SIMD/epilogue, duplicating
             *      the logic from block_op_binary_out. Now routes through the shared processor.
             */
            template <typename T>
            void lerp (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, T t, std::size_t count)
            {
                block_op_binary_out (dst, a, b, count, kernels::LerpKernel<T> (t));
            }

            /** @brief data[i] = clamp(data[i], min_val, max_val) */
            template <typename T>
            void clamp (T* CASPI_RESTRICT data, T min_val, T max_val, std::size_t count)
            {
                block_op_inplace (data, count, kernels::ClampKernel<T> (min_val, max_val));
            }

            /**
             * @brief data[i] = |data[i]|  — float specialisation.
             *
             * Bypasses AbsKernel dispatch. Uses SIMD::abs() directly with the same
             * prologue/SIMD/epilogue structure as block_op_inplace, plus NT stores
             * for large buffers and 4x unrolling for small ones.
             */
                        /**
             * @brief data[i] = |data[i]|  — float specialisation.
             *
             * Direct SIMD::abs() call bypasses AbsKernel dispatch overhead.
             * 4x unroll in aligned path. No NT stores: this is RMW.
             */
            inline void abs (float* CASPI_RESTRICT data, std::size_t count)
            {
                if (data == nullptr || count == 0)
                    return;

                constexpr std::size_t Width     = Strategy::min_simd_width<float>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<float>();
                std::size_t i                   = 0;

                const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
                for (; i < prologue_count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }

                const std::size_t simd_end = i + ((count - i) / Width) * Width;
                const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

                if (aligned)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (data + i,           SIMD::abs (load_aligned<float> (data + i)));
                        store_aligned (data + i + Width,   SIMD::abs (load_aligned<float> (data + i + Width)));
                        store_aligned (data + i + Width*2, SIMD::abs (load_aligned<float> (data + i + Width*2)));
                        store_aligned (data + i + Width*3, SIMD::abs (load_aligned<float> (data + i + Width*3)));
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<float> (data + i)));
                    }
                }
                else
                {
                    for (; i < simd_end; i += Width)
                    {
                        store_unaligned (data + i, SIMD::abs (load_unaligned<float> (data + i)));
                    }
                }

                for (; i < count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }
            }

            /**
             * @brief data[i] = |data[i]|  — double specialisation.
             */
            inline void abs (double* CASPI_RESTRICT data, std::size_t count)
            {
                if (data == nullptr || count == 0)
                    return;

                constexpr std::size_t Width     = Strategy::min_simd_width<double>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<double>();
                std::size_t i                   = 0;

                const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
                for (; i < prologue_count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }

                const std::size_t simd_end = i + ((count - i) / Width) * Width;
                const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

                if (aligned)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (data + i,           SIMD::abs (load_aligned<double> (data + i)));
                        store_aligned (data + i + Width,   SIMD::abs (load_aligned<double> (data + i + Width)));
                        store_aligned (data + i + Width*2, SIMD::abs (load_aligned<double> (data + i + Width*2)));
                        store_aligned (data + i + Width*3, SIMD::abs (load_aligned<double> (data + i + Width*3)));
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<double> (data + i)));
                    }
                }
                else
                {
                    for (; i < simd_end; i += Width)
                    {
                        store_unaligned (data + i, SIMD::abs (load_unaligned<double> (data + i)));
                    }
                }

                for (; i < count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }
            }

            // Generic template version (for any T not specialised above) — unchanged
            template <typename T>
            void abs (T* CASPI_RESTRICT data, std::size_t count)
            {
                block_op_inplace (data, count, kernels::AbsKernel<T>());
            }

            /**
             * @brief Minimum element in array. Returns T(0) for empty input.
             *
             * FIX: Prologue was computed from data+1, causing up to Width-1 unnecessary
             *      scalar iterations on aligned buffers. Now computed from data (index 0).
             */
            template <typename T>
            T find_min (const T* data, std::size_t count)
            {
                if (data == nullptr || (count == 0))
                {
                    return T (0);
                }

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                // FIX: align from data (base), not data+1
                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    if (data[i] < result)
                    {
                        result = data[i];
                    }
                }

                if (i + Width <= count)
                {
                    simd_t vmin = set1<T> (result);
                    for (; i + Width <= count; i += Width)
                    {
                        vmin = min (vmin, load<T> (data + i));
                    }
                    result = hmin (vmin);
                }

                for (; i < count; ++i)
                {
                    if (data[i] < result)
                    {
                        result = data[i];
                    }
                }

                return result;
            }

            /**
             * @brief Maximum element in array. Returns T(0) for empty input.
             *
             * FIX: Same prologue alignment fix as find_min.
             */
            template <typename T>
            T find_max (const T* data, std::size_t count)
            {
                if (data == nullptr || (count == 0))
                {
                    return T (0);
                }

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                // FIX: align from data (base), not data+1
                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    if (data[i] > result)
                    {
                        result = data[i];
                    }
                }

                if (i + Width <= count)
                {
                    simd_t vmax = set1<T> (result);
                    for (; i + Width <= count; i += Width)
                    {
                        vmax = max (vmax, load<T> (data + i));
                    }
                    result = hmax (vmax);
                }

                for (; i < count; ++i)
                {
                    if (data[i] > result)
                    {
                        result = data[i];
                    }
                }

                return result;
            }

            /** @brief Sum all elements. */
            template <typename T>
            T sum (const T* data, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    result += data[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));
                    for (; i + Width <= count; i += Width)
                        vsum = SIMD::add (vsum, load<T> (data + i));
                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                {
                    result += data[i];
                }

                return result;
            }

            /** @brief Dot product: sum(a[i] * b[i]). Uses FMA when available. */
            template <typename T>
            T dot_product (const T* a, const T* b, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (a),
                    count);

                for (; i < prologue_end; ++i)
                {
                    result += a[i] * b[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));
                    for (; i + Width <= count; i += Width)
                        vsum = mul_add (load<T> (a + i), load<T> (b + i), vsum);
                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                    result += a[i] * b[i];

                return result;
            }

        } // namespace ops
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_SIMD_H