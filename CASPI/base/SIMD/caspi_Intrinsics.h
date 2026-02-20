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

* @file caspi_Intrinsics.h
* @author CS Islay
* @brief SIMD vector type definitions for cross-platform support.
*
*
* OVERVIEW
*
* Provides portable type aliases for SIMD vectors across multiple platforms:
*   - x86/x64: SSE, SSE2 (via __m128, __m128d)
*   - ARM: NEON, NEON64 (via float32x4_t, float64x2_t)
*   - WebAssembly: WASM SIMD (via v128_t)
*   - Fallback: Plain structs with array storage
*
* USAGE
*
* @code
* // Automatic selection based on platform capabilities
* CASPI::SIMD::float32x4 v1 = CASPI::SIMD::set1<float>(1.0f);
* CASPI::SIMD::float64x2 v2 = CASPI::SIMD::set1<double>(2.0);
* @endcode
*
*
* PLATFORM DETECTION
*
* These types are selected based on preprocessor macros defined in caspi_Features.h:
*   - CASPI_HAS_SSE   : 128-bit float (4 lanes)
*   - CASPI_HAS_SSE2  : 128-bit double (2 lanes)
*   - CASPI_HAS_NEON  : ARM NEON float (4 lanes)
*   - CASPI_HAS_NEON64: ARM NEON64 double (2 lanes)
*   - CASPI_HAS_WASM_SIMD: WebAssembly 128-bit (configurable lanes)
*
*
* VECTOR TYPES
*
* +---------------+--------+------+-------+--------+
* | Type          | Width  | Bits | Lanes | Native |
* +---------------+--------+------+-------+--------+
* | float32x4     | 128    | 32   | 4     | SSE    |
* | float64x2     | 128    | 64   | 2     | SSE2   |
* +---------------+--------+------+-------+--------+
*
* On platforms without SIMD support, these are replaced with plain struct
* wrappers containing scalar arrays, maintaining API compatibility.
*
 ************************************************************************/

#ifndef CASPI_SIMDINTRINSICS_H
#define CASPI_SIMDINTRINSICS_H

#include "base/caspi_Features.h"
#include "base/caspi_Platform.h"

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
         * @brief 4-lane single-precision floating-point vector.
         *
         * Represents a 128-bit vector containing four 32-bit float values.
         * Used for processing audio samples with SIMD acceleration on SSE,
         * NEON, or WebAssembly targets.
         *
         * @code
         * // Example: Creating and using float32x4
         * float32x4 v = set1<float>(2.0f);  // Broadcast 2.0 to all lanes: [2,2,2,2]
         * @endcode
         */
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

        /**
         * @brief 2-lane double-precision floating-point vector.
         *
         * Represents a 128-bit vector containing two 64-bit double values.
         * Used for processing audio samples with higher precision or for
         * complex number operations on SSE2, NEON64, or WebAssembly targets.
         *
         * @code
         * // Example: Creating and using float64x2
         * float64x2 v = set1<double>(1.5);  // Broadcast 1.5 to all lanes: [1.5,1.5]
         * @endcode
         */
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
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_SIMDINTRINSICS_H
