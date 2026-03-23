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
* QUICK DEV GUIDE TO SIMD (CASPI)
* ============================================================================
*
* This guide summarises the practical patterns used when developing with the
* CASPI SIMD API. It complements the architecture overview already present
* above by focusing on developer workflows, typical problems and common
* performance pitfalls.
*
* 1) Prefer block operations (SIMD::ops::*) for array processing
*    - They handle prologue/aligned SIMD/epilogue automatically.
*    - They choose aligned/unaligned code paths and may use non-temporal
*      stores when appropriate (see Strategy::nt_store_threshold).
*    - Example: SIMD::ops::add(dst, src, n);
*
* 2) Writing new kernels
*    - Implement a small functor with both a SIMD operator() and a scalar
*      operator() overload. Example kernels exist in SIMD::kernels (AddKernel,
*      ScaleKernel, MACKernel, PolyKernel, etc.).
*    - Use set1<T>(), load<T>(), store() and other primitives inside your
*      kernel to remain portable.
*
*    Minimal kernel template:
*    @code
*    struct MyKernel {
*      using simd_type = Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
*      simd_type operator()(simd_type a, simd_type b) const { return add(a,b); }
*      T operator()(T a, T b) const { return a + b; }
*    };
*    SIMD::block_op_binary(dst, src, count, MyKernel());
*    @endcode
*
* 3) Strategy / portability
*    - Query compile-time traits via Strategy::min_simd_width<T> and
*      Strategy::simd_alignment<T>().
*    - Use Strategy::is_aligned<N>(ptr) and Strategy::samples_to_alignment<N>(ptr)
*      inside hot-path code if you need to implement custom prologue/epilogue.
*    - Use Strategy::nt_store_threshold_runtime<T>() to decide when streaming
*      (non-temporal) stores are beneficial at runtime.
*
* 4) Correctness vs performance trade-offs
*    - mul_add() uses FMA when available; using it improves both speed and
*      numerical accuracy (single rounding instead of two).
*    - rcp()/rsqrt() are fast approximations with ~0.15% error; only use
*      where tolerance allows.
*    - Horizontal reductions (hsum/hmax/hmin) are more expensive than
*      lane-wise ops; keep accumulation in SIMD registers if possible.
*
* 5) Alignment and streaming rules
*    - load<T>/store() auto-select aligned/unaligned variants. For maximum
*      throughput, place performance-sensitive buffers on proper alignment:
*        alignas(16) for SSE/NEON/WASM, alignas(32) for AVX.
*    - Call store_fence() after any sequence of stream_store() calls.
*
* 6) Debugging tips
*    - To check runtime SIMD availability use feature macros (CASPI_HAS_*)
*      or fall back to Strategy::min_simd_width<T> and L1 probe helpers.
*    - When inspecting SIMD results, store vectors to a local scalar array:
*      float tmp[4]; SIMD::store(tmp, vec); then log tmp[].
*
* 7) Common pitfalls
*    - Do not assume stream_store() is faster for small buffers — streaming
*      stores aim to avoid polluting caches and usually help only when the
*      working set exceeds L1 (see nt_store_threshold_runtime()).
*    - When writing multi-threaded code, ensure memory visibility after NT
*      stores by using store_fence(); also ensure proper synchronisation when
*      multiple threads write the same memory region.
*
* 8) Quick cross-reference
*    - Low-level primitives: SIMD::set1, load, store, add, mul, mul_add, min, max
*    - Kernels: SIMD::kernels::{AddKernel, ScaleKernel, MACKernel, ...}
*    - Block ops: SIMD::ops::{add, mul, scale, fill, copy_with_gain, ...}
*
* ============================================================================
* EXAMPLES (patterns)
* ============================================================================
*
* 1) Fast in-place scale:
* @code
* SIMD::ops::scale(data, count, 0.5f); // in-place
* @endcode
*
* 2) Mixed read/write (accumulate with gain):
* @code
* SIMD::ops::accumulate_with_gain(dst, src, count, gain);
* @endcode
*
* 3) Implementing a custom block operation:
* @code
* struct MyKernel { ... };  // implement SIMD and scalar operator()
* SIMD::block_op_binary<float>(dst, src, count, MyKernel());
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

