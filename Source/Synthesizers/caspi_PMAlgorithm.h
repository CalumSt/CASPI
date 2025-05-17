#ifndef CASPI_PMALGORITHM_H
#define CASPI_PMALGORITHM_H
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
* @file caspi_PMAlgorithm.h
* @author CS Islay
************************************************************************/

#include "Oscillators/caspi_PMOperator.h"
#include "Utilities/caspi_Constants.h"

#include <Utilities/caspi_Maths.h>

/**
 * @namespace Algorithms
 * @brief Algorithms define various combinations of modulators and carriers (typically 1).
 *        The required functions are defined by the virtual class AlgBase.
 *        An algorithm is a way to wrap up all the logic required to create an FM signal, similar to
 *        a voice in a subtractive synth.
 */
namespace CASPI::PM
{
    enum class OpIndex : int
    {
        OpA,
        OpB,
        OpC,
        OpD,
        OpE,
        OpF,
        OpG,
        OpH,
        OpI,
        OpJ,
        OpK,
        OpL,
        OpM,
        All
    }; // You probably don't need more than 12 operators

    constexpr int max_operators = 12;

    /**
     * @brief Converts an enum class to the underlying type
     * @tparam E Enum class
     * @param e enum object to use
     * @return the enum as an index
     */
    template <typename E>
    constexpr auto to_index(E e) -> std::underlying_type_t<E>
    {
        return static_cast<std::underlying_type_t<E>>(e);
    }

    /**
     * @class AlgBase
     * @brief Defines a common API between algorithms.
     */
    template <typename FloatType>
    class AlgBase
    {
    public:
        virtual void noteOn() noexcept                                                       = 0;
        virtual void noteOff() noexcept                                                      = 0;
        virtual FloatType render() noexcept                                                  = 0;
        virtual void reset() noexcept                                                        = 0;
        virtual void setFrequency (FloatType newFrequency, FloatType newSampleRate) noexcept = 0;
        virtual void setSampleRate (FloatType newSampleRate) noexcept                        = 0;
        virtual void setADSR (FloatType attackTime_s, FloatType decayTime_s,
                              FloatType sustainLevel, FloatType releaseType_s) noexcept      = 0;
        virtual void enableADSR() noexcept                                                   = 0;
        virtual void disableADSR() noexcept                                                  = 0;
        virtual void prepareToPlay () noexcept                                               = 0;
        virtual ~AlgBase()                                                          = default;
        [[nodiscard]] int getNumOperators() const { return numOperators; }

        FloatType getSampleRate () const { return sampleRate; }
        FloatType getFrequency () const { return frequency; }

        int numOperators     = 0;
        FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
        FloatType frequency  = CASPI::Constants::one<FloatType>; // non-zero to prevent assertion hitting
    };

    /**
    * @class Algorithm
    * @brief Defines the base for algorithms of a variable number of operators.
    *        Implements everything except the algorithms themselves.
    *        A final concrete implementation will override render() and setAlgorithm().
    */
    template <typename FloatType, int numOperators, typename AlgIndexEnum>
    class Algorithm : public AlgBase<FloatType>
    {
        using OP = Operator<FloatType>;
        public:

        explicit Algorithm()
        {
            CASPI_STATIC_ASSERT(numOperators <= max_operators, "Tried to construct an algorithm with too many operators.");
            this->numOperators = numOperators;
        }

        /**
         * @brief enables sound generation for a specific frequency on all operators
         */
        void noteOn() noexcept override { for (auto& op : operators) op.noteOn(); }

        /**
         * @brief disables sound generation for all operators
         */
        void noteOff() noexcept override { for (auto& op : operators) op.noteOff();  }

        /**
         * @brief Reset the algorithm to its default state.
         */
        void reset() noexcept override
        {
            for (auto& op : operators) op.reset();

            this->frequency = CASPI::Constants::one<FloatType>;
            this->sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;

        }

        /**
         * @brief Prepares the algorithm to play. You will likely need to override this if you need to allocate anything (E.G. buffers) before playing!
         */
        void prepareToPlay () noexcept override
        {
            // Unimplemented for now!
        }

        /**
         * @brief Sets the algorithm to use
         * @param alg An enum with each alogrithm
         */
        void setAlgorithm (const AlgIndexEnum alg) noexcept
        {
            currentAlgorithm = alg;
        }

        /**
         * @brief Set the frequency of the algorithm and operators.
         * @param newFrequency The frequency in Hz to use.
         * @param newSampleRate The new sample rate to use.
         */
        void setFrequency (FloatType newFrequency, FloatType newSampleRate) noexcept override
        {
            this->frequency = newFrequency;
            this->sampleRate = newSampleRate;
            for (auto& op : operators) op.setFrequency(newFrequency, newSampleRate);
        }

        /**
         * @brief Set the master sample rate.
         * @param newSampleRate The new sample rate to use.
         */
        void setSampleRate (FloatType newSampleRate) noexcept override
        {
            this->sampleRate = newSampleRate;

            for (auto& op : operators)
            {
                op.setFrequency(this->frequency, newSampleRate);
            }

        }

        /**
         * @brief Set the ADSR parameters for all operators in seconds, and linear for sustain level.
         * @param attackTime_s The attack time in seconds.
         * @param decayTime_s  The decay time in seconds.
         * @param sustainLevel The linear sustain level between 0 and 1.
         * @param releaseType_s The release time in seconds.
         */
        void setADSR (FloatType attackTime_s, FloatType decayTime_s,
                              FloatType sustainLevel, FloatType releaseType_s) noexcept override
        {
            for (auto& op : operators) op.setADSR(attackTime_s, decayTime_s, sustainLevel, releaseType_s);
        }

        /**
         * @brief Set the ADSR parameters for a specific operator.
         * @param op The operator to set the ADSR parameters.
         * @param attackTime_s The attack time in seconds.
         * @param decayTime_s  The decay time in seconds.
         * @param sustainLevel The linear sustain level between 0 and 1.
         * @param releaseType_s The release time in seconds.
         */
        void setADSR (const OpIndex op, const FloatType attackTime_s, const FloatType decayTime_s,
                              const FloatType sustainLevel, const FloatType releaseType_s) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).setADSR(attackTime_s, decayTime_s, sustainLevel, releaseType_s); }
        }

        /**
         * @brief Set the attack time for a specific operator.
         * @param op an Enum class index for the specific enum.
         * @param attackTime_s The attack time to use in seconds.
         */
        void setAttackTime(const OpIndex op, const FloatType attackTime_s) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).setAttackTime(attackTime_s); }
        }

        /**
         * @brief Set the decay time for specific operator. Due to how ADSR is calculated, it is required for this to be called AFTER setSustainLevel, so that decay can happen to the right level.
         * @param op an Enum class index for the specific enum.
         * @param decayTime_s  The decay time in seconds.
         */
        void setDecayTime(const OpIndex op, const FloatType decayTime_s) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).setDecayTime(decayTime_s); }
        }

        /**
         * @brief Set the sustain level of a specific operator.
         * @param op The operator to set the ADSR parameters.
         * @param sustainLevel The linear sustain level between 0 and 1.
         */
        void setSustainLevel(const OpIndex op, const FloatType sustainLevel) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).setSustainLevel(sustainLevel); }
        }

        /**
         * @brief Set the release time in seconds of the specific operator.
         * @param op an Enum class index for the specific enum.
         * @param releaseTime_s  The release time in seconds.
         */
        void setReleaseTime(const OpIndex op, const FloatType releaseTime_s) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).setReleaseTime(releaseTime_s); }
        }

        /**
         * @brief Enables ADSR envelopes for all operators.
         */
        void enableADSR() noexcept override { for (auto& op : operators) op.enableEnvelope(); }

        /**
         * @brief Enables ADSR envelopes for a specific operator.
         * @param op The operator enum index.
         */
        void enableADSR(const OpIndex op) noexcept
        {
            if (op < OpIndex::All) { operators.at(to_index(op)).enableEnvelope(); }
        }

        /**
         * @brief Disables ADSR envelopes for all operators.
         */
        void disableADSR() noexcept override { for (auto& op : operators) op.disableEnvelope(); }

        /**
         * @brief Disables ADSR envelopes for a specific operator.
         * @param op The operator enum index.
         */
        void disableADSR(const OpIndex op) noexcept
        {
            if (op < OpIndex::All)
            {
                operators.at(to_index(op)).disableEnvelope();
            }
        }

        /**
         * @brief Sets the modulation index of a specific operator.
         * @param op The operator enum index.
         * @param modIndex The modulation index to use.
         */
        void setModulationIndex(const OpIndex op, const FloatType modIndex) noexcept
        {
            if (op < OpIndex::All)
            {
                operators.at(to_index(op)).setModulationIndex(modIndex);
            }
        }

        /**
         * @brief Sets the modulation depth of a specific operator. Note that for carriers, this is equivalent to setting an output level. Clamps the value to between 0 and 1.
         * @param op The operator enum index.
         * @param modDepth The modDepth to use.
         */
        void setModulationDepth(const OpIndex op, FloatType modDepth) noexcept
        {
            if (op < OpIndex::All)
            {
                CASPI::Maths::clamp(modDepth, 0, 1);
                operators.at(to_index(op)).setModulationDepth(modDepth);
            }
        }

        /**
         * @brief Sets all modulation parameters for a specific operator.
         * @param op The operator enum index.
         * @param modIndex
         * @param modDepth
         * @param modFeedback
         */
        void setModulation(const OpIndex op, const FloatType modIndex, const FloatType modDepth, const FloatType modFeedback)
        {
            if (op < OpIndex::All)
            {
                operators.at(to_index(op)).setModulation(modIndex, modDepth, modFeedback);
            }
        }

        /**
         * @brief Sets the modulation feedback amount for a specific operator. Clamps the feedback to between 0 and 1.
         * @param op The operator enum index.
         * @param modFeedback The mod feedback to use, between 0 and 1.
         */
        void setModulationFeedback(const OpIndex op, FloatType modFeedback) noexcept
        {
            if (op < OpIndex::All)
            {
                CASPI::Maths::clamp(modFeedback, 0, 1);
                operators.at(to_index(op)).setModFeedback(modFeedback);
            }
        }

        /**
         * @brief Enables mod feedback for a specific operator.
         * @param op The operator enum index.
         */
        void enableModFeedback(const OpIndex op) noexcept
        {
            if (op < OpIndex::All)
            {
                operators.at(to_index(op)).enableModFeedback();
            }
        }

        /**
         * @brief Disables mod feedback for a specific operator.
         * @param op The operator enum index.
         */
        void disableModFeedback(const OpIndex op) noexcept
        {
            if (op < OpIndex::All)
            {
                operators.at(to_index(op)).disableModFeedback();
            }
        }

        /**
         * @brief Sets the output level for the algorithm, in linear units.
         * @param newOutputLevel The output level in linear units.
         */
        void setOutputLevel(FloatType newOutputLevel) noexcept
        {
            CASPI::Maths::clamp(newOutputLevel, 0, 1);
            this->outputLevel = Maths::clamp(newOutputLevel, CASPI::Constants::zero<FloatType>, CASPI::Constants::one<FloatType>);
        }

        private:

            std::vector<OP> operators = std::vector<OP>(numOperators);

            FloatType outputLevel = CASPI::Constants::one<FloatType>;

            AlgIndexEnum currentAlgorithm;

    };


    enum class Algorithms
    {
        BasicCascade,
        TwoCarriers
    };

/**
* @class TwoOperatorAlgs
* @brief A class containing all possible two operator algorithms.
*
*        Basic Cascade:
*        1 Modulator modulates the carrier, with an option for ADSR on either using an enum.
*        The modulator can also self-feedback.
*
*         __________
*        |    _____|_____        __________
*        |   |           |      |          |
*        |-->|    A      |----->|     B    | -----> Output
*            |___________|      |__________|
*
*        Two Carriers :
*        Two sine waves are summed together, where both can self-modulate
*
*         __________
*        |    _____|_____
*        |   |           |
*        |-->|     A     |-----|
*            |___________|     |
*                              |
*         __________           |-----------> Output
*        |    _____|_____      |
*        |   |           |     |
*        |-->|     B     |-----|
*            |___________|
*
*
*/
    template <typename FloatType=double>
    class TwoOperatorAlgs final: private AlgBase<FloatType>
    {
        using OP = CASPI::PM::Operator<FloatType>;

    public:

        TwoOperatorAlgs() = default;
        TwoOperatorAlgs(const TwoOperatorAlgs&) = default;
        TwoOperatorAlgs(TwoOperatorAlgs&&) = default;
        TwoOperatorAlgs& operator=(const TwoOperatorAlgs&) = default;
        TwoOperatorAlgs& operator=(TwoOperatorAlgs&&) = default;

        void noteOn() noexcept override { OperatorB.noteOn(); OperatorA.noteOn(); }
        void noteOff() noexcept override { OperatorB.noteOff(); OperatorA.noteOff(); }

        void setFrequency (const FloatType newFrequency, const FloatType newSampleRate) noexcept override
        {
            this->frequency = newFrequency;
            this->sampleRate = newSampleRate;
            OperatorB.setFrequency (newFrequency, newSampleRate);
            OperatorA.setFrequency (newFrequency, newSampleRate);
        }

        void setSampleRate (const FloatType newSampleRate) noexcept override
        {
            OperatorB.setSampleRate (newSampleRate);
            OperatorA.setSampleRate (newSampleRate);
            this->sampleRate = newSampleRate;
        }

        void setOutputLevel (const FloatType outputLevel) noexcept
        {
            if (currentAlg == Algorithms::TwoCarriers)
            {
                OperatorA.setModDepth (outputLevel);
            }
            OperatorB.setModDepth (outputLevel);
        }

        void setModulation (FloatType modIndex, FloatType modDepth) noexcept
        {
            if (currentAlg == Algorithms::BasicCascade)
            {
                OperatorA.setModulation (modIndex, modDepth);
            }
        }

        void setModulationFeedback (OpIndex op, FloatType modFeedback) noexcept
        {
            OP *Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                OperatorA.setModFeedback (modFeedback);
                OperatorB.setModFeedback (modFeedback);
            } else
            {
                Operator->setModFeedback (modFeedback);
            }
        }

        void enableADSR () noexcept override
        {
            enableADSR (OpIndex::All);
        }

        void disableADSR () noexcept override
        {
            disableADSR (OpIndex::All);
        }


        void enableADSR (OpIndex op) noexcept
        {
            OP *Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                OperatorA.enableEnvelope();
                OperatorB.enableEnvelope();
            } else
            {
                Operator->enableEnvelope();
            }
        }

        void disableADSR (OpIndex op) noexcept
        {
            OP *Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                OperatorA.disableEnvelope();
                OperatorB.disableEnvelope();
            } else
            {
                Operator->disableEnvelope();
            }

        }

        void setAlgorithm (const Algorithms algToUse) noexcept
        {
            switch (algToUse)
            {
                case Algorithms::BasicCascade:
                    initialiseBasicCascade();
                    break;
                case Algorithms::TwoCarriers:
                    initialiseTwoCarriers();
                    break;
                default:
                    break;
            }
        }

        void prepareToPlay () noexcept override
        {
            setAlgorithm(currentAlg);
        }

        FloatType render () noexcept override
        {
            if (currentAlg == Algorithms::BasicCascade)
            {
                auto modSignal = OperatorA.render();
                auto signal    = OperatorB.render (modSignal);
                return signal;
            }

            if (currentAlg == Algorithms::TwoCarriers)
            {
                auto modSignal = OperatorA.render();
                auto signal    = OperatorB.render();
                return (signal + modSignal) / 2;
            }
            return CASPI::Constants::zero<FloatType>;
        }

        void reset () noexcept override
        {
            OperatorB.reset();
            OperatorA.reset();
        }

        void setADSR ( OpIndex op,
            FloatType attackTime_s, FloatType decayTime_s,
            FloatType sustainLevel, FloatType releaseType_s ) noexcept
        {
            auto Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                setADSR(OpIndex::OpA, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
                setADSR(OpIndex::OpB, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
            } else
            {
                Operator->setAttackTime(attackTime_s);
                Operator->setSustainLevel(sustainLevel);
                Operator->setDecayTime(decayTime_s);
                Operator->setReleaseTime(releaseType_s);
            }

        }

        void setADSR (FloatType attackTime_s, FloatType decayTime_s,
                      FloatType sustainLevel, FloatType releaseType_s) noexcept override
        {
            setADSR (OpIndex::All, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
        }

        void setAttackTime (OpIndex op, const FloatType attackTime_s) noexcept
        {
            auto Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                OperatorA.setAttackTime(attackTime_s);
                OperatorB.setAttackTime(attackTime_s);
            } else
            {
                Operator->setAttackTime(attackTime_s);
            }

        }

        void setDecayTime (OpIndex op, const FloatType decayTime_s) noexcept
        {
            auto Operator = operatorMap[op];
            if (Operator == nullptr)
            {
                OperatorA.setDecayTime(decayTime_s);
                OperatorB.setDecayTime(decayTime_s);
            } else
            {
                Operator->setDecayTime(decayTime_s);
            }

        }

        void setSustainLevel (OpIndex op, const FloatType sustainLevel) noexcept
        {
            auto Operator = operatorMap[op];

            if (Operator == nullptr)
            {
                OperatorA.setSustainLevel(sustainLevel);
                OperatorB.setSustainLevel(sustainLevel);
            } else
            {
                Operator->setSustainLevel(sustainLevel);
            }

        }

        void setReleaseTime (OpIndex op, const FloatType releaseTime_s) noexcept
        {
            auto Operator = operatorMap[op];

            if (Operator == nullptr)
            {
                OperatorA.setReleaseTime(releaseTime_s);
                OperatorB.setReleaseTime(releaseTime_s);
            } else
            {
                Operator->setReleaseTime(releaseTime_s);
            }
        }

        void setModFeedback(OpIndex op, const FloatType modFeedback) noexcept
        {
            auto Operator = operatorMap[op];

            if (Operator == nullptr)
            {
                OperatorA.setModFeedback(modFeedback);
                OperatorB.setModFeedback(modFeedback);
            } else
            {
                Operator->setModFeedback(modFeedback);
            }
        }

    private:

        OP OperatorA;

        OP OperatorB;

        const int numOperators = 2;

        Algorithms currentAlg = Algorithms::BasicCascade;

        std::unordered_map<OpIndex, OP*> operatorMap =
        {
            { OpIndex::OpA, &OperatorA },
            { OpIndex::OpB, &OperatorB },
            { OpIndex::All, nullptr }
        };

        void resetAll()
        {
            OperatorA.reset();
            OperatorB.reset();
        }

        void initialiseBasicCascade ()
        {
            resetAll();

            OperatorA.setFrequency(this->frequency, this->sampleRate);

            OperatorB.setFrequency(this->frequency, this->sampleRate);

            OperatorA.setModulation(OperatorA.getModulationIndex(),
                        OperatorA.getModulationDepth(),
                        OperatorA.getModulationFeedback());

            currentAlg = Algorithms::BasicCascade;
        }

        void initialiseTwoCarriers ()
        {
            resetAll();

            OperatorA.setFrequency(this->frequency, this->sampleRate);

            OperatorB.setFrequency(this->frequency, this->sampleRate);

            OperatorA.setModDepth(OperatorB.getModulationDepth());

            currentAlg = Algorithms::TwoCarriers;
        }

    };


}; // namespace Algorithms

#endif //CASPI_PMALGORITHM_H
