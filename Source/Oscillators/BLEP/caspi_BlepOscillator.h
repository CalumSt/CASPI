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


* @file caspi_BlepOscillator.h
* @author CS Islay
* @class caspi_BlepOscillator
* @brief A class implementing a BLEP oscillator.
*        I wanted to make a super performant oscillator with a BLEP
*        implementation, so have made it templated.
*
*        Should provide a decent implementation for 'trivial' waveforms.
*        The core 'BLEP' function is based on Martin Finke's implementation.
*
* @see https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/
*
* TODO: Add modulation
* TODO: Make Stereo
*
************************************************************************/

#ifndef CASPI_BLEPOSCILLATOR_H
#define CASPI_BLEPOSCILLATOR_H


#include<cmath>
#include "Utilities/caspi_CircularBuffer.h"
#include "Utilities/caspi_Constants.h"
#include"Utilities/caspi_assert.h"

// TODO: Incorporate phase modulation into the oscillator to allow external modulation
    // This could be done by adding a method that adds the generated sample to the buffer,
    // then reads in from the buffer once its full
// For now, can just call the appropriate waveform getSample
template <typename FloatType>
class caspi_BlepOscillator {
public:

    /// Structure for holding phase information conveniently
    struct Phase {

        void resetPhase() { phase = 0; }

        void setFrequency(FloatType frequency, FloatType sampleRate) {
            CASPI_ASSERT((sampleRate > 0 && frequency >= 0), "Sample Rate and Frequency must be larger than 0.");
            increment = frequency / sampleRate;
            this->sampleRate = sampleRate;
        }

        void setModulationFrequency(FloatType modulationFrequency) {
            CASPI_ASSERT(modulationFrequency >= 0, "Modulation Frequency cannot be negative.");
            this->modulationFrequency = modulationFrequency;
        }

        void setModulationIndex(FloatType modulationIndex) {
            CASPI_ASSERT(modulationIndex >= 0, "Modulation Index cannot be negative.");
            this->modulationIndex = modulationIndex;
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

        FloatType modulatePhase() {

            return modulationIndex * sin(phase * modulationFrequency * CASPI::TWO_PI<float> / sampleRate);
        }
        FloatType sampleRate = static_cast<FloatType>(44100.0);
        FloatType phase = 0;
        FloatType increment = 0;
        FloatType modulationIndex = 0;
        FloatType modulationFrequency = 0;
    };

    /// Sine oscillator
    struct Sine
    {
        void resetPhase()                                            { phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(CASPI::TWO_PI<float> * frequency,sampleRate); }
        FloatType getNextSample()                                    { return std::sin(phase.incrementPhase(CASPI::TWO_PI<float>)); }
        Phase phase;
    };

    /// Saw oscillator
    struct Saw
    {
        void resetPhase()                                            { phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(frequency,sampleRate); }
        FloatType getNextSample() {
            auto phaseInternal = phase.incrementPhase(1);
            return 2 * phaseInternal - 1 - blep (phaseInternal, phase.increment);
        }
        Phase phase;
    };

    /// Square oscillator
    struct Square
    {
        void resetPhase()                                            { phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(frequency,sampleRate); }

        FloatType getNextSample() {
            auto phaseInternal = phase.incrementPhase(1);
            auto half =  static_cast<FloatType>(0.5);
            auto one =  static_cast<FloatType>(1);
            /// These static casts are ugly
            return ((phaseInternal < half) ?  -one :  one)
                - blep (phaseInternal, phase.increment)
                + blep(std::fmod( phaseInternal + half,one), phase.increment);
        }

        Phase phase;
    };

    /// Triangle Oscillator
    struct Triangle
    {
        void resetPhase()                                            { square.resetPhase(); sum = 1;  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { square.setFrequency(frequency,sampleRate); }

        FloatType getNextSample()
        {
            sum += 4 * square.phase.increment * square.getNextSample();
            return sum;
        }

    private:
        Square square;
        FloatType sum = 1;
    };


private:
    /// This is the core blep function
    static FloatType blep (FloatType phase, FloatType increment)
    {
        if (phase < increment)
        {
            /// internal phase variable
            auto phaseInternal = phase / increment;
            return (2 - phaseInternal) * phaseInternal - 1;
        }

        if (phase > 1 - increment)
        {
            auto phaseInternal = (phase - 1) / increment;
            return (phaseInternal + 2) * phaseInternal + 1;
        }
    // Unsure how this return statement works
    return {};
    }

    /// Placeholder until I work out how to better implement this
    template <typename OscillatorType>
    void renderToCaspiBuffer(caspi_CircularBuffer<FloatType> audioBuffer,FloatType frequency, FloatType sampleRate, const int numberOfSamples = 1) {
        OscillatorType osc;
        osc.setFrequency(frequency, sampleRate);
        for (int sampleIndex = 0; sampleIndex << numberOfSamples; sampleIndex++) {
            // get sample from (mono) oscillator
            // todo: panning?
            auto sample = osc.getNextSample();
            audioBuffer.writeBuffer(sample);
        }
    }


};



#endif //CASPI_BLEPOSCILLATOR_H
