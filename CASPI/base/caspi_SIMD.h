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

#include "SIMD/caspi_Intrinsics.h"
#include "SIMD/caspi_Strategy.h"
#include "SIMD/caspi_LoadStore.h"
#include "SIMD/caspi_Operations.h"
#include "SIMD/caspi_AVX.h"
#include "SIMD/caspi_Complex.h"
#include "SIMD/caspi_Blocks.h"

#endif // CASPI_SIMD_H