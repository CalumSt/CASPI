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

* @file caspi_LoadStore.h
* @author CS Islay
* @brief SIMD load and store operations with alignment handling.
*
*
* OVERVIEW
*
* Provides functions for loading from and storing to memory with automatic
* or explicit alignment handling:
*   - load_aligned: Load from aligned memory (faster but requires alignment)
*   - load_unaligned: Load from any memory location
*   - load: Auto-detects alignment and chooses optimal path
*   - store_aligned: Store to aligned memory
*   - store_unaligned: Store to any memory location
*   - store: Auto-detects alignment
*   - stream_store: Non-temporal (streaming) stores for large buffers
*   - store_fence: Memory fence for ensuring NT stores complete
*
*
* USAGE EXAMPLES
*
* @code
* // Aligned load/store (fastest when data is aligned)
* float32x4 v = load_aligned<float>(aligned_data);
* store_aligned(output_ptr, v);
*
* // Unaligned load/store (works with any pointer)
* float32x4 v = load_unaligned<float>(unaligned_ptr);
* store_unaligned(output_ptr, v);
*
* // Auto-detect alignment
* float32x4 v = load<float>(data_ptr);  // Picks best option automatically
* store(data_ptr, v);
*
* // Non-temporal stores for large buffers (bypasses cache)
* for (size_t i = 0; i < count; i += 4) {
*     stream_store(output + i, load_aligned<float>(input + i));
* }
* store_fence();  // Required after NT stores
* @endcode
*
*
* ALIGNMENT REQUIREMENTS
*
* Platform    | Aligned | Unaligned | Stream Store
* ------------|---------|-----------|-------------
* SSE/NEON   | 16-byte | Any       | 16-byte
* AVX        | 32-byte | Any       | 32-byte
* WASM       | 16-byte | Any       | N/A
* Scalar     | 1-byte  | Any       | N/A
*
*
* PERFORMANCE NOTES
*
* - Always prefer aligned loads/stores when data is guaranteed to be aligned
* - Use auto-alignment (load/store) for ad-hoc access
* - Block operations handle alignment internally with prologue/epilogue
* - Use stream_store for large buffers (> L1 cache) to avoid cache pollution
* - Always call store_fence() after stream_store sequences
*
 ************************************************************************/

#ifndef CASPI_LOADSTORE_H
#define CASPI_LOADSTORE_H

#include "caspi_Strategy.h"

#include <cstring>

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Load a 128-bit vector from aligned memory.
         *
         * @tparam T         Scalar type (float or double)
         * @param p          Pointer to aligned memory
         * @return           Loaded vector
         *
         * @warning Undefined behavior if pointer is not properly aligned.
         *
         * @code
         * alignas(16) float data[4];
         * float32x4 v = load_aligned<float>(data);
         * @endcode
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load_aligned (const T* p);

        /**
         * @brief Load a 128-bit float vector from aligned memory.
         *
         * @param p          Pointer to 16-byte aligned memory
         * @return           Loaded 4-lane float vector
         */
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

        /**
         * @brief Load a 128-bit double vector from aligned memory.
         *
         * @param p          Pointer to 16-byte aligned memory
         * @return           Loaded 2-lane double vector
         */
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
         * @brief Load a 128-bit vector from unaligned memory.
         *
         * @tparam T         Scalar type (float or double)
         * @param p          Pointer to memory (any alignment)
         * @return           Loaded vector
         *
         * @note Works with any alignment but may be slower than aligned load.
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load_unaligned (const T* p);

        /**
         * @brief Load a 128-bit float vector from unaligned memory.
         *
         * @param p          Pointer to memory (any alignment)
         * @return           Loaded 4-lane float vector
         */
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

        /**
         * @brief Load a 128-bit double vector from unaligned memory.
         *
         * @param p          Pointer to memory (any alignment)
         * @return           Loaded 2-lane double vector
         */
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
         * @brief Load vector with automatic alignment detection.
         *
         * Automatically chooses between aligned and unaligned loads based on
         * the pointer's alignment. Use this for ad-hoc loads; block
         * processors hoist the check for better performance.
         *
         * @tparam T         Scalar type (float or double)
         * @param p          Pointer to memory
         * @return           Loaded vector
         *
         * @code
         * float32x4 v = load<float>(data_ptr);  // Auto-detects alignment
         * @endcode
         */
        template <typename T>
        typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type
            load (const T* p)
        {
            constexpr std::size_t alignment = Strategy::simd_alignment<T>();
            return Strategy::is_aligned<alignment> (p) ? load_aligned<T> (p)
                                                       : load_unaligned<T> (p);
        }

        /**
         * @brief Store a 128-bit float vector to aligned memory.
         *
         * @param p          Pointer to 16-byte aligned memory
         * @param v          Vector to store
         *
         * @warning Undefined behavior if pointer is not properly aligned.
         */
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

        /**
         * @brief Store a 128-bit double vector to aligned memory.
         *
         * @param p          Pointer to 16-byte aligned memory
         * @param v          Vector to store
         *
         * @warning Undefined behavior if pointer is not properly aligned.
         *
         * @example
         * @code
         * alignas(16) double buf[2];
         * float64x2 v = set1<double>(1.234);
         * store_aligned(buf, v);
         * @endcode
         */
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

        /**
         * @brief Store a 128-bit float vector to unaligned memory.
         *
         * @param p          Pointer to memory (any alignment)
         * @param v          Vector to store
         *
         * @note This variant accepts any pointer alignment but may be slower
         *       than the aligned version on platforms where unaligned accesses
         *       are penalised.
         *
         * @example
         * @code
         * float tmp[4];
         * float32x4 v = set1<float>(0.5f);
         * store_unaligned(tmp + 1, v); // safe even if tmp+1 is unaligned
         * @endcode
         */
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

        /**
         * @brief Store a 128-bit double vector to unaligned memory.
         *
         * @param p          Pointer to memory (any alignment)
         * @param v          Vector to store
         *
         * @note Prefer store_aligned when you can guarantee alignment for best throughput.
         */
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

        /**
         * @brief Store vector with automatic alignment detection (float).
         *
         * Chooses store_aligned or store_unaligned based on pointer alignment.
         *
         * @param p          Pointer to memory
         * @param v          Vector to store
         *
         * @note For repeated hot-path stores prefer explicitly using aligned
         *       APIs and ensuring buffer alignment instead of relying on this helper.
         */
        inline void store (float* p, float32x4 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<float>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /**
         * @brief Store double vector with automatic alignment detection.
         *
         * @param p          Pointer to memory
         * @param v          Vector to store
         */
        inline void store (double* p, float64x2 v)
        {
            Strategy::is_aligned<Strategy::simd_alignment<double>()> (p)
                ? store_aligned (p, v)
                : store_unaligned (p, v);
        }

        /**
         * @brief Non-temporal (streaming) store for 128-bit float vector.
         *
         * Bypasses the cache, writing directly to memory. Use for large
         * buffers where data won't be reused (e.g., audio output).
         *
         * @param p          Pointer to 16-byte aligned memory
         * @param v          Vector to store
         *
         * @note Requires 16-byte alignment.
         * @note Always follow with store_fence() before reading the data.
         *
         * @code
         * for (size_t i = 0; i < count; i += 4) {
         *     stream_store(output + i, load_aligned<float>(input + i));
         * }
         * store_fence();  // Required!
         * @endcode
         */
        inline void stream_store (float* p, float32x4 v) noexcept
        {
#if defined(CASPI_HAS_SSE)
            _mm_stream_ps (p, v);
#else
            store_aligned (p, v);
#endif
        }

        /**
         * @brief Non-temporal (streaming) store for 128-bit double vector.
         *
         * Bypasses the cache, writing directly to memory. Use for large buffers
         * where the written data will not be reused soon.
         *
         * @param p          Pointer to 16-byte aligned memory
         * @param v          Vector to store
         *
         * @note On platforms without non-temporal store intrinsics this falls back
         *       to a normal aligned store.
         *
         * @example
         * @code
         * for (size_t i = 0; i < count; i += 2) {
         *     stream_store(output + i, load_aligned<double>(input + i));
         * }
         * store_fence(); // make sure NT stores are globally visible
         * @endcode
         */
        inline void stream_store (double* p, float64x2 v) noexcept
        {
#if defined(CASPI_HAS_SSE2)
            _mm_stream_pd (p, v);
#else
            store_aligned (p, v);
#endif
        }

        /**
         * @brief Store fence - ensures all prior non-temporal stores are visible.
         *
         * Must be called after any sequence of stream_store() calls before
         * subsequent reads of the written region by any thread.
         *
         * @note On non-SSE targets (NEON, WASM), this is a no-op.
         *
         * @example
         * @code
         * stream_store(...);
         * stream_store(...);
         * store_fence();
         * // Now safe to read the region from another thread
         * @endcode
         */
        inline void store_fence() noexcept
        {
#if defined(CASPI_HAS_SSE)
            _mm_sfence();
#endif
        }
    } // namespace SIMD
} // namespace CASPI
#endif // CASPI_LOADSTORE_H
