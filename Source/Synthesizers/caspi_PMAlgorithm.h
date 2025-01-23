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
        virtual void noteOn()                                                 = 0;
        virtual void noteOff()                                                = 0;
        virtual FloatType render()                                            = 0;
        virtual void reset()                                                  = 0;
        virtual void setModulation()                                          = 0;
        virtual void setFrequency (FloatType frequency, FloatType sampleRate) = 0;
        virtual void setADSR()                                                = 0;
        virtual void enableADSR()                                             = 0;
        virtual void disableADSR()                                            = 0;
        virtual ~AlgBase()                                                    = default;
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
    template <typename FloatType>
    class BasicCascade final : public AlgBase<FloatType>
    {
        using OP = CASPI::PM::Operator<FloatType>;

    public:
        enum class OpCodes
        {
            Carrier,
            Modulator,
            All
        };

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
        }

        void setOutputLevel (const FloatType outputLevel) { Carrier.setModDepth (outputLevel); }

        void setModulation (FloatType modIndex, FloatType modDepth) { Modulator.setModulation (modIndex, modDepth); }

        void setModulationFeedback (FloatType modFeedback) { Modulator.setModFeedback (modFeedback); }

        void enableADSR() override { enableADSR (OpCodes::All); }

        void disableADSR() override { disableADSR (OpCodes::All); }


        void enableADSR (const OpCodes op) { getOperator(op)->enableEnvelope(); }

        void disableADSR (const OpCodes op) { getOperator(op)->disableEnvelope(); }

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

        void setADSR() override
        {
            // Do nothing!
        }

        void setADSR (const OpCodes op, FloatType attackTime_s, FloatType decayTime_s, FloatType sustainLevel, FloatType releaseType_s)
        {
            setAttack (op, attackTime_s);
            setSustain (op, sustainLevel);
            setDecay (op, decayTime_s);
            setRelease (op, releaseType_s);
        }

        void setAttack (const OpCodes op, const FloatType attackTime_s) { getOperator (op)->setAttackTime (attackTime_s); }
        void setDecay (const OpCodes op, const FloatType decayTime_s) { getOperator (op)->setDecayTime (decayTime_s); }
        void setSustain (const OpCodes op, const FloatType sustainLevel) { getOperator (op)->setSustainLevel (sustainLevel); }
        void setRelease (const OpCodes op, const FloatType releaseTime_s) { getOperator (op)->setReleaseTime (releaseTime_s); }

    private:
        OP Carrier;
        OP Modulator;
        int numOperators = 2;

        /*
         * returns a pointer to the operator. This is private as it's used to avoid repeated switch statements (this is more of an issue with algs with more operators!)
         */
        OP* getOperator (const OpCodes op)
        {
            switch (op)
            {
                case OpCodes::Carrier:
                    return &Carrier;
                case OpCodes::Modulator:
                    return &Modulator;
                case OpCodes::All:
                    std::cout << "OpCode::All not yet implemented!" << std::endl;
                    return nullptr;
                default:
                    return nullptr;
            }
        }
    };

}; // namespace Algorithms

#endif //CASPI_PMALGORITHM_H
