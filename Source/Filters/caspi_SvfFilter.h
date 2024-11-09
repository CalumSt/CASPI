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


* @file caspi_SvfFilter.h
* @author CS Islay
* @class caspi_SvfFilter
* @brief A class implementing a one pole filter. Based on the Cytomic SVF filter design.
*		 Very 'clean' and cheap filter, but not particularly musical.
* @see https://cytomic.com/files/cybot.pdf
*
************************************************************************/

#pragma once
#include <cmath>
#include <Oscillators/BLEP/caspi_BlepOscillator.h>
#include "Utilities/caspi_assert.h"
#include "Utilities/caspi_Constants.h"

template <typename FloatType>
class caspi_SvfFilter
{
public:
    void setSampleRate (const float _sampleRate) { CASPI_ASSERT(_sampleRate > 0,"Sample rate must be greater than 0."); sampleRate = _sampleRate; }
    [[nodiscard]] FloatType getSampleRate() const { return sampleRate; }

    /**
     * @brief Updates the filter coefficients based on the cutoff frequency and quality factor.
     *
     * @param cutoff The cutoff frequency of the filter.
     * @param Q The quality factor of the filter.
     */
    void updateCoefficients(FloatType cutoff, FloatType Q)
    {
        CASPI_ASSERT(cutoff > 0 && Q > 0, "Cutoff and Q must be positive.");
    	auto one = static_cast<FloatType>(1.0);
        g = std::tan (CASPI::Constants::TWO_PI<FloatType> * cutoff / sampleRate);
        k = one / Q;
        a1 = one / (one + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    /**
     * @brief Resets the filter coefficients and internal state variables to their default values.
     */
    void reset()
    {
    	auto zero = static_cast<FloatType>(0.0);
        g = zero;
        k = zero;
        a1 = zero;
        a2 = zero;
        a3 = zero;

        ic1eq = zero;
        ic2eq = zero;
    }

    /**
     * @brief Processes an input sample and produces a filtered output.
     *
     * @param x The input sample.
     * @return The filtered output sample.
     */
    auto render(FloatType x)
    {
        FloatType v3 = x - ic2eq;
        FloatType v1 = a1 * ic1eq + a2 * v3;
        FloatType v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        /// TODO: change return to different v values based on lowpass / highpass etc
        return v2;
    }

    auto getFrequencyResponse(FloatType freq) {
        /// TODO: Implement me
    }



private:
    FloatType sampleRate = static_cast<FloatType>(44100.0); ///< The sample rate of the filter
    FloatType zero =  static_cast<FloatType>(0.0);
    FloatType g = zero; ///< The normalized angular frequency coefficient.
    FloatType k = zero; ///< The damping coefficient, inversely related to the quality factor.
    FloatType a1 = zero; ///< Coefficient a1 used in the filter difference equations.
    FloatType a2 = zero; ///< Coefficient a2 used in the filter difference equations.
    FloatType a3 = zero; ///< Coefficient a3 used in the filter difference equations.
    FloatType ic1eq = zero; ///< Internal state variable for the first integrator.
    FloatType ic2eq = zero; ///< Internal state variable for the second integrator.
};