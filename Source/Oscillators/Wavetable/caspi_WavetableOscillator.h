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

* @file caspi_WavetableOscillator.h
* @author CS Islay
* @class caspi_WavetableOscillator
* @brief A wavetable oscillator class.
*
*
*
*
************************************************************************/

#ifndef CASPI_WAVETABLEOSCILLATOR_H
#define CASPI_WAVETABLEOSCILLATOR_H

#include "Utilities/caspi_assert.h"
#include "Utilities/caspi_CircularBuffer.h"

template <typename FloatType>
class caspi_WavetableOscillator {

public:
    [[nodiscard]] FloatType getSampleRate() const { return sampleRate; }
    void setSampleRate(const FloatType _sampleRate) { sampleRate = _sampleRate; }
    FloatType getNextSample();

private:
// Wavetables
    caspi_CircularBuffer<FloatType> Wavetable;





 // Parameters
 FloatType sampleRate = static_cast<FloatType>(44100.0);


};



#endif //CASPI_WAVETABLEOSCILLATOR_H
