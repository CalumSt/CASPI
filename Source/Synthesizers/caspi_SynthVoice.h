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


* @file caspi_SynthVoice.h
* @author CS Islay
* @class caspi_SynthVoice
* @brief A class implementing a synthesizer voice. Typically holds an oscillator, envelope and filter.
* This is currently implemented for these three.
*
************************************************************************/

#pragma once
#include "Utilities/caspi_Assert.h"
#include <cmath>
#include <algorithm>
template <typename FloatType,typename... Processors>
class fm_SynthVoice
{
    public:
        // methods
        void noteOn(const int _note, const int _velocity)
        {
            auto frequency = convertMidiToHz (note);
            auto modIndex = static_cast<FloatType>(0.5); /// CHANGE ME
            oscillator.setFrequency (frequency, modIndex ,sampleRate);
            note = _note;
            velocity = _velocity;
            gain = static_cast<FloatType>(0.75f); /// CHANGE ME
            envelope.noteOn();

        }

        void derampGain(FloatType time)
        {
            auto gainIncrement = gain / (time * sampleRate);
        }

        void noteOff()
        {
            envelope.noteOff();
            gain = oscillator.zero;
        }
        void reset()
        {
            noteOff();
            oscillator.reset();
            envelope.reset();
        }
        FloatType render() {
            FloatType nextSample = oscillator.getNextSample();
            FloatType envSample = envelope.render();
            return gain * envSample * nextSample;
        }
        void setSampleRate (FloatType _sampleRate)
        {
            CASPI_ASSERT (sampleRate > 0, "Sample Rate must be greater than zero.");
            sampleRate = _sampleRate;
            envelope.setSampleRate (_sampleRate);
        }

        void setADSR(FloatType _attackTime, FloatType _decayTime, FloatType _sustainLevel, FloatType _releaseTime)
        {
            setAttackTime (_attackTime);
            setSustainLevel (_sustainLevel);
            setDecayTime (_decayTime);
            setReleaseTime (_releaseTime);
        }

        void setAttackTime(FloatType _attackTime) { envelope.setAttackTime (_attackTime); }
        void setDecayTime(FloatType _decayTime) { envelope.setDecayTime (_decayTime); }
        void setSustainLevel(FloatType _sustainLevel) { envelope.setSustainLevel (_sustainLevel); }
        void setReleaseTime(FloatType _releaseLevel) { envelope.setReleaseTime (_releaseLevel); }

        [[nodiscard]] int getNote() const { return note; }
        [[nodiscard]] int getVelocity() const { return velocity; }
        [[nodiscard]] FloatType getSampleRate() const { return sampleRate; }

        static FloatType convertMidiToHz(const int _note)
        {
            constexpr auto A4_FREQUENCY = 440.0;
            constexpr auto A4_NOTE_NUMBER = 69.0;
            constexpr auto NOTES_IN_AN_OCTAVE = 12.0;
            return static_cast<FloatType>(A4_FREQUENCY * std::pow(2, (static_cast<double>(_note) - A4_NOTE_NUMBER) / NOTES_IN_AN_OCTAVE));
        }

private:
        FloatType gain = static_cast<FloatType>(0.0);
        int note = 0;
        int velocity = 0;
        FloatType sampleRate = static_cast<FloatType>(44100.0);
};