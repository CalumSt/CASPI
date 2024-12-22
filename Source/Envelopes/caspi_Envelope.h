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


* @file caspi_EnvelopeGenerator.h
* @author CS Islay
* @brief A class implementing a variety of envelopes using ADSR stages.
*
************************************************************************/
#ifndef CASPI_ENVELOPEGENERATOR_H
#define CASPI_ENVELOPEGENERATOR_H

#include <Utilities/caspi_CircularBuffer.h>
#include "Utilities/caspi_assert.h"
#include <string>
#include <iostream>
#include <Utilities/caspi_Constants.h>
namespace CASPI::Envelope {

        /// ADSR parameter interface
    template <typename FloatType>
        struct Parameters {
            /// member variables
            /// Implemented with Redmon's analog equations
            const FloatType attackTCO   = static_cast<FloatType>(exp(-1.5));
            FloatType attackCoefficient = zero;
            FloatType attackOffset      = zero;

            const FloatType decayTCO   = static_cast<FloatType>(exp(-4.95));
            FloatType decayCoefficient = silence;
            FloatType decayOffset      = zero;

            FloatType sustainLevel = zero;

            FloatType releaseCoefficient = zero;
            FloatType releaseOffset      = zero;

            FloatType sampleRate         = static_cast<FloatType>(44100.0f);

            // Constants
            const FloatType silence = static_cast<FloatType>(0.0001f);
            const FloatType zero    = static_cast<FloatType>(0.0f);
            const FloatType one     = static_cast<FloatType>(1.0f);
            const FloatType two     = static_cast<FloatType>(2.0f);

            /// setters
            void setAttackTime(FloatType _attackTime_s)   {
                auto lengthInSamples =  sampleRate * _attackTime_s;
                attackCoefficient    = static_cast<FloatType>(std::exp(std::log((one + attackTCO) / attackTCO) / -lengthInSamples));
                attackOffset         = (one + attackTCO) * (one - attackCoefficient);
            }

            void setDecayTime(FloatType _decayTime_s) {
                CASPI_ASSERT(sustainLevel > zero,"Set sustain level to be non-zero before decay.");
                auto lengthInSamples =  sampleRate * _decayTime_s;
                decayCoefficient     = static_cast<FloatType>(std::exp(std::log((one + decayTCO) / decayTCO) / -lengthInSamples));
                decayOffset          = (sustainLevel - decayTCO) * (one - decayCoefficient);
            }

            void setSustainLevel(FloatType _sustainLevel) {
                if (_sustainLevel <= zero) { _sustainLevel = zero; }
                CASPI_ASSERT(sustainLevel < one, "Sustain level must be between 0 and 1.");
                sustainLevel = _sustainLevel;
            }

            void setReleaseTime(FloatType _releaseTime_s) {
                auto lengthInSamples =  sampleRate * _releaseTime_s;
                releaseCoefficient   = static_cast<FloatType>(std::exp(std::log((one + decayTCO) / decayTCO) / -lengthInSamples));
                releaseOffset        = (- decayTCO) * (one - releaseCoefficient);
            }

            /// getters - mostly for debugging
            FloatType getAttack()       { return attackCoefficient; }
            FloatType getDecay()        { return decayCoefficient; }
            FloatType getSustainLevel() { return sustainLevel; }
            FloatType getRelease()      { return releaseCoefficient; }

        };


        // Struct to hold state and related functionality
        enum class State { idle, attack, decay, slope, sustain, release, noteOn, noteOff };

        /**
        * @brief renders the next samples and applies to the samples within the buffer.
        */
        template <typename EnvelopeType, typename FloatType>
        void renderToBuffer(caspi_CircularBuffer<FloatType> buffer, const int bufferLength = 1) {
            EnvelopeType env;
            for (int sampleIndex = 0; sampleIndex << bufferLength; sampleIndex++) {
                auto sample = buffer.readBuffer();
                buffer.writeBuffer(sample * env.render());
            }
        }
    template <typename FloatType>
        struct EnvelopeBase {
            virtual ~EnvelopeBase() = default;

            State state;
            Parameters<FloatType> parameters;
            /// These are used to calculate the envelope
            FloatType level       = parameters.zero;
            FloatType target      = parameters.zero;
            FloatType coefficient = parameters.zero;
            FloatType offset      = parameters.zero;

            FloatType render() {
                level = coefficient * level + offset;
                nextState();
                return level;
            }
            /**
            * @brief Instructs the ADSR envelope that a note has been played.
            */
            void noteOn() {
                state = State::noteOn;
                nextState();
            }

            /**
            * @brief Instructs the ADSR envelope that a note has been released.
            */
            void noteOff() {
                state = State::noteOff;
                nextState();
            }

            /**
             * @brief reset the ADSR envelope to idle.
             */
            void reset() {
                level       = parameters.zero;
                coefficient = parameters.zero;
                target      = parameters.zero;
                state       = State::idle;
            }

            // TODO: convert these to individual functions that return a boolean
            std::string getState() {
                switch (state) {
                    case State::idle: return std::string { "idle" };
                    case State::attack: return std::string { "attack" };
                    case State::decay: return std::string { "decay" };
                    case State::slope: return std::string { "slope" };
                    case State::sustain: return std::string { "sustain" };
                    case State::release: return std::string { "release" };
                    case State::noteOn: return std::string { "noteOn" };
                    case State::noteOff: return std::string { "noteOff" };
                    default: return std::string { "idle" };
                }
            }

            // setters
            void setAttackTime(FloatType _attackTime_s)   { parameters.setAttackTime(_attackTime_s); }
            void setDecayTime(FloatType _decayTime_s)     { parameters.setDecayTime(_decayTime_s); }
            void setSustainLevel(FloatType _sustainLevel) { parameters.setSustainLevel(_sustainLevel); }
            void setReleaseTime(FloatType _releaseTime_s) { parameters.setReleaseTime(_releaseTime_s); }
            void setSampleRate(FloatType _sampleRate)     { CASPI_ASSERT(_sampleRate > parameters.zero,"Sample rate must be positive, non-zero."); parameters.sampleRate = _sampleRate; }

            // getters - mostly for debugging
            FloatType getAttack()       { return parameters.getAttack(); }
            FloatType getDecay()        { return parameters.getDecay(); }
            FloatType getSustainLevel() { return parameters.getSustainLevel(); }
            FloatType getRelease()      { return parameters.getRelease(); }

            virtual void nextState() { };
        };

        /// ADSR Envelope - most common envelope to use
        template <typename FloatType>
        struct ADSR final : EnvelopeBase<FloatType> {
        /// TODO: make this better
            using EnvelopeBase<FloatType>::level;
            using EnvelopeBase<FloatType>::coefficient;
            using EnvelopeBase<FloatType>::offset;
            using EnvelopeBase<FloatType>::target;
            using EnvelopeBase<FloatType>::parameters;
            using EnvelopeBase<FloatType>::state;
            using EnvelopeBase<FloatType>::render;
            using EnvelopeBase<FloatType>::noteOn;
            using EnvelopeBase<FloatType>::noteOff;
            using EnvelopeBase<FloatType>::reset;
            using EnvelopeBase<FloatType>::getState;

        private:
            /// This function handles state switching using the target parameter relative to levels.
            void nextState() override
            {
                if (state == State::noteOn) {
                    state       = State::attack;
                    level       = parameters.zero;
                    target      = parameters.one;
                    coefficient = parameters.attackCoefficient;
                    offset      = parameters.attackOffset;
                }
                else if (state == State::attack && level + target >= parameters.two) {
                    state       = State::decay;
                    level       = parameters.one;
                    target      = parameters.sustainLevel;
                    coefficient = parameters.decayCoefficient;
                    offset      = parameters.decayOffset;
                }
                else if (state==State::decay && level <= target) {
                    state       = State::sustain;
                    level       = parameters.sustainLevel;
                    coefficient = parameters.one;
                    offset      = parameters.zero;
                }
                else if (state == State::noteOff) {
                    state       = State::release;
                    target      = parameters.zero;
                    coefficient = parameters.releaseCoefficient;
                    offset      = parameters.releaseOffset;
                }
                else if (level <= parameters.zero) {
                    reset();
                }
            }
        };

    };

#endif // CASPI_ENVELOPEGENERATOR_H
