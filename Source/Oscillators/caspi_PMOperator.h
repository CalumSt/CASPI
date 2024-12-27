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
* @class PMOperator
* @brief A class implementing a basic phase modulation operator, i.e. with a modulator and a carrier.
************************************************************************/

#include "Utilities/caspi_Constants.h"

#include <Utilities/caspi_Assert.h>

namespace CASPI
{
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
     */
    void setFrequency (const FloatType _frequency, const FloatType _sampleRate)
    {
        CASPI_ASSERT(_frequency > 0 && _sampleRate > 0, "Frequency and Sample Rate must be larger than 0.");
        frequency      = _frequency;
        sampleRate     = _sampleRate;
        phaseIncrement = CASPI::Constants::TWO_PI<FloatType> * frequency / sampleRate;
    }

    /** @brief Gets the current sample rate.
     * @return The sample rate of the operator.
     */
    [[nodiscard]] FloatType getSampleRate() const { return sampleRate; }

    /** @brief Sets the sample rate of the operator.
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
    * @brief setFrequency sets the carrier frequency
    */
    void setModulation (const FloatType modulationIndex, const FloatType modulationDepth)
    {
        modIndex          = modulationIndex;
        modDepth          = modulationDepth;
        modPhaseIncrement = CASPI::Constants::TWO_PI<FloatType> * modIndex * frequency / sampleRate;
    }

    /**
     * @brief render generates the next sample.
     */
    FloatType render()
    {
        // calculate modulator signal
        const auto modSignal = modDepth * std::sin (currentModPhase);
        // calculate value
        const auto output = std::sin (currentPhase + modSignal);
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
};
} // namespace CASPI

#endif //CASPI_PMOPERATOR_H
