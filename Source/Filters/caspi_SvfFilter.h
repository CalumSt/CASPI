#pragma once
#include <cmath>
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


* @file caspi_SvfFilter.h
* @author CS Islay
* @class caspi_SvfFilter
* @brief A class implements a two-pole State Variable Filter (SVF) using the Cytomic filter design.
* TODO: Template me!
*
************************************************************************/
constexpr float PI = 3.14159265358979323846f;

class caspi_SvfFilter
{
public:
    void setSampleRate (const float _sampleRate) { sampleRate = _sampleRate; }
    [[nodiscard]] float getSampleRate() const { return sampleRate; }

    /**
     * @brief Updates the filter coefficients based on the cutoff frequency and quality factor.
     *
     * @param cutoff The cutoff frequency of the filter.
     * @param Q The quality factor of the filter.
     */
    void updateCoefficients(float cutoff, float Q)
    {
       /// TODO: make variables more intuitively named
        g = std::tan (PI * cutoff / sampleRate);
        k = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    /**
     * @brief Resets the filter coefficients and internal state variables to their default values.
     */
    void reset()
    {
        g = 0.0f;
        k = 0.0f;
        a1 = 0.0f;
        a2 = 0.0f;
        a3 = 0.0f;

        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    /**
     * @brief Processes an input sample and produces a filtered output.
     *
     * @param x The input sample.
     * @return The filtered output sample.
     */
    float render(float x)
    {
        float v3 = x - ic2eq;
        float v1 = a1 * ic1eq + a2 * v3;
        float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return v2;
    }

private:
    float sampleRate = 44100.0f; ///< The sample rate of the filter

    float g = 0.0f; ///< The normalized angular frequency coefficient.

    float k = 0.0f; ///< The damping coefficient, inversely related to the quality factor.

    float a1 = 0.0f; ///< Coefficient a1 used in the filter difference equations.
    float a2 = 0.0f; ///< Coefficient a2 used in the filter difference equations.
    float a3 = 0.0f; ///< Coefficient a3 used in the filter difference equations.

    float ic1eq = 0.0f; ///< Internal state variable for the first integrator.
    float ic2eq = 0.0f; ///< Internal state variable for the second integrator.
};