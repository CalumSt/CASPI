/*****************************************************************************
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

 * @file   caspi_WavetableOscillator.h
 * @author CS Islay
 * @brief  Single-cycle wavetable oscillator with per-sample modulation
 *         and multi-table morphing.
 *
 * @details
 * Three cooperating types form the public API:
 *
 * ### WaveTable\<FloatType, TableSize\>
 * A single normalised single-cycle waveform stored as a fixed-size array.
 * TableSize must be a power of two (index wrapping uses a bitmask rather
 * than modulo). Fill via fillSine(), fillSaw(), fillTriangle(), or
 * fillWith(callable). Two interpolation kernels are provided:
 * - readLinear()  — branchless; suitable for TableSize >= 2048.
 * - readHermite() — 4-point Catmull-Rom; ~4x FMA cost, better alias rejection.
 *
 * ### WaveTableBank\<FloatType, TableSize, NumTables\>
 * Ordered array of WaveTables owned by value (no heap allocation).
 * The oscillator crossfades between adjacent tables based on morphPosition.
 * With NumTables == 1 the morph path is eliminated at compile time via
 * `if constexpr`.
 *
 * ### WavetableOscillator\<FloatType, TableSize, NumTables\>
 * The oscillator. Holds a non-owning pointer to a WaveTableBank; the bank
 * must outlive the oscillator. The API matches BlepOscillator exactly:
 * - Inherits Core::Producer<FloatType, Traversal::PerFrame>
 * - Inherits Core::SampleRateAware<FloatType>
 * - Public ModulatableParameter members: amplitude, frequency, morphPosition
 * - setFrequency(hz) — writes into the log-scale parameter and snaps the
 *   smoother (same contract as BlepOscillator::setFrequency)
 * - phaseWrapped() / forceSync() — hard sync
 * - renderSample() — scalar per-sample override
 * - renderBlock(FloatType*, int) — block path (preferred on the audio thread)
 *
 * ### Modulatable parameters
 * | Parameter     | Range              | Scale       | Notes                         |
 * |---------------|--------------------|-------------|-------------------------------|
 * | amplitude     | [0, 1]             | Linear      | Output gain                   |
 * | frequency     | [20, 20000] Hz     | Logarithmic | Phase increment recomputed    |
 * | morphPosition | [0, 1] normalised  | Linear      | Scaled to [0, N-1] at render  |
 *
 * phaseModDepth is a plain FloatType set via setPhaseModDepth(). It is added
 * to phase before each table lookup (PM / FM input) and is not smoothed.
 *
 * ### Morphing
 * morphPosition is normalised [0, 1], scaled to [0, NumTables-1] inside
 * readTable(). At render time:
 * @code
 *   morphScaled = morphPosition.value() * (NumTables - 1)
 *   iA   = floor(morphScaled)
 *   iB   = min(iA + 1, NumTables - 1)
 *   frac = morphScaled - iA
 *   out  = lerp(table[iA].read(phase), table[iB].read(phase), frac)
 * @endcode
 * With NumTables == 1 the lerp is eliminated at compile time.
 *
 * ### Hard sync
 * No discontinuity correction is applied on forceSync() because the
 * wavetable content is band-limited by construction. Any residual click
 * at the sync point is the caller's responsibility.
 * @code
 *   primary.renderSample();
 *   if (primary.phaseWrapped())
 *       secondary.forceSync();
 * @endcode
 *
 * ### Block rendering and SIMD
 * renderBlock() steps parameters once per block and runs a scalar loop.
 * The loop body has no loop-carried data dependency (unlike BlepOscillator
 * Triangle), making it a strong auto-vectorisation candidate. The intended
 * SIMD upgrade path is a renderBlockSIMD() override:
 * - Accumulate phase into float32x4/x8.
 * - floor(phase * TableSize) via CASPI::SIMD::floor.
 * - Gather table[i0] / table[i1] via CASPI::SIMD::gather or scalar + blend.
 * - frac lerp via CASPI::SIMD::fmadd.
 * - Morph lerp at a second level.
 * The scalar loop is written so that replacing its body requires no
 * structural change to the surrounding code.
 *
 * ### Typical usage
 * @code
 *   // Build a 4-table morph bank
 *   CASPI::Oscillators::WaveTableBank<float, 2048, 4> bank;
 *   bank[0].fillSine();
 *   bank[1].fillSaw();
 *   bank[2].fillTriangle();
 *   bank[3].fillWith([](float t) {
 *       float s = 0.f;
 *       for (int k = 1; k < 20; k += 2)
 *           s += std::sin(2.f * M_PI * k * t) / k;
 *       return s * (4.f / M_PI);
 *   });
 *
 *   CASPI::Oscillators::WavetableOscillator<float, 2048, 4> osc(bank, 44100.f, 440.f);
 *   osc.setMorphPosition(1.5f);   // halfway between Saw and Triangle
 *
 *   // Per-sample modulation
 *   osc.frequency.addModulation(lfoOut * depth);
 *   float s = osc.renderSample();
 *   osc.frequency.clearModulation();
 *
 *   // Block rendering (preferred)
 *   float buffer[512];
 *   osc.renderBlock(buffer, 512);
 * @endcode
 *****************************************************************************/

#ifndef CASPI_WAVETABLEOSCILLATOR_H
#define CASPI_WAVETABLEOSCILLATOR_H

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Parameter.h"
#include "core/caspi_Phase.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace CASPI
{
namespace Oscillators
{

/*******************************************************************************
 * detail — compile-time helpers; not public API
 ******************************************************************************/

namespace detail
{

/**
 * @brief Compile-time power-of-two check.
 *
 * @tparam N  Value to test.
 */
template <std::size_t N>
struct IsPowerOfTwo
{
    static constexpr bool value = (N > 0) && ((N & (N - 1)) == 0);
};

/**
 * @brief Convert Hz to a normalised [0, 1] value on a log scale.
 *
 * @details
 * Clamps to [0, 1] at the boundaries. Identical to the conversion used
 * in BlepOscillator::setFrequency().
 *
 * @tparam FloatType  float or double.
 * @param  hz         Frequency in Hz.
 * @param  minHz      Log-scale minimum (e.g. 20 Hz).
 * @param  maxHz      Log-scale maximum (e.g. 20000 Hz).
 * @return            Normalised value in [0, 1].
 */
template <typename FloatType>
FloatType hzToNormLog (FloatType hz,
                        FloatType minHz,
                        FloatType maxHz) noexcept
{
    if (hz <= minHz) return FloatType (0);
    if (hz >= maxHz) return FloatType (1);
    return (std::log (hz) - std::log (minHz))
         / (std::log (maxHz) - std::log (minHz));
}

} // namespace detail


/*******************************************************************************
 * InterpolationMode
 ******************************************************************************/

/**
 * @brief Selects the table interpolation kernel for WavetableOscillator.
 *
 * @details
 * - Linear:  Branchless two-point lerp. Sufficient for TableSize >= 2048 at
 *            audible frequencies (SNR > 90 dB). Auto-vectorisation friendly.
 * - Hermite: 4-point Catmull-Rom cubic. Better alias rejection at high
 *            frequencies, at approximately 4x the FMA cost of Linear.
 *
 * Switch at any time via setInterpolationMode(). Takes effect on the next
 * rendered sample.
 */
enum class InterpolationMode
{
    Linear,
    Hermite
};


/*******************************************************************************
 * WaveTable
 ******************************************************************************/

/**
 * @brief Single-cycle normalised waveform stored as a fixed-size array.
 *
 * @details
 * TableSize must be a power of two. Index wrapping in readLinear() and
 * readHermite() uses a bitmask (`& (TableSize - 1)`) rather than modulo
 * for performance.
 *
 * Fill methods return `*this` for chaining:
 * @code
 *   CASPI::Oscillators::WaveTable<float, 2048> table;
 *   table.fillSine();
 *
 *   // Custom waveform
 *   table.fillWith([](float t) {
 *       return std::sin(2.f * M_PI * t) * 0.5f
 *            + std::sin(4.f * M_PI * t) * 0.25f;
 *   });
 * @endcode
 *
 * @tparam FloatType   float or double.
 * @tparam TableSize   Number of samples per cycle. Must be a power of two.
 */
template <typename FloatType, std::size_t TableSize = 2048>
class WaveTable
{
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                   "WaveTable requires a floating-point type");
    CASPI_STATIC_ASSERT (detail::IsPowerOfTwo<TableSize>::value,
                   "WaveTable TableSize must be a power of two");

public:
    static constexpr std::size_t size = TableSize;

    /*************************************************************************
     * Fill methods
     *************************************************************************/

    /**
     * @brief Fill with one period of a sine wave in [-1, 1].
     *
     * @return Reference to this table for chaining.
     */
    WaveTable& fillSine() noexcept CASPI_NON_BLOCKING
    {
        for (std::size_t i = 0; i < TableSize; ++i)
        {
            samples[i] = std::sin (Constants::TWO_PI<FloatType>
                                   * static_cast<FloatType> (i)
                                   / static_cast<FloatType> (TableSize));
        }
        return *this;
    }

    /**
     * @brief Fill with a rising sawtooth in [-1, 1].
     *
     * @details
     * Linear ramp from -1 (sample 0) approaching +1 (sample TableSize-1).
     * No band-limiting is applied; use a WavetableOscillator with a
     * table large enough that aliasing is inaudible at the target frequency.
     *
     * @return Reference to this table for chaining.
     */
    WaveTable& fillSaw() noexcept CASPI_NON_BLOCKING
    {
        for (std::size_t i = 0; i < TableSize; ++i)
        {
            samples[i] = FloatType (2) * static_cast<FloatType> (i)
                       / static_cast<FloatType> (TableSize)
                       - FloatType (1);
        }
        return *this;
    }

    /**
     * @brief Fill with a triangle wave in [-1, 1].
     *
     * @details
     * Rises from -1 at t=0 to +1 at t=0.5, then falls back to -1 at t=1.
     *
     * @return Reference to this table for chaining.
     */
    WaveTable& fillTriangle() noexcept CASPI_NON_BLOCKING
    {
        for (std::size_t i = 0; i < TableSize; ++i)
        {
            const FloatType t = static_cast<FloatType> (i)
                              / static_cast<FloatType> (TableSize);
            samples[i] = (t < FloatType (0.5))
                             ? (FloatType (4) * t - FloatType (1))
                             : (FloatType (3) - FloatType (4) * t);
        }
        return *this;
    }

    /**
     * @brief Fill using a callable: `FloatType fn(FloatType normalised_phase)`.
     *
     * @details
     * The callable receives phase in [0, 1). It must return a value
     * ideally in [-1, 1]; values outside this range will not be clipped.
     *
     * @param fn  Any callable matching `FloatType(FloatType)`.
     * @return    Reference to this table for chaining.
     *
     * @code
     *   // Additive synthesis — band-limited square (first 5 odd harmonics)
     *   table.fillWith([](float t) {
     *       float s = 0.f;
     *       for (int k = 1; k <= 9; k += 2)
     *           s += std::sin(2.f * M_PI * k * t) / k;
     *       return s * (4.f / M_PI);
     *   });
     * @endcode
     */
    WaveTable& fillWith (std::function<FloatType (FloatType)> fn) noexcept CASPI_NON_BLOCKING
    {
        for (std::size_t i = 0; i < TableSize; ++i)
        {
            samples[i] = fn (static_cast<FloatType> (i)
                             / static_cast<FloatType> (TableSize));
        }
        return *this;
    }

    /*************************************************************************
     * Sample access
     *************************************************************************/

    /**
     * @brief Write access to sample at index @p i.
     *
     * @param i  Index in [0, TableSize). No bounds check in release builds.
     * @return   Reference to the sample.
     */
    FloatType& operator[] (std::size_t i) noexcept       { return samples[i]; }

    /**
     * @brief Read access to sample at index @p i.
     *
     * @param i  Index in [0, TableSize). No bounds check in release builds.
     * @return   Sample value.
     */
    FloatType  operator[] (std::size_t i) const noexcept { return samples[i]; }

    /** @brief Pointer to the raw sample data. Useful for SIMD or NumPy views. */
    const FloatType* data() const noexcept { return samples.data(); }

    /** @brief Mutable pointer to the raw sample data. */
    FloatType*       data()       noexcept { return samples.data(); }

    /*************************************************************************
     * Interpolated reads
     *************************************************************************/

    /**
     * @brief Linear interpolation at normalised phase in [0, 1).
     *
     * @details
     * Index arithmetic:
     * @code
     *   idx  = phase * TableSize
     *   i0   = floor(idx)
     *   i1   = (i0 + 1) & (TableSize - 1)   // wraps via bitmask
     *   frac = idx - i0
     *   out  = samples[i0] + frac * (samples[i1] - samples[i0])
     * @endcode
     * Entirely branchless. Suitable for compiler auto-vectorisation when
     * inlined inside a block render loop.
     *
     * @param phase  Normalised phase in [0, 1). Must not be negative.
     * @return       Interpolated sample value.
     */
    CASPI_NO_DISCARD CASPI_ALWAYS_INLINE
    FloatType readLinear (FloatType phase) const noexcept CASPI_NON_BLOCKING
    {
        constexpr std::size_t mask = TableSize - 1;

        const FloatType   idx  = phase * static_cast<FloatType> (TableSize);
        const std::size_t i0   = static_cast<std::size_t> (idx);
        const std::size_t i1   = (i0 + 1) & mask;
        const FloatType   frac = idx - static_cast<FloatType> (i0);

        return samples[i0] + frac * (samples[i1] - samples[i0]);
    }

    /**
     * @brief 4-point Catmull-Rom cubic interpolation at normalised phase.
     *
     * @details
     * Uses four surrounding points to fit a cubic polynomial. Provides
     * better alias rejection than readLinear() at high frequencies
     * (approximately +6 dB SNR) at approximately 4x the FMA cost.
     * Recommended when driving the oscillator above SR/8 or when table
     * content contains high-frequency energy.
     *
     * Catmull-Rom coefficients:
     * @code
     *   c3 = -0.5*y0 + 1.5*y1 - 1.5*y2 + 0.5*y3
     *   c2 =  y0     - 2.5*y1 + 2.0*y2 - 0.5*y3
     *   c1 = -0.5*y0           + 0.5*y2
     *   c0 =  y1
     *   out = ((c3*frac + c2)*frac + c1)*frac + c0
     * @endcode
     *
     * @param phase  Normalised phase in [0, 1). Must not be negative.
     * @return       Interpolated sample value.
     */
    CASPI_NO_DISCARD CASPI_ALWAYS_INLINE
    FloatType readHermite (FloatType phase) const noexcept CASPI_NON_BLOCKING
    {
        constexpr std::size_t mask = TableSize - 1;

        const FloatType   idx  = phase * static_cast<FloatType> (TableSize);
        const std::size_t i1   = static_cast<std::size_t> (idx) & mask;
        const std::size_t i0   = (i1 - 1) & mask;
        const std::size_t i2   = (i1 + 1) & mask;
        const std::size_t i3   = (i1 + 2) & mask;
        const FloatType   frac = idx - std::floor (idx);

        const FloatType y0 = samples[i0];
        const FloatType y1 = samples[i1];
        const FloatType y2 = samples[i2];
        const FloatType y3 = samples[i3];

        const FloatType c3 = FloatType (-0.5) * y0 + FloatType ( 1.5) * y1
                           + FloatType (-1.5) * y2 + FloatType ( 0.5) * y3;
        const FloatType c2 = y0 + FloatType (-2.5) * y1
                           + FloatType ( 2.0) * y2 + FloatType (-0.5) * y3;
        const FloatType c1 = FloatType (-0.5) * y0 + FloatType (0.5) * y2;
        const FloatType c0 = y1;

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

private:
    std::array<FloatType, TableSize> samples {};
};


/*******************************************************************************
 * WaveTableBank
 ******************************************************************************/

/**
 * @brief Ordered collection of WaveTables for morphing.
 *
 * @details
 * Tables are owned by value — no heap allocation. The oscillator reads
 * adjacent tables and crossfades between them based on morphPosition.
 * With NumTables == 1 the crossfade is eliminated at compile time via
 * `if constexpr`, leaving a single unconditional table lookup.
 *
 * @code
 *   CASPI::Oscillators::WaveTableBank<float, 2048, 2> bank;
 *   bank[0].fillSine();
 *   bank[1].fillSaw();
 *   // morph at 0.5 → equal mix of sine and saw
 * @endcode
 *
 * @tparam FloatType   float or double.
 * @tparam TableSize   Power-of-two table length. Must match the oscillator.
 * @tparam NumTables   Number of morph tables. Must be >= 1.
 */
template <typename FloatType,
          std::size_t TableSize  = 2048,
          std::size_t NumTables  = 1>
class WaveTableBank
{
    CASPI_STATIC_ASSERT (NumTables >= 1,
                   "WaveTableBank requires at least one table");

public:
    static constexpr std::size_t numTables = NumTables;
    static constexpr std::size_t tableSize = TableSize;

    using Table = WaveTable<FloatType, TableSize>;

    /*************************************************************************
     * Table access
     *************************************************************************/

    /**
     * @brief Mutable access to table at index @p i.
     *
     * @param i  Table index in [0, NumTables). Asserts in debug builds.
     * @return   Reference to the WaveTable.
     */
    Table& operator[] (std::size_t i) noexcept
    {
        CASPI_ASSERT (i < NumTables, "Table index out of range");
        return tables[i];
    }

    /**
     * @brief Const access to table at index @p i.
     *
     * @param i  Table index in [0, NumTables). Asserts in debug builds.
     * @return   Const reference to the WaveTable.
     */
    const Table& operator[] (std::size_t i) const noexcept
    {
        CASPI_ASSERT (i < NumTables, "Table index out of range");
        return tables[i];
    }

    /*************************************************************************
     * Fill helpers
     *************************************************************************/

    /**
     * @brief Fill all tables with the same callable.
     *
     * @param fn  Callable matching `FloatType(FloatType normalised_phase)`.
     * @return    Reference to this bank for chaining.
     */
    WaveTableBank& fillAll (std::function<FloatType (FloatType)> fn) noexcept CASPI_NON_BLOCKING
    {
        for (auto& t : tables)
            t.fillWith (fn);
        return *this;
    }

    /**
     * @brief Fill table @p i with @p fn.
     *
     * @param i   Table index in [0, NumTables).
     * @param fn  Callable matching `FloatType(FloatType normalised_phase)`.
     * @return    Reference to this bank for chaining.
     */
    WaveTableBank& fillTable (std::size_t                        i,
                               std::function<FloatType (FloatType)> fn) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (i < NumTables, "Table index out of range");
        tables[i].fillWith (fn);
        return *this;
    }

    /*************************************************************************
     * Interpolated reads — called by WavetableOscillator::readTable()
     *************************************************************************/

    /**
     * @brief Linear crossfade read at (phase, morphPos).
     *
     * @details
     * @p morphPos is pre-scaled to [0, NumTables-1] by the oscillator.
     * With NumTables == 1 this compiles to a single unconditional table
     * lookup (no crossfade arithmetic).
     *
     * @param phase     Normalised oscillator phase in [0, 1).
     * @param morphPos  Pre-scaled morph position in [0, NumTables-1].
     * @return          Crossfaded sample value.
     */
    CASPI_NO_DISCARD
    FloatType readLinear (FloatType phase, FloatType morphPos) const noexcept CASPI_NON_BLOCKING
    {
        CASPI_CPP17_IF_CONSTEXPR (NumTables == 1)
        {
            return tables[0].readLinear (phase);
        }
        else
        {
            const FloatType   clamped = std::max (FloatType (0),
                                                   std::min (morphPos,
                                                             static_cast<FloatType> (NumTables - 1)));
            const std::size_t iA   = static_cast<std::size_t> (clamped);
            const std::size_t iB   = std::min (iA + 1, NumTables - 1);
            const FloatType   frac = clamped - static_cast<FloatType> (iA);

            return tables[iA].readLinear (phase)
                 + frac * (tables[iB].readLinear (phase) - tables[iA].readLinear (phase));
        }
    }

    /**
     * @brief Hermite crossfade read at (phase, morphPos).
     *
     * @details
     * Each table read uses the Catmull-Rom kernel. The two table outputs
     * are then linearly crossfaded (crossfading the cubic outputs is
     * sufficient; cubic-cubic crossfade adds no perceptual benefit).
     *
     * With NumTables == 1 this compiles to a single unconditional Hermite
     * read with no crossfade arithmetic.
     *
     * @param phase     Normalised oscillator phase in [0, 1).
     * @param morphPos  Pre-scaled morph position in [0, NumTables-1].
     * @return          Crossfaded sample value.
     */
    CASPI_NO_DISCARD
    FloatType readHermite (FloatType phase, FloatType morphPos) const noexcept CASPI_NON_BLOCKING
    {
        CASPI_CPP17_IF_CONSTEXPR (NumTables == 1)
        {
            return tables[0].readHermite (phase);
        }
        else
        {
            const FloatType   clamped = std::max (FloatType (0),
                                                   std::min (morphPos,
                                                             static_cast<FloatType> (NumTables - 1)));
            const std::size_t iA   = static_cast<std::size_t> (clamped);
            const std::size_t iB   = std::min (iA + 1, NumTables - 1);
            const FloatType   frac = clamped - static_cast<FloatType> (iA);

            return tables[iA].readHermite (phase)
                 + frac * (tables[iB].readHermite (phase) - tables[iA].readHermite (phase));
        }
    }

private:
    std::array<Table, NumTables> tables {};
};


/*******************************************************************************
 * WavetableOscillator
 ******************************************************************************/

/**
 * @brief Band-limited wavetable oscillator with morphing and per-sample
 *        modulation.
 *
 * @details
 * Holds a non-owning pointer to a WaveTableBank. The bank must outlive the
 * oscillator. The bank can be swapped at runtime via setBank(); thread-safety
 * of the swap is the caller's responsibility.
 *
 * ### Thread safety
 * - setFrequency(), setAmplitude(), setMorphPosition(), setPhaseOffset(),
 *   setPhaseModDepth(), setInterpolationMode(), setSampleRate(), setBank() —
 *   call from the audio thread or before streaming starts. Not thread-safe
 *   with concurrent renderSample() / renderBlock() calls.
 * - amplitude / frequency / morphPosition parameter writes
 *   (setBaseNormalised, addModulation) — the underlying atomic base value
 *   is thread-safe; smoother state is not.
 * - renderSample() / renderBlock() — audio thread only.
 *
 * @tparam FloatType   float or double.
 * @tparam TableSize   Power-of-two table length. Must match the bank.
 * @tparam NumTables   Number of morph tables. Must match the bank.
 *
 * @code
 *   CASPI::Oscillators::WaveTableBank<float, 2048, 1> bank;
 *   bank[0].fillSine();
 *
 *   CASPI::Oscillators::WavetableOscillator<float, 2048, 1> osc(bank, 44100.f, 440.f);
 *   float buf[512];
 *   osc.renderBlock(buf, 512);
 * @endcode
 */
template <typename FloatType,
          std::size_t TableSize = 2048,
          std::size_t NumTables = 1>
class WavetableOscillator
    : public Core::Producer<FloatType, Core::Traversal::PerFrame>
    , public Core::SampleRateAware<FloatType>
{
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                   "WavetableOscillator requires a floating-point type");
    CASPI_STATIC_ASSERT (detail::IsPowerOfTwo<TableSize>::value,
                   "TableSize must be a power of two");
    CASPI_STATIC_ASSERT (NumTables >= 1,
                   "NumTables must be >= 1");

    static constexpr FloatType kFreqMin = FloatType (20);
    static constexpr FloatType kFreqMax = FloatType (20000);

public:
    using Bank = WaveTableBank<FloatType, TableSize, NumTables>;

    /*************************************************************************
     * Construction
     *************************************************************************/

    /**
     * @brief Default constructor.
     *
     * @details
     * Initialises all parameters. The bank pointer is null; call setBank()
     * and setSampleRate() before rendering. Rendering without a bank will
     * assert in debug builds.
     */
    WavetableOscillator() noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
    }

    /**
     * @brief Construct with an external bank.
     *
     * @details
     * The bank must outlive the oscillator. Call setSampleRate() and
     * setFrequency() before rendering.
     *
     * @param bank  Reference to the WaveTableBank. Must outlive this object.
     */
    explicit WavetableOscillator (Bank& bank) noexcept CASPI_NON_ALLOCATING
        : bank_ (&bank)
    {
        initParameters();
    }

    /**
     * @brief Named constructor: bank + sample rate + frequency.
     *
     * @details
     * setSampleRate() is called before setFrequency() because setFrequency()
     * uses getSampleRate() to compute phase.increment. This is the preferred
     * constructor when all three are known at construction time.
     *
     * @param bank        Reference to the WaveTableBank. Must outlive this object.
     * @param sampleRate  Audio sample rate in Hz. Must be > 0.
     * @param hz          Oscillator frequency in Hz. Must be in (0, sampleRate/2).
     *
     * @code
     *   WavetableOscillator<float, 2048, 4> osc(bank, 44100.f, 220.f);
     * @endcode
     */
    WavetableOscillator (Bank& bank, FloatType sampleRate, FloatType hz) noexcept CASPI_NON_ALLOCATING
        : bank_ (&bank)
    {
        initParameters();
        this->setSampleRate (sampleRate);
        setFrequency (hz);
    }

    /*************************************************************************
     * Configuration
     *************************************************************************/

    /**
     * @brief Set frequency in Hz, bypassing parameter smoothing.
     *
     * @details
     * Converts @p hz to a normalised log value, writes into the frequency
     * parameter, and snaps the smoother (skip(1000)). Also recomputes
     * phase.increment.
     *
     * This must be called for initialisation. Without it, frequency.value()
     * returns ~20 Hz until the smoother converges, producing a wrong
     * phase.increment on every renderSample() call.
     *
     * For real-time modulated frequency changes, write to the frequency
     * parameter directly:
     * @code
     *   osc.frequency.addModulation(pitchBendNorm);
     *   float s = osc.renderSample();
     *   osc.frequency.clearModulation();
     * @endcode
     *
     * @param hz  Frequency in Hz. Must be > 0 and < sampleRate / 2.
     *
     * @note setSampleRate() must be called before setFrequency() for
     *       phase.increment to be computed correctly.
     */
    void setFrequency (FloatType hz) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (hz > FloatType (0), "Frequency must be positive");

        frequency.setBaseNormalised (detail::hzToNormLog (hz, kFreqMin, kFreqMax));
        frequency.skip (1000);
        phase.increment = hz / this->getSampleRate();
    }

    /**
     * @brief Set amplitude in [0, 1], bypassing parameter smoothing.
     *
     * @details
     * Snaps the amplitude smoother immediately. For smoothed real-time
     * changes, write to the amplitude parameter directly.
     *
     * @param amp  Amplitude in [0, 1]. Asserts in debug builds if out of range.
     */
    void setAmplitude (FloatType amp) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (amp >= FloatType (0) && amp <= FloatType (1),
                      "Amplitude must be in [0, 1]");
        amplitude.setBaseNormalised (amp);
        amplitude.skip (1000);
    }

    /**
     * @brief Set morph position in [0, NumTables-1], bypassing parameter smoothing.
     *
     * @details
     * Converts the raw position to a normalised [0, 1] value for the
     * parameter. readTable() scales it back to [0, NumTables-1] at render
     * time. For real-time morphing, write to morphPosition directly:
     * @code
     *   osc.morphPosition.addModulation(lfoOut * 0.5f);
     *   float s = osc.renderSample();
     *   osc.morphPosition.clearModulation();
     * @endcode
     *
     * @param pos  Morph position in [0, NumTables-1]. Clamped to range.
     *
     * @note With NumTables == 1 this has no effect; the morph path is
     *       eliminated at compile time.
     */
    void setMorphPosition (FloatType pos) noexcept CASPI_NON_BLOCKING
    {
        constexpr FloatType maxPos = static_cast<FloatType> (NumTables > 1 ? NumTables - 1 : 1);
        morphPosition.setBaseNormalised (
            std::max (FloatType (0), std::min (pos / maxPos, FloatType (1))));
        morphPosition.skip (1000);
    }

    /**
     * @brief Set the phase offset applied on resetPhase() and forceSync().
     *
     * @details
     * The offset is wrapped into [0, 1) via fmod. Does not immediately alter
     * the running phase; takes effect on the next resetPhase() or forceSync()
     * call.
     *
     * @param offset  Phase offset in [0, 1). Values outside are wrapped via
     *                fmod(abs(offset), 1).
     *
     * @code
     *   osc.setPhaseOffset(0.5f);  // start at 180 degrees
     *   osc.resetPhase();
     * @endcode
     */
    void setPhaseOffset (FloatType offset) noexcept CASPI_NON_BLOCKING
    {
        phaseOffset = std::fmod (std::abs (offset), FloatType (1));
    }

    /**
     * @brief Set phase modulation depth added to phase before each table lookup.
     *
     * @details
     * PM / FM input. Added to the pre-advance phase in readTable(), then
     * wrapped to [0, 1). Not smoothed; set once per block or per sample.
     * Value in [-1, 1]; values outside this range produce over-modulation.
     *
     * @param depth  Phase modulation depth in [-1, 1].
     *
     * @code
     *   // Simple FM: modulator drives carrier via PM
     *   float mod = modOsc.renderSample() * modDepth;
     *   carrier.setPhaseModDepth(mod);
     *   float s = carrier.renderSample();
     * @endcode
     */
    void setPhaseModDepth (FloatType depth) noexcept CASPI_NON_BLOCKING
    {
        phaseModDepth = depth;
    }

    /**
     * @brief Select the table interpolation kernel. Default: Linear.
     *
     * @details
     * Takes effect on the next rendered sample. Switching mid-block is
     * safe but will introduce a one-sample discontinuity at the switch
     * point if the two kernels produce different values.
     *
     * @param mode  InterpolationMode::Linear or InterpolationMode::Hermite.
     */
    void setInterpolationMode (InterpolationMode mode) noexcept CASPI_NON_BLOCKING
    {
        interpMode = mode;
    }

    /**
     * @brief Swap the wavetable bank at runtime.
     *
     * @details
     * The oscillator holds no state derived from the bank (no cached table
     * data, no per-table phase). The swap takes effect on the next
     * renderSample() / renderBlock() call.
     *
     * Thread-safety is the caller's responsibility. Typically called from
     * a non-audio thread with appropriate synchronisation, or from the
     * audio thread between blocks.
     *
     * @param newBank  Reference to the new bank. Must outlive the oscillator.
     */
    void setBank (Bank& newBank) noexcept CASPI_NON_BLOCKING
    {
        bank_ = &newBank;
    }

    /*************************************************************************
     * SampleRateAware override
     *************************************************************************/

    /**
     * @brief Override from SampleRateAware. Recomputes phase increment.
     *
     * @details
     * Called by the audio engine on startup or sample rate change. Also
     * call manually if constructing the oscillator before the sample rate
     * is known.
     *
     * @param newRate  Sample rate in Hz. Must be > 0.
     *
     * @note If called after setFrequency(), phase.increment is recomputed
     *       from the current frequency.value(). If the smoother has not yet
     *       converged, call setFrequency() again after setSampleRate().
     */
    void setSampleRate (FloatType newRate) override CASPI_NON_BLOCKING
    {
        Core::SampleRateAware<FloatType>::setSampleRate (newRate);
        const FloatType hz = frequency.value();
        phase.increment    = (newRate > FloatType (0)) ? hz / newRate : FloatType (0);
    }

    /*************************************************************************
     * Phase control
     *************************************************************************/

    /**
     * @brief Reset phase to phaseOffset.
     *
     * @details
     * Typical use: note-on event in a synth voice.
     * @code
     *   void noteOn(float freqHz) {
     *       osc.setFrequency(freqHz);
     *       osc.resetPhase();
     *   }
     * @endcode
     */
    void resetPhase() noexcept CASPI_NON_BLOCKING
    {
        phase.phase = phaseOffset;
    }

    /**
     * @brief Returns true if the phase wrapped on the most recent
     *        renderSample() call.
     *
     * @details
     * Use this flag to drive hard sync of a secondary oscillator. The flag
     * is updated by renderSample() only; renderBlock() updates it once from
     * the final phase state, which is insufficient for per-sample sync
     * detection inside a block. For block-level hard sync use the
     * render_hard_sync() helper in the Python bindings, or a manual
     * renderSample() loop.
     *
     * @return true if a phase wrap occurred on the last renderSample() call.
     *
     * @code
     *   primary.renderSample();
     *   if (primary.phaseWrapped())
     *       secondary.forceSync();
     *   float s = secondary.renderSample();
     * @endcode
     */
    CASPI_NO_DISCARD bool phaseWrapped() const noexcept CASPI_NON_BLOCKING
    {
        return wrapped;
    }

    /*************************************************************************
     * Hard sync
     *************************************************************************/

    /**
     * @brief Immediately reset phase to phaseOffset.
     *
     * @details
     * No discontinuity correction is applied. The wavetable content is
     * band-limited by construction, so the click at the sync point is
     * generally inaudible for well-designed tables. For abrupt hard sync
     * timbres (classic oscillator sync sound) this is the correct behaviour.
     * If clicks are audible, apply a short amplitude crossfade at the
     * mixer level around the sync point.
     *
     * @note Unlike BlepOscillator::forceSync(), no syncCorrection term is
     *       computed or stored. The wavetable path has no equivalent of
     *       the PolyBLEP correction.
     *
     * @code
     *   primary.renderSample();
     *   if (primary.phaseWrapped())
     *       secondary.forceSync();
     * @endcode
     */
    void forceSync() noexcept CASPI_NON_BLOCKING
    {
        phase.phase = phaseOffset;
    }

    /*************************************************************************
     * Per-sample rendering
     *************************************************************************/

    /**
     * @brief Render one sample (scalar path).
     *
     * @details
     * Steps all three parameter smoothers (amplitude, frequency,
     * morphPosition) once per call, recomputes phase.increment from the
     * smoothed frequency, advances phase, and reads the table. The
     * phaseWrapped() flag is updated after each call.
     *
     * This is the scalar path called by Producer::render(AudioBuffer&).
     * For direct buffer filling, prefer renderBlock() to amortise the
     * per-block parameter step overhead.
     *
     * @return Waveform sample scaled by amplitude.value(), in approximately
     *         [-amplitude, +amplitude].
     *
     * @note Not safe to call concurrently from multiple threads.
     *
     * @code
     *   // Per-sample modulation pattern
     *   osc.frequency.addModulation(lfoOut * depth);
     *   float s = osc.renderSample();
     *   osc.frequency.clearModulation();
     * @endcode
     */
    CASPI_NO_DISCARD
    FloatType renderSample() noexcept override CASPI_NON_BLOCKING
    {
        amplitude.process();
        frequency.process();
        morphPosition.process();

        const FloatType hz = frequency.value();
        const FloatType fs = this->getSampleRate();
        phase.increment    = hz / fs;

        const FloatType pBefore = phase.phase;
        phase.advanceAndWrap (FloatType (1));
        wrapped = (phase.phase < pBefore);

        return readTable (pBefore) * amplitude.value();
    }

    /*************************************************************************
     * Block rendering (preferred path for audio threads)
     *************************************************************************/

    /**
     * @brief Render @p numSamples into a raw output buffer.
     *
     * @details
     * Steps each parameter smoother once per block, reducing overhead by a
     * factor of numSamples compared to calling renderSample() in a loop.
     *
     * The inner loop body has no loop-carried data dependency, making it a
     * strong auto-vectorisation candidate for the compiler.
     *
     * @param output      Pointer to a buffer of at least @p numSamples
     *                    elements. Must not be null. Must not alias internal
     *                    oscillator state (CASPI_RESTRICT applied).
     * @param numSamples  Number of samples to generate. Must be > 0.
     *
     * @note phaseWrapped() is updated once from the final phase state, not
     *       per-sample. It is not suitable for driving hard sync inside a
     *       block from renderBlock() alone. Use render_hard_sync() in the
     *       Python bindings, or a manual renderSample() loop.
     *
     * @code
     *   float buffer[256];
     *   osc.renderBlock(buffer, 256);
     *
     *   // Block-level amplitude modulation
     *   osc.amplitude.addModulation(envValue - 1.f);
     *   osc.renderBlock(buffer, 256);
     *   osc.amplitude.clearModulation();
     * @endcode
     */
    void renderBlock (FloatType* CASPI_RESTRICT output,
                      int                       numSamples) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (output     != nullptr, "Output buffer must not be null");
        CASPI_ASSERT (numSamples >  0,       "numSamples must be positive");

        amplitude.process();
        frequency.process();
        morphPosition.process();

        const FloatType hz  = frequency.value();
        const FloatType fs  = this->getSampleRate();
        const FloatType amp = amplitude.value();
        phase.increment     = hz / fs;

        for (int i = 0; i < numSamples; ++i)
        {
            const FloatType pBefore = phase.phase;
            phase.advanceAndWrap (FloatType (1));
            output[i] = readTable (pBefore) * amp;
        }

        // Update wrapped flag from final phase state.
        wrapped = (numSamples > 0) && (phase.phase < phase.increment);
    }

    /*************************************************************************
     * Public modulatable parameters
     *************************************************************************/

    /**
     * @brief Output amplitude in [0, 1]. Linear scale.
     *
     * @details
     * Stepped once per renderSample() call or once per renderBlock() call.
     * @code
     *   osc.amplitude.addModulation(envOut - 1.f);
     *   float s = osc.renderSample();
     *   osc.amplitude.clearModulation();
     * @endcode
     */
    Core::ModulatableParameter<FloatType> amplitude;

    /**
     * @brief Frequency in Hz. Logarithmic range [20, 20000].
     *
     * @details
     * Stepped once per renderSample() or renderBlock() call. phase.increment
     * is recomputed from frequency.value() on each step.
     *
     * @note For initialisation use setFrequency() rather than writing to
     *       this parameter directly, to avoid a slow smoother convergence
     *       period at the wrong frequency.
     */
    Core::ModulatableParameter<FloatType> frequency;

    /**
     * @brief Morph position. Normalised [0, 1], maps to [0, NumTables-1].
     *
     * @details
     * Stepped once per renderSample() or renderBlock() call. Scaled to
     * [0, NumTables-1] in readTable(). With NumTables == 1 this has no
     * audible effect; the morph code path is eliminated at compile time.
     *
     * @code
     *   // Automate morph with an LFO
     *   osc.morphPosition.addModulation(lfoOut * 0.5f);
     *   float s = osc.renderSample();
     *   osc.morphPosition.clearModulation();
     * @endcode
     */
    Core::ModulatableParameter<FloatType> morphPosition;

private:

    /*************************************************************************
     * Parameter initialisation
     *************************************************************************/

    /**
     * @brief Initialise all parameters to default ranges and snap smoothers.
     *
     * @details
     * Called from every constructor. Snapping all smoothers (skip(1000))
     * ensures value() is correct on the first render call without a
     * warm-up period, matching the BlepOscillator pattern.
     */
    void initParameters() noexcept CASPI_NON_BLOCKING
    {
        amplitude.setRange (FloatType (0), FloatType (1));
        amplitude.setBaseNormalised (FloatType (1));

        frequency.setRange (kFreqMin, kFreqMax, Core::ParameterScale::Logarithmic);
        frequency.setBaseNormalised (
            detail::hzToNormLog (FloatType (440), kFreqMin, kFreqMax));

        morphPosition.setRange (FloatType (0), FloatType (1));
        morphPosition.setBaseNormalised (FloatType (0));

        amplitude.skip (1000);
        frequency.skip (1000);
        morphPosition.skip (1000);
    }

    /*************************************************************************
     * readTable — phase modulation + interpolation dispatch
     *************************************************************************/

    /**
     * @brief Apply phase modulation, then read the bank at the current morph.
     *
     * @details
     * phaseModDepth is added to @p pBefore and the result is wrapped into
     * [0, 1) via conditional subtract. Conditional subtract is faster than
     * std::fmod when phaseModDepth is small (which covers all normal PM use).
     *
     * morphPosition.value() is normalised [0, 1]; this method scales it to
     * [0, NumTables-1] before passing to the bank.
     *
     * @param pBefore  Oscillator phase before the most recent advance, in [0, 1).
     * @return         Interpolated sample value before amplitude scaling.
     */
    CASPI_ALWAYS_INLINE
    FloatType readTable (FloatType pBefore) const noexcept CASPI_NON_BLOCKING
    {
        FloatType p = pBefore + phaseModDepth;
        if (p >= FloatType (1)) p -= FloatType (1);
        if (p <  FloatType (0)) p += FloatType (1);

        constexpr FloatType kMorphScale =
            static_cast<FloatType> (NumTables > 1 ? NumTables - 1 : 1);
        const FloatType morphScaled = morphPosition.value() * kMorphScale;

        if (interpMode == InterpolationMode::Hermite)
            return bank_->readHermite (p, morphScaled);

        return bank_->readLinear (p, morphScaled);
    }

    /*************************************************************************
     * State
     *************************************************************************/

    Bank*             bank_         { nullptr };             ///< Non-owning pointer. Must not be null when rendering.
    Phase<FloatType>  phase         {};                      ///< Phase accumulator and increment.
    FloatType         phaseOffset   { FloatType (0) };       ///< Applied on resetPhase() / forceSync().
    FloatType         phaseModDepth { FloatType (0) };       ///< Added to phase before each table lookup.
    InterpolationMode interpMode    { InterpolationMode::Linear };
    bool              wrapped       { false };               ///< Updated by renderSample(); approximated by renderBlock().
};

} // namespace Oscillators
} // namespace CASPI

#endif // CASPI_WAVETABLEOSCILLATOR_H