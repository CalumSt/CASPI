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

 * @file   caspi_BlepOscillator.h
 * @author CS Islay
 * @brief  Band-limited oscillator using the PolyBLEP antialiasing method.
 *
 * @details
 * BlepOscillator<FloatType> produces band-limited Sine, Saw, Square,
 * Triangle, and Pulse waveforms from a single class. Waveform selection
 * is a runtime enum, allowing shape changes without reconstructing the
 * oscillator or losing phase state (e.g. an LFO morphing from Sine to
 * Square mid-modulation).
 *
 * ### Inheritance
 * - Core::Producer<FloatType, Traversal::PerFrame>
 *   Provides render(AudioBuffer&) through the PerFrame traversal policy.
 *   renderSample() is the scalar override called per frame.
 * - Core::SampleRateAware<FloatType>
 *   Provides getSampleRate() / setSampleRate().
 *
 * ### Modulatable parameters
 * | Parameter   | Range          | Scale       | Notes                         |
 * |-------------|----------------|-------------|-------------------------------|
 * | amplitude   | [0, 1]         | Linear      | Output gain                   |
 * | frequency   | [20, 20000] Hz | Logarithmic | Phase increment recomputed    |
 * | pulseWidth  | [0.01, 0.99]   | Linear      | Square / Pulse shapes only    |
 *
 * Phase offset is a plain FloatType set via setPhaseOffset(). It is not a
 * ModulatableParameter because phase is cumulative state, not a per-sample
 * target value.
 *
 * ### Block rendering and SIMD
 * renderBlock() steps parameters once per block and evaluates the waveform
 * in a scalar loop. For Sine, Saw, Square, and Pulse the loop body is
 * branch-free (branchless polyBlep()), enabling auto-vectorisation by the
 * compiler over consecutive samples (no loop-carried data dependency).
 *
 * Triangle has an integrator feedback dependency:
 * @code
 *   integrator(n) = integrator(n-1) * leak + 4 * dt * sq(n)
 * @endcode
 * This prevents auto-vectorisation. It runs on the scalar path.
 *
 * An explicit SIMD override (renderBlockSIMD) using CASPI::SIMD::blend and
 * cmp_lt / cmp_gt on float32x4 / float32x8 vectors is the intended
 * extension point for Saw and Square. It is not implemented here.
 *
 * ### Hard sync
 * forceSync() resets phase and applies a one-sample discontinuity
 * correction on the next render call. Drive it from the primary
 * oscillator's render loop:
 * @code
 *   primary.renderSample();
 *   if (primary.phaseWrapped())
 *       secondary.forceSync();
 * @endcode
 *
 * ### Typical usage
 * @code
 *   CASPI::Oscillators::BlepOscillator<float> osc;
 *   osc.setSampleRate(44100.f);
 *   osc.setShape(CASPI::Oscillators::WaveShape::Saw);
 *   osc.setFrequency(440.f);
 *
 *   // Per-sample modulation
 *   osc.frequency.addModulation(lfoOut * depthNorm);
 *   float sample = osc.renderSample();
 *   osc.frequency.clearModulation();
 *
 *   // Block rendering (preferred on the audio thread)
 *   float buffer[512];
 *   osc.renderBlock(buffer, 512);
 * @endcode
 ************************************************************************/

#ifndef CASPI_BLEPOSCILLATOR_H
#define CASPI_BLEPOSCILLATOR_H

#include <cmath>
#include <type_traits>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Parameter.h"
#include "core/caspi_Phase.h"

namespace CASPI
{
namespace Oscillators
{

/*******************************************************************************
 * WaveShape
 ******************************************************************************/

/**
 * @brief Selects the output waveform of BlepOscillator at runtime.
 *
 * @details
 * Pulse is a semantic alias for Square. Both produce the same band-limited
 * waveform controlled by pulseWidth; the distinction communicates intent
 * at the call site (e.g. a narrow-duty-cycle percussion click vs a 50%
 * square).
 *
 * Switching to Triangle via setShape() resets the leaky integrator to
 * prevent a DC transient on the first rendered block.
 */
enum class WaveShape
{
    Sine,
    Saw,
    Square,
    Triangle,
    Pulse   ///< Semantic alias for Square; audibly identical, pulseWidth applies.
};


/*******************************************************************************
 * detail — internal helpers, not part of the public API
 ******************************************************************************/
namespace detail
{

/**
 * @brief PolyBLEP residual for a single discontinuity at phase == 0.
 *
 * @details
 * Computes a piecewise correction that blends the ideal bandlimited step
 * into the naive waveform around each discontinuity. Applied once per
 * discontinuity per sample (once per cycle for Saw, twice for Square/Pulse).
 *
 * The two branch residuals are always computed; a ternary select chooses
 * the active one. This produces CMOV / BLENDVPS on x86 rather than
 * mispredicting branches at audio rate.
 *
 * Piecewise definition:
 * @code
 *   if phase in [0, dt):
 *       t = phase / dt
 *       return (2 - t) * t - 1
 *   else if phase in (1 - dt, 1):
 *       t = (phase - 1) / dt
 *       return (t + 2) * t + 1
 *   else:
 *       return 0
 * @endcode
 *
 * @tparam FloatType  float or double.
 * @param  phase      Current oscillator phase in [0, 1).
 * @param  dt         Phase increment per sample (frequency / sampleRate).
 * @return            Additive correction to apply to the naive waveform sample.
 *
 * @note dt must be > 0. At very low frequencies (dt << 1 / tableSize) the
 *       correction is negligibly small but still mathematically valid.
 */
template <typename FloatType>
CASPI_ALWAYS_INLINE FloatType polyBlep (FloatType phase, FloatType dt) noexcept
{
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                   "polyBlep requires a floating-point type");

    const FloatType tRise    = phase / dt;
    const FloatType blepRise = (FloatType (2) - tRise) * tRise - FloatType (1);

    const FloatType tFall    = (phase - FloatType (1)) / dt;
    const FloatType blepFall = (tFall + FloatType (2)) * tFall + FloatType (1);

    return (phase < dt)                 ? blepRise
         : (phase > FloatType (1) - dt) ? blepFall
                                        : FloatType (0);
}

/**
 * @brief Compute the leak coefficient for the Triangle leaky integrator.
 *
 * @details
 * The Triangle waveform is produced by integrating a band-limited Square.
 * A leaky (rather than ideal) integrator prevents DC accumulation at the
 * cost of a slight droop at very low frequencies. The coefficient is
 * frequency-dependent; setSampleRate() and setFrequency() recompute it.
 *
 * Formula:
 * @code
 *   w    = TWO_PI * hz / sampleRate
 *   leak = clamp(1.0 - (w * w) * 0.25,  0.9,  0.9999)
 * @endcode
 *
 * The clamp ensures stability (leak < 1) and limits low-frequency droop
 * (leak >= 0.9).
 *
 * @tparam FloatType   float or double.
 * @param  hz          Oscillator frequency in Hz. Must be > 0.
 * @param  sampleRate  Audio sample rate in Hz. Must be > 0.
 * @return             Leak coefficient in [0.9, 0.9999].
 */
template <typename FloatType>
CASPI_ALWAYS_INLINE FloatType leakCoeff (FloatType hz, FloatType sampleRate) noexcept
{
    const FloatType w       = Constants::TWO_PI<FloatType> * hz / sampleRate;
    const FloatType rawLeak = FloatType (1) - (w * w) * FloatType (0.25);
    return std::max (FloatType (0.9), std::min (rawLeak, FloatType (0.9999)));
}

} // namespace detail


/*******************************************************************************
 * BlepOscillator
 ******************************************************************************/

/**
 * @brief Band-limited oscillator using PolyBLEP antialiasing.
 *
 * @details
 * Produces Sine, Saw, Square, Triangle, and Pulse waveforms. Shape is
 * selected at runtime via setShape(). All modulatable state (amplitude,
 * frequency, pulseWidth) is held as Core::ModulatableParameter and is
 * accessible on the public interface for direct modulation by a ModMatrix
 * or manual add/clearModulation() calls.
 *
 * Integrates with the CASPI audio engine via Producer::render(AudioBuffer&).
 * For direct buffer filling without an AudioBuffer, use renderBlock().
 *
 * ### Thread safety
 * - setShape(), setFrequency(), setPhaseOffset(), setSampleRate() —
 *   call from the audio thread or before streaming starts. Not thread-safe
 *   with concurrent renderSample() / renderBlock() calls.
 * - amplitude / frequency / pulseWidth parameter writes (setBaseNormalised,
 *   addModulation) — the underlying atomic base value is thread-safe;
 *   smoother state is not.
 * - renderSample() / renderBlock() — audio thread only.
 *
 * @tparam FloatType  float or double.
 *
 * @code
 *   // Minimal setup
 *   CASPI::Oscillators::BlepOscillator<float> osc;
 *   osc.setSampleRate(48000.f);
 *   osc.setShape(CASPI::Oscillators::WaveShape::Square);
 *   osc.setFrequency(220.f);
 *   osc.pulseWidth.setBaseNormalised(0.3f);   // narrow pulse
 *
 *   float buffer[256];
 *   osc.renderBlock(buffer, 256);
 * @endcode
 */
template <typename FloatType>
class BlepOscillator
    : public Core::Producer<FloatType, Core::Traversal::PerFrame>
    , public Core::SampleRateAware<FloatType>
{
    static_assert (std::is_floating_point<FloatType>::value,
                   "BlepOscillator requires a floating-point type");

public:

    /*************************************************************************
     * Construction
     *************************************************************************/

    /**
     * @brief Default constructor.
     *
     * @details
     * Initialises all parameters to their default ranges. No sample rate is
     * set; call setSampleRate() and setFrequency() before rendering.
     *
     * Default state:
     * - Shape:      Sine
     * - Frequency:  ~440 Hz (normalised ≈ 0.023 on log scale [20, 20000])
     * - Amplitude:  1.0
     * - PulseWidth: 0.5 (50% duty cycle)
     */
    BlepOscillator() CASPI_ALLOCATING
    {
        initParameters();
    }

    /**
     * @brief Construct with explicit shape, sample rate, and frequency.
     *
     * @details
     * Convenience constructor for the common case where all three are known
     * at construction time. Equivalent to calling the default constructor
     * followed by setSampleRate(), setShape(), and setFrequency().
     *
     * @param shape  Initial waveform shape.
     * @param sr     Sample rate in Hz. Must be > 0.
     * @param hz     Oscillator frequency in Hz. Must be in (0, sr/2).
     *
     * @code
     *   BlepOscillator<float> osc(WaveShape::Saw, 44100.f, 440.f);
     *   float buf[512];
     *   osc.renderBlock(buf, 512);
     * @endcode
     */
    BlepOscillator (WaveShape shape, FloatType sr, FloatType hz)
    {
        initParameters();
        this->setSampleRate (sr);
        this->setShape (shape);
        this->setFrequency (hz);
    }

    /*************************************************************************
     * Configuration
     *************************************************************************/

    /**
     * @brief Select the output waveform shape.
     *
     * @details
     * Shape changes take effect immediately on the next rendered sample.
     * Switching to Triangle resets the leaky integrator to prevent a DC
     * transient on the first output block. Switching away from Triangle
     * does not reset the integrator (it will decay naturally).
     *
     * Switching between Square and Pulse has no audible effect; both use
     * the same computation with pulseWidth applied.
     *
     * @param newShape  Target waveform shape.
     *
     * @code
     *   osc.setShape(WaveShape::Triangle);
     *   // integrator is reset; first block has no DC offset
     * @endcode
     */
    void setShape (WaveShape newShape) noexcept CASPI_NON_BLOCKING
    {
        if (newShape == WaveShape::Triangle && shape != WaveShape::Triangle)
        {
            triangleIntegrator = FloatType (1);
        }

        shape = newShape;
    }

    /**
     * @brief Returns the current waveform shape.
     *
     * @return Current WaveShape enum value.
     */
    CASPI_NO_DISCARD WaveShape getShape() const noexcept CASPI_NON_BLOCKING
    {
        return shape;
    }

    /**
     * @brief Set frequency in Hz, bypassing parameter smoothing.
     *
     * @details
     * Converts @p hz to a normalised log value, writes it into the frequency
     * parameter, and snaps the smoother to the target immediately (skip(1000)).
     * Also recomputes phase.increment and the Triangle leak coefficient.
     *
     * This must be called for initialisation. Without it, frequency.value()
     * returns the log-scale minimum (~20 Hz) until the smoother converges,
     * which would overwrite phase.increment with a wrong value on every
     * renderSample() call.
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

        const FloatType norm = (std::log (hz)                  - std::log (FloatType (20)))
                             / (std::log (FloatType (20000))   - std::log (FloatType (20)));

        frequency.setBaseNormalised (std::max (FloatType (0), std::min (norm, FloatType (1))));
        frequency.skip (1000);

        const FloatType fs = this->getSampleRate();
        phase.increment    = hz / fs;
        triangleLeak       = detail::leakCoeff (hz, fs);
    }

    /**
     * @brief Set the phase offset applied on resetPhase() and forceSync().
     *
     * @details
     * The offset is normalised to [0, 1) via fmod. It does not immediately
     * alter the running phase; it takes effect on the next resetPhase() or
     * forceSync() call.
     *
     * @param offset  Phase offset in [0, 1). Values outside [0, 1) are
     *                wrapped via fmod(abs(offset), 1).
     *
     * @code
     *   osc.setPhaseOffset(0.25f);  // start at 90 degrees
     *   osc.resetPhase();
     * @endcode
     */
    void setPhaseOffset (FloatType offset) noexcept CASPI_NON_BLOCKING
    {
        phaseOffset = std::fmod (std::abs (offset), FloatType (1));
    }

    /**
     * @brief Override from SampleRateAware. Recomputes phase increment and
     *        Triangle leak coefficient.
     *
     * @details
     * Called by the audio engine on startup or sample rate change. Also
     * call manually if constructing the oscillator before the sample rate
     * is known.
     *
     * @param newRate  Sample rate in Hz. Must be > 0.
     *
     * @note If called after setFrequency(), the phase increment is
     *       recomputed from the current frequency.value(). Call
     *       setFrequency() again if the smoother has not yet converged.
     */
    void setSampleRate (FloatType newRate) override
    {
        Core::SampleRateAware<FloatType>::setSampleRate (newRate);
        const FloatType hz = frequency.value();
        phase.increment    = hz / newRate;
        triangleLeak       = detail::leakCoeff (hz, newRate);
    }

    /**
     * @brief Reset phase to phaseOffset.
     *
     * @details
     * Clears syncCorrection. Does not reset the Triangle integrator; use
     * setShape(WaveShape::Triangle) to zero the integrator cleanly, or
     * accept the natural integrator decay.
     *
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
        phase.phase    = phaseOffset;
        syncCorrection = FloatType (0);
    }

    /**
     * @brief Returns true if the phase wrapped on the most recent
     *        renderSample() call.
     *
     * @details
     * Use this flag to drive hard sync of a secondary oscillator. The flag
     * is updated by renderSample() only; it is not updated by renderBlock().
     * For block-level sync, use the sync_indices returned by
     * render_hard_sync() in the Python bindings, or implement a per-sample
     * loop manually.
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
     * @brief Force an immediate phase reset with a one-sample discontinuity
     *        correction.
     *
     * @details
     * Computes the jump height at the current phase position and stores it
     * as a correction term (syncCorrection) applied on the next rendered
     * sample or block. This suppresses the worst of the click artefact
     * that would otherwise occur at a hard reset.
     *
     * Shape-specific behaviour:
     * - Saw: correction proportional to the jump at the current phase.
     * - Square / Pulse: correction proportional to the output polarity
     *   change at the reset point.
     * - Triangle: integrator zeroed; no correction applied (the integrator
     *   state is the waveform state, zeroing is the cleanest reset).
     * - Sine: phase is reset; no correction (sine is continuous).
     *
     * @note forceSync() is intended for hard sync driven by a primary
     *       oscillator. For note-on resets with no primary, use resetPhase().
     *
     * @code
     *   // Hard sync pattern — call inside the render loop
     *   primary.renderSample();
     *   if (primary.phaseWrapped())
     *       secondary.forceSync();
     *   float s = secondary.renderSample();
     * @endcode
     */
    void forceSync() noexcept CASPI_NON_BLOCKING
    {
        const FloatType p  = phase.phase;
        const FloatType dt = phase.increment;

        if (shape == WaveShape::Saw)
        {
            syncCorrection = -(FloatType (2) * p - FloatType (1)) * dt;
        }
        else if (shape == WaveShape::Square || shape == WaveShape::Pulse)
        {
            const FloatType pw         = pulseWidth.value();
            const FloatType currentOut = (p < pw) ? FloatType (-1) : FloatType (1);
            syncCorrection = (FloatType (-1) - currentOut) * dt;
        }
        else if (shape == WaveShape::Triangle)
        {
            triangleIntegrator = FloatType (0);
        }

        phase.phase = phaseOffset;
    }

    /*************************************************************************
     * Per-sample rendering
     *************************************************************************/

    /**
     * @brief Render one sample.
     *
     * @details
     * Steps all three parameter smoothers (amplitude, frequency, pulseWidth)
     * once, recomputes phase.increment from the smoothed frequency, advances
     * phase, and evaluates the waveform. The phaseWrapped() flag is updated
     * after each call.
     *
     * This is the scalar path called by Producer::render(AudioBuffer&).
     * For direct buffer filling without an AudioBuffer, prefer renderBlock()
     * to amortise the per-block parameter step overhead.
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
    FloatType renderSample() noexcept CASPI_NON_BLOCKING override
    {
        amplitude.process();
        frequency.process();
        pulseWidth.process();

        const FloatType hz = frequency.value();
        const FloatType fs = this->getSampleRate();
        phase.increment    = hz / fs;

        if (shape == WaveShape::Triangle)
        {
            triangleLeak = detail::leakCoeff (hz, fs);
        }

        const FloatType correction = syncCorrection;
        syncCorrection = FloatType (0);

        const FloatType p = phase.phase;
        phase.advanceAndWrap (FloatType (1));
        wrapped = (phase.phase < p);

        return computeSample (p, phase.increment, correction) * amplitude.value();
    }

    /*************************************************************************
     * Block rendering (preferred path for audio threads)
     *************************************************************************/

    /**
     * @brief Render @p numSamples into a raw output buffer.
     *
     * @details
     * Steps each parameter smoother once per block (not per sample),
     * reducing smoother overhead by a factor of numSamples compared to
     * calling renderSample() in a loop.
     *
     * The scalar loop body is branch-free for Sine, Saw, Square, and Pulse
     * (branchless polyBlep()), enabling compiler auto-vectorisation over
     * consecutive samples. Triangle runs on the scalar path due to the
     * integrator feedback dependency.
     *
     * @param output      Pointer to a buffer of at least @p numSamples
     *                    elements. Must not be null. Must not alias internal
     *                    oscillator state (CASPI_RESTRICT is applied in the
     *                    implementation).
     * @param numSamples  Number of samples to generate. Must be > 0.
     *
     * @note phaseWrapped() is not updated by renderBlock(). If you need
     *       per-sample wrap detection for hard sync inside a block, use a
     *       manual renderSample() loop or the render_hard_sync() helper in
     *       the Python bindings.
     *
     * @code
     *   float buffer[512];
     *   osc.renderBlock(buffer, 512);
     *
     *   // Block-level amplitude modulation — set once before the block
     *   osc.amplitude.addModulation(envValue - 1.f);
     *   osc.renderBlock(buffer, 512);
     *   osc.amplitude.clearModulation();
     * @endcode
     */
    void renderBlock (FloatType* CASPI_RESTRICT output,
                      int                       numSamples) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (output != nullptr, "Output buffer must not be null");
        CASPI_ASSERT (numSamples > 0,    "numSamples must be positive");

        amplitude.process();
        frequency.process();
        pulseWidth.process();

        const FloatType hz  = frequency.value();
        const FloatType fs  = this->getSampleRate();
        const FloatType dt  = hz / fs;
        const FloatType amp = amplitude.value();
        phase.increment     = dt;

        if (shape == WaveShape::Triangle)
        {
            triangleLeak = detail::leakCoeff (hz, fs);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const FloatType correction = syncCorrection;
            syncCorrection = FloatType (0);

            const FloatType p = phase.phase;
            phase.advanceAndWrap (FloatType (1));

            output[i] = computeSample (p, dt, correction) * amp;
        }
    }

    /*************************************************************************
     * Public modulatable parameters
     *************************************************************************/

    /**
     * @brief Output amplitude in [0, 1]. Linear scale.
     *
     * @details
     * Stepped once per renderSample() call or once per renderBlock() call.
     * Modulate per-sample:
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
     * Stepped once per renderSample() call or once per renderBlock() call.
     * phase.increment is recomputed from frequency.value() on each step.
     * Modulate per-sample:
     * @code
     *   osc.frequency.addModulation(pitchBendNorm);
     *   float s = osc.renderSample();
     *   osc.frequency.clearModulation();
     * @endcode
     *
     * @note For initialisation, use setFrequency() rather than writing to
     *       this parameter directly, to avoid a slow smoother convergence
     *       period at the wrong frequency.
     */
    Core::ModulatableParameter<FloatType> frequency;

    /**
     * @brief Pulse width in [0.01, 0.99]. Linear scale.
     *
     * @details
     * Only audible on Square and Pulse shapes. At 0.5 produces a symmetric
     * square wave. Stepped once per renderSample() or renderBlock() call.
     *
     * @code
     *   osc.setShape(WaveShape::Pulse);
     *   osc.pulseWidth.setBaseNormalised(0.1f);  // narrow pulse
     * @endcode
     */
    Core::ModulatableParameter<FloatType> pulseWidth;

private:

    /*************************************************************************
     * computeSample — waveform evaluation at a given phase
     *************************************************************************/

    /**
     * @brief Evaluate the waveform at phase @p p with PolyBLEP correction.
     *
     * @param p           Phase in [0, 1) at the start of this sample
     *                    (before advancement).
     * @param dt          Phase increment (frequency / sampleRate).
     * @param correction  One-sample sync discontinuity correction from
     *                    forceSync(). Zero on normal samples.
     * @return            Waveform value before amplitude scaling.
     */
    FloatType computeSample (FloatType p,
                              FloatType dt,
                              FloatType correction) noexcept CASPI_NON_BLOCKING
    {
        switch (shape)
        {
            case WaveShape::Sine:
            {
                return std::sin (Constants::TWO_PI<FloatType> * p);
            }

            case WaveShape::Saw:
            {
                const FloatType naive = FloatType (2) * p - FloatType (1);
                return naive - detail::polyBlep (p, dt) + correction;
            }

            case WaveShape::Square:
            case WaveShape::Pulse:
            {
                const FloatType pw    = pulseWidth.value();
                const FloatType naive = (p < pw) ? FloatType (-1) : FloatType (1);
                const FloatType p2    = std::fmod (p + (FloatType (1) - pw), FloatType (1));

                return naive
                     - detail::polyBlep (p,  dt)
                     + detail::polyBlep (p2, dt)
                     + correction;
            }

            case WaveShape::Triangle:
            {
                // Build band-limited square at 50% duty cycle
                const FloatType naive = (p < FloatType (0.5)) ? FloatType (-1) : FloatType (1);
                const FloatType p2    = std::fmod (p + FloatType (0.5), FloatType (1));
                const FloatType sq    = naive
                                      - detail::polyBlep (p,  dt)
                                      + detail::polyBlep (p2, dt);

                // Leaky integration: integrator(n) = integrator(n-1)*leak + 4*dt*sq(n)
                triangleIntegrator = triangleIntegrator * triangleLeak
                                   + FloatType (4) * dt * sq;
                return triangleIntegrator + correction;
            }

            default:
                return FloatType (0);
        }
    }

    /**
     * @brief Shared parameter initialisation called from all constructors.
     */
    void initParameters() CASPI_ALLOCATING
    {
        amplitude.setRange (FloatType (0), FloatType (1));
        amplitude.setBaseNormalised (FloatType (1));

        frequency.setRange (FloatType (20), FloatType (20000),
                            Core::ParameterScale::Logarithmic);
        frequency.setBaseNormalised (FloatType (0.023)); // ≈ 440 Hz

        pulseWidth.setRange (FloatType (0.01), FloatType (0.99));
        pulseWidth.setBaseNormalised (FloatType (0.5));
    }

    /*************************************************************************
     * State
     *************************************************************************/

    Phase<FloatType> phase              {};              ///< Phase accumulator and increment.
    WaveShape        shape              { WaveShape::Sine };
    FloatType        phaseOffset        { FloatType (0) }; ///< Applied on resetPhase() / forceSync().
    FloatType        syncCorrection     { FloatType (0) }; ///< One-sample correction from forceSync().
    FloatType        triangleIntegrator { FloatType (1) }; ///< Leaky integrator state for Triangle.
    FloatType        triangleLeak       { FloatType (0.9999) }; ///< Recomputed by setFrequency() / setSampleRate().
    bool             wrapped            { false };         ///< Updated by renderSample(); not by renderBlock().
};

} // namespace Oscillators
} // namespace CASPI

#endif // CASPI_BLEPOSCILLATOR_H