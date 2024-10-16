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




* @file caspi_OnePoleFilter.h
* @author CS Islay
* @class caspi_OnePoleFilter
* @brief A class implementing a basic one pole filter, for use in the ladder filter.
*
*
************************************************************************/

#ifndef CASPI_ONEPOLEFILTER_H
#define CASPI_ONEPOLEFILTER_H

#include "Utilities/caspi_assert.h"
#include "Utilities/caspi_Constants.h"

template <typename FloatType>
class caspi_OnePoleFilter {

    struct FilterCoefficients {

        void reset() {
            FloatType zero = static_cast<FloatType>(0.0);
            FloatType one  = static_cast<FloatType>(1.0);
            alpha   = one;
            beta    = zero;
            gamma   = one;
            delta   = zero;
            epsilon = zero;
            a0      = one;
        }

        void setAlpha(FloatType _alpha)     { alpha = _alpha; }
        void setBeta(FloatType _beta)       { beta = _beta; }

    private:
        FloatType a0      = static_cast<FloatType>(1.0);
        FloatType alpha   = static_cast<FloatType>(1.0);
        FloatType beta    = static_cast<FloatType>(0.0);
        FloatType gamma   = static_cast<FloatType>(1.0);
        FloatType delta   = static_cast<FloatType>(0.0);
        FloatType epsilon = static_cast<FloatType>(0.0);
    };

    FilterCoefficients coefficients;

    void setFeedback(FloatType _feedback)     {feedback = _feedback; }
    void setSampleRate(FloatType _sampleRate) { CASPI_ASSERT(_sampleRate > 0,"Sample rate must be positive."); sampleRate = _sampleRate; }

    FloatType getNextFeedback() {
        return coefficients.beta * (z1 + feedback * coefficients.delta);
    }

    FloatType getNextSample(FloatType inputSample) {
        inputSample = inputSample * coefficients.gamma + feedback + coefficients.epsilon * getNextFeedback();
        FloatType vn = (coefficients.a0 * inputSample - z1) * coefficients.alpha;
        FloatType out = vn + z1;
        z1 = vn + out;
        return out;
    }

private:
    FloatType z1 = static_cast<FloatType>(0.0);
    FloatType feedback = static_cast<FloatType>(0.0);
    FloatType sampleRate = static_cast<FloatType>(44100.0);
};

#endif //CASPI_ONEPOLEFILTER_H
