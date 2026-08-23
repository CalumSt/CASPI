/**
 * @file caspi_Maths.h
 * @brief Math utility functions for audio DSP.
 * @ingroup maths
 *
 * Provides commonly used audio DSP math operations: range mapping,
 * interpolation, decibel conversion, MIDI note conversion, and clamping.
 */

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
         * @brief Map a value from one range to another.
         *
         * @tparam FloatType  Floating point type.
         * @param input     Input value to map.
         * @param inputMin  Lower bound of input range.
         * @param inputMax  Upper bound of input range.
         * @param outputMin Lower bound of output range.
         * @param outputMax Upper bound of output range.
         * @return Mapped value in [outputMin, outputMax].
         */
        template <typename FloatType>
        static FloatType cmap (FloatType input, FloatType inputMin, FloatType inputMax, FloatType outputMin, FloatType outputMax)
        {
            return (((input - inputMin) / (inputMax - inputMin)) * (outputMax - outputMin)) + outputMin;
        }

        /**
         * @brief Linear interpolation between two values.
         *
         * @tparam FloatType  Floating point type.
         * @param y1           Value at fraction 0.
         * @param y2           Value at fraction 1.
         * @param fractional_X Interpolation factor in [0, 1].
         * @return Interpolated value (y2 if fractional_X >= 1).
         */
        template <typename FloatType>
        static auto linearInterpolation (const FloatType y1, const FloatType y2, const FloatType fractional_X)
        {
            auto one = static_cast<FloatType> (1.0);
            if (fractional_X >= one)
                return y2;
            return fractional_X * y2 + (one - fractional_X) * y1;
        }

        /**
         * @brief Generate a range of values from start to end with a fixed step.
         *
         * @tparam FloatType Floating point type.
         * @param start   Start of range.
         * @param end     End of range (exclusive).
         * @param step    Step size between values.
         * @return Vector of values in [start, end).
         */
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

        /**
         * @brief Generate a range of values with a fixed number of steps.
         *
         * @tparam FloatType     Floating point type.
         * @param start          Start of range.
         * @param end            End of range (exclusive).
         * @param numberOfSteps  Number of steps to divide the range into.
         * @return Vector of numberOfSteps values.
         */
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

        /**
         * @brief Convert linear amplitude to dBFS.
         *
         * @param linear Linear amplitude value.
         * @return Level in dBFS (MINUS_INF_DBFS if linear <= 0).
         */
        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType linearTodBFS (const FloatType linear)
        {
            if (linear > CASPI::Constants::zero<FloatType>)
            {
                return 20 * std::log10 (abs (linear));
            }
            return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
        }

        /**
         * @brief Convert dBFS to linear amplitude.
         *
         * @param dBFS Level in dBFS.
         * @return Linear amplitude value.
         */
        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType dBFSToLinear (const FloatType dBFS)
        {
            if (dBFS > CASPI::Constants::MINUS_INF_DBFS<FloatType>)
            {
                return std::pow (10, dBFS * FloatType (0.05));
            }
            return CASPI::Constants::MINUS_INF_DBFS<FloatType>;
        }

        /**
         * @brief Convert MIDI note number to frequency in Hz.
         *
         * Uses A4 = 440 Hz as reference (MIDI note 69).
         *
         * @param noteNumber MIDI note number (0-127).
         * @return Frequency in Hz.
         */
        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType midiNoteToHz (const int noteNumber)
        {
            return static_cast<FloatType> (Constants::A4_FREQUENCY<FloatType> * std::pow (2, (static_cast<FloatType> (noteNumber) - Constants::A4_MIDI<FloatType>) / Constants::NOTES_IN_OCTAVE<FloatType>));
        }

        /**
         * @brief Clamp a value between lower and upper bounds.
         *
         * @param value Value to clamp.
         * @param lower Lower bound.
         * @param upper Upper bound.
         * @return Clamped value in [lower, upper].
         */
        template <typename FloatType>
        CASPI_NO_DISCARD FloatType clamp (const FloatType value, const FloatType lower, const FloatType upper)
        {
            return value < lower ? lower : (value > upper ? upper : value);
        }

    /**
     * @brief Convert an enum to its underlying integer type.
     *
     * @tparam Enum Enum type.
     * @param e     Enum value.
     * @return Underlying integer value.
     */
    template <typename Enum>
    std::underlying_type_t<Enum> to_underlying (Enum e) noexcept
    {
        return static_cast<std::underlying_type_t<Enum>> (e);
    }

        /**
         * @brief Compute 1/n! for use in series expansions.
         *
         * @tparam FloatType Floating point type.
         * @param n Term index (n >= 0).
         * @return 1/n! as FloatType.
         */
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

        /**
         * @brief Branchless absolute value for signed integers and floats.
         *
         * Uses two's complement for integers and sign-bit masking for
         * floating point. Falls back to ternary on pre-C++17 compilers.
         *
         * @tparam T Signed integer or floating point type.
         * @param x Input value.
         * @return Absolute value of x.
         */
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

    /**
     * @brief Branchless linear interpolation (fused multiply-add friendly).
     *
     * @tparam T Floating point type.
     * @param a Value at t = 0.
     * @param b Value at t = 1.
     * @param t Interpolation factor.
     * @return a + t * (b - a).
     */
    template <typename T>
    CASPI_NO_DISCARD inline T linearInterpolation_bl (T a, T b, T t) noexcept
    {
        return a + t * (b - a);
    }

        /**
         * @brief Branchless fractional part (wrap to [0, 1)).
         *
         * @tparam T Floating point type.
         * @param x Input value.
         * @return Fractional part of x in [0, 1).
         */
        template <typename T>
        CASPI_NO_DISCARD inline T wrap_01_branchless (T x) noexcept
        {
            return x - static_cast<T> (static_cast<int> (x));
        }

        /**
         * @brief Fast approximate cosine using parabolic approximation.
         *
         * Less accurate than std::cos but significantly faster.
         * Useful when precision is not critical (e.g. LFO waveforms).
         *
         * @tparam FloatType Floating point type.
         * @param x Input angle in radians.
         * @return Approximate cosine of x.
         */
        template <typename FloatType>
        FloatType fast_cos (const FloatType x)
        {
            constexpr FloatType B = 4.0 / Constants::PI<FloatType>;
            constexpr FloatType C = -4.0 / (Constants::PI<FloatType> * Constants::PI<FloatType>);
            const FloatType y     = B * x + C * x * abs_branchless (x);
            return 0.775 * y; // optional correction factor
        }

        /**
         * @brief Inverse square root.
         *
         * @tparam FloatType Floating point type.
         * @param x Input value (must be > 0).
         * @return 1 / sqrt(x).
         */
        template <typename FloatType>
        float inv_sqrt (FloatType x)
        {
            return 1.0f / std::sqrt (x); // or use Quake-style fast inv sqrt
        }

        /**
         * @brief Compute the per-sample coefficient for an analog-style
         *        (TCO / time-constant-overshoot) one-pole segment.
         *
         * Shapes a one-pole recurrence `level = coefficient * level + offset`
         * so it reaches its target over `lengthInSamples` samples with the
         * same curvature as an analog RC envelope stage, instead of settling
         * exponentially forever. Originally used by ADSR's attack/decay/
         * release stages; reusable anywhere a segment needs that character
         * (e.g. a compressor's attack/release detector).
         *
         * @tparam FloatType     Floating point type.
         * @param tco            Time-constant-overshoot value — see
         *                       Constants::ATTACK_TCO / Constants::DECAY_TCO.
         * @param lengthInSamples Segment length in samples (must be > 0).
         * @return Per-sample coefficient for the recurrence.
         */
        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType analogTcoCoefficient (const FloatType tco, const FloatType lengthInSamples)
        {
            const auto one = CASPI::Constants::one<FloatType>;
            return static_cast<FloatType> (std::exp (std::log ((one + tco) / tco) / -lengthInSamples));
        }

        /**
         * @brief Compute the per-sample offset for an analog-style
         *        (TCO / time-constant-overshoot) one-pole segment.
         *
         * Pairs with analogTcoCoefficient(): pass the segment's true target
         * level adjusted by tco in the direction of travel (target + tco for
         * a rising segment, target - tco for a falling one) as
         * overshootTarget.
         *
         * @tparam FloatType      Floating point type.
         * @param overshootTarget Target level offset by tco (see above).
         * @param coefficient     Coefficient from analogTcoCoefficient().
         * @return Per-sample offset for the recurrence.
         */
        template <typename FloatType>
        CASPI_NO_DISCARD static FloatType analogTcoOffset (const FloatType overshootTarget, const FloatType coefficient)
        {
            return overshootTarget * (CASPI::Constants::one<FloatType> - coefficient);
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
