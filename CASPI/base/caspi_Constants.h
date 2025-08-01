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


* @file caspi_Constants.h
* @author CS Islay
* @brief A collection of useful constant values as templates.
*        Currently:
*        - PI
*        - TWO_PI
*        - A4_FREQUENCY         (440 Hz)
*        - A4_MIDI              (69)
*        - NOTES_IN_OCTAVE      (12)
*        - DEFAULT_SAMPLE_RATE  (44100 Hz)
*        - MINUS_INF_DBFS       (-100 dBFS)
*
************************************************************************/

#ifndef CASPI_CONSTANTS_H
#define CASPI_CONSTANTS_H
#include <cstddef>
namespace CASPI::Constants {

template <typename FloatType>
    constexpr FloatType PI = static_cast<FloatType>(3.14159265358979323846);

template <typename FloatType>
    constexpr FloatType TWO_PI = static_cast<FloatType>(6.2831853071795864769);

template <typename FloatType>
    constexpr FloatType one = static_cast<FloatType>(1.0);

template <typename FloatType>
    constexpr FloatType zero = static_cast<FloatType>(0.0);

template <typename FloatType>
    constexpr FloatType A4_FREQUENCY = static_cast<FloatType>(440.0);

template <typename FloatType>
    constexpr FloatType A4_MIDI = static_cast<FloatType>(69.0);

template <typename FloatType>
    constexpr FloatType NOTES_IN_OCTAVE = static_cast<FloatType>(12.0);

template <typename FloatType>
    constexpr FloatType DEFAULT_SAMPLE_RATE = static_cast<FloatType>(44100.0);

template <typename FloatType>
    constexpr FloatType MINUS_INF_DBFS = static_cast<FloatType>(-100.0);

    constexpr std::size_t DEFAULT_MAX_BUFFER_SIZE = 4096;
};

#endif //CASPI_CONSTANTS_H
