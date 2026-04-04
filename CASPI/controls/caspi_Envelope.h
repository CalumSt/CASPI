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


* @file caspi_Envelope.h
* @author CS Islay
* @brief A class implementing a variety of envelopes using ADSR stages.
*
************************************************************************/
#ifndef CASPI_ENVELOPEGENERATOR_H
#define CASPI_ENVELOPEGENERATOR_H

#include "base/caspi_Assert.h"
#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Producer.h"
#include <cmath>
#include <string>
namespace CASPI
{
    namespace Envelope
    {

        /// ADSR parameter interface
        template <typename FloatType>
        struct Parameters
        {
                // Constants
                const FloatType silence = static_cast<FloatType> (0.0001f);
                const FloatType zero    = static_cast<FloatType> (0.0f);
                const FloatType one     = static_cast<FloatType> (1.0f);
                const FloatType two     = static_cast<FloatType> (2.0f);
                /// member variables
                /// Implemented with Redmon's analog equations
                const FloatType attackTCO   = static_cast<FloatType> (std::exp (-1.5));
                FloatType attackCoefficient = zero;
                FloatType attackOffset      = zero;

                const FloatType decayTCO   = static_cast<FloatType> (std::exp (-4.95));
                FloatType decayCoefficient = silence;
                FloatType decayOffset      = zero;

                FloatType sustainLevel = zero;

                FloatType releaseCoefficient = zero;
                FloatType releaseOffset      = zero;

                FloatType sampleRate = static_cast<FloatType> (44100.0f);

                /// setters
                void setAttackTime (FloatType _attackTime_s)
                {
                    auto lengthInSamples = sampleRate * _attackTime_s;
                    attackCoefficient =
                        static_cast<FloatType> (std::exp (std::log ((one + attackTCO) / attackTCO) / -lengthInSamples));
                    attackOffset = (one + attackTCO) * (one - attackCoefficient);
                }

                void setDecayTime (FloatType _decayTime_s)
                {
                    CASPI_ASSERT (sustainLevel > zero, "Set sustain level to be non-zero before decay.");
                    auto lengthInSamples = sampleRate * _decayTime_s;
                    decayCoefficient =
                        static_cast<FloatType> (std::exp (std::log ((one + decayTCO) / decayTCO) / -lengthInSamples));
                    decayOffset = (sustainLevel - decayTCO) * (one - decayCoefficient);
                }

                bool setSustainLevel (FloatType _sustainLevel) noexcept
                {
                    if (_sustainLevel <= zero)
                    {
                        _sustainLevel = zero;
                        return false;
                    }
                    if (_sustainLevel > one)
                    {
                        _sustainLevel = one;
                        return false;
                    }
                    sustainLevel = _sustainLevel;
                    return true;
                }

                void setReleaseTime (FloatType _releaseTime_s)
                {
                    auto lengthInSamples = sampleRate * _releaseTime_s;
                    releaseCoefficient =
                        static_cast<FloatType> (std::exp (std::log ((one + decayTCO) / decayTCO) / -lengthInSamples));
                    releaseOffset = (-decayTCO) * (one - releaseCoefficient);
                }

                /// getters - mostly for debugging
                FloatType getAttack()
                {
                    return attackCoefficient;
                }
                FloatType getDecay()
                {
                    return decayCoefficient;
                }
                FloatType getSustainLevel()
                {
                    return sustainLevel;
                }
                FloatType getRelease()
                {
                    return releaseCoefficient;
                }
        };

        // Struct to hold state and related functionality
        enum class State
        {
            idle,
            attack,
            decay,
            slope,
            sustain,
            release,
            noteOn,
            noteOff
        };

        template <CASPI_FLOAT_TYPE FloatType>
        struct Envelope
        {
            virtual ~Envelope() = default;
            virtual void      noteOn()              noexcept = 0;
            virtual void      noteOff()             noexcept = 0;
            virtual void      reset()               noexcept = 0;
            virtual bool      isIdle()        const noexcept = 0;
            virtual FloatType getLevel()      const noexcept = 0;
        };

        /**********************************************************************************************==
         * ADSR<FloatType>
         *
         * Concrete ADSR envelope. Usable standalone via render(), or as a
         * graph Producer node via AudioGraph::addNode().
         *
         * Inherits Producer<ADSR<F>, F, PerFrame>:
         *   - AudioGraph calls process(ctx) -> processImpl(ctx) -> render(outputBuffer)
         *   - render(outputBuffer) calls renderSample() once per frame (PerFrame policy)
         *   - renderSample() calls render() — the same path as standalone use
         *
         * There is no wrapper, no delegation, no separate node class.
         * The ADSR IS the graph node.
         ***********************************************************************************************/
        template <CASPI_FLOAT_TYPE FloatType>
        class ADSR final : Envelope<FloatType>, public Core::Producer<ADSR<FloatType>, FloatType, Core::Traversal::PerFrame>
        {
            public:
                ADSR()
                    : Core::Producer<ADSR<FloatType>, FloatType, Core::Traversal::PerFrame> (0, 1)
                {
                }

                // *********************************************************************************************
                // Parameter API
                // *********************************************************************************************

                /**
                 * @brief Set all ADSR parameters in the correct internal order.
                 *
                 * Sustain is always applied before decay to satisfy the internal
                 * coefficient calculation dependency. Use this rather than individual
                 * setters to avoid ordering errors.
                 */
                void setADSR (FloatType attack, FloatType decay, FloatType sustain, FloatType release)
                {
                    parameters.setSustainLevel (sustain);
                    parameters.setAttackTime (attack);
                    parameters.setDecayTime (decay);
                    parameters.setReleaseTime (release);
                }

                void setAttackTime (FloatType t)
                {
                    parameters.setAttackTime (t);
                }
                void setDecayTime (FloatType t)
                {
                    parameters.setDecayTime (t);
                }
                void setReleaseTime (FloatType t)
                {
                    parameters.setReleaseTime (t);
                }
                bool setSustainLevel (FloatType s)
                {
                    return parameters.setSustainLevel (s);
                }

                // *********************************************************************************************
                // Voice control
                // *********************************************************************************************

                void noteOn() noexcept CASPI_NON_BLOCKING override
                {
                    state = State::noteOn;
                    advanceState();
                }

                void noteOff() noexcept CASPI_NON_BLOCKING override
                {
                    state = State::noteOff;
                    advanceState();
                }

                void reset() noexcept CASPI_NON_BLOCKING override
                {
                    level       = parameters.zero;
                    coefficient = parameters.zero;
                    offset      = parameters.zero;
                    target      = parameters.zero;
                    state       = State::idle;
                }

                // *********************************************************************************************
                // Standalone render
                // *********************************************************************************************

                /**
                 * @brief Render one envelope sample and advance state.
                 *
                 * Called directly in standalone use. Called via renderSample() when
                 * used as a graph node — same code path either way.
                 */
                FloatType render() noexcept CASPI_NON_BLOCKING
                {
                    level = coefficient * level + offset;
                    advanceState();
                    return level;
                }

                // *********************************************************************************************
                // Observers
                // *********************************************************************************************

                /**
                 * @brief Gets the current state of the ADSR.
                 * @return The current state.
                 */
                CASPI_NO_DISCARD
                State getState() const noexcept { return state; }
                /**
                 * @brief Gets the current level of the ADSR
                 * @return The current level between 0 and 1
                 */
                CASPI_NO_DISCARD
                FloatType getLevel() const CASPI_NON_BLOCKING noexcept { return level; }
                /**
                 * @brief Gets the current level of the target.
                 * @return The current level of the target
                 */
                CASPI_NO_DISCARD
                FloatType getTarget() const CASPI_NON_BLOCKING noexcept { return target; }

                /// getters - mostly for debugging
                FloatType getAttack()       { return parameters.attackCoefficient; }
                FloatType getDecay()        { return parameters.decayCoefficient; }
                FloatType getSustainLevel() { return parameters.sustainLevel; }
                FloatType getRelease()      { return parameters.releaseCoefficient; }

                /**
                 * @brief Checks if the envelope is idle.
                 * @return True if it is idle, false otherwise.
                 */
                CASPI_NO_DISCARD
                bool isIdle() const noexcept CASPI_NO_DISCARD override
                {
                    return state == State::idle;
                }
                /**
                 *
                 * @return
                 */
                bool isActive() const noexcept CASPI_NO_DISCARD
                {
                    return state != State::idle;
                }


                std::string getStateString() const
                {
                    switch (state)
                    {
                        case State::idle:
                            return "idle";
                        case State::attack:
                            return "attack";
                        case State::decay:
                            return "decay";
                        case State::sustain:
                            return "sustain";
                        case State::release:
                            return "release";
                        case State::noteOn:
                            return "noteOn";
                        case State::noteOff:
                            return "noteOff";
                        default:
                            return "idle";
                    }
                }

                // *********************************************************************************************
                // CRTP hooks — graph integration
                // These are called automatically by AudioGraph; do not call directly.
                // *********************************************************************************************

                /**
                 * @brief Called by AudioGraph::prepare() when sample rate changes.
                 *
                 * Forwards to Parameters::sampleRate so coefficient recalculation
                 * on next setADSR() uses the correct rate. If setADSR() was already
                 * called before prepare(), call it again after prepare().
                 */
                void onSampleRateChanged (FloatType rate) override
                {
                    CASPI_ASSERT (rate > FloatType (0), "Sample rate must be positive.");
                    parameters.sampleRate = rate;
                }

                /**
                 * @brief Called by Producer::render() once per frame (PerFrame policy).
                 *
                 * This is the single point connecting standalone and graph use.
                 * In standalone: call render() directly.
                 * In graph: AudioGraph -> process() -> processImpl() -> render(outputBuffer)
                 *           -> renderSample() -> render().
                 */
                FloatType renderSample() CASPI_NON_BLOCKING override
                {
                    return render();
                }

            private:
                // *********************************************************************************************
                // State machine
                // *********************************************************************************************

                void advanceState() noexcept
                {
                    switch (state)
                    {
                        case State::noteOn:
                            state       = State::attack;
                            level       = parameters.zero;
                            target      = parameters.one;
                            coefficient = parameters.attackCoefficient;
                            offset      = parameters.attackOffset;
                            break;

                        case State::attack:
                            if (level + target >= parameters.two)
                            {
                                state       = State::decay;
                                level       = parameters.one;
                                target      = parameters.sustainLevel;
                                coefficient = parameters.decayCoefficient;
                                offset      = parameters.decayOffset;
                            }
                            break;

                        case State::decay:
                            if (level <= target)
                            {
                                state       = State::sustain;
                                level       = parameters.sustainLevel;
                                coefficient = parameters.one;
                                offset      = parameters.zero;
                            }
                            break;

                        case State::sustain:
                            break;

                        case State::noteOff:
                            state       = State::release;
                            target      = parameters.zero;
                            coefficient = parameters.releaseCoefficient;
                            offset      = parameters.releaseOffset;
                            break;

                        case State::release:
                            if (level <= parameters.zero) reset();
                            break;

                        case State::idle:
                        default:
                            break;
                    }
                }

                // *********************************************************************************************
                // Data
                // *********************************************************************************************

                Parameters<FloatType> parameters;

                State state           = State::idle;
                FloatType level       = FloatType (0);
                FloatType target      = FloatType (0);
                FloatType coefficient = FloatType (0);
                FloatType offset      = FloatType (0);
        };

    }; // namespace Envelope
} // namespace CASPI

#endif // CASPI_ENVELOPEGENERATOR_H
