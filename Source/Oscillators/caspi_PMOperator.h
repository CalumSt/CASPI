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

#include "Utilities/caspi_Assert.h"
#include "Utilities/caspi_CircularBuffer.h"
#include "Utilities/caspi_Constants.h"
#include <cmath>

namespace CASPI
{
/**
* @class PMOperator
* @brief A class implementing a basic phase modulation operator, i.e. with a modulator and a carrier.
* @details The PM Operator implemented here is a simple modulator-carrier with modulator feedback.
* This is where there are two sine oscillators, with one modulating (the modulator) another which
* provides the output signal (the carrier). The modulator can also modulate itself.
* The amount of modulation is determined by the modulation index and modulation depth. Modulation
* index is the frequency of the modulator, expressed as a fraction of the carrier frequency.
* Modulation depth is the output level of the modulator as it is applied to the carrier.
* Modulation feedback is the amount of self-modulation of the modulator.
* Note that for implementation, there is a unit delay associated with this.
*
* What could be improved here?
*     Split this up into PMOperator and PMAlgorithm
*     Operator should be a fundamental block that can modulate or be modulated
*     Then PMAlgorithm is a collection of FM algorithms (DX7 and beyond?)
 */
template <typename FloatType>
class PMOperator
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
    void setSampleRate (const FloatType _sampleRate) { sampleRate = _sampleRate; }

    /** @brief Gets the current modulation index.
     * @return The modulation index of the modulator
     */
    [[nodiscard]] FloatType getModulationIndex() const { return modIndex; }

    /**
     * @brief getModulationDepth gets the current modulation depth
     * @return The modulation depth of the modulator
     */
    [[nodiscard]] FloatType getModulationDepth() const { return modDepth; }

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
    void setModDepth (const FloatType newModDepth)
    {
        modDepth = newModDepth;
    }

    /**
     * @brief setModIndex sets the modulator index.
     * @param newModIndex The new modulator index.
     */
    void setModIndex (const FloatType newModIndex)
    {
        modIndex          = newModIndex;
        modPhaseIncrement = CASPI::Constants::TWO_PI<FloatType> * modIndex * frequency / sampleRate;
    }

    /**
    * @brief setModFeedback sets the modulator self-feedback.
    * @param newModFeedback The new modulator feedback amount.
    */
    void setModFeedback (const FloatType newModFeedback)
    {
        modFeedback = newModFeedback;
    }

    /**
     * @brief render generates the next sample.
     */
    FloatType render()
    {
        // calculate modulator signal
        currentModSignal = modDepth * std::sin (currentModPhase + modFeedback * currentModSignal);
        // calculate value
        const auto output = std::sin (currentPhase + currentModSignal);
        // increment phase
        currentPhase    += phaseIncrement;
        currentModPhase += modPhaseIncrement;
        // wrap phase
        while (currentPhase >= CASPI::Constants::TWO_PI<FloatType>)
        {
            currentPhase -= CASPI::Constants::TWO_PI<FloatType>;
        }
        while (currentModPhase >= CASPI::Constants::TWO_PI<FloatType>)
        {
            currentModPhase -= CASPI::Constants::TWO_PI<FloatType>;
        }

        return output;
    }

    /**
     * @brief renders a block of samples to a vector.
     * @param blockSize the number of samples to render.
     * @return a vector of samples.
     */
    std::vector<FloatType> renderBlock (int blockSize)
    {
        auto output = std::vector<FloatType> (blockSize);
        for (int i = 0; i < blockSize; i++)
        {
            output.at (i) = render();
        }
        return output;
    }

    /**
   * @brief renders a block of samples to a Circular Buffer.
   * @param blockSize the number of samples to render.
   * @return a circular buffer containing the samples.
   */
    CASPI::CircularBuffer<FloatType> renderBuffer (int blockSize)
    {
        auto vector = renderBlock (blockSize);
        return CASPI::CircularBuffer<FloatType> (vector);
    }

    /**
     * @brief resets the carrier and modulator phase.
     */
    void resetPhase()
    {
        currentPhase    = 0.0;
        currentModPhase = 0.0;
    }

    /**
     * @brief resets the entire operator to its default state
     */
    void reset()
    {
        frequency         = 0.0;
        phaseIncrement    = 0.0;
        modIndex          = 0.0;
        modPhaseIncrement = 0.0;
        modDepth          = 0.0;
        resetPhase();
    }

private:
    /// Base parameters
    FloatType sampleRate = 44100.0;

    /// Carrier frequency parameters
    FloatType frequency      = 0.0;
    FloatType phaseIncrement = 0.0;
    FloatType currentPhase   = 0.0;

    /// Modulator parameters
    FloatType modIndex          = 0.0;
    FloatType currentModPhase   = 0.0;
    FloatType modPhaseIncrement = 0.0;
    FloatType modDepth          = 0.0;
    FloatType modFeedback       = 0.0;
    FloatType currentModSignal  = 0.0;
};
} // namespace CASPI

#endif //CASPI_PMOPERATOR_H
