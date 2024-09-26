#ifndef CASPI_WAVETABLEOSCILLATOR_H
#define CASPI_WAVETABLEOSCILLATOR_H

#include "../caspi_Oscillator.h"

/************************************************************************
      ___           ___           ___           ___
     /\__\         /\  \         /\__\         /\  \
    /:/  /        /::\  \       /:/ _/_       /::\  \     ___
   /:/  /        /:/\:\  \     /:/ /\  \     /:/\:\__\   /\__\
  /:/  /  ___   /:/ /::\  \   /:/ /::\  \   /:/ /:/  /  /:/__/
 /:/__/  /\__\ /:/_/:/\:\__\ /:/_/:/\:\__\ /:/_/:/  /  /::\  \
 \:\  \ /:/  / \:\/:/  \/__/ \:\/:/ /:/  / \:\/:/  /   \/\:\  \__
  \:\  /:/  /   \::/__/       \::/ /:/  /   \::/__/     ~~\:\/\__\
   \:\/:/  /     \:\  \        \/_/:/  /     \:\  \        \::/  /
    \::/  /       \:\__\         /:/  /       \:\__\       /:/  /
     \/__/         \/__/         \/__/         \/__/       \/__/


* @file caspi_WavetableOscillator.h
* @author CS Islay
* @class caspi_WavetableOscillator
* @brief A base class for oscillators providing a common API
*
************************************************************************/

class caspi_WavetableOscillator : caspi_Oscillator {

public:
 [[nodiscard]] float getSampleRate() const { return sampleRate; }
 void setSampleRate(const float _sampleRate) { sampleRate = _sampleRate; }

private:
 // Supported waveforms
 enum Waveform
 {
  sawtooth,
  sine,
  triangle,
  square,
};

 // Methods
void generateWavetable(Waveform);

 // Parameters
 float sampleRate = 44100.0f;


};



#endif //CASPI_WAVETABLEOSCILLATOR_H
