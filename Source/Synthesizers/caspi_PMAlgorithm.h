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

/**
 * @namespace Algorithms
 * @brief Algorithms define various combinations of modulators and carriers (typically 1).
 *        The required functions are defined by the virtual class AlgBase.
 *        An algorithm is a way to wrap up all the logic required to create an FM signal, similar to
 *        a voice in a subtractive synth.
 */
namespace CASPI::PM::Algorithms
{
    /**
     * @class AlgBase
     * @brief Defines a common API between algorithms.
     */
    template <typename FloatType>
    class AlgBase
    {
    public:
        virtual void noteOn()                                                       = 0;
        virtual void noteOff()                                                      = 0;
        virtual FloatType render()                                                  = 0;
        virtual void reset()                                                        = 0;
        virtual void setModulation()                                                = 0;
        virtual void setFrequency (FloatType newFrequency, FloatType newSampleRate) = 0;
        virtual void setSampleRate (FloatType newSampleRate)                        = 0;
        virtual void setADSR (FloatType attackTime_s, FloatType decayTime_s,
                              FloatType sustainLevel, FloatType releaseType_s)      = 0;
        virtual void enableADSR()                                                   = 0;
        virtual void disableADSR()                                                  = 0;
        virtual void prepareToPlay ()                                               = 0;
        virtual ~AlgBase()                                                          = default;
        [[nodiscard]] int getNumOperators() const { return numOperators; }

        FloatType getSampleRate () const { return sampleRate; }
        FloatType getFrequency () const { return frequency; }

        int numOperators     = 0;
        FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
        FloatType frequency  = CASPI::Constants::one<FloatType>; // non-zero to prevent assertion hitting
    };


    enum class Algorithms
    {
        BasicCascade,
        TwoCarriers
    };

    enum class OpIndex
    {
        OpA,
        OpB,
        All
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

        void noteOn() override { OperatorB.noteOn(); OperatorA.noteOn(); }
        void noteOff() override { OperatorB.noteOff(); OperatorA.noteOff(); }

        void setFrequency (const FloatType newFrequency, const FloatType newSampleRate) override
        {
            this->frequency = newFrequency;
            this->sampleRate = newSampleRate;
            OperatorB.setFrequency (newFrequency, newSampleRate);
            OperatorA.setFrequency (newFrequency, newSampleRate);
        }

        void setModulation () override
        {
            /* Currently unimplemented for this algorithm */
        }

        void setSampleRate (const FloatType newSampleRate) override
        {
            OperatorB.setSampleRate (newSampleRate);
            OperatorA.setSampleRate (newSampleRate);
            this->sampleRate = newSampleRate;
        }

        void setOutputLevel (const FloatType outputLevel)
        {
            if (currentAlg == Algorithms::TwoCarriers)
            {
                OperatorA.setModDepth (outputLevel);
            }
            OperatorB.setModDepth (outputLevel);
        }

        void setModulation (FloatType modIndex, FloatType modDepth)
        {
            if (currentAlg == Algorithms::BasicCascade)
            {
                OperatorA.setModulation (modIndex, modDepth);
            }
        }

        void setModulationFeedback (OpIndex op, FloatType modFeedback)
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

        void enableADSR () override
        {
            enableADSR (OpIndex::All);
        }

        void disableADSR () override
        {
            disableADSR (OpIndex::All);
        }


        void enableADSR (OpIndex op)
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

        void disableADSR (OpIndex op)
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

        void setAlgorithm (const Algorithms algToUse)
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

        void prepareToPlay () override
        {
            setAlgorithm(currentAlg);
        }

        FloatType render () override
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

        void reset () override
        {
            OperatorB.reset();
            OperatorA.reset();
        }

        void setADSR ( OpIndex op,
            FloatType attackTime_s, FloatType decayTime_s,
            FloatType sustainLevel, FloatType releaseType_s )
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
                      FloatType sustainLevel, FloatType releaseType_s) override
        {
            setADSR (OpIndex::All, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
        }

        void setAttackTime (OpIndex op, const FloatType attackTime_s)
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

        void setDecayTime (OpIndex op, const FloatType decayTime_s)
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

        void setSustainLevel (OpIndex op, const FloatType sustainLevel)
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

        void setReleaseTime (OpIndex op, const FloatType releaseTime_s)
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

        void setModFeedback(OpIndex op, const FloatType modFeedback)
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

    template <typename FloatType=double>
    class FourOperatorAlgs final : public AlgBase<FloatType>
    {
        using OP = CASPI::PM::Operator<FloatType>;

        enum class Algorithms
        {
            Cascade,
            TwoCascades
        };

        enum class OpIndex
        {
            OpA,
            OpB,
            OpC,
            OpD,
            All
        };

        public:


        private:
            OP OperatorA;

            OP OperatorB;

            OP OperatorC;

            OP OperatorD;

            const int numOperators = 4;

            FloatType frequency = CASPI::Constants::one<FloatType>;

            FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;

            void initialiseCascade ()
            {
                OperatorA.setFrequency(this->frequency, this->sampleRate);
                OperatorB.setFrequency(this->frequency, this->sampleRate);
                OperatorC.setFrequency(this->frequency, this->sampleRate);
                OperatorD.setFrequency(this->frequency, this->sampleRate);
            }
    };


}; // namespace Algorithms

#endif //CASPI_PMALGORITHM_H
