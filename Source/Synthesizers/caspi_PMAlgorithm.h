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
#include "Utilities/caspi_LeakDetector.h"

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
        virtual void noteOn()                                                  = 0;
        virtual void noteOff()                                                 = 0;
        virtual FloatType render()                                             = 0;
        virtual void reset()                                                   = 0;
        virtual void setModulation()                                           = 0;
        virtual void setFrequency (FloatType frequency, FloatType sampleRate)  = 0;
        virtual void setADSR (FloatType attackTime_s, FloatType decayTime_s,
                              FloatType sustainLevel, FloatType releaseType_s) = 0;
        virtual void enableADSR()                                              = 0;
        virtual void disableADSR()                                             = 0;
        virtual ~AlgBase()                                                     = default;
        [[nodiscard]] int getNumOperators() const { return numOperators; }

    private:
        int numOperators     = 0;
        FloatType sampleRate = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
    };

    /**
    * @class BasicCascade
    * @brief A class implementing a 2-Operator linear algorithm.
    *        1 Modulator modulates the carrier, with an option for ADSR on either using an enum.
    *        The modulator can also self-feedback.
    *
    *         __________
    *        |    _____|_____        __________
    *        |   |           |      |          |
    *        |-->| Modulator |----->|  Carrier | -----> Output
    *            |___________|      |__________|
    *
    *        The OpCodes enum is used to determine if you are applying the function to the Carrier or Modulator.
    */
        enum class BasicCascadeOpCodes
        {
            Carrier,
            Modulator,
            All
        };;

    template <typename FloatType>
    class BasicCascade final : public AlgBase<FloatType>
    {
        using OP = CASPI::PM::Operator<FloatType>;

    public:
        void noteOn() override
        {
            Carrier.noteOn();
            Modulator.noteOn();
        }

        void noteOff() override
        {
            Carrier.noteOff();
            Modulator.noteOff();
        }

        void setFrequency (const FloatType frequency, const FloatType sampleRate) override
        {
            Carrier.setFrequency (frequency, sampleRate);
            Modulator.setFrequency (frequency, sampleRate);
        }

        void setModulation() override
        {
            /* Currently unimplemented for this algorithm */
        }

        void setSampleRate(const FloatType newSampleRate)
        {
            Carrier.setSampleRate (newSampleRate);
            Modulator.setSampleRate (newSampleRate);
        }

        void setOutputLevel (const FloatType outputLevel)
        {
            Carrier.setModDepth (outputLevel);
        }

        void setModulation (FloatType modIndex, FloatType modDepth)
        {
            Modulator.setModulation (modIndex, modDepth);
        }

        void setModulationFeedback (FloatType modFeedback)
        {
            Modulator.setModFeedback (modFeedback);
        }

        void enableADSR() override { enableADSR (BasicCascadeOpCodes::All); }

        void disableADSR() override { disableADSR (BasicCascadeOpCodes::All); }


        void enableADSR (const BasicCascadeOpCodes op) { getOperator(op)->enableEnvelope(); }

        void disableADSR (const BasicCascadeOpCodes op) { getOperator(op)->disableEnvelope(); }

        FloatType render() override
        {
            auto modSignal = Modulator.render();
            auto signal    = Carrier.render (modSignal);
            return signal;
        }

        void reset() override
        {
            Carrier.reset();
            Modulator.reset();
        }

        void setADSR (FloatType attackTime_s, FloatType decayTime_s, FloatType sustainLevel, FloatType releaseType_s) override
        {
            setADSR (BasicCascadeOpCodes::Carrier, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
            setADSR (BasicCascadeOpCodes::Modulator, attackTime_s, decayTime_s, sustainLevel, releaseType_s);
        }

        void setADSR (const BasicCascadeOpCodes op, FloatType attackTime_s, FloatType decayTime_s, FloatType sustainLevel, FloatType releaseType_s)
        {
            setAttackTime (op, attackTime_s);
            setSustainLevel (op, sustainLevel);
            setDecayTime (op, decayTime_s);
            setReleaseTime (op, releaseType_s);
        }

        void setAttackTime (const BasicCascadeOpCodes op, const FloatType attackTime_s) { getOperator (op)->setAttackTime (attackTime_s); }
        void setDecayTime (const BasicCascadeOpCodes op, const FloatType decayTime_s) { getOperator (op)->setDecayTime (decayTime_s); }
        void setSustainLevel (const BasicCascadeOpCodes op, const FloatType sustainLevel) { getOperator (op)->setSustainLevel (sustainLevel); }
        void setReleaseTime (const BasicCascadeOpCodes op, const FloatType releaseTime_s) { getOperator (op)->setReleaseTime (releaseTime_s); }

    private:
        OP Carrier;
        OP Modulator;
        int numOperators = 2;

        /*
         * returns a pointer to the operator. This is private as it's used to avoid repeated switch statements (this is more of an issue with algs with more operators!)
         */
        OP* getOperator (const BasicCascadeOpCodes op)
        {
            switch (op)
            {
                case BasicCascadeOpCodes::Carrier:
                    return &Carrier;
                case BasicCascadeOpCodes::Modulator:
                    return &Modulator;
                case BasicCascadeOpCodes::All:
                    std::cout << "OpCode::All not yet implemented!" << std::endl;
                    return nullptr;
                default:
                    return nullptr;
            }
        }
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

    template <typename FloatType>
    class TwoOperatorAlgs final:  AlgBase<FloatType>
    {
        using OP = CASPI::PM::Operator<FloatType>;

    public:

        TwoOperatorAlgs(const TwoOperatorAlgs&) = default;
        TwoOperatorAlgs(TwoOperatorAlgs&&) = default;
        TwoOperatorAlgs& operator=(const TwoOperatorAlgs&) = default;
        TwoOperatorAlgs& operator=(TwoOperatorAlgs&&) = default;

        void noteOn() override
        {
            OperatorB.noteOn();
            OperatorA.noteOn();
        }

        void noteOff() override
        {
            OperatorB.noteOff();
            OperatorA.noteOff();
        }

        void setFrequency (const FloatType frequency, const FloatType sampleRate) override
        {
            OperatorB.setFrequency (frequency, sampleRate);
            OperatorA.setFrequency (frequency, sampleRate);
        }

        void setModulation() override
        {
            /* Currently unimplemented for this algorithm */
        }

        void setSampleRate(const FloatType newSampleRate)
        {
            OperatorB.setSampleRate (newSampleRate);
            OperatorA.setSampleRate (newSampleRate);
        }

        void setOutputLevel (const FloatType outputLevel)
        {
            OperatorB.setModDepth (outputLevel);
        }

        void setModulation (FloatType modIndex, FloatType modDepth)
        {
            OperatorA.setModulation (modIndex, modDepth);
        }

        void setModulationFeedback (FloatType modFeedback)
        {
            OperatorA.setModFeedback (modFeedback);
        }

        void enableADSR() override
        {
            enableADSR (BasicCascadeOpCodes::All);
        }

        void disableADSR() override
        {
            disableADSR (BasicCascadeOpCodes::All);
        }


        void enableADSR (OpIndex op)
        {
            auto Operator = operatorMap[op];
            Operator->enableEnvelope();
        }

        void disableCarrierADSR (OpIndex op)
        {
            auto Operator = operatorMap[op];
            Operator->disableEnvelope();
        }

        void setAlgorithm (const Algorithms algToUse)
        {
            currentAlg = algToUse;
        }

        FloatType render() override
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

        void reset() override
        {
            OperatorB.reset();
            OperatorA.reset();
        }

        void setADSR ( OpIndex op,
            FloatType attackTime_s, FloatType decayTime_s,
            FloatType sustainLevel, FloatType releaseType_s)
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

    private:

        OP OperatorA;

        OP OperatorB;

        int numOperators = 2;

        Algorithms currentAlg = Algorithms::BasicCascade;

        std::unordered_map<OpIndex, OP*> operatorMap =
        {
            { OpIndex::OpA, &OperatorA },
            { OpIndex::OpB, &OperatorB },
            { OpIndex::OpB, nullptr }
        };


        CASPI_LEAK_DETECTOR(TwoOperatorAlgs);
    };


}; // namespace Algorithms

#endif //CASPI_PMALGORITHM_H
