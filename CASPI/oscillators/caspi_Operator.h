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
 * @brief A modulation operator that works as Producer with external modulation input.
 *
 * This operator combines a sine wave generator with modulation capabilities.
 * It can perform Phase Modulation (PM), Frequency Modulation (FM), and phase distortion.
 * The operator is designed to work with the CASPI Producer/Processor architecture.
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Phase.h"
#include "envelopes/caspi_Envelope.h"
#include <cmath>

namespace CASPI
{
    enum class ModulationMode
    {
        Phase,
        Frequency
    };

    /// Modulation operator that inherits from Producer base class
    template <typename FloatType, typename Policy = Core::Traversal::PerSample>
    class Operator final : public Core::Producer<FloatType, Policy>,
                           public Core::SampleRateAware<FloatType>
    {
        public:
            /// Constructor
            Operator() = default;

            /// Basic frequency parameter
            void setFrequency (FloatType freq);

            /// Modulation parameters
            void setModulationIndex (FloatType index);
            void setModulationDepth (FloatType depth);
            void setModulationFeedback (FloatType feedback);
            void setModulationMode (ModulationMode mode);

            /// Convenience method for all modulation parameters
            void setModulation (FloatType index, FloatType depth, FloatType feedback = FloatType (0.0));

            /// Modulation input interface
            void setModulationInput (const FloatType* modulationBuffer, std::size_t numSamples);
            void clearModulationInput();

            /// Envelope control
            void enableEnvelope (bool enabled = true);
            void disableEnvelope();
            void setADSR (FloatType attackTime_s, FloatType decayTime_s, FloatType sustainLevel, FloatType releaseTime_s);
            void noteOn();
            void noteOff();

            /// Reset state
            void reset();

            // Getters for testing
            FloatType getFrequency() const { return frequency; }
            FloatType getModulationIndex() const { return modulationIndex; }
            FloatType getModulationDepth() const { return modulationDepth; }
            FloatType getModulationFeedback() const { return modulationFeedback; }
            ModulationMode getModulationMode() const { return modulationMode; }

            // Producer interface implementation
            FloatType renderSample() override;
            FloatType renderSample (std::size_t channel, std::size_t frame) override;

        private:
            // Internal phase management using new Phase class
            Phase<FloatType> phase;
            FloatType frequency           = FloatType (0.0);
            FloatType modulationIndex     = FloatType (1.0);
            FloatType modulationDepth     = FloatType (1.0);
            FloatType modulationFeedback  = FloatType (0.0);
            ModulationMode modulationMode = ModulationMode::Phase;

            // State for feedback
            FloatType previousOutput = FloatType (0.0);

            // Envelope
            bool envelopeEnabled = false;
            Envelope::ADSR<FloatType> envelope;

            // Modulation input
            const FloatType* modulationBuffer = nullptr;
            std::size_t modulationBufferSize  = 0;
            std::size_t modulationFrame       = 0;

            // Internal helper methods
            void updatePhaseIncrement();
            FloatType getCurrentModulationSignal() const;
            FloatType generateSample (FloatType modulationSignal) const;
    };

    // Template method implementations
    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setFrequency (FloatType freq)
    {
        CASPI_ASSERT (freq >= FloatType (0.0), "Frequency must be non-negative.");
        frequency = freq;
        updatePhaseIncrement();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulationIndex (FloatType index)
    {
        CASPI_ASSERT (index >= FloatType (0.0), "Modulation index must be non-negative.");
        modulationIndex = index;
        updatePhaseIncrement();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulationDepth (FloatType depth)
    {
        modulationDepth = depth;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulationFeedback (FloatType feedback)
    {
        modulationFeedback = feedback;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulationMode (ModulationMode mode)
    {
        modulationMode = mode;
        updatePhaseIncrement();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulation (FloatType index, FloatType depth, FloatType feedback)
    {
        setModulationIndex (index);
        setModulationDepth (depth);
        setModulationFeedback (feedback);
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setModulationInput (const FloatType* buffer, std::size_t numSamples)
    {
        modulationBuffer     = buffer;
        modulationBufferSize = numSamples;
        modulationFrame      = 0;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::clearModulationInput()
    {
        modulationBuffer     = nullptr;
        modulationBufferSize = 0;
        modulationFrame      = 0;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::enableEnvelope (bool enabled)
    {
        envelopeEnabled = enabled;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::disableEnvelope()
    {
        envelopeEnabled = false;
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::setADSR (FloatType attackTime_s, FloatType decayTime_s, FloatType sustainLevel, FloatType releaseTime_s)
    {
        envelope.setAttackTime (attackTime_s);
        envelope.setSustainLevel (sustainLevel);
        envelope.setDecayTime (decayTime_s);
        envelope.setReleaseTime (releaseTime_s);
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::noteOn()
    {
        envelope.noteOn();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::noteOff()
    {
        envelope.noteOff();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::reset()
    {
        phase.resetPhase();
        frequency          = FloatType (0.0);
        modulationIndex    = FloatType (1.0);
        modulationDepth    = FloatType (1.0);
        modulationFeedback = FloatType (0.0);
        modulationMode     = ModulationMode::Phase;
        previousOutput     = FloatType (0.0);
        envelopeEnabled    = false;
        envelope.reset();
        clearModulationInput();
    }

    template <typename FloatType, typename Policy>
    void Operator<FloatType, Policy>::updatePhaseIncrement()
    {
        FloatType modFrequency = modulationIndex * frequency;

        switch (modulationMode)
        {
            case ModulationMode::Phase:
                phase.increment = CASPI::Constants::TWO_PI<FloatType> * modFrequency / this->getSampleRate();
                break;
            case ModulationMode::Frequency:
                // For FM modulation, phase increment is based on base frequency
                phase.increment = CASPI::Constants::TWO_PI<FloatType> * frequency / this->getSampleRate();
                break;
        }
    }

    template <typename FloatType, typename Policy>
    FloatType Operator<FloatType, Policy>::getCurrentModulationSignal() const
    {
        if (! modulationBuffer || modulationFrame >= modulationBufferSize)
        {
            return FloatType (0.0);
        }
        return modulationBuffer[modulationFrame];
    }

    template <typename FloatType, typename Policy>
    FloatType Operator<FloatType, Policy>::generateSample (FloatType modulationSignal) const
    {
        FloatType selfMod = FloatType (0.0);
        if (modulationFeedback != FloatType (0.0))
        {
            selfMod = modulationFeedback * previousOutput;
        }

        // Generate sine wave with modulation and self-feedback
        return modulationDepth * std::sin (phase.phase + modulationSignal + selfMod);
    }

    template <typename FloatType, typename Policy>
    FloatType Operator<FloatType, Policy>::renderSample()
    {
        FloatType envAmount = FloatType (1.0);
        if (envelopeEnabled)
        {
            envAmount = envelope.render();
        }

        FloatType modulationSignal = getCurrentModulationSignal();

        // Update frame counter for modulation buffer
        if (modulationBuffer)
        {
            modulationFrame = (modulationFrame + 1) % modulationBufferSize;
        }

        FloatType output = envAmount * generateSample (modulationSignal);

        // Store previous output for feedback
        previousOutput = output;

        // Advance phase using appropriate increment
        if (modulationMode == ModulationMode::Frequency)
        {
            // For FM, advance phase with modulated frequency
            FloatType currentFreq = frequency + modulationSignal;
            phase.increment       = CASPI::Constants::TWO_PI<FloatType> * currentFreq / this->getSampleRate();
            phase.advanceAndWrap (CASPI::Constants::TWO_PI<FloatType>);
        }
        else // Phase modulation
        {
            // For PM, advance phase normally
            phase.advanceAndWrap (CASPI::Constants::TWO_PI<FloatType>);
        }

        return output;
    }

    template <typename FloatType, typename Policy>
    FloatType Operator<FloatType, Policy>::renderSample (std::size_t channel, std::size_t frame)
    {
        // For multi-channel rendering, use the same renderSample logic
        (void) channel;
        (void) frame;
        return renderSample();
    }
} // namespace CASPI

#endif // CASPI_OPERATOR_H