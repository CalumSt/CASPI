#ifndef CASPI_PMOPERATOR_NEW_H
#define CASPI_PMOPERATOR_NEW_H
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


* @file caspi_PMOperator_new.h
* @author CS Islay
* @class PMOperator_new
* @brief A class implementing a basic phase modulation operator, i.e. with a modulator and a carrier.
*
************************************************************************/

#include "Utilities/caspi_Constants.h"

namespace CASPI
{
class PMOperator_new
{
public:
    /*
     * @brief getFrequency gets the carrier frequency
     * @return The frequency of the operator
     */
    [[nodiscard]] double getFrequency() const { return frequency; }

    /*
     * @brief setFrequency sets the carrier frequency and sample rate
     */
    void setFrequency (const double _frequency, const double _sampleRate)
    {
        frequency = _frequency;
        sampleRate = _sampleRate;
        phaseIncrement = CASPI::Constants::TWO_PI<double> * frequency / sampleRate;
    }

    /*
     * @brief getSampleRate gets the sample rate
     * @return The sample rate of the operator
     */
    [[nodiscard]] double getSampleRate() const { return sampleRate; }

    /*
     * @brief setFrequency sets the carrier frequency
     */
    void setSampleRate (const double _sampleRate) { sampleRate = _sampleRate; }

    /*
     * @brief getModulationIndex gets the current modulation index
     * @return The modulation index of the modulator
     */
    [[nodiscard]] double getModulationIndex() const { return modIndex; }

    /*
     * @brief getModulationDepth gets the current modulation depth
     * @return The modulation depth of the modulator
     */
    [[nodiscard]] double getModulationDepth() const { return modDepth; }

    /*
    * @brief setFrequency sets the carrier frequency
    */
    void setModulation (const double modulationIndex, const double modulationDepth)
    {
        modIndex = modulationIndex;
        modDepth = modulationDepth;
    }

    /*
     * @brief render generates the next sample.
     */
    double render()
    {
        // calculate modulator signal
        const auto modSignal = modDepth * std::sin (currentModPhase);
        // calculate value
        const auto output = std::sin (currentPhase + modSignal);
        // increment phase
        currentPhase += phaseIncrement;
        currentModPhase += modPhaseIncrement;
        // wrap phase
        while (currentPhase >= CASPI::Constants::TWO_PI<double>)
        {
            currentPhase -= CASPI::Constants::TWO_PI<double>;
        }
        while (currentModPhase >= CASPI::Constants::TWO_PI<double>)
        {
            currentModPhase -= CASPI::Constants::TWO_PI<double>;
        }

        return output;
    }

private:
    /// Base parameters
    double sampleRate = 44100.0;

    /// Carrier frequency parameters
    double frequency = 0.0;
    double modIndex = 0.0;
    double phaseIncrement = 0.0;
    double currentPhase = 0.0;

    /// Modulator parameters
    double currentModPhase = 0.0;
    double modPhaseIncrement = 0.0;
    double modDepth = 0.0;
};
} // namespace CASPI

#endif //CASPI_PMOPERATOR_NEW_H
