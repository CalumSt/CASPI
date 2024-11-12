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


* @file caspi_PMOperator.h
* @author CS Islay
* @class caspi_PMOperator
* @brief A class implementing a basic phase modulation operator, i.e. with a modulator and a carrier.
*
************************************************************************/

#ifndef CASPI_PMOPERATOR_H
#define CASPI_PMOPERATOR_H

#include "Utilities/caspi_Assert.h"
#include "Utilities/caspi_Constants.h"


namespace CASPI {
    template <typename FloatType>
    struct Phase {
        /*
         * This structure holds phase information. We use a separate one for the carrier and modulator.
         * This is slightly different to the one in the CASPI::BlepOscillator, as we recalculate the increment
         * each time.
         */
        void resetPhase() { phase = 0; }

        void setFrequency(FloatType _frequency, FloatType _sampleRate) {
            CASPI_ASSERT((_sampleRate > 0 && _frequency >= 0), "Sample Rate and Frequency must be larger than 0.");
            increment = _frequency / _sampleRate;
            this->sampleRate = _sampleRate;
        }

        void modulateIncrement(FloatType frequency, FloatType modulation) {
            auto modulatedFrequency = frequency + modulation;
            increment = CASPI::Constants::TWO_PI<FloatType> / sampleRate * modulatedFrequency;
        }

        FloatType incrementPhase(FloatType wrapLimit) {
            /// take previous phase value
            auto phaseInternal = phase;
            /// update phase counter
            phase += increment;
            /// wrap to the limit
            while (phase >= wrapLimit) { phase -= wrapLimit; }

            return phaseInternal;
        } /// wrap limit is 2pi for sine, 1 for others
        FloatType sampleRate = static_cast<FloatType>(44100.0);
        FloatType phase = 0;
        FloatType increment = 0;
    };

    template <typename FloatType>
    struct PMOperator {
        Phase<FloatType> carrierPhase;
        Phase<FloatType> modulatorPhase;
        const FloatType zero = static_cast<FloatType>(0.0f);
        FloatType carrierFrequency    = zero;
        FloatType modulationFrequency = zero;
        FloatType modulationIndex     = zero;

        void reset() {
            carrierFrequency    = zero;
            modulationFrequency = zero;
            modulationIndex     = zero;
            carrierPhase.resetPhase();
            modulatorPhase.resetPhase();
        }

        void setFrequency(FloatType frequency, FloatType modIndex, FloatType sampleRate) {
            carrierFrequency    = frequency;
            modulationFrequency = modIndex * carrierFrequency;
            modulationIndex     = modIndex;
            carrierPhase.setFrequency(carrierFrequency, sampleRate);
            modulatorPhase.setFrequency(modulationFrequency, sampleRate);
        }

        FloatType getNextSample() {
            auto mod = std::sin(modulatorPhase.incrementPhase(CASPI::Constants::TWO_PI<FloatType>));
            auto output = std::sin(carrierPhase.incrementPhase(CASPI::Constants::TWO_PI<FloatType>));
            carrierPhase.modulateIncrement(carrierFrequency, mod);
            return output;
        }
    };
}

#endif //CASPI_PMOPERATOR_H
