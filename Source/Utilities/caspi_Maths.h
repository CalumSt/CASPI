//
// Created by calum on 01/11/2024.
//

#ifndef CASPI_MATHS_H
#define CASPI_MATHS_H

namespace CASPI::Maths {


    /*
     * Maps an input value between a given range to a value between a given range.
     * Assumes that inputMin < input < inputMax, and that outputMin < outputMax,
     * but doesn't assert this.
     */
    template <typename FloatType>
    FloatType cmap(FloatType input, FloatType inputMin, FloatType inputMax, FloatType outputMin, FloatType outputMax) {
		return (((input - inputMin) / (inputMax - inputMin)) * (outputMax - outputMin)) + outputMin;
    }

    /*
     * converts a value from linear to dB
     */
    template <typename FloatType>
    FloatType mag2db(FloatType input) {

    }

    template <typename FloatType>
    auto linearInterpolation(const FloatType y1, const FloatType y2, const FloatType fractional_X)
    {
        auto one = static_cast<FloatType>(1.0);
        // check for invalid inputs
        if (fractional_X >= one) return y2;
        // otherwise apply weighted sum interpolation
        return fractional_X * y2 + (one - fractional_X) * y1;
    }

}

#endif //CASPI_MATHS_H
