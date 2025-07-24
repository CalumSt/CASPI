//
// Created by calum on 01/11/2024.
//

#ifndef CASPI_MATHS_H
#define CASPI_MATHS_H

#include "core/caspi_Constants.h"
#include <cmath>
#include <vector>

namespace CASPI::Maths
{

    /**
     * Maps an input value between a given range to a value between a given range.
     * Assumes that inputMin < input < inputMax, and that outputMin < outputMax,
     * but doesn't assert this.
     */
template <typename FloatType>
static FloatType cmap (FloatType input, FloatType inputMin, FloatType inputMax, FloatType outputMin, FloatType outputMax)
{
    return (((input - inputMin) / (inputMax - inputMin)) * (outputMax - outputMin)) + outputMin;
}

template <typename FloatType>
static auto linearInterpolation (const FloatType y1, const FloatType y2, const FloatType fractional_X)
{
    auto one = static_cast<FloatType> (1.0);
    // check for invalid inputs
    if (fractional_X >= one)
        return y2;
    // otherwise apply weighted sum interpolation
    return fractional_X * y2 + (one - fractional_X) * y1;
}

template <typename FloatType>
std::vector<FloatType> range (FloatType start, FloatType end, FloatType step)
{
    std::vector<FloatType> result;
    for (FloatType i = start; i < end; i += step)
    {
        result.push_back (i);
    }
    return result;
}

template <typename FloatType>
std::vector<FloatType> range (FloatType start, FloatType end, int numberOfSteps)
{
    std::vector<FloatType> result;
    auto timeStep = (end - start) / numberOfSteps;
    for (int i = 0; i < numberOfSteps; i++)
    {
        auto value = start + static_cast<FloatType> (timeStep * i);
        result.push_back (value);
    }
    return result;
}

template <typename FloatType>
static FloatType linearTodBFS (const FloatType linear)
{
    if (linear > CASPI::Constants::zero<FloatType>)
    {
        return 20 * std::log10 (abs (linear));
    }
    return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
}

template <typename FloatType>
static FloatType dBFSToLinear (const FloatType dBFS)
{
    if (dBFS > CASPI::Constants::MINUS_INF_DBFS<FloatType>)
    {
        return std::pow (10, dBFS * FloatType (0.05));
    }
    return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
}

template <typename FloatType>
static FloatType midiNoteToHz(const int noteNumber)
{
    return static_cast<FloatType>(Constants::A4_FREQUENCY<FloatType> * std::pow(2, (static_cast<double>(noteNumber) - Constants::A4_MIDI<FloatType>) / Constants::NOTES_IN_OCTAVE<FloatType>));
}

template <typename FloatType>
[[nodiscard]] FloatType clamp(const FloatType value, const FloatType lower, const FloatType upper)
{
    return value < lower ? lower : (value > upper ? upper : value);
}

template <typename FloatType>
void clamp(FloatType& value, const FloatType lower, const FloatType upper)
{
    value = (value < lower ? lower : (value > upper ? upper : value));
}

template <typename Enum>
std::underlying_type_t<Enum> to_underlying(Enum e) noexcept
{
    return static_cast<std::underlying_type_t<Enum>>(e);
}

} // namespace CASPI::Maths

#endif //CASPI_MATHS_H
