//
// Created by calum on 01/11/2024.
//

#ifndef CASPI_MATHS_H
#define CASPI_MATHS_H

#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace CASPI
{
    namespace Maths {
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
        CASPI_NO_DISCARD static FloatType linearTodBFS (const FloatType linear)
        {
            if (linear > CASPI::Constants::zero<FloatType>)
            {
                return 20 * std::log10 (abs (linear));
            }
            return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
        }

        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType dBFSToLinear (const FloatType dBFS)
        {
            if (dBFS > CASPI::Constants::MINUS_INF_DBFS<FloatType>)
            {
                return std::pow (10, dBFS * FloatType (0.05));
            }
            return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
        }

        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType midiNoteToHz (const int noteNumber)
        {
            return static_cast<FloatType> (Constants::A4_FREQUENCY<FloatType> * std::pow (2, (static_cast<FloatType> (noteNumber) - Constants::A4_MIDI<FloatType>) / Constants::NOTES_IN_OCTAVE<FloatType>));
        }

        template <typename FloatType>
        CASPI_NO_DISCARD FloatType clamp (const FloatType value, const FloatType lower, const FloatType upper)
        {
            return value < lower ? lower : (value > upper ? upper : value);
        }

    template <typename Enum>
    std::underlying_type_t<Enum> to_underlying (Enum e) noexcept
    {
        return static_cast<std::underlying_type_t<Enum>> (e);
    }

        template <typename FloatType>
        CASPI_ALWAYS_INLINE FloatType factorialTerm (int n) noexcept
        {
            FloatType term = 1.0;
            for (int i = 1; i <= n; ++i)
            {
                term /= static_cast<FloatType> (i);
            }
            return term;
        }

        template <typename T>
        CASPI_NO_DISCARD inline T abs_branchless (T x) noexcept
        {
#if defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)

            if constexpr (std::is_integral<T>::value && std::is_signed<T>::value)
            {
                // Two's complement branchless abs
                const T mask = x >> (sizeof (T) * 8 - 1);
                return (x ^ mask) - mask;
            }
            else if constexpr (std::is_floating_point<T>::value)
            {
                // Clear sign bit
                using UInt =
                    typename std::conditional<sizeof (T) == 4, uint32_t, uint64_t>::type;

                UInt bits;
                std::memcpy (&bits, &x, sizeof (T));
                bits &= ~(UInt (1) << (sizeof (T) * 8 - 1));
                std::memcpy (&x, &bits, sizeof (T));
                return x;
            }
            else
            {
                return x;
            }

#else
            // C++11 fallback (still branchless at machine level)
            return x < T (0) ? -x : x;
#endif
        }

    template <typename T>
    CASPI_NO_DISCARD inline T linearInterpolation_bl (T a, T b, T t) noexcept
    {
        return a + t * (b - a);
    }

        template <typename T>
        CASPI_NO_DISCARD inline T wrap_01_branchless (T x) noexcept
        {
            return x - static_cast<T> (static_cast<int> (x));
        }

        template <typename FloatType>
        FloatType fast_cos (const FloatType x)
        {
            constexpr FloatType B = 4.0 / Constants::PI<FloatType>;
            constexpr FloatType C = -4.0 / (Constants::PI<FloatType> * Constants::PI<FloatType>);
            const FloatType y     = B * x + C * x * abs_branchless (x);
            return 0.775 * y; // optional correction factor
        }

        template <typename FloatType>
        float inv_sqrt (FloatType x)
        {
            return 1.0f / std::sqrt (x); // or use Quake-style fast inv sqrt
        }

        /**
         * @brief Compute Bessel function of the first kind, J_n(x)
         * @param n Order of the Bessel function
         * @param x Argument (modulation index β for FM synthesis)
         * @return Value of J_n(x)
         *
         * Uses series expansion for accuracy:
         * J_n(x) = Σ(k=0 to ∞) [(-1)^k / (k!(n+k)!)] * (x/2)^(n+2k)
         *
         * For FM synthesis:
         * - β = 0: J_0(0) = 1, all other J_n = 0 (pure carrier)
         * - β = 2.4: J_0(2.4) ≈ 0 (carrier null, first zero)
         * - β = 5.5: J_0(5.5) ≈ 0 (carrier null, second zero)
         *
         * Reference: Abramowitz & Stegun (1964), Chapter 9
         */
        template <typename FloatType>
        CASPI_NO_DISCARD
        FloatType besselJ (int n, FloatType x) CASPI_NON_BLOCKING
        {
            // Symmetry for negative order
            if (n < 0)
            {
                int abs_n     = -n;
                FloatType val = besselJ (abs_n, x);
                return (abs_n % 2 == 0) ? val : -val;
            }

            // Symmetry for negative x
            if (x < 0)
            {
                FloatType val = besselJ (n, -x);
                return (n % 2 == 0) ? val : -val;
            }

            // x == 0
            if (abs_branchless (x) < 1e-12)
                return (n == 0) ? 1.0 : 0.0;

            // Small x: series expansion
            if (abs_branchless (x) < 8.0)
            {
                FloatType result = 0.0;
                FloatType term   = 1.0;

                for (int i = 1; i <= n; ++i)
                    term *= x / (2.0 * i);

                FloatType xsq = x * x / 4.0;

                for (int k = 0; k < 100; ++k)
                {
                    result += term;
                    if (abs_branchless (term) < 1e-15 * abs_branchless (result))
                        break;

                    term *= -xsq / ((k + 1) * (n + k + 1));
                }

                return result;
            }

            // Large x: asymptotic expansion (high accuracy)
            FloatType phase = x - n * Constants::PI<FloatType> / 2.0 - Constants::PI<FloatType> / 4.0;
            return std::sqrt (2.0 / (Constants::PI<FloatType> * x)) * std::cos (phase);
        }
    } // namespace CASPI::Maths
}


#endif // CASPI_MATHS_H
