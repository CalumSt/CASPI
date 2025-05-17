#ifndef CASPI_PMOPERATOR_H
#define CASPI_PMOPERATOR_H
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
* @file caspi_PMOperator.h
* @author CS Islay
************************************************************************/

#include "Envelopes/caspi_Envelope.h"
#include "Utilities/caspi_Assert.h"
#include "Utilities/caspi_Constants.h"
#include <cmath>
namespace CASPI::PM
{
/**
* @class Operator
* @brief A class implementing a basic phase modulation operator.
* @details The PM Operator implemented here is a sine signal generator with self-feedback.
* The amount of modulation is determined by the modulation index and modulation depth. Modulation
* index is the frequency of the modulator, expressed as a fraction of the carrier frequency.
* Modulation depth (or output level, if it is the carrier) is the output level of the modulator as
* it is applied to the carrier.
* Modulation feedback is the amount of self-modulation of the modulator.
* Note that for implementation, there is a unit delay associated with this.
*
* What could be improved here?
*     The frequency/modIndex API is clunky.
 */
template <typename FloatType = double>
class Operator
{
public:
    /**
     * @brief getFrequency gets the carrier frequency
     * @return The frequency of the operator
     */
    [[nodiscard]] FloatType getFrequency() const { return frequency; }

    /**
     * @brief setFrequency sets the carrier frequency and sample rate
     * @param _frequency The frequency of the operator
     * @param _sampleRate The sample rate of the operator
     */
    void setFrequency (const FloatType _frequency, const FloatType _sampleRate)
    {
        CASPI_ASSERT (_frequency > 0 && _sampleRate > 0, "Frequency and Sample Rate must be larger than 0.");
        if (_frequency <= 0 || _sampleRate <= 0)
        {
            std::cout << "Uh oh!" << "\n";
        }
        frequency      = _frequency;

        sampleRate     = _sampleRate;

        phaseIncrement = CASPI::Constants::TWO_PI<FloatType> * frequency / sampleRate;
    }

    /** @brief Gets the current sample rate.
     * @return The sample rate of the operator.
     */
    [[nodiscard]] FloatType getSampleRate() const { return sampleRate; }

    /** @brief Sets the sample rate of the operator.
     * @param _sampleRate The sample rate of the operator
     */
    void setSampleRate (const FloatType _sampleRate)
    {
        sampleRate = _sampleRate;
        Envelope.setSampleRate (_sampleRate);
    }

    /** @brief Gets the current modulation index.
     * @return The modulation index of the modulator
     */
    [[nodiscard]] FloatType getModulationIndex() const { return modIndex; }

    /**
     * @brief getModulationDepth gets the current modulation depth
     * @return The modulation depth of the modulator
     */
    [[nodiscard]] FloatType getModulationDepth() const { return modDepth; }

    [[nodiscard]] FloatType getModulationFeedback() const { return modFeedback; }

    /**
    * @brief setModulation sets the modulation index, depth and feedback.
    * @param modulationIndex The modulation index of the modulator
    * @param modulationDepth The modulation depth of the modulator
    * @param modulationFeedback The modulation feedback amount of the modulator.
    */
    void setModulation (const FloatType modulationIndex, const FloatType modulationDepth, const FloatType modulationFeedback)
    {
        setModDepth (modulationDepth);
        setModFeedback (modulationFeedback);
        setModIndex (modulationIndex);
    }

    /**
    * @brief setModulation sets the modulation index and depth.
    * @param modulationIndex The modulation index of the modulator
    * @param modulationDepth The modulation depth of the modulator
    */
    void setModulation (const FloatType modulationIndex, const FloatType modulationDepth)
    {
        setModDepth (modulationDepth);
        setModIndex (modulationIndex);
    }

    /**
    * @brief setModDepth sets the modulator depth.
    * @param newModDepth The new modulator depth.
    */
    void setModDepth (const FloatType newModDepth) { modDepth = newModDepth; }

    /**
     * @brief setModIndex sets the modulator index. Updates the phase increment accordingly.
     * If setting up a modulator, it requires the frequency to be set first equal to the carrier first.
     * @param newModIndex The new modulator index.
     */
    void setModIndex (const FloatType newModIndex)
    {
        modIndex = newModIndex;

        modFrequency = modIndex * frequency;

        phaseIncrement = CASPI::Constants::TWO_PI<FloatType> * modFrequency / sampleRate;
    }

    void enableModFeedback() noexcept
    {
        isSelfModulating = true;
    }

    void disableModFeedback() noexcept
    {
        isSelfModulating = false;
    }

    /**
    * @brief setModFeedback sets the modulator self-feedback.
    * @param newModFeedback The new modulator feedback amount.
    */
    void setModFeedback (const FloatType newModFeedback)
    {
        modFeedback = newModFeedback;
    }

    void enableEnvelope()
    {
        envelopeEnabled = true;
    }

    void disableEnvelope()
    {
        envelopeEnabled = false;
    }

    void noteOn()
    {
        Envelope.noteOn();
    }

    void noteOff()
    {
        Envelope.noteOff();
    }

    void setADSR (const FloatType attackTime_s, const FloatType decayTime_s, const FloatType sustainLevel, const FloatType releaseTime_s)
    {
        Envelope.setSustainLevel (sustainLevel);
        Envelope.setAttackTime (attackTime_s);
        Envelope.setDecayTime (decayTime_s);
        Envelope.setReleaseTime (releaseTime_s);
    }

    void setAttackTime (const FloatType attackTime_s) { Envelope.setAttackTime (attackTime_s); }
    void setDecayTime (const FloatType decayTime_s) { Envelope.setDecayTime (decayTime_s); }
    void setSustainLevel (const FloatType sustainLevel) { Envelope.setSustainLevel (sustainLevel); }
    void setReleaseTime (const FloatType releaseTime_s) { Envelope.setReleaseTime (releaseTime_s); }

    FloatType render()
    {
        auto envAmount = CASPI::Constants::one<FloatType>;

        if (envelopeEnabled)
        {
            envAmount = Envelope.render();
        }

        FloatType selfMod = CASPI::Constants::zero<FloatType>;
        if (isSelfModulating)
        {
            selfMod = modFeedback * output;
        }

        auto sineSignal = modDepth * std::sin (currentPhase + selfMod);

        output = envAmount * sineSignal;

        currentPhase += phaseIncrement;

        while (currentPhase >= CASPI::Constants::TWO_PI<FloatType>)
        {
            currentPhase -= CASPI::Constants::TWO_PI<FloatType>;
        }
        return output;
    }

    FloatType render (FloatType modulationSignal)
    {
        auto envAmount = CASPI::Constants::one<FloatType>;

        if (envelopeEnabled)
        {
            envAmount = Envelope.render();
        }

        FloatType selfMod = CASPI::Constants::zero<FloatType>;
        if (isSelfModulating)
        {
            selfMod = modFeedback * output;
        }

        auto sineSignal = modDepth * std::sin (currentPhase + modulationSignal + selfMod);

        output = envAmount * sineSignal;

        currentPhase += phaseIncrement;

        while (currentPhase >= CASPI::Constants::TWO_PI<FloatType>)
        {
            currentPhase -= CASPI::Constants::TWO_PI<FloatType>;
        }
        return output;
    }

    void reset()
    {
        isSelfModulating = false;
        envelopeEnabled  = false;
        sampleRate       = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
        modIndex         = CASPI::Constants::one<FloatType>;
        modDepth         = CASPI::Constants::one<FloatType>;
        modFeedback      = CASPI::Constants::zero<FloatType>;
        frequency        = CASPI::Constants::zero<FloatType>;
        modFrequency     = CASPI::Constants::zero<FloatType>;
        phaseIncrement   = CASPI::Constants::zero<FloatType>;
        currentPhase     = CASPI::Constants::zero<FloatType>;
        output           = CASPI::Constants::zero<FloatType>;
    }

private:
    bool isSelfModulating    = false;
    bool envelopeEnabled     = false;
    FloatType sampleRate     = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
    FloatType modIndex       = CASPI::Constants::one<FloatType>;
    FloatType modDepth       = CASPI::Constants::one<FloatType>;
    FloatType modFeedback    = CASPI::Constants::zero<FloatType>;
    FloatType frequency      = CASPI::Constants::zero<FloatType>;
    FloatType modFrequency   = CASPI::Constants::zero<FloatType>;
    FloatType phaseIncrement = CASPI::Constants::zero<FloatType>;
    FloatType currentPhase   = CASPI::Constants::zero<FloatType>;
    FloatType output         = CASPI::Constants::zero<FloatType>;

    // Envelope
    Envelope::ADSR<FloatType> Envelope;
};

} // namespace CASPI::PM
#endif //CASPI_PMOPERATOR_H
