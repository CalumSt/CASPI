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
 * @ingroup oscillators
 *
 * This operator generates sine waves with support for Phase Modulation (PM)
 * and Frequency Modulation (FM). It can accept external modulation input and
 * includes self-modulation (feedback) for additional harmonic complexity.
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "controls/caspi_Envelope.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Phase.h"
#include "core/caspi_Node.h"
#include "oscillators/caspi_BlepOscillator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace CASPI
{
    /// Modulation modes for the operator
    enum class ModulationMode
    {
        Phase, ///< Phase modulation (PM) - modulation affects phase directly
        Frequency ///< Frequency modulation (FM) - modulation affects instantaneous frequency
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
     */
    template <CASPI_FLOAT_TYPE FloatType>
    class Operator final : public Graph::AudioNode<Operator<FloatType>, FloatType>
    {
            using Base = Graph::AudioNode<Operator<FloatType>, FloatType>;

        public:
            /**
             * @brief Default constructor
             */
            Operator()
            {
                envelope.setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
            }

            Operator (FloatType SampleRate,
                      FloatType freq,
                      FloatType modIndex,
                      FloatType modDepth,
                      FloatType modFeedback,
                      const ModulationMode modMode)
                : frequency (freq)
                , modulationIndex (modIndex)
                , modulationDepth (modDepth)
                , modulationFeedback (modFeedback)
                , modulationMode (modMode)
            {
                this->setSampleRate (SampleRate);
                envelope.setSampleRate (SampleRate);
                updatePhaseIncrement();
            }

            /**
             * @brief Construct an operator with N declared modulation input ports.
             *
             * @details
             * Use this constructor when placing the operator directly in a
             * Graph::AudioGraph as a modulation target, with modulators connected
             * via graph.connect(). By default (the no-argument constructor) an
             * Operator declares zero input ports and can only be modulated via
             * setModulationInput() / renderSample(modulation) — a graph connection
             * into port 0 would fail with GraphError::InvalidPort.
             *
             * Each declared port has an independent weight (default 1.0, set via
             * setModulationPortWeight()), analogous to FMGraphBuilder::connect()'s
             * per-edge modulation depth. processImpl() sums all connected ports'
             * signals (each scaled by its weight) before applying modulationIndex,
             * so multiple modulators can feed one carrier at different depths —
             * the same topology FMGraphDSP supports internally, expressed with
             * plain graph connections instead of a separate connection list.
             *
             * @param numModulationPorts  Number of independent modulation input ports.
             * @param numOutputPorts      Number of output ports (default 1).
             */
            explicit Operator (std::size_t numModulationPorts, std::size_t numOutputPorts = 1)
                : Base (numModulationPorts, numOutputPorts)
                , modulationPortWeights (numModulationPorts, FloatType (1))
            {
                envelope.setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
            }

            // ====================================================================
            // Frequency Control
            // ====================================================================

            /**
             * @brief Set the oscillator frequency
             * REAL-TIME SAFE: No allocations
             */
            void setFrequency (FloatType freq) CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (freq >= FloatType (0.0), "Frequency must be non-negative.");
                frequency = freq;
                updatePhaseIncrement();
            }

            FloatType getFrequency() const CASPI_NON_BLOCKING
            {
                return frequency;
            }

            // ====================================================================
            // Modulation Control
            // ====================================================================

            /**
             * @brief Set the modulation index
             * REAL-TIME SAFE: No allocations
             */
            void setModulationIndex (FloatType index) CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (index >= FloatType (0.0), "Modulation index must be non-negative.");
                modulationIndex = index;
            }

            void setModulationDepth (FloatType depth) CASPI_NON_BLOCKING
            {
                modulationDepth = depth;
            }

            void setModulationFeedback (FloatType feedback) CASPI_NON_BLOCKING
            {
                modulationFeedback = feedback;
            }

            void setModulationMode (ModulationMode mode) CASPI_NON_BLOCKING
            {
                modulationMode = mode;
            }

            // ====================================================================
            // Waveform Control
            // ====================================================================

            /**
             * @brief Select the operator's output waveform.
             *
             * @details
             * Defaults to Sine (the classic FM operator core, evaluated directly
             * with std::sin — no behavioural change from prior releases).
             * Saw / Square / Triangle / Pulse reuse Oscillators::BlepOscillator's
             * PolyBLEP-antialiased waveform evaluation instead of duplicating it,
             * so operators can act as band-limited FM/PM carriers with richer
             * base timbres than a pure sine.
             *
             * @param shape  Target waveform. Triangle switches reset the internal
             *               BLEP core's leaky integrator to avoid a DC transient.
             */
            void setWaveform (Oscillators::WaveShape shape) CASPI_NON_BLOCKING
            {
                waveformCore.setShape (shape);
                waveform = shape;
            }

            Oscillators::WaveShape getWaveform() const CASPI_NON_BLOCKING
            {
                return waveform;
            }

            /**
             * @brief Set pulse width for Square / Pulse waveforms. Range [0.01, 0.99].
             *
             * No effect on Sine, Saw, or Triangle.
             */
            void setPulseWidth (FloatType width) CASPI_NON_BLOCKING
            {
                waveformCore.pulseWidth.setBaseNormalised (width);
                waveformCore.pulseWidth.skip (1000);
            }

            void setModulation (FloatType index, FloatType depth, FloatType feedback = FloatType (0.0))
                CASPI_NON_BLOCKING
            {
                setModulationIndex (index);
                setModulationDepth (depth);
                setModulationFeedback (feedback);
            }

            // ====================================================================
            // Graph Modulation Ports
            //
            // Only meaningful when this Operator was constructed with the
            // (numModulationPorts, numOutputPorts) constructor and placed in a
            // Graph::AudioGraph. Unrelated to setModulationInput() / renderSample(),
            // which remain the standalone/FMGraphDSP-internal API.
            // ====================================================================

            /**
             * @brief Set the weight applied to a graph modulation input port.
             *
             * @details
             * processImpl() sums each connected port's signal times its weight,
             * then applies modulationIndex to the total (same two-stage scaling
             * as FMGraphBuilder::connect()'s per-edge depth followed by the
             * operator's own modulationIndex).
             *
             * @param port    Port index; must be < getNumModulationPorts().
             * @param weight  Modulation depth for this port. Default: 1.0.
             */
            void setModulationPortWeight (std::size_t port, FloatType weight) CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (port < modulationPortWeights.size(), "Modulation port index out of range.");
                if (port < modulationPortWeights.size())
                {
                    modulationPortWeights[port] = weight;
                }
            }

            FloatType getModulationPortWeight (std::size_t port) const CASPI_NON_BLOCKING
            {
                return (port < modulationPortWeights.size()) ? modulationPortWeights[port] : FloatType (0);
            }

            CASPI_NO_DISCARD std::size_t getNumModulationPorts() const CASPI_NON_BLOCKING
            {
                return modulationPortWeights.size();
            }

            /**
             * @brief Set single modulation value for current sample
             * @param value Modulation value (normalized or Hz depending on mode)
             *
             * REAL-TIME SAFE: No allocations
             * NEW: Added for FMGraph compatibility
             */
            void setModulationInput (FloatType value) CASPI_NON_BLOCKING
            {
                currentModulation = value;
            }

            /**
             * @brief Set external modulation input buffer (legacy API)
             * REAL-TIME SAFE: Only stores pointer (buffer must remain valid)
             */
            void setModulationInput (const FloatType* buffer, std::size_t numSamples) CASPI_NON_BLOCKING
            {
                modulationBuffer     = buffer;
                modulationBufferSize = numSamples;
                modulationFrame      = 0;
            }

            void clearModulationInput() CASPI_NON_BLOCKING
            {
                modulationBuffer     = nullptr;
                modulationBufferSize = 0;
                modulationFrame      = 0;
                currentModulation    = FloatType (0);
            }

            // Getters
            FloatType getModulationIndex() const CASPI_NON_BLOCKING
            {
                return modulationIndex;
            }

            FloatType getModulationDepth() const CASPI_NON_BLOCKING
            {
                return modulationDepth;
            }

            FloatType getModulationFeedback() const CASPI_NON_BLOCKING
            {
                return modulationFeedback;
            }

            ModulationMode getModulationMode() const CASPI_NON_BLOCKING
            {
                return modulationMode;
            }

            // ====================================================================
            // Envelope Control
            // ====================================================================
            void enableEnvelope (bool enabled = true) CASPI_NON_BLOCKING
            {
                envelopeEnabled = enabled;
            }

            void disableEnvelope() CASPI_NON_BLOCKING
            {
                envelopeEnabled = false;
            }

            void setADSR (FloatType attackTime_s,
                          FloatType decayTime_s,
                          FloatType sustainLevel,
                          FloatType releaseTime_s) CASPI_NON_BLOCKING
            {
                envelope.setAttackTime (attackTime_s);
                envelope.setSustainLevel (sustainLevel);
                envelope.setDecayTime (decayTime_s);
                envelope.setReleaseTime (releaseTime_s);
            }

            void noteOn() CASPI_NON_BLOCKING
            {
                envelope.noteOn();
            }

            void noteOff() CASPI_NON_BLOCKING
            {
                envelope.noteOff();
            }

            // ====================================================================
            // State Management
            // ====================================================================

            /** @brief No additional setup needed at prepare time. */
            void onPrepare (std::size_t, std::size_t, double) noexcept
            {
            }

            /**
             * @brief Graph dispatch: sums weighted modulation from all declared input
             *        ports, renders sample-by-sample into outputBuffer.
             *
             * @details
             * Each connected port contributes `sample * modulationPortWeight[port]`
             * to the combined modulation signal for that frame (see
             * setModulationPortWeight() — this is how multiple modulators can feed
             * one carrier at independent depths using plain graph connections).
             * If no port is connected (including the common case of an Operator
             * constructed with zero ports), falls back to stored
             * currentModulation / buffer, matching pre-existing single-value usage.
             *
             * getAudioInput() is re-resolved per frame rather than hoisted outside
             * the loop; with the small port/connection counts typical of FM voices
             * this is negligible next to the per-sample waveform/envelope work
             * already done below, and keeps this path allocation-free.
             */
            void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
            {
                const std::size_t C        = this->outputBuffer.numChannels();
                const std::size_t Fm       = this->outputBuffer.numFrames();
                const std::size_t numPorts = this->getNumInputPorts();

                for (std::size_t fr = 0; fr < Fm; ++fr)
                {
                    FloatType modSignal    = FloatType (0);
                    bool anyPortConnected  = false;

                    for (std::size_t port = 0; port < numPorts; ++port)
                    {
                        const auto* buf = ctx.getAudioInput (this->getId(), port);
                        if (buf != nullptr)
                        {
                            anyPortConnected = true;
                            modSignal += buf->sample (0, fr) * modulationPortWeights[port];
                        }
                    }

                    if (! anyPortConnected)
                    {
                        modSignal = getCurrentModulationSignal();
                    }

                    const FloatType out = renderSampleWithModulation (modSignal);
                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        this->outputBuffer.sample (ch, fr) = out;
                    }
                }
            }

            /**
             * @brief Reset all operator state to defaults
             * REAL-TIME SAFE: No allocations
             */
            void reset() CASPI_NON_BLOCKING
            {
                phase.resetPhase();
                frequency          = FloatType (0.0);
                modulationIndex    = FloatType (1.0);
                modulationDepth    = FloatType (1.0);
                modulationFeedback = FloatType (0.0);
                modulationMode     = ModulationMode::Phase;
                previousOutput     = FloatType (0.0);
                currentModulation  = FloatType (0.0);
                envelopeEnabled    = false;
                envelope.reset();
                clearModulationInput();
                waveform = Oscillators::WaveShape::Sine;
                waveformCore.setShape (Oscillators::WaveShape::Sine);
                std::fill (modulationPortWeights.begin(), modulationPortWeights.end(), FloatType (1));
            }

            // ====================================================================
            // Audio Rendering
            // ====================================================================

            /**
             * @brief Render a single audio sample (uses stored modulation)
             * @return Generated audio sample
             *
             * REAL-TIME SAFE: No allocations, bounded execution time
             * Uses modulation from setModulationInput() or buffer
             */

            CASPI_NO_DISCARD FloatType renderSample() CASPI_NON_BLOCKING
            {
                // Get modulation signal
                FloatType modulationSignal = getCurrentModulationSignal();

                // Render with modulation
                return renderSampleWithModulation (modulationSignal);
            }

            /**
             * @brief Render sample with explicit modulation input
             * @param modulationInput Modulation value for this sample
             * @return Generated audio sample
             *
             * REAL-TIME SAFE: No allocations, bounded execution time
             * NEW: Preferred API for explicit data flow
             */

            CASPI_NO_DISCARD FloatType renderSample (FloatType modulationInput) CASPI_NON_BLOCKING
            {
                return renderSampleWithModulation (modulationInput);
            }

            /**
             * @brief Render sample for multi-channel rendering
             */
            FloatType renderSample (std::size_t channel, std::size_t frame) CASPI_NON_BLOCKING
            {
                (void) channel;
                (void) frame;
                return renderSample();
            }

            /**
             * @brief Handle sample rate changes
             * NOT REAL-TIME SAFE: May trigger envelope recalculations
             */
            void onSampleRateChanged (FloatType rate) CASPI_NON_BLOCKING override
            {
                envelope.setSampleRate (rate);
                updatePhaseIncrement();
            }

        private:
            // ====================================================================
            // State
            // ====================================================================

            Phase<FloatType> phase;
            FloatType frequency           = FloatType (0.0);
            FloatType modulationIndex     = FloatType (1.0);
            FloatType modulationDepth     = FloatType (1.0);
            FloatType modulationFeedback  = FloatType (0.0);
            ModulationMode modulationMode = ModulationMode::Phase;
            FloatType previousOutput      = FloatType (0.0);
            FloatType currentModulation   = FloatType (0.0); // NEW: Single-value modulation

            // Waveform core: Sine is evaluated directly (no behavioural change from
            // prior releases). Saw/Square/Triangle/Pulse delegate to a BlepOscillator
            // instance for its PolyBLEP antialiasing rather than reimplementing it.
            Oscillators::WaveShape waveform = Oscillators::WaveShape::Sine;
            Oscillators::BlepOscillator<FloatType> waveformCore;

            // Graph modulation ports: one weight per declared input port. Empty for
            // the default/legacy constructors (0 declared ports) — see the
            // (numModulationPorts, numOutputPorts) constructor and processImpl().
            std::vector<FloatType> modulationPortWeights;

            // Envelope
            bool envelopeEnabled = false;
            Envelope::ADSR<FloatType> envelope;

            // Modulation buffer (legacy)
            const FloatType* modulationBuffer   = nullptr;
            std::size_t modulationBufferSize    = 0;
            mutable std::size_t modulationFrame = 0; // Mutable for buffer iteration

            // ====================================================================
            // Internal Methods
            // ====================================================================

            /**
             * @brief Update phase increment when frequency or sample rate changes
             * REAL-TIME SAFE: No allocations
             */

            void updatePhaseIncrement() CASPI_NON_BLOCKING
            {
                phase.increment = Constants::TWO_PI<FloatType> * frequency / this->getSampleRate();
            }

            /**
             * @brief Optimized branchless phase wrapping
             * REAL-TIME SAFE: Predictable, bounded execution
             * IMPROVED: Single modulo operation instead of loops
             */

            void wrapPhase() CASPI_NON_BLOCKING
            {
                const FloatType twoPi = Constants::TWO_PI<FloatType>;

                // Branchless wrap: phase = phase - twoPi * floor(phase / twoPi)
                phase.phase = phase.phase - twoPi * std::floor (phase.phase / twoPi);

                // Note: This handles both positive and negative phases correctly
                // For negative: floor(-x/2π) gives the correct wrap
            }

            /**
             * @brief Evaluate the operator's waveform at a phase (in radians).
             *
             * REAL-TIME SAFE: No allocations
             *
             * Sine is evaluated directly via std::sin. Other shapes are delegated to
             * waveformCore (a BlepOscillator instance) after converting to its
             * normalised [0, 1) phase convention, reusing its PolyBLEP evaluation
             * instead of duplicating it here.
             */
            CASPI_NO_DISCARD FloatType evaluateWaveform (FloatType phaseRadians) noexcept CASPI_NON_BLOCKING
            {
                if (waveform == Oscillators::WaveShape::Sine)
                {
                    return std::sin (phaseRadians);
                }

                const FloatType twoPi = Constants::TWO_PI<FloatType>;
                FloatType normalisedPhase = std::fmod (phaseRadians, twoPi) / twoPi;
                if (normalisedPhase < FloatType (0))
                {
                    normalisedPhase += FloatType (1);
                }
                const FloatType normalisedIncrement = phase.increment / twoPi;

                return waveformCore.evaluateAtPhase (normalisedPhase, normalisedIncrement);
            }

            /**
             * @brief Get current modulation sample
             * @return Modulation value, or 0.0 if no modulation input
             *
             * REAL-TIME SAFE: No allocations
             * Priority: currentModulation > buffer > 0.0
             */
            CASPI_NO_DISCARD FloatType getCurrentModulationSignal() const CASPI_NON_BLOCKING
            {
                // Priority 1: Single-value modulation (from setModulationInput(value))
                if (currentModulation != FloatType (0)) return currentModulation;

                // Priority 2: Buffer-based modulation
                if (modulationBuffer && modulationFrame < modulationBufferSize)
                {
                    FloatType value = modulationBuffer[modulationFrame];
                    modulationFrame++; // Note: mutable member
                    return value;
                }

                // Priority 3: No modulation
                return FloatType (0.0);
            }

            /**
             * @brief Core rendering logic with explicit modulation
             * @param modulationSignal Modulation input for this sample
             * @return Generated audio sample
             *
             * REAL-TIME SAFE: No allocations, bounded execution
             */
            CASPI_NO_DISCARD FloatType renderSampleWithModulation (FloatType modulationSignal) CASPI_NON_BLOCKING
            {
                Core::ScopedFlushDenormals flush {};
                // Get envelope amount
                FloatType envAmount = envelopeEnabled ? envelope.render() : FloatType (1.0);

                // Apply feedback (self-modulation)
                FloatType selfMod = modulationFeedback * previousOutput;

                FloatType output;

                if (modulationMode == ModulationMode::Frequency)
                {
                    // Frequency Modulation: modulation affects instantaneous frequency
                    FloatType freqDeviation   = modulationSignal * modulationIndex;
                    FloatType instantFreq     = frequency + freqDeviation;
                    FloatType instantPhaseInc = Constants::TWO_PI<FloatType> * instantFreq / this->getSampleRate();

                    // Generate sample
                    output = envAmount * modulationDepth * evaluateWaveform (phase.phase + selfMod);

                    // Advance phase by modulated increment
                    phase.phase += instantPhaseInc;
                }
                else // ModulationMode::Phase
                {
                    // Phase Modulation: modulation directly offsets phase
                    FloatType phaseDeviation = modulationSignal * modulationIndex;

                    // Generate sample with modulated phase
                    output = envAmount * modulationDepth * evaluateWaveform (phase.phase + phaseDeviation + selfMod);

                    // Advance phase normally
                    phase.phase += phase.increment;
                }

                // Wrap phase (branchless)
                wrapPhase();

                // Store for feedback
                previousOutput = output;

                return output;
            }
    };
} // namespace CASPI
#endif // CASPI_OPERATOR_H