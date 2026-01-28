#ifndef CASPI_OPERATOR_H
#define CASPI_OPERATOR_H

/*************************************************************************
 *  .d8888b.                             d8b
 * d88P  Y88b                            Y8P
 * 888    888
 * 888         8888b.  .d8888b  88888b.  888
 * 888            "88b 88K      888 "88b 888
 * 888    888 .d888888 "Y8888b. 888  888
 * Y88b  d88P 888  888      X88 888 d88P 888
 *  "Y8888P"  "Y888888  88888P' 88888P"  888
 *                              888
 *                              888
 *                              888
 *
 * @file caspi_Operator.h
 * @author CS Islay
 * @brief A modulation operator for FM/PM synthesis.
 *
 * This operator generates sine waves with support for Phase Modulation (PM)
 * and Frequency Modulation (FM). It can accept external modulation input and
 * includes self-modulation (feedback) for additional harmonic complexity.
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Phase.h"
#include "envelopes/caspi_Envelope.h"
#include <cmath>

namespace CASPI
{
    /// Modulation modes for the operator
    enum class ModulationMode
    {
        Phase,     ///< Phase modulation (PM) - modulation affects phase directly
        Frequency  ///< Frequency modulation (FM) - modulation affects instantaneous frequency
    };

    /**
     * @class Operator
     * @brief A sine wave oscillator with FM/PM synthesis capabilities.
     *
     * This operator can function as either a modulator or carrier in FM/PM synthesis.
     * It accepts external modulation input and can self-modulate via feedback.
     *
     * ## Key Concepts
     *
     * **Modulation Index**: Controls the depth/amount of modulation
     * - In PM: scales the phase deviation (in radians)
     * - In FM: scales the frequency deviation (in Hz)
     * - Typical range: 0.0 to 10.0
     *
     * **Modulation Depth**: Output amplitude multiplier (0.0 to 1.0)
     * - Controls the overall output level
     * - Does NOT affect modulation amount
     *
     * **Feedback**: Self-modulation using previous output
     * - Adds harmonic complexity and brightness
     * - Range: 0.0 (none) to ~5.0 (heavy distortion)
     *
     * ## Phase Modulation (PM)
     * ```
     * output = sin(phase + modulationIndex × modulationSignal)
     * ```
     * The modulation signal directly offsets the carrier's phase.
     *
     * ## Frequency Modulation (FM)
     * ```
     * instantFrequency = carrierFreq + (modulationIndex × modulationSignal)
     * output = sin(∫ instantFrequency dt)
     * ```
     * The modulation signal affects the instantaneous frequency.
     *
     * ## Usage Examples
     *
     * ### Basic Sine Wave
     * ```cpp
     * Operator<double> osc;
     * osc.setSampleRate(48000.0);
     * osc.setFrequency(440.0);
     * osc.setModulationDepth(1.0);
     *
     * double sample = osc.renderSample();  // Pure sine at 440 Hz
     * ```
     *
     * ### Phase Modulation (2:1 ratio)
     * ```cpp
     * // Modulator at 880 Hz
     * Operator<double> modulator;
     * modulator.setSampleRate(48000.0);
     * modulator.setFrequency(880.0);
     *
     * // Carrier at 440 Hz
     * Operator<double> carrier;
     * carrier.setSampleRate(48000.0);
     * carrier.setFrequency(440.0);
     * carrier.setModulationMode(ModulationMode::Phase);
     * carrier.setModulationIndex(3.0);  // 3 radians of phase deviation
     *
     * // Render
     * std::vector<double> modSignal(1024);
     * for (size_t i = 0; i < 1024; ++i)
     *     modSignal[i] = modulator.renderSample();
     *
     * carrier.setModulationInput(modSignal.data(), modSignal.size());
     * for (size_t i = 0; i < 1024; ++i)
     *     double output = carrier.renderSample();
     * ```
     *
     * ### Frequency Modulation (vibrato)
     * ```cpp
     * Operator<double> carrier;
     * carrier.setSampleRate(48000.0);
     * carrier.setFrequency(440.0);
     * carrier.setModulationMode(ModulationMode::Frequency);
     * carrier.setModulationIndex(1.0);
     *
     * // Create 5 Hz LFO with ±10 Hz deviation
     * std::vector<double> lfo(1024);
     * for (size_t i = 0; i < 1024; ++i)
     *     lfo[i] = 10.0 * sin(2π × 5.0 × i / 48000.0);
     *
     * carrier.setModulationInput(lfo.data(), lfo.size());
     * // Output will vibrate ±10 Hz around 440 Hz
     * ```
     *
     * @tparam FloatType Floating point type (float or double)
     * @tparam Policy Traversal policy (default: PerSample)
     */
    template <typename FloatType, typename Policy = Core::Traversal::PerSample>
    class Operator final : public Core::Producer<FloatType, Policy>,
                           public Core::SampleRateAware<FloatType>
    {
    public:
        /**
         * @brief Default constructor
         */
        Operator()
        {
            envelope.setSampleRate(Constants::DEFAULT_SAMPLE_RATE<FloatType>);
        }

        // ====================================================================
        // Frequency Control
        // ====================================================================

        /**
         * @brief Set the oscillator frequency
         * @param freq Frequency in Hz (must be non-negative)
         */
        void setFrequency(FloatType freq)
        {
            CASPI_ASSERT(freq >= FloatType(0.0), "Frequency must be non-negative.");
            frequency = freq;
            updatePhaseIncrement();
        }

        /**
         * @brief Get the current frequency
         * @return Frequency in Hz
         */
        FloatType getFrequency() const { return frequency; }

        // ====================================================================
        // Modulation Control
        // ====================================================================

        /**
         * @brief Set the modulation index
         *
         * **Phase Modulation**: Scales phase deviation in radians
         * - Example: index = 3.0 means ±3 radians of phase deviation
         * - Larger values create more sidebands and brightness
         *
         * **Frequency Modulation**: Scales frequency deviation in Hz
         * - Example: if modSignal = ±10 Hz and index = 2.0, deviation is ±20 Hz
         * - Larger values create wider frequency sweeps
         *
         * @param index Modulation index (0.0 = no modulation, typical: 0.1 to 10.0)
         */
        void setModulationIndex(FloatType index)
        {
            CASPI_ASSERT(index >= FloatType(0.0), "Modulation index must be non-negative.");
            modulationIndex = index;
        }

        /**
         * @brief Set the output amplitude multiplier
         * @param depth Amplitude (0.0 = silent, 1.0 = full amplitude)
         */
        void setModulationDepth(FloatType depth)
        {
            modulationDepth = depth;
        }

        /**
         * @brief Set self-modulation (feedback) amount
         * @param feedback Feedback amount (0.0 = none, ~5.0 = heavy distortion)
         */
        void setModulationFeedback(FloatType feedback)
        {
            modulationFeedback = feedback;
        }

        /**
         * @brief Set the modulation mode
         * @param mode Phase or Frequency modulation
         */
        void setModulationMode(ModulationMode mode)
        {
            modulationMode = mode;
        }

        /**
         * @brief Set all modulation parameters at once
         */
        void setModulation(FloatType index, FloatType depth, FloatType feedback = FloatType(0.0))
        {
            setModulationIndex(index);
            setModulationDepth(depth);
            setModulationFeedback(feedback);
        }

        /**
         * @brief Set external modulation input buffer
         *
         * **Expected signal format:**
         * - PM mode: Normalized values -1.0 to 1.0 (scaled by modulationIndex)
         * - FM mode: Frequency deviation in Hz (scaled by modulationIndex)
         *
         * @param buffer Pointer to modulation samples
         * @param numSamples Number of samples in buffer
         * @note Buffer pointer is stored, not copied. Must remain valid during rendering.
         */
        void setModulationInput(const FloatType* buffer, std::size_t numSamples)
        {
            modulationBuffer = buffer;
            modulationBufferSize = numSamples;
            modulationFrame = 0;
        }

        /**
         * @brief Clear modulation input (revert to unmodulated sine)
         */
        void clearModulationInput()
        {
            modulationBuffer = nullptr;
            modulationBufferSize = 0;
            modulationFrame = 0;
        }

        // Getters
        FloatType getModulationIndex() const { return modulationIndex; }
        FloatType getModulationDepth() const { return modulationDepth; }
        FloatType getModulationFeedback() const { return modulationFeedback; }
        ModulationMode getModulationMode() const { return modulationMode; }

        // ====================================================================
        // Envelope Control
        // ====================================================================

        void enableEnvelope(bool enabled = true) { envelopeEnabled = enabled; }
        void disableEnvelope() { envelopeEnabled = false; }

        void setADSR(FloatType attackTime_s, FloatType decayTime_s,
                     FloatType sustainLevel, FloatType releaseTime_s)
        {
            envelope.setAttackTime(attackTime_s);
            envelope.setSustainLevel(sustainLevel);
            envelope.setDecayTime(decayTime_s);
            envelope.setReleaseTime(releaseTime_s);
        }

        void noteOn() { envelope.noteOn(); }
        void noteOff() { envelope.noteOff(); }

        // ====================================================================
        // State Management
        // ====================================================================

        /**
         * @brief Reset all operator state to defaults
         */
        void reset()
        {
            phase.resetPhase();
            frequency = FloatType(0.0);
            modulationIndex = FloatType(1.0);
            modulationDepth = FloatType(1.0);
            modulationFeedback = FloatType(0.0);
            modulationMode = ModulationMode::Phase;
            previousOutput = FloatType(0.0);
            envelopeEnabled = false;
            envelope.reset();
            clearModulationInput();
        }

        // ====================================================================
        // Audio Rendering
        // ====================================================================

        /**
         * @brief Render a single audio sample
         * @return Generated audio sample
         */
        FloatType renderSample() override
        {
            // Get envelope amount
            FloatType envAmount = envelopeEnabled ? envelope.render() : FloatType(1.0);

            // Get current modulation signal
            FloatType modulationSignal = getCurrentModulationSignal();
            if (modulationBuffer && modulationFrame < modulationBufferSize)
                modulationFrame++;

            // Apply feedback
            FloatType selfMod = modulationFeedback * previousOutput;

            FloatType output;

            if (modulationMode == ModulationMode::Frequency)
            {
                // Frequency Modulation: modulation affects instantaneous frequency
                FloatType freqDeviation = modulationSignal * modulationIndex;
                FloatType instantFreq = frequency + freqDeviation;
                FloatType instantPhaseInc = Constants::TWO_PI<FloatType> * instantFreq / this->getSampleRate();

                // Generate sample (no modulation added to phase in FM mode)
                output = envAmount * modulationDepth * std::sin(phase.phase + selfMod);

                // Advance phase by modulated increment
                phase.phase += instantPhaseInc;
                wrapPhase();
            }
            else // ModulationMode::Phase
            {
                // Phase Modulation: modulation directly offsets phase
                FloatType phaseDeviation = modulationSignal * modulationIndex;

                // Generate sample with modulated phase
                output = envAmount * modulationDepth *
                         std::sin(phase.phase + phaseDeviation + selfMod);

                // Advance phase normally
                phase.phase += phase.increment;
                wrapPhase();
            }

            previousOutput = output;
            return output;
        }

        /**
         * @brief Render sample for multi-channel rendering
         */
        FloatType renderSample(std::size_t channel, std::size_t frame) override
        {
            (void)channel;
            (void)frame;
            return renderSample();
        }

        /**
         * @brief Handle sample rate changes
         */
        void onSampleRateChanged(FloatType rate) override
        {
            envelope.setSampleRate(rate);
            updatePhaseIncrement();
        }

    private:
        // ====================================================================
        // State
        // ====================================================================

        Phase<FloatType> phase;
        FloatType frequency = FloatType(0.0);
        FloatType modulationIndex = FloatType(1.0);
        FloatType modulationDepth = FloatType(1.0);
        FloatType modulationFeedback = FloatType(0.0);
        ModulationMode modulationMode = ModulationMode::Phase;
        FloatType previousOutput = FloatType(0.0);

        // Envelope
        bool envelopeEnabled = false;
        Envelope::ADSR<FloatType> envelope;

        // Modulation input
        const FloatType* modulationBuffer = nullptr;
        std::size_t modulationBufferSize = 0;
        std::size_t modulationFrame = 0;

        // ====================================================================
        // Internal Methods
        // ====================================================================

        /**
         * @brief Update phase increment when frequency or sample rate changes
         */
        void updatePhaseIncrement()
        {
            phase.increment = Constants::TWO_PI<FloatType> * frequency / this->getSampleRate();
        }

        /**
         * @brief Wrap phase to [0, 2π) range
         */
        void wrapPhase()
        {
            while (phase.phase >= Constants::TWO_PI<FloatType>)
                phase.phase -= Constants::TWO_PI<FloatType>;
            while (phase.phase < FloatType(0.0))
                phase.phase += Constants::TWO_PI<FloatType>;
        }

        /**
         * @brief Get current modulation sample from buffer
         * @return Modulation value, or 0.0 if no modulation input
         */
        FloatType getCurrentModulationSignal() const
        {
            if (!modulationBuffer || modulationFrame >= modulationBufferSize)
                return FloatType(0.0);
            return modulationBuffer[modulationFrame];
        }
    };

} // namespace CASPI

#endif // CASPI_OPERATOR_H