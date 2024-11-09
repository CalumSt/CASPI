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

}

#endif //CASPI_MATHS_H
