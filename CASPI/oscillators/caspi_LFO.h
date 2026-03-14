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

 * @file   caspi_LFO.h
 * @author CS Islay
 * @brief  Low-frequency oscillator — control signal source.
 *
 * @details
 * LFO<FloatType> is a control-rate modulation source. It does not inherit
 * Core::Producer because:
 *
 * - Producer::render(AudioBuffer&) implies audio-rate sample generation
 *   written into a multichannel frame buffer. An LFO produces a single
 *   modulation scalar per sample, not a buffer of audio content.
 *
 * - The PerFrame traversal policy would broadcast one LFO value to all
 *   channels of an AudioBuffer. This is mechanically valid but semantically
 *   wrong: LFO output is consumed by a ModulatableParameter or a mixer
 *   stage, not stored in an audio frame.
 *
 * - An ADSR envelope has the same relationship to audio: it runs at audio
 *   rate but produces a control signal, not audio content.
 *
 * Inherits only Core::SampleRateAware<FloatType>. The public interface
 * (renderSample, renderBlock) matches other CASPI generators for API
 * consistency. The caller is responsible for routing the output.
 *
 * ### Shapes
 * All shapes are computed analytically from phase — no wavetable, no BLEP.
 * | Shape      | Range     | Notes                                  |
 * |------------|-----------|----------------------------------------|
 * | Sine       | [-1,  1]  |                                        |
 * | Triangle   | [-1,  1]  | Linear rise and fall, 50% duty         |
 * | Saw        | [-1,  1]  | -1 at phase=0, +1 at phase=1           |
 * | ReverseSaw | [-1,  1]  | +1 at phase=0, -1 at phase=1           |
 * | Square     | {-1, +1}  | Exact; no intermediate values          |
 *
 * ### Output modes
 * | Mode     | Range  | Formula                   | Typical use               |
 * |----------|--------|---------------------------|---------------------------|
 * | Bipolar  | [-1,1] | (default)                 | Pitch, pan, FM depth      |
 * | Unipolar | [0, 1] | bipolar * 0.5 + 0.5       | Amplitude, filter cutoff  |
 *
 * ### Modulatable parameters
 * | Parameter | Range                              | Scale       |
 * |-----------|------------------------------------|-------------|
 * | rate      | [LfoRates::MINIMUM, MAXIMUM] Hz    | Logarithmic |
 * | amplitude | [0, 1]                             | Linear      |
 *
 * ### Tempo sync
 * setTempoSync(bpm, beatsPerCycle) computes:
 * @code
 *   hz = bpm / (60 * beatsPerCycle)
 * @endcode
 * Re-call on every tempo change; sync is not dynamic.
 *
 * ### One-shot mode
 * Phase advances to 1.0 then halts. phaseWrapped() returns true exactly once.
 * resetPhase() or forceSync() re-triggers.
 *
 * ### Thread safety
 * - setRate(), setAmplitude(), setShape(), setOutputMode(), setOneShot(),
 *   setPhaseOffset(), setTempoSync(), setSampleRate() — call from the audio
 *   thread or before streaming starts. Not thread-safe with concurrent
 *   renderSample() / renderBlock() calls.
 * - rate / amplitude parameter writes (setBaseNormalised, addModulation) —
 *   the underlying atomic base value is thread-safe; smoother state is not.
 * - renderSample() / renderBlock() — audio thread only.
 *
 * ### Typical usage
 * @code
 *   CASPI::Oscillators::LFO<float> lfo(44100.f, 5.0f,
 *       CASPI::Oscillators::LfoShape::Sine);
 *
 *   // Per-sample modulation
 *   float mod = lfo.renderSample();
 *   targetOscillator.frequency.addModulation(mod * depth);
 *   targetOscillator.frequency.clearModulation();
 *
 *   // Tempo sync
 *   lfo.setTempoSync(120.f, 4.f);  // 0.5 Hz — one cycle per bar at 120 BPM
 *
 *   // One-shot (e.g. pitch envelope on note-on)
 *   lfo.setOneShot(true);
 *   lfo.resetPhase();
 * @endcode
 *****************************************************************************/

#ifndef CASPI_LFO_H
#define CASPI_LFO_H

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Parameter.h"
#include "core/caspi_Phase.h"

#include <cmath>
#include <type_traits>

namespace CASPI
{
namespace Oscillators
{

/*******************************************************************************
 * LfoShape
 ******************************************************************************/

/**
 * @brief Selects the LFO waveform shape at runtime.
 *
 * @details
 * All shapes are computed analytically from phase. Shape changes take effect
 * on the next renderSample() call with no state reset (no integrator to
 * clear, unlike BlepOscillator Triangle).
 */
enum class LfoShape
{
    Sine,
    Triangle,
    Saw,        ///< Rising: -1 at phase=0, +1 at phase=1.
    ReverseSaw, ///< Falling: +1 at phase=0, -1 at phase=1.
    Square      ///< Exact {-1, +1}; no intermediate values, no BLEP.
};

/*******************************************************************************
 * LfoOutputMode
 ******************************************************************************/

/**
 * @brief Selects the LFO output range.
 *
 * @details
 * Conversion from Bipolar to Unipolar: `out = bipolar * 0.5 + 0.5`.
 * The conversion is applied after shape computation and before amplitude
 * scaling in computeOutput().
 */
enum class LfoOutputMode
{
    Bipolar,  ///< Output in [-1, 1]. Default. Use for pitch, pan, FM depth.
    Unipolar  ///< Output in [0, 1]. Use for amplitude, filter cutoff, pulse width.
};

/*******************************************************************************
 * LFO
 ******************************************************************************/

/**
 * @brief Low-frequency oscillator — control signal source.
 *
 * @details
 * Produces a modulation scalar per sample. Does not inherit Core::Producer;
 * see file header for rationale. Inherits only Core::SampleRateAware<FloatType>.
 *
 * @tparam FloatType  float or double.
 *
 * @code
 *   CASPI::Oscillators::LFO<float> lfo(44100.f, 2.f);
 *   lfo.setShape(CASPI::Oscillators::LfoShape::Triangle);
 *   lfo.setOutputMode(CASPI::Oscillators::LfoOutputMode::Unipolar);
 *
 *   float buffer[512];
 *   lfo.renderBlock(buffer, 512);
 * @endcode
 */
template <typename FloatType>
class LFO : public Core::SampleRateAware<FloatType>
{
    CASPI_STATIC_ASSERT (std::is_floating_point<FloatType>::value,
                           "LFO requires a floating-point type");

public:

    /*************************************************************************
     * Construction
     *************************************************************************/

    /**
     * @brief Default constructor.
     *
     * @details
     * Initialises all parameters to defaults. No sample rate is set; call
     * setSampleRate() and setRate() before rendering.
     *
     * Default state:
     * - Shape:      Sine
     * - Mode:       Bipolar
     * - Rate:       LfoRates::DEFAULT Hz
     * - Amplitude:  1.0
     * - One-shot:   false
     */
    LFO() noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
    }

    /**
     * @brief Construct with sample rate and rate in Hz.
     *
     * @param sampleRate  Audio sample rate in Hz. Must be > 0.
     * @param rateHz      LFO rate in Hz. Clamped to
     *                    [LfoRates::MINIMUM, LfoRates::MAXIMUM].
     *
     * @code
     *   LFO<float> lfo(44100.f, 0.5f);  // 0.5 Hz — one cycle per 2 s
     * @endcode
     */
    LFO (FloatType sampleRate, FloatType rateHz) noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
        this->setSampleRate (sampleRate);
        setRate (rateHz);
    }

    /**
     * @brief Construct with sample rate, rate, shape, and output mode.
     *
     * @details
     * Calls resetPhase() after configuration so the LFO starts cleanly
     * at phaseOffset (default 0). This is the preferred constructor when
     * all parameters are known at construction time.
     *
     * @param sampleRate  Audio sample rate in Hz. Must be > 0.
     * @param rateHz      LFO rate in Hz. Clamped to
     *                    [LfoRates::MINIMUM, LfoRates::MAXIMUM].
     * @param s           Initial waveform shape.
     * @param m           Output mode. Defaults to Bipolar.
     *
     * @code
     *   LFO<float> lfo(44100.f, 5.f,
     *       LfoShape::Sine, LfoOutputMode::Unipolar);
     * @endcode
     */
    LFO (FloatType      sampleRate,
         FloatType      rateHz,
         LfoShape       s,
         LfoOutputMode  m = LfoOutputMode::Bipolar) noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
        this->setSampleRate (sampleRate);
        setRate (rateHz);
        setShape (s);
        setOutputMode (m);
        resetPhase();
    }

    /*************************************************************************
     * Configuration
     *************************************************************************/

    /**
     * @brief Set the LFO rate in Hz, bypassing parameter smoothing.
     *
     * @details
     * Converts @p hz to a normalised log value, writes into the rate
     * parameter, and snaps the smoother. Also recomputes phase.increment.
     * The rate is clamped to [LfoRates::MINIMUM, LfoRates::MAXIMUM] before
     * conversion.
     *
     * For real-time rate modulation, write to the rate parameter directly:
     * @code
     *   lfo.rate.addModulation(tempoMod);
     *   float s = lfo.renderSample();
     *   lfo.rate.clearModulation();
     * @endcode
     *
     * @param hz  Rate in Hz. Clamped; values outside the range are not an error.
     *
     * @note setSampleRate() must be called before setRate() for
     *       phase.increment to be computed correctly.
     */
    void setRate (FloatType hz) noexcept CASPI_NON_BLOCKING
    {
        const FloatType clamped = clampRate (hz);
        rate.setBaseNormalised (hzToNormLog (clamped));
        rate.skip (1000);
        phase.increment = (this->getSampleRate() > FloatType (0))
                              ? rate.value() / this->getSampleRate()
                              : FloatType (0);
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
     * @brief Set the waveform shape.
     *
     * @details
     * Takes effect on the next renderSample() call. No state is reset.
     *
     * @param s  Target LfoShape.
     */
    void setShape (LfoShape s) noexcept CASPI_NON_BLOCKING       { shape = s; }

    /**
     * @brief Set the output mode (Bipolar or Unipolar).
     *
     * @details
     * Takes effect on the next renderSample() call.
     *
     * @param m  LfoOutputMode::Bipolar or LfoOutputMode::Unipolar.
     */
    void setOutputMode (LfoOutputMode m) noexcept CASPI_NON_BLOCKING { outputMode = m; }

    /**
     * @brief Enable or disable one-shot mode.
     *
     * @details
     * In one-shot mode the LFO completes one cycle then halts. All output
     * after the halt is exactly 0. isHalted() returns true. Call resetPhase()
     * or forceSync() to re-trigger.
     *
     * @param enable  true to enable one-shot, false for free-running (default).
     *
     * @code
     *   lfo.setOneShot(true);
     *   lfo.resetPhase();
     *   // Render until halted
     *   while (!lfo.isHalted())
     *       float s = lfo.renderSample();
     * @endcode
     */
    void setOneShot (bool enable) noexcept CASPI_NON_BLOCKING { oneShot = enable; }

    /**
     * @brief Set the phase offset applied on resetPhase() and forceSync().
     *
     * @details
     * Wrapped into [0, 1) via fmod. Does not immediately alter the running
     * phase; takes effect on the next resetPhase() or forceSync() call.
     *
     * @param offset  Phase offset in [0, 1). Values outside are wrapped via
     *                fmod(abs(offset), 1).
     *
     * @code
     *   lfo.setPhaseOffset(0.25f);  // start at 90 degrees
     *   lfo.resetPhase();
     * @endcode
     */
    void setPhaseOffset (FloatType offset) noexcept CASPI_NON_BLOCKING
    {
        phaseOffset = std::fmod (std::abs (offset), FloatType (1));
    }

    /**
     * @brief Compute and set the rate from a BPM and beats-per-cycle value.
     *
     * @details
     * Formula: `hz = bpm / (60 * beatsPerCycle)`.
     * Re-call on every tempo change; the rate is not dynamically linked to
     * a tempo source.
     *
     * Common values:
     * | BPM | beatsPerCycle | Rate (Hz) | Period    |
     * |-----|---------------|-----------|-----------|
     * | 120 | 1             | 2.0 Hz    | 0.5 s     |
     * | 120 | 4             | 0.5 Hz    | 2.0 s (1 bar) |
     * | 120 | 16            | 0.125 Hz  | 8.0 s     |
     *
     * @param bpm            Tempo in beats per minute. Must be > 0.
     * @param beatsPerCycle  Number of beats per LFO cycle. Must be > 0.
     *
     * @code
     *   lfo.setTempoSync(120.f, 4.f);  // 1 cycle per bar at 120 BPM
     * @endcode
     */
    void setTempoSync (FloatType bpm, FloatType beatsPerCycle) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (bpm           > FloatType (0), "BPM must be positive");
        CASPI_ASSERT (beatsPerCycle > FloatType (0), "beatsPerCycle must be positive");
        setRate (bpm / (FloatType (60) * beatsPerCycle));
    }

    /*************************************************************************
     * SampleRateAware override
     *************************************************************************/

    /**
     * @brief Override from SampleRateAware. Recomputes phase increment.
     *
     * @details
     * Called by the audio engine on startup or sample rate change. Also
     * call manually if constructing the LFO before the sample rate is known.
     *
     * @param newRate  Sample rate in Hz. Must be > 0.
     */
    void setSampleRate (FloatType newRate) override CASPI_NON_BLOCKING
    {
        Core::SampleRateAware<FloatType>::setSampleRate (newRate);
        const FloatType hz = rate.value();
        phase.increment    = (newRate > FloatType (0)) ? hz / newRate : FloatType (0);
    }

    /*************************************************************************
     * Phase control
     *************************************************************************/

    /**
     * @brief Reset phase to phaseOffset and clear halted / wrapped state.
     *
     * @details
     * Clears isHalted() and phaseWrapped(). Use on note-on events or to
     * synchronise the LFO to a beat grid.
     *
     * @code
     *   void noteOn() {
     *       lfo.resetPhase();
     *   }
     * @endcode
     */
    void resetPhase() noexcept CASPI_NON_BLOCKING
    {
        phase.phase = phaseOffset;
        halted      = false;
        wrapped     = false;
    }

    /**
     * @brief Alias for resetPhase().
     *
     * @details
     * Provided for API consistency with BlepOscillator and
     * WavetableOscillator. Identical in behaviour to resetPhase(); the LFO
     * has no discontinuity correction to apply.
     */
    void forceSync() noexcept CASPI_NON_BLOCKING { resetPhase(); }

    /**
     * @brief Returns true if the phase wrapped on the most recent
     *        renderSample() call.
     *
     * @details
     * The flag is cleared at the start of each renderSample() call and set
     * if the phase wrapped during that call. It is not updated by
     * renderBlock(). In one-shot mode this flag fires exactly once (at the
     * wrap that triggers the halt).
     *
     * @return true if a phase wrap occurred on the last renderSample() call.
     *
     * @code
     *   // Count LFO cycles over one second
     *   int wraps = 0;
     *   for (int i = 0; i < SR; ++i) {
     *       lfo.renderSample();
     *       if (lfo.phaseWrapped()) ++wraps;
     *   }
     * @endcode
     */
    CASPI_NO_DISCARD bool phaseWrapped() const noexcept CASPI_NON_BLOCKING { return wrapped; }

    /**
     * @brief Returns true if the LFO has halted in one-shot mode.
     *
     * @details
     * Always false when one-shot mode is disabled. Becomes true after the
     * first phase wrap in one-shot mode. Cleared by resetPhase() or
     * forceSync().
     *
     * @return true if the LFO is halted (one-shot completed).
     */
    CASPI_NO_DISCARD bool isHalted() const noexcept CASPI_NON_BLOCKING { return halted; }

    /*************************************************************************
     * Rendering
     *************************************************************************/

    /**
     * @brief Render one control-signal sample.
     *
     * @details
     * Steps the rate and amplitude smoothers once per call, recomputes
     * phase.increment from the smoothed rate, advances phase, and evaluates
     * the waveform. In one-shot mode returns 0 after the cycle completes.
     *
     * phaseWrapped() is updated each call; isHalted() becomes true on the
     * call where the one-shot wrap is detected.
     *
     * @return Waveform sample scaled by amplitude.value(). In Bipolar mode
     *         approximately in [-amplitude, +amplitude]; in Unipolar mode
     *         approximately in [0, amplitude].
     *
     * @note Not safe to call concurrently from multiple threads.
     *
     * @code
     *   // Per-sample modulation pattern
     *   lfo.rate.addModulation(tempoMod);
     *   float mod = lfo.renderSample();
     *   lfo.rate.clearModulation();
     *   targetOsc.frequency.addModulation(mod * depth);
     * @endcode
     */
    CASPI_NO_DISCARD FloatType renderSample() noexcept CASPI_NON_BLOCKING
    {
        wrapped = false;

        if (halted)
        {
            return FloatType (0);
        }

        rate.process();
        amplitude.process();

        const FloatType hz = rate.value();
        const FloatType fs = this->getSampleRate();
        phase.increment    = (fs > FloatType (0)) ? hz / fs : FloatType (0);

        const FloatType pBefore = phase.phase;
        phase.advanceAndWrap (FloatType (1));
        wrapped = (phase.phase < pBefore);

        if (oneShot && wrapped)
        {
            halted      = true;
            phase.phase = FloatType (0);
        }

        return computeOutput (pBefore) * amplitude.value();
    }

    /**
     * @brief Render @p numSamples of modulation values into a raw buffer.
     *
     * @details
     * Delegates to renderSample() for each sample. Steps smoothers once per
     * sample (same rate as renderSample()). Suitable for pre-computing a
     * block of modulation values to apply per-sample to a parameter, or for
     * the Python bindings.
     *
     * phaseWrapped() reflects the state after the final sample only. If you
     * need per-sample wrap detection inside the block, use a manual
     * renderSample() loop.
     *
     * @param output      Pointer to a buffer of at least @p numSamples
     *                    elements. Must not be null.
     * @param numSamples  Number of samples to generate. Must be > 0.
     *
     * @code
     *   float modBuffer[512];
     *   lfo.renderBlock(modBuffer, 512);
     *
     *   for (int i = 0; i < 512; ++i) {
     *       target.frequency.addModulation(modBuffer[i] * depth);
     *       target.renderSample();
     *       target.frequency.clearModulation();
     *   }
     * @endcode
     */
    void renderBlock (FloatType* CASPI_RESTRICT output,
                      int                       numSamples) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (output     != nullptr, "Output buffer must not be null");
        CASPI_ASSERT (numSamples >  0,       "numSamples must be positive");

        for (int i = 0; i < numSamples; ++i)
        {
            output[i] = renderSample();
        }
    }

    /*************************************************************************
     * Public modulatable parameters
     *************************************************************************/

    /**
     * @brief LFO rate in Hz. Logarithmic range [LfoRates::MINIMUM, MAXIMUM].
     *
     * @details
     * Stepped once per renderSample() call. phase.increment is recomputed
     * from rate.value() on each step.
     *
     * @note For initialisation, use setRate() rather than writing to this
     *       parameter directly, to avoid a slow smoother convergence period
     *       at the wrong rate.
     *
     * @code
     *   lfo.rate.addModulation(tempoMod);
     *   float s = lfo.renderSample();
     *   lfo.rate.clearModulation();
     * @endcode
     */
    Core::ModulatableParameter<FloatType> rate;

    /**
     * @brief Output amplitude in [0, 1]. Linear scale.
     *
     * @details
     * Stepped once per renderSample() call. Applied after shape computation
     * and output mode conversion.
     *
     * @code
     *   lfo.amplitude.addModulation(envOut - 1.f);
     *   float s = lfo.renderSample();
     *   lfo.amplitude.clearModulation();
     * @endcode
     */
    Core::ModulatableParameter<FloatType> amplitude;

private:

    /*************************************************************************
     * Shape computation
     *************************************************************************/

    /**
     * @brief Evaluate the current shape at phase @p p and apply output mode.
     *
     * @details
     * Called from renderSample() with the pre-advance phase. The Unipolar
     * conversion (`out * 0.5 + 0.5`) is applied after shape evaluation.
     * Amplitude scaling is applied by the caller.
     *
     * @param p  Phase in [0, 1) at the start of this sample.
     * @return   Shape value after output mode conversion, before amplitude scaling.
     */
    CASPI_ALWAYS_INLINE FloatType computeOutput (FloatType p) const noexcept CASPI_NON_BLOCKING
    {
        FloatType out = FloatType (0);

        switch (shape)
        {
            case LfoShape::Sine:
                out = std::sin (Constants::TWO_PI<FloatType> * p);
                break;
            case LfoShape::Triangle:
                out = (p < FloatType (0.5))
                          ? (FloatType (4) * p - FloatType (1))
                          : (FloatType (3) - FloatType (4) * p);
                break;
            case LfoShape::Saw:
                out = FloatType (2) * p - FloatType (1);
                break;
            case LfoShape::ReverseSaw:
                out = FloatType (1) - FloatType (2) * p;
                break;
            case LfoShape::Square:
                out = (p < FloatType (0.5)) ? FloatType (-1) : FloatType (1);
                break;
            default:
                out = FloatType (0);
                break;
        }

        if (outputMode == LfoOutputMode::Unipolar)
        {
        out = out * FloatType (0.5) + FloatType (0.5);
        }

        return out;
    }

    /*************************************************************************
     * Parameter helpers
     *************************************************************************/

    /**
     * @brief Initialise all parameters and snap smoothers.
     *
     * @details
     * Called from every constructor. Snapping smoothers (skip(1000)) ensures
     * value() is correct on the first render call without a warm-up period,
     * matching the BlepOscillator and WavetableOscillator pattern.
     */
    void initParameters() noexcept CASPI_NON_BLOCKING
    {
        rate.setRange (Constants::LfoRates<FloatType>::MINIMUM,
                       Constants::LfoRates<FloatType>::MAXIMUM,
                       Core::ParameterScale::Logarithmic);
        rate.setBaseNormalised (hzToNormLog (Constants::LfoRates<FloatType>::DEFAULT));
        rate.skip (1000);

        amplitude.setRange (FloatType (0), FloatType (1));
        amplitude.setBaseNormalised (FloatType (1));
        amplitude.skip (1000);
    }

    /**
     * @brief Convert Hz to normalised [0, 1] on the log rate scale.
     *
     * @param hz  Rate in Hz. Clamped at boundaries.
     * @return    Normalised value in [0, 1].
     */
    CASPI_ALWAYS_INLINE FloatType hzToNormLog (FloatType hz) const noexcept CASPI_NON_BLOCKING
    {
        if (hz <= Constants::LfoRates<FloatType>::MINIMUM) return FloatType (0);
        if (hz >= Constants::LfoRates<FloatType>::MAXIMUM) return FloatType (1);
        return (std::log (hz) - std::log (Constants::LfoRates<FloatType>::MINIMUM))
             / (std::log (Constants::LfoRates<FloatType>::MAXIMUM)
                - std::log (Constants::LfoRates<FloatType>::MINIMUM));
    }

    /**
     * @brief Convert normalised [0, 1] back to Hz on the log rate scale.
     *
     * @param norm  Normalised value in [0, 1].
     * @return      Rate in Hz.
     */
    CASPI_ALWAYS_INLINE FloatType normLogToHz (FloatType norm) const noexcept CASPI_NON_BLOCKING
    {
        return Constants::LfoRates<FloatType>::MINIMUM
             * std::pow (Constants::LfoRates<FloatType>::MAXIMUM
                         / Constants::LfoRates<FloatType>::MINIMUM, norm);
    }

    /**
     * @brief Clamp a rate value to [LfoRates::MINIMUM, LfoRates::MAXIMUM].
     *
     * @param hz  Unclamped rate in Hz.
     * @return    Clamped rate in Hz.
     */
    CASPI_ALWAYS_INLINE FloatType clampRate (FloatType hz) const noexcept CASPI_NON_BLOCKING
    {
        if (hz < Constants::LfoRates<FloatType>::MINIMUM) return Constants::LfoRates<FloatType>::MINIMUM;
        if (hz > Constants::LfoRates<FloatType>::MAXIMUM) return Constants::LfoRates<FloatType>::MAXIMUM;
        return hz;
    }

    /*************************************************************************
     * State
     *************************************************************************/

    Phase<FloatType> phase       {};                          ///< Phase accumulator and increment.
    FloatType        phaseOffset { FloatType (0) };           ///< Applied on resetPhase() / forceSync().
    LfoShape         shape       { LfoShape::Sine };
    LfoOutputMode    outputMode  { LfoOutputMode::Bipolar };
    bool             oneShot     { false };
    bool             halted      { false };                   ///< true after one-shot cycle completes.
    bool             wrapped     { false };                   ///< Cleared at start of each renderSample().
};

} // namespace Oscillators
} // namespace CASPI

#endif // CASPI_LFO_H