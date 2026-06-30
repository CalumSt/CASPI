#ifndef CASPI_DENORMALS_H
#define CASPI_DENORMALS_H
/*************************************************************************
 *  .d8888b.                             d8b
 * d88P  Y88b                            Y8P
 * 888    888
 * 888         8888b.  .d8888b  88888b.  888
 * 888            "88b 88K      888 "88b 888
 * 888    888 .d888888 "Y8888b. 888  888 888
 * Y88b  d88P 888  888      X88 888 d88P 888
 *  "Y8888P"  "Y888888  88888P' 88888P"  888
 *                              888
 *                              888
 *                              888
 *
 * @file   core/caspi_Denormals.h
 * @author CS Islay
 * @brief  Denormal number handling utilities.
 *
 * @details
 * Denormal (subnormal) floating-point numbers can cause severe CPU stalls
 * on x86 hardware — up to 100x slower than normal floats on some
 * microarchitectures. This header provides three strategies:
 *
 * ### ScopedFlushDenormals
 * RAII guard. Sets the MXCSR Flush-to-Zero (FZ) and Denormals-Are-Zero (DAZ)
 * bits for the duration of the scope, then restores the original MXCSR.
 * No-op on non-x86 platforms.
 * @code
 *   void processBlock(...) {
 *       CASPI::Core::ScopedFlushDenormals flush;
 *       // All denormals in this scope become 0.
 *   }
 * @endcode
 *
 * ### configureFlushToZero(bool)
 * Permanent (non-scoped) MXCSR write. Use for process-global configuration.
 *
 * ### flushToZeroScalar<F>(value, threshold)
 * Portable scalar flush: multiplies by 0 if |value| < threshold.
 * Useful on non-x86 or when you want to flush a specific value without
 * touching MXCSR. Define CASPI_DISABLE_FLUSH_DENORMALS to make it a no-op.
 *
 * ### Platform detection
 * FZ and DAZ support is detected via CASPI_FEATURES_HAS_FLUSH_ZERO and
 * CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS (set in caspi_Features.h).
 * On platforms without SSE, all three utilities degrade gracefully.
 ************************************************************************/

#include "base/caspi_Features.h"

#include <cmath>
#include <type_traits>

#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
#include <xmmintrin.h>
#endif

namespace CASPI
{
namespace Core
{

/**
 * @brief Enable or disable hardware Flush-to-Zero mode.
 *
 * When enabled, the CPU flushes denormal results to zero rather than
 * trapping or entering the slow microcode denormal path. Also enables
 * Denormals-Are-Zero (DAZ) if supported, which treats denormal inputs
 * as zero before arithmetic.
 *
 * @param enable  true to enable FZ+DAZ, false to restore default IEEE behaviour.
 */
inline void configureFlushToZero (bool enable)
{
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS)
    if (enable)
        _mm_setcsr (_mm_getcsr() | (1u << 15) | (1u << 6));
    else
        _mm_setcsr (_mm_getcsr() & ~((1u << 15) | (1u << 6)));
#elif defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
    if (enable)
        _mm_setcsr (_mm_getcsr() | (1u << 15));
    else
        _mm_setcsr (_mm_getcsr() & ~(1u << 15));
#else
    (void) enable;
#endif
}

/**
 * @brief Return @p value flushed to zero if its magnitude is below @p threshold.
 *
 * Branchless: `value * (|value| >= threshold)`.
 * Suitable for per-sample use in IIR filter state, leaky integrators, etc.
 * Define CASPI_DISABLE_FLUSH_DENORMALS to make this a no-op identity function.
 *
 * @tparam FloatType  float, double, or long double.
 * @param  value      Value to conditionally flush.
 * @param  threshold  Magnitude below which the value is zeroed. Default 1e-15.
 * @return            @p value if |value| >= threshold, else 0.
 */
template <typename FloatType>
inline FloatType flushToZeroScalar (FloatType value,
                                    FloatType threshold = FloatType (1e-15))
{
    static_assert (std::is_floating_point<FloatType>::value,
                   "flushToZeroScalar requires a floating-point type");
#if defined(CASPI_DISABLE_FLUSH_DENORMALS)
    (void) threshold;
    return value;
#else
    return value * static_cast<FloatType> (std::abs (value) >= threshold);
#endif
}

/**
 * @brief RAII guard: enables hardware Flush-to-Zero for the current scope.
 *
 * Saves the current MXCSR value on construction, enables FZ and DAZ
 * (if supported), and restores the original MXCSR on destruction.
 * Not movable or copyable.
 *
 * Place at the top of any audio callback or block-processing function:
 * @code
 *   void process (AudioBuffer& buf) noexcept
 *   {
 *       CASPI::Core::ScopedFlushDenormals flush;
 *       // Denormals treated as 0 for the duration of this function.
 *       for (int i = 0; i < buf.numSamples(); ++i) { ... }
 *   }
 * @endcode
 *
 * @note On non-x86 platforms (ARM, WASM, etc.) this class is a no-op.
 *       The constructor and destructor are trivial; the compiler elides them.
 */
class ScopedFlushDenormals
{
public:
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO)

    ScopedFlushDenormals() noexcept
        : savedMxcsr (_mm_getcsr())
    {
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS)
        _mm_setcsr (savedMxcsr | (1u << 15) | (1u << 6)); // FZ | DAZ
#else
        _mm_setcsr (savedMxcsr | (1u << 15));              // FZ only
#endif
    }

    ~ScopedFlushDenormals() noexcept
    {
        _mm_setcsr (savedMxcsr);
    }
#else
    ScopedFlushDenormals()  noexcept = default;
    ~ScopedFlushDenormals() noexcept = default;
#endif

    ScopedFlushDenormals (const ScopedFlushDenormals&)            = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
#if defined(CASPI_FEATURES_HAS_FLUSH_ZERO)
    unsigned int savedMxcsr;
#endif
};

} // namespace Core
} // namespace CASPI

#endif // CASPI_DENORMALS_H