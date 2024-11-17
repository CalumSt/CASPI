#ifndef CASPI_CONSTANTS_H
#define CASPI_CONSTANTS_H
namespace CASPI::Constants {

template <typename FloatType>
    constexpr FloatType PI = static_cast<FloatType>(3.14159265358979323846);

template <typename FloatType>
    constexpr FloatType TWO_PI = static_cast<FloatType>(6.2831853071795864769);

template <typename FloatType>
    constexpr FloatType one = static_cast<FloatType>(1.0);

template <typename FloatType>
    constexpr FloatType zero = static_cast<FloatType>(0.0);

};

#endif //CASPI_CONSTANTS_H
