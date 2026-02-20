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

* @file caspi_Strategy.h
* @author CS Islay
* @brief Compile-time SIMD strategy and trait definitions.
*
* Provides compile-time traits and utilities for SIMD operations:
*   - Type mappings (scalar to SIMD vector)
*   - Minimum SIMD widths per type
*   - Alignment checking and calculations
*   - Non-temporal store thresholds based on cache size
*
* These traits are used internally by block operations and help adapt
* SIMD code to different platform capabilities.
*
* USAGE
*
* @code
* // Get the SIMD type for a given scalar type
* using SimdVec = CASPI::SIMD::Strategy::simd_type<float, 4>::type;
*
* // Check alignment
* bool aligned = CASPI::SIMD::Strategy::is_aligned<16>(data_ptr);
*
* // Get SIMD alignment requirement
* constexpr size_t align = CASPI::SIMD::Strategy::simd_alignment<float>();
*
* // Get minimum SIMD width
* constexpr size_t width = CASPI::SIMD::Strategy::min_simd_width<float>::value;
* @endcode
*
*
* CACHE CONFIGURATION
*
* The L1 cache size is used to determine when non-temporal stores are beneficial.
* Override at compile time:
*   -DCASPI_L1_CACHE_BYTES=32768 (default: 32KB)
*
 ************************************************************************/

#ifndef CASPI_STRATEGY_H
#define CASPI_STRATEGY_H

#include "base/caspi_Assert.h"
#include "caspi_Intrinsics.h"

#include <cstddef>
#include <cstdint>

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Compile-time strategy namespace for SIMD traits.
         *
         * Contains type traits and utility functions for determining optimal
         * SIMD processing parameters at compile time.
         */
        namespace Strategy
        {
            /**
             * @brief Maps a scalar type and lane count to the corresponding SIMD vector type.
             *
             * Specializations exist for:
             *   - float with 4 lanes  → float32x4
             *   - double with 2 lanes → float64x2
             *   - float with 8 lanes  → float32x8 (AVX only)
             *   - double with 4 lanes → float64x4 (AVX only)
             *
             * @tparam T         Scalar type (float or double)
             * @tparam Width     Number of SIMD lanes
             */
            template <typename T, std::size_t Width>
            struct simd_type;

            /**
             * @brief SIMD type for 4-lane float vectors.
             */
            template <>
            struct simd_type<float, 4>
            {
                    using type = float32x4;
            };
            /**
             * @brief SIMD type for 2-lane double vectors.
             */
            template <>
            struct simd_type<double, 2>
            {
                    using type = float64x2;
            };

#if defined(CASPI_HAS_AVX)
            /**
             * @brief SIMD type for 8-lane float vectors (AVX).
             */
            template <>
            struct simd_type<float, 8>
            {
                    using type = float32x8;
            };
            /**
             * @brief SIMD type for 4-lane double vectors (AVX).
             */
            template <>
            struct simd_type<double, 4>
            {
                    using type = float64x4;
            };
#endif

            /**
             * @brief Minimum SIMD width for a given scalar type.
             *
             * Returns the smallest practical SIMD width available for the type.
             * Used as default when automatic detection is needed.
             *
             * @tparam T     Scalar type (float or double)
             *
             * Specializations:
             *   - float  → 4 lanes (SSE/NEON/WASM) or 1 (scalar fallback)
             *   - double → 2 lanes (SSE2/NEON64/WASM) or 1 (scalar fallback)
             */
            template <typename T>
            struct min_simd_width
            {
                    static constexpr std::size_t value = 1;
            };

            /**
             * @brief Minimum SIMD width for float (4 with SIMD, 1 scalar).
             */
            template <>
            struct min_simd_width<float>
            {
#if defined(CASPI_HAS_SSE) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_WASM_SIMD)
                    static constexpr std::size_t value = 4;
#else
                    static constexpr std::size_t value = 1;
#endif
            };

            /**
             * @brief Minimum SIMD width for double (2 with SIMD, 1 scalar).
             */
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
             * @brief Check if a pointer is aligned to a specified number of bytes.
             *
             * @tparam N         Alignment boundary (must be power of 2)
             * @param ptr        Pointer to check
             * @return           true if pointer is aligned to N bytes
             *
             * @code
             * float* data = ...;
             * if (Strategy::is_aligned<16>(data)) {
             *     // Use aligned loads
             * }
             * @endcode
             */
            template <std::size_t N>
            inline bool is_aligned (const void* ptr) noexcept
            {
                return (reinterpret_cast<std::uintptr_t> (ptr) % N) == 0;
            }

            /**
             * @brief Calculate the number of elements until the next alignment boundary.
             *
             * Used internally for prologue calculations in block operations.
             *
             * @tparam Alignment     Desired alignment in bytes
             * @tparam T             Element type
             * @param ptr            Pointer to check
             * @return               Number of elements to advance to reach alignment
             *
             * @code
             * float* data = unaligned_ptr;
             * size_t prologue = Strategy::samples_to_alignment<16>(data);
             * // Process 'prologue' elements scalar, then use SIMD
             * @endcode
             */
            template <std::size_t Alignment, typename T>
            inline std::size_t samples_to_alignment (const T* ptr) noexcept
            {
                const auto addr                   = reinterpret_cast<std::uintptr_t> (ptr);
                const std::uintptr_t misalignment = addr % Alignment;

                if (misalignment == 0)
                {
                    return 0;
                }

                const std::uintptr_t bytes_to_align = Alignment - misalignment;

                return (bytes_to_align + sizeof (T) - 1) / sizeof (T);
            }

            /**
             * @brief Get preferred SIMD alignment for a given scalar type.
             *
             * @tparam T         Scalar type
             * @return           Alignment in bytes:
             *                     - 32 for AVX
             *                     - 16 for SSE/NEON/WASM
             *                     - alignof(T) for scalar fallback
             *
             * @code
             * constexpr size_t float_align = Strategy::simd_alignment<float>();   // 16 or 32
             * constexpr size_t double_align = Strategy::simd_alignment<double>(); // 16 or 32
             * @endcode
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
             * @brief L1 data cache size in bytes.
             *
             * Used for non-temporal store threshold calculations.
             * Defaults to 32KB (smallest L1 on modern x86/ARM).
             *
             * Override at compile time:
             *   -DCASPI_L1_CACHE_BYTES=32768
             *
             * @note The NT threshold is set to 2x L1 so that a working set that
             *       no longer fits in L1 does not pollute it with streaming writes.
             */
#if ! defined(CASPI_L1_CACHE_BYTES)
#define CASPI_L1_CACHE_BYTES 32768
#endif

            static constexpr std::size_t L1_CACHE_BYTES = CASPI_L1_CACHE_BYTES;

            /**
             * @brief Number of elements above which non-temporal stores are preferred.
             *
             * At this point the working set has overflowed L1 and temporal stores
             * would cause unnecessary cache pollution.
             *
             * @tparam T         Element type
             * @return           Element count threshold
             *
             * @code
             * size_t threshold = Strategy::nt_store_threshold<float>();  // ~16384 for 32KB L1
             * if (count > threshold) {
             *     // Use stream_store for better performance
             * }
             * @endcode
             */
            template <typename T>
            constexpr std::size_t nt_store_threshold() noexcept
            {
                return (2 * L1_CACHE_BYTES) / sizeof (T);
            }
        } // namespace Strategy
    } // namespace SIMD
} // namespace CASPI
#endif // CASPI_STRATEGY_H
