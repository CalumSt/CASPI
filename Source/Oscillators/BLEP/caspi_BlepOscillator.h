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
*        Takes some influence from wavetables, the oscillators render
*        to a circular buffer, then loops over this generated waveform once it's full.
*
*        Should provide a decent implementation for 'trivial' waveforms.
*        The core 'BLEP' function is based on Martin Finke's implementation.
*
* @see https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/
*
************************************************************************/

#ifndef CASPI_BLEPOSCILLATOR_H
#define CASPI_BLEPOSCILLATOR_H

#ifndef CASPI_ASSERT
#include <cassert>
#define CASPI_ASSERT(x) assert(x)
#endif

#include<cmath>
#include "../../Utilities/caspi_CircularBuffer.h"

constexpr float PI = 3.14159265358979323846f;

// TODO: Use a circular buffer to create a faux wavetable, so that render is only called when the waveform is changed
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
            CASPI_ASSERT(sampleRate > 0 && frequency >= 0);
            increment = frequency / sampleRate;
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
        FloatType phase = 0;
        FloatType increment = 0;
    };

    /// Sine oscillator
    struct Sine
    {
        void resetPhase()                                            {phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(2 * static_cast<FloatType>(PI) * frequency,sampleRate); }
        FloatType getNextSample()                                    { return sin(phase); }
        Phase phase;
    };

    /// Saw oscillator
    struct Saw
    {
        void resetPhase()                                            {phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(frequency,sampleRate); }
        FloatType getNextSample() {
            auto phaseInternal = phase.incrementPhase(1);
            return 2 * phaseInternal - 1 - blep (phaseInternal, phase.increment());
        }
        Phase phase;
    };

    /// Square oscillator
    struct Square
    {
        void resetPhase()                                            {phase.resetPhase();  }
        void setFrequency(FloatType frequency, FloatType sampleRate) { phase.setFrequency(frequency,sampleRate); }

        FloatType getNextSample() {
            auto phaseInternal = phase.incrementPhase(1);
            return ((phaseInternal < 0.5) ? -1 : 1) - blep (phaseInternal, phase.increment()) +
                blep(std::fmod( phaseInternal + 0.5,1), phase.increment());
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
            sum += 4 * square.phase.increment * square.getSample();
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
            return (phaseInternal + 2) * phaseInternal - 1;
        }
    // Unsure how this return statement works
    return {};
    }

    /// This function renders a circular buffer of the requested waveform type
    template <typename OscillatorType>
    void render(OscillatorType oscillator,FloatType frequency, FloatType sampleRate)
    {
     /// set frequency and sample rate
        oscillator.setFrequency(frequency,sampleRate);
     /// create buffer of correct size
        auto bufferSize = static_cast<int>(std::ceil(sampleRate / frequency));
     /// TODO: Check how to use the circular buffer and write example
        caspi_CircularBuffer<FloatType>::createCircularBuffer(bufferSize);

    /// write to buffer
        for (int i = 0; i < bufferSize; i++) {
            FloatType s = oscillator.getNextSample();
        }

    }



};



#endif //CASPI_BLEPOSCILLATOR_H
