#ifndef CASPI_FFT_H
#define CASPI_FFT_H
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

* @file caspi_FFT.h
* @author CS Islay
* @brief FFT related functionality and processing classes.
*
* ALGORITHM
* =========
* Cooley-Tukey radix-2 DIT (Decimation-In-Time), iterative.
*   - No recursion stack; O(N log N) time, O(N) space.
*   - Input permuted to bit-reversed order before butterfly passes.
*
* SIMD STRATEGY
* =============
* Each std::complex<double> = 16 bytes = one float64x2 register.
* The butterfly:
*   t        = complex_mul(twiddle, odd)
*   new_even = even + t
*   new_odd  = even - t
*
* Platform selection is fully delegated to CASPI::SIMD via the complex
* arithmetic block (caspi_SIMD_complex.h). The butterfly loop contains
* no platform #ifdefs: it calls only CASPI::SIMD::complex_mul,
* interleave_lo, negate_imag, load_unaligned, store_unaligned.
*
* Twiddle register construction:
*   interleave_lo(set1<double>(wr), set1<double>(wi)) → [wr, wi]
* This replaces the SSE2-only _mm_set_pd and works on all platforms.
*
* Platform coverage:
*   SSE2+SSE3 : complex_mul uses _mm_addsub_pd
*   SSE2 only : complex_mul uses shuffle blend
*   NEON64    : complex_mul uses negate_imag + add
*   WASM SIMD : complex_mul uses negate_imag + add
*   Scalar    : portable fallback throughout
*
* TWIDDLE TABLE
* =============
* Precomputed once in prepare() as parallel re[]/im[] arrays (N-1 entries).
* Indexed by [stage_offset + k]. Avoids sin/cos in the butterfly inner loop.
* IFFT: conjugate via negate_imag() at butterfly time — no second table.
*
* References:
*   [1] Cooley & Tukey (1965). Math. Comp. 19(90), 297-301.
*       https://doi.org/10.1090/S0025-5718-1965-0178586-1
*   [2] Frigo & Johnson (2005). Proc. IEEE 93(2), 216-231.
*       https://doi.org/10.1109/JPROC.2004.840301
*   [3] Van Loan (1992). Computational Frameworks for the Fast Fourier
*       Transform. SIAM. (FLOP count: 5*N*log2(N), §1.4)
************************************************************************/

#include <complex>
#include <vector>
#include <cmath>
#include <stdexcept>
#include "base/caspi_Constants.h"
#include "base/caspi_Assert.h"
#include "base/caspi_SIMD.h"   // provides float64x2, CASPI_HAS_*, add/sub/mul,
                          // set1, load_unaligned, store_unaligned, and the
                          // complex arithmetic block at the bottom

namespace CASPI
{

// ============================================================================
// Type Aliases
// ============================================================================

using Complex = std::complex<double>;
using CArray  = std::vector<Complex>;

// ============================================================================
// FFTConfig
// ============================================================================

struct FFTConfig
{
    size_t size       = 256;
    double sampleRate = 44100.0;

    bool isValid() const { return size > 0 && (size & (size - 1)) == 0; }

    double getFrequencyResolution() const
    {
        return sampleRate / static_cast<double>(size);
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

inline bool isPowerOfTwo (size_t n)   { return n > 0 && (n & (n - 1)) == 0; }

inline size_t nextPowerOfTwo (size_t n)
{
    if (n == 0) return 1;
    n--;
    n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    n |= n >> 32;
#endif
    return ++n;
}

/**
 * @brief Bit-reversal permutation in-place. O(N), single pass.
 * Required by DIT-FFT: input must be in bit-reversed order before butterflies.
 * Reference: [1] §2
 */
inline void bitReversalPermutation (CArray& data)
{
    const size_t N = data.size();
    size_t j = 0;
    for (size_t i = 1; i < N; ++i)
    {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (data[i], data[j]);
    }
}

inline std::vector<double> generateFrequencyBins (size_t fftSize, double sampleRate)
{
    const double fpb = sampleRate / static_cast<double>(fftSize);
    std::vector<double> bins (fftSize / 2);
    for (size_t i = 0; i < fftSize / 2; ++i)
        bins[i] = fpb * static_cast<double>(i);
    return bins;
}

inline CArray realToComplex (const std::vector<double>& real)
{
    CArray out (real.size());
    for (size_t i = 0; i < real.size(); ++i)
        out[i] = Complex (real[i], 0.0);
    return out;
}

inline std::vector<double> getMagnitude (const CArray& fftData)
{
    std::vector<double> mag (fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
        mag[i] = std::abs (fftData[i]);
    return mag;
}

inline std::vector<double> getPower (const CArray& fftData)
{
    std::vector<double> power (fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
    {
        const double m = std::abs (fftData[i]);
        power[i] = m * m;
    }
    return power;
}

inline std::vector<double> getPhase (const CArray& fftData)
{
    std::vector<double> phase (fftData.size());
    for (size_t i = 0; i < fftData.size(); ++i)
        phase[i] = std::arg (fftData[i]);
    return phase;
}

// ============================================================================
// Windowing Functions
// ============================================================================

inline std::vector<double> applyHannWindow (const std::vector<double>& samples)
{
    std::vector<double> out (samples.size());
    const double N = static_cast<double>(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (2.0 * CASPI::Constants::PI<double> * i / (N - 1.0)));
        out[i] = samples[i] * w;
    }
    return out;
}

inline std::vector<double> applyHammingWindow (const std::vector<double>& samples)
{
    std::vector<double> out (samples.size());
    const double N = static_cast<double>(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const double w = 0.54 - 0.46 * std::cos (2.0 * CASPI::Constants::PI<double> * i / (N - 1.0));
        out[i] = samples[i] * w;
    }
    return out;
}

inline std::vector<double> applyBlackmanWindow (const std::vector<double>& samples)
{
    std::vector<double> out (samples.size());
    const double N  = static_cast<double>(samples.size());
    constexpr double a0 = 0.42, a1 = 0.5, a2 = 0.08;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const double w = a0
            - a1 * std::cos (2.0 * CASPI::Constants::PI<double> * i / (N - 1.0))
            + a2 * std::cos (4.0 * CASPI::Constants::PI<double> * i / (N - 1.0));
        out[i] = samples[i] * w;
    }
    return out;
}

// ============================================================================
// Twiddle Factor Table
// ============================================================================

/**
 * @brief Precomputed forward twiddle factor table.
 *
 * Stores W_N^k = e^{-2πi k / len} for all butterfly positions.
 *
 * Layout: stage s (len=2^{s+1}, halfLen=len/2) occupies halfLen entries
 * starting at the cumulative offset of all prior stages.
 *
 *   Stage 0: len=2,  halfLen=1  →  1 entry  at offset 0
 *   Stage 1: len=4,  halfLen=2  →  2 entries at offset 1
 *   ...
 *   Total = 1+2+4+...+N/2 = N-1 entries.
 *
 * Parallel re[]/im[] arrays: butterfly constructs the twiddle register as
 *   interleave_lo(set1<double>(wr), set1<double>(wi)) → [wr, wi]
 * This is platform-agnostic and needs no memcpy or _mm_set_pd.
 *
 * IFFT: negate_imag() applied at butterfly time — no second table needed.
 */
struct TwiddleTable
{
    std::vector<double> re;
    std::vector<double> im;
    size_t fftSize = 0;

    bool isValid() const { return fftSize >= 2 && re.size() == fftSize - 1; }
};

/** @brief Compute twiddle table. Cost: O(N) sin/cos evaluations. */
inline TwiddleTable computeTwiddleTable (size_t N)
{
    CASPI_ASSERT (isPowerOfTwo (N) && N >= 2, "FFT size must be power of 2 and >= 2");

    TwiddleTable table;
    table.fftSize = N;
    table.re.resize (N - 1);
    table.im.resize (N - 1);

    size_t offset = 0;
    for (size_t len = 2; len <= N; len <<= 1)
    {
        const size_t halfLen = len >> 1;
        for (size_t k = 0; k < halfLen; ++k)
        {
            const double angle = -2.0 * CASPI::Constants::PI<double>
                                 * static_cast<double>(k) / static_cast<double>(len);
            table.re[offset + k] = std::cos (angle);
            table.im[offset + k] = std::sin (angle);
        }
        offset += halfLen;
    }
    return table;
}

// ============================================================================
// Iterative DIT-FFT Core
// ============================================================================

/**
 * @brief Iterative Cooley-Tukey DIT butterfly kernel.
 *
 * Precondition: data is in bit-reversed order.
 *
 * SIMD path (all platforms via CASPI::SIMD):
 *   std::complex<double> is layout-compatible with [re:double, im:double]
 *   (guaranteed by the C++ standard; §26.4). Each complex occupies 16 bytes.
 *
 *   Twiddle construction (platform-agnostic):
 *     interleave_lo(set1<double>(wr), set1<double>(wi)) → [wr, wi]
 *   No _mm_set_pd, no memcpy, works identically on SSE2/NEON64/WASM.
 *
 *   Load/store: unaligned (std::vector<complex<double>> is not guaranteed
 *   16-byte aligned). On aligned allocators this could use load_aligned;
 *   the difference is one instruction per butterfly.
 *
 * @param data    In-place complex array, length == table.fftSize
 * @param table   Precomputed forward twiddle table
 * @param inverse If true, conjugates twiddle factors via negate_imag()
 */
inline void fftIterativeCore (CArray& data, const TwiddleTable& table, bool inverse)
{
    const size_t N = data.size();
    CASPI_ASSERT (N == table.fftSize, "Data size must match twiddle table size");

    // Alias into the SIMD namespace for readability in the inner loop.
    using namespace CASPI::SIMD;

    size_t offset = 0;
    for (size_t len = 2; len <= N; len <<= 1)
    {
        const size_t halfLen = len >> 1;

        for (size_t i = 0; i < N; i += len)
        {
            for (size_t k = 0; k < halfLen; ++k)
            {
                const size_t idx_even = i + k;
                const size_t idx_odd  = i + k + halfLen;

#if defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_NEON64) || defined(CASPI_HAS_WASM_SIMD)
                // ---- Platform-agnostic SIMD butterfly ----
                //
                // Twiddle construction: interleave_lo([wr,wr], [wi,wi]) = [wr,wi]
                // No _mm_set_pd, no memcpy. Works on SSE2, NEON64, and WASM SIMD.
                //
                // For IFFT: negate_imag flips sign of lane 1 (the im component)
                // in one instruction (XOR sign mask), making the twiddle conjugate.
                // This is cheaper than a conditional branch per butterfly.
                float64x2 twiddle = interleave_lo (set1<double> (table.re[offset + k]),
                                                   set1<double> (table.im[offset + k]));
                if (inverse)
                    twiddle = negate_imag (twiddle);

                // std::complex<double> is [re:double, im:double] in memory.
                // Load as 16-byte float64x2 via unaligned load (std::vector
                // does not guarantee 16-byte alignment of its elements).
                const float64x2 even = load_unaligned<double> (
                    reinterpret_cast<const double*> (&data[idx_even]));
                const float64x2 odd  = load_unaligned<double> (
                    reinterpret_cast<const double*> (&data[idx_odd]));

                const float64x2 t        = complex_mul (twiddle, odd);
                const float64x2 new_even = add (even, t);
                const float64x2 new_odd  = sub (even, t);

                store_unaligned (reinterpret_cast<double*> (&data[idx_even]), new_even);
                store_unaligned (reinterpret_cast<double*> (&data[idx_odd]),  new_odd);
#else
                // ---- Scalar fallback ----
                const double wr = table.re[offset + k];
                const double wi = inverse ? -table.im[offset + k] : table.im[offset + k];
                const Complex twiddle (wr, wi);
                const Complex t = twiddle * data[idx_odd];
                data[idx_odd]  = data[idx_even] - t;
                data[idx_even] = data[idx_even] + t;
#endif
            }
        }
        offset += halfLen;
    }
}

// ============================================================================
// Stateless FFT Functions
// ============================================================================

/** @brief Forward FFT in-place. Recomputes twiddle table per call.
 *  Use the FFT class for repeated transforms of the same size. */
inline void fft (CArray& data)
{
    CASPI_ASSERT (isPowerOfTwo (data.size()), "FFT size must be power of 2");
    bitReversalPermutation (data);
    fftIterativeCore (data, computeTwiddleTable (data.size()), false);
}

/** @brief Inverse FFT in-place, normalised by 1/N.
 *  Use the FFT class for repeated transforms of the same size. */
inline void ifft (CArray& data)
{
    CASPI_ASSERT (isPowerOfTwo (data.size()), "IFFT size must be power of 2");
    bitReversalPermutation (data);
    fftIterativeCore (data, computeTwiddleTable (data.size()), true);
    const double scale = 1.0 / static_cast<double>(data.size());
    for (auto& v : data) v *= scale;
}

/** @brief Forward FFT of real-valued input. */
inline CArray fftReal (const std::vector<double>& realData)
{
    CArray data = realToComplex (realData);
    fft (data);
    return data;
}

// ============================================================================
// FFT Class — Stateful Engine with Precomputed Twiddle Factors
// ============================================================================

/**
 * @brief Stateful FFT engine. Twiddle factors computed once in prepare().
 *
 * @code
 *   CASPI::FFT engine;
 *   engine.setSize(1024);
 *   engine.setSampleRate(44100.0);
 *   engine.prepare();
 *
 *   CArray buf = realToComplex(samples);
 *   engine.perform(buf);
 *   engine.performInverse(buf);
 * @endcode
 *
 * Thread safety: after prepare(), perform() and performInverse() are read-only
 * on engine state. Concurrent calls with separate CArray instances are safe.
 */
class FFT
{
public:
    FFT() = default;

    explicit FFT (const FFTConfig& config) : config_ (config)
    {
        if (! config_.isValid())
            throw std::invalid_argument ("FFT size must be power of 2");
        prepare();
    }

    void setSize (size_t size)
    {
        if (! isPowerOfTwo (size))
            throw std::invalid_argument ("FFT size must be power of 2");
        config_.size = size;
        ready_ = false;
    }

    void setSampleRate (double sr) { config_.sampleRate = sr; }

    size_t getSize()       const { return config_.size; }
    double getSampleRate() const { return config_.sampleRate; }
    bool   isReady()       const { return ready_; }

    /** @brief Precompute twiddle table. O(N) sin/cos. Must be called after setSize(). */
    void prepare()
    {
        if (! config_.isValid())
            throw std::invalid_argument ("FFT size must be power of 2");
        twiddleTable_ = computeTwiddleTable (config_.size);
        ready_ = true;
    }

    std::vector<double> generateFrequencyBins() const
    {
        return CASPI::generateFrequencyBins (config_.size, config_.sampleRate);
    }

    double getFrequencyResolution() const { return config_.getFrequencyResolution(); }

    /** @brief Forward FFT in-place. data.size() must equal getSize(). */
    void perform (CArray& data) const
    {
        checkReady (data.size());
        bitReversalPermutation (data);
        fftIterativeCore (data, twiddleTable_, false);
    }

    /** @brief Inverse FFT in-place.
     *  @param normalise Divide by N (default: true). */
    void performInverse (CArray& data, bool normalise = true) const
    {
        checkReady (data.size());
        bitReversalPermutation (data);
        fftIterativeCore (data, twiddleTable_, true);
        if (normalise)
        {
            const double scale = 1.0 / static_cast<double>(config_.size);
            for (auto& v : data) v *= scale;
        }
    }

    FFTConfig    config_;       ///< public for test access (#define private public)
    TwiddleTable twiddleTable_; ///< public for test access

private:
    bool ready_ = false;

    void checkReady (size_t dataSize) const
    {
        if (! ready_)
            throw std::runtime_error ("FFT not prepared — call prepare() before transform");
        if (dataSize != config_.size)
            throw std::invalid_argument ("Data size does not match FFT configuration");
    }
};

} // namespace CASPI

#endif // CASPI_FFT_H