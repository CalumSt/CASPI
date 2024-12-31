//
// Created by calum on 01/11/2024.
//

#ifndef CASPI_MATHS_H
#define CASPI_MATHS_H

#include "caspi_Assert.h"
#include "caspi_Constants.h"
#include <type_traits>
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

/// These helpers verify at compile-time that a type has a valid * operator to do vector multiplication
template <typename, typename T>
struct has_multiply : std::false_type
{
};

template <typename T>
struct has_multiply<T, std::void_t<decltype (std::declval<T>() * std::declval<T>())>> : std::true_type
{
};

template <typename T>
constexpr bool has_multiply_v = has_multiply<T, void>::value;

/***
     * @brief Multiplies two vectors element-wise.
     *
     * The function multiplies elements of two given vectors and stores the result in a third vector.
     * The type T must support the * operator.
     *
     * @tparam T The type of the elements in the vectors.
     * @param v1 The first input vector.
     * @param v2 The second input vector.
     * @param result The vector to store the results of element-wise multiplication.
     * @throw std::invalid_argument If the input vectors are not of the same size.
     */
template <typename DataType>
bool vectorMultiply (std::vector<DataType>& v1, std::vector<DataType>& v2) noexcept
{
    CASPI_STATIC_ASSERT (has_multiply_v<DataType>, "Type must have * operator.");
    if (v1.size() == v2.size())
    {
        return false;
    }
    for (size_t i = 0; i < v1.size(); i++)
    {
        v1.at (i) *= v2.at (i);
    }
    return true;
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
} // namespace CASPI::Maths

#endif //CASPI_MATHS_H
