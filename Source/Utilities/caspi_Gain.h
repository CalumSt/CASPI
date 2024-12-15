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


* @file caspi_Gain.h
* @author CS Islay
* @class Gain
* @brief A class for gain-related functionality, such as ramps.
*
* TODO: Add non-linear ramps
* TODO: Add dB functionality
* TODO: Fix gain up unit test
*
************************************************************************/

#ifndef CASPI_GAIN_H
#define CASPI_GAIN_H
#include "caspi_Constants.h"
#include <algorithm>

/**
 * @brief Template struct for representing a gain value with ramping functionality.
 * @tparam FloatType The floating-point type to use for the gain value.
 */
namespace CASPI
{
template <typename FloatType>
struct Gain
{
    /**
   * @brief Increments the gain value based on the current ramping state.
   */
    void incrementGain()
    {
        if (derampGain)
        {
            gain = (gain <= CASPI::Constants::zero<FloatType>) ? CASPI::Constants::zero<FloatType> : gain - gainIncrement;
        }
        else if (rampGain)
        {
            gain = std::min (gain + gainIncrement, CASPI::Constants::one<FloatType>);
        }

        if (rampGain & gain >= CASPI::Constants::one<FloatType>)
        {
            rampGain = false;
        }
        else if (derampGain & gain <= CASPI::Constants::zero<FloatType>)
        {
            derampGain = false;
        }
    }

    /**
   * @brief Ramps the gain value down to the specified target gain over the given time period.
   * @param targetGain The target gain value to ramp down to.
   * @param time The time period over which to ramp down the gain.
   * @param sampleRate The sample rate of the audio signal.
   */
    void gainRampDown (FloatType targetGain, FloatType time, FloatType sampleRate)
    {
        targetGain = (targetGain < CASPI::Constants::zero<FloatType>) ? CASPI::Constants::zero<FloatType> : targetGain;
        if (targetGain < gain)
        {
            derampGain = true;
            gainIncrement = (gain - targetGain) / (time * sampleRate);
        }
        else
        {
            gainIncrement = CASPI::Constants::zero<FloatType>;
            derampGain = false;
        }
    }

    /**
   * @brief Ramps the gain value up to the specified target gain over the given time period.
   * @param targetGain The target gain value to ramp up to.
   * @param time The time period over which to ramp up the gain.
   * @param sampleRate The sample rate of the audio signal.
   */
    void gainRampUp (FloatType targetGain, FloatType time, FloatType sampleRate)
    {
        targetGain = (targetGain > CASPI::Constants::one<FloatType>) ? CASPI::Constants::one<FloatType> : targetGain;
        if (targetGain > gain)
        {
            rampGain = true;
            gainIncrement = (targetGain - gain) / (time * sampleRate);
        }
        else
        {
            gainIncrement = CASPI::Constants::zero<FloatType>;
            rampGain = false;
        }
    }

    /**
   * @brief Resets the gain value to its default state.
   */
    void reset()
    {
        derampGain = false;
        rampGain = false;
        gain = CASPI::Constants::one<FloatType>;
        gainIncrement = CASPI::Constants::zero<FloatType>;
    }

    /**
   * @brief Gets the current gain value.
   * @return The current gain value.
   */
    FloatType getGain()
    {
        incrementGain();
        return gain;
    }

    /**
   * @brief Sets the gain value to the specified value. Will clamp to 1 or 0 if outside this range.
   * @param newGain The new gain value to set.
   */
    void setGain (FloatType newGain)
    {
        gain = std::clamp (newGain, CASPI::Constants::zero<FloatType>, CASPI::Constants::one<FloatType>);
    };

    /**
   * @brief Checks if the gain is currently ramping up.
   * @return True if the gain is ramping up, false otherwise.
   */
    [[nodiscard]] bool isRampUp() const { return rampGain; }

    /**
   * @brief Checks if the gain is currently ramping down.
   * @return True if the gain is ramping down, false otherwise.
   */
    [[nodiscard]] bool isRampDown() const { return derampGain; }

private:
    FloatType gain = CASPI::Constants::zero<FloatType>;
    FloatType gainIncrement = CASPI::Constants::zero<FloatType>;
    bool derampGain = false;
    bool rampGain = false;
};
} // namespace CASPI

#endif //CASPI_GAIN_H
