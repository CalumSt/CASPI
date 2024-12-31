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
************************************************************************/

#ifndef CASPI_GAIN_H
#define CASPI_GAIN_H
#include "caspi_CircularBuffer.h"
#include "caspi_Constants.h"
#include "caspi_Maths.h"
#include <algorithm>

namespace CASPI
{
/**
 * @brief Template struct for representing a gain value with ramping functionality.
 * @tparam FloatType The floating-point type to use for the gain value.
 */
template <typename FloatType>
struct Gain
{
    /**
     * @brief Sets the gain value with ramping functionality. Clamps gain to be between 0 and 1.
     *        Override flag allows you to bypass ramping functionality.
     * @param newGain the new target gain to ramp to.
     * @param sampleRate the sample rate of the audio signal.
     * @param override set TRUE to override the current gain value (i.e. ignore the target gain).
     */
    void setGain (FloatType newGain, FloatType sampleRate, const bool override = false)
    {
        setSampleRate (sampleRate);

        if (override)
        {
            gain = newGain;
        }

        if (newGain > gain)
        {
            newGain    = (newGain > CASPI::Constants::one<FloatType>) ? CASPI::Constants::one<FloatType> : newGain;
            targetGain = newGain;
        }
        else if (newGain < gain)
        {
            newGain    = (newGain < CASPI::Constants::zero<FloatType>) ? CASPI::Constants::zero<FloatType> : newGain;
            targetGain = newGain;
        }

        setGainIncrement();
    }

    /**
     * @brief Sets the gain value with ramping functionality. Clamps gain to be between 0 and 1.
     *        Override flag allows you to bypass ramping functionality.
     * @param newGain_db the new target gain to ramp to in dBs.
     * @param sampleRate the sample rate of the audio signal.
     * @param override set TRUE to override the current gain value (i.e. ignore the target gain).
     */
    void setGain_db (FloatType newGain_db, FloatType sampleRate, const bool override = false)
    {
        setGain (Maths::dBFSToLinear (newGain_db), sampleRate, override);
    }

    /**
    * @brief Sets the gain ramp duration in seconds.
    * @param newTime_s the ramp duration in seconds.
    * @param sampleRate the sample rate of the audio signal.
    */
    void setGainRampDuration (FloatType newTime_s, const FloatType sampleRate)
    {
        setSampleRate (sampleRate);
        newTime_s      = (newTime_s < CASPI::Constants::zero<FloatType>) ? static_cast<FloatType> (0.02) : newTime_s;
        rampDuration_s = newTime_s;
        setGainIncrement();
    }

    /**
    * @brief Sets the gain ramp duration in seconds based on number of samples.
    * @param numberOfSamples the ramp duration in samples.
    * @param newSampleRate the sample rate of the audio signal.
    */
    void setGainRampDuration (int numberOfSamples, const FloatType newSampleRate)
    {
        setSampleRate (newSampleRate);
        numberOfSamples = (numberOfSamples < 1) ? 1 : numberOfSamples;
        rampDuration_s  = numberOfSamples / sampleRate;
        setGainIncrement();
    }

    /**
    * @brief Applies gain to the single sample input.
    * @param input the input sample.
    */
    void apply (FloatType& input)
    {
        incrementGain();
        input *= gain;
    }

    /**
    * @brief Applies gain to the vector input, using the size of the vector as the number of samples.
    * @param input the input vector.
    */
    void apply (std::vector<FloatType> input)
    {
        for (int i = 0; i < input.size(); i++)
        {
            apply (input.at (i));
        }
    }

    /**
    * @brief Applies gain to the vector input, up to the number of entries specified.
    * @param input the input vector.
    * @param numSamples the number of samples to process.
    */
    void apply (std::vector<FloatType> input, const int numSamples)
    {
        for (int i = 0; i < numSamples; i++)
        {
            apply (input.at (i));
        }
    }

    /**
   * @brief Resets the gain value to its default state.
   */
    void reset()
    {
        sampleRate    = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
        targetGain    = CASPI::Constants::zero<FloatType>;
        gain          = CASPI::Constants::zero<FloatType>;
        gainIncrement = CASPI::Constants::zero<FloatType>;
    }

    /**
   * @brief Gets the current gain value without incrementing.
   * @return The current gain value.
   */
    FloatType getGain()
    {
        return gain;
    }

    /**
   * @brief Sets the sample rate of the gain processor.
   * @param newSampleRate The new sample rate.
   */
    void setSampleRate (FloatType newSampleRate)
    {
        CASPI_ASSERT (newSampleRate > 0, "Sample Rate must be larger than 0.");
        sampleRate = newSampleRate;
    };

    /**
   * @brief Checks if the gain is currently ramping up.
   * @return True if the gain is ramping up, false otherwise.
   */
    [[nodiscard]] bool isRampUp() const { return targetGain > gain; }

    /**
   * @brief Checks if the gain is currently ramping down.
   * @return True if the gain is ramping down, false otherwise.
   */
    [[nodiscard]] bool isRampDown() const { return targetGain < gain; }

private:
    FloatType gain           = CASPI::Constants::zero<FloatType>;
    FloatType gainIncrement  = CASPI::Constants::zero<FloatType>;
    FloatType rampDuration_s = static_cast<FloatType> (0.02); // Short enough to suppress audible pops
    FloatType sampleRate     = CASPI::Constants::DEFAULT_SAMPLE_RATE<FloatType>;
    FloatType targetGain     = gain;

    /**
    * @brief Increments the gain value based on the current ramping state.
    */
    void incrementGain()
    {
        if (isRampUp())
        {
            gain += gainIncrement;
            if (gain > targetGain)
            {
                gain = targetGain;
            }
        }
        else if (isRampDown())
        {
            gain -= gainIncrement;
            if (gain < targetGain)
            {
                gain = targetGain;
            }
        }
    }

    /**
    * @brief Calculates gain increment based on the current ramping state.
    */
    void setGainIncrement()
    {
        if (isRampDown())
        {
            gainIncrement = (gain - targetGain) / (rampDuration_s * sampleRate);
        }
        else if (isRampUp())
        {
            gainIncrement = (targetGain - gain) / (rampDuration_s * sampleRate);
        }
        else
        {
            gainIncrement = CASPI::Constants::zero<FloatType>;
        }
    }
};

} // namespace CASPI

#endif //CASPI_GAIN_H
