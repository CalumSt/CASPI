#ifndef CASPI_BIQUAD_H
#define CASPI_BIQUAD_H

/*************************************************************************
 *  .d8888b.                             d8b
 * d88P  Y88b                            Y8P
 * 888    888
 * 888         8888b.  .d8888b  88888b.  888
 * 888            "88b 88K      888 "88b 888
 * 888    888 .d888888 "Y8888b. 888  888 888
 * Y88b  d88P 888  888      X88 888 d88P 888
 *  "Y8888P"  "Y888888  88888P' 88888P"  888
 *                              888
 *                              888
 *                              888
 *
 * @file   filters/caspi_Biquad.h
 * @author CS Islay
 * @brief  Direct Form II transposed biquad using the RBJ Audio EQ Cookbook
 *         coefficient formulas.
 *
 * STATE LAYOUT (NumStates = 2)
 *
 *   states[0] = z1  (first delay register, one-sample delay)
 *   states[1] = z2  (second delay register)
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 5)
 *
 *   Pre-normalised DF2T coefficients:
 *     coeffs[0] = b0'
 *     coeffs[1] = b1'
 *     coeffs[2] = b2'
 *     coeffs[3] = -a1'  (sign pre-flipped for DF2T subtraction)
 *     coeffs[4] = -a2'
 *
 *   Where b0'=b0/a0, b1'=b1/a0, b2'=b2/a0, a1'=a1/a0, a2'=a2/a0
 *   from the raw RBJ coefficients (b0,b1,b2,a0,a1,a2).
 *
 * PROCESS SAMPLE (Direct Form II Transposed)
 *
 *   y = b0' * x + z1
 *   z1 = b1' * x + (-a1') * y + z2
 *   z2 = b2' * x + (-a2') * y
 *
 * MODES
 *
 *   All 8 FilterMode values are supported via different RBJ formulas:
 *     LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf, AllPass.
 *   Peaking and shelf modes read gainDb from the base class.
 *
 * REFERENCE
 *
 *   Robert Bristow-Johnson, "Audio EQ Cookbook"
 *   https://www.w3.org/TR/audio-eq-cookbook/
 *
 ************************************************************************/

#include <cmath>
#include <complex>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "filters/caspi_Filter.h"

namespace CASPI
{
    namespace Filters
    {

        template <CASPI_FLOAT_TYPE FloatType>
        class Biquad : public FilterBase<Biquad<FloatType>, FloatType, 2u, 5u>
        {
            public:
                using Base = FilterBase<Biquad<FloatType>, FloatType, 2u, 5u>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default and zeroes all coefficients.
                 */
                Biquad() noexcept CASPI_NON_ALLOCATING
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Construct with sample rate, cutoff, Q, gain, and mode.
                 *
                 * Computes coefficients immediately.
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff/centre frequency in Hz. Must be > 0.
                 * @param q             Quality factor. Must be > 0.
                 *                       Default: 1/sqrt(2) (Butterworth).
                 * @param m             Filter mode. Default: LowPass.
                 * @param gain          Gain in dB (peaking/shelf only). Default: 0.
                 */
                Biquad (FloatType sampleRateHz,
                        FloatType cutoffHz,
                        FloatType q  = FloatType (0.7071067811865476),
                        FilterMode m = FilterMode::LowPass,
                        FloatType gain = FloatType (0)) noexcept CASPI_NON_ALLOCATING
                {
                    CASPI_ASSERT (sampleRateHz > FloatType (0), "Sample rate must be positive");
                    CASPI_ASSERT (cutoffHz > FloatType (0), "Cutoff must be positive");
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");

                    this->cutoff = cutoffHz;
                    this->Q      = q;
                    this->mode   = m;
                    this->gainDb = gain;
                    Graph::NodeBase<FloatType>::setSampleRate (sampleRateHz);
                }

                Biquad (const Biquad&)            = delete;
                Biquad& operator= (const Biquad&) = delete;
                Biquad (Biquad&&)                 = default;
                Biquad& operator= (Biquad&&)      = default;

                /**
                 * @brief Override the sample rate and recompute coefficients.
                 */
                void setSampleRate (FloatType fs) noexcept
                {
                    CASPI_ASSERT (fs > FloatType (0), "Sample rate must be positive");
                    Graph::NodeBase<FloatType>::setSampleRate (fs);
                    if (this->cutoff > FloatType (0))
                    {
                        updateCoefficients();
                    }
                }

                /**
                 * @brief Compute RBJ cookbook coefficients from the current
                 *        (cutoff, Q, gain, sampleRate, mode).
                 *
                 * All 8 FilterMode values are supported. Peaking and shelf
                 * modes use gainDb from the base class.
                 */
                void updateCoefficients() noexcept
                {
                    const FloatType fs = this->getSampleRate();

                    if (fs <= FloatType (0) || this->cutoff <= FloatType (0))
                    {
                        return;
                    }

                    const FloatType one  = FloatType (1);
                    const FloatType two  = FloatType (2);
                    const FloatType half = FloatType (0.5);

                    const FloatType w0 = two * Constants::PI<FloatType> * this->cutoff / fs;
                    const FloatType c  = std::cos (w0);
                    const FloatType s  = std::sin (w0);
                    const FloatType alpha = s / (two * this->Q);

                    FloatType A = one;
                    if (this->mode == FilterMode::Peak   ||
                        this->mode == FilterMode::LowShelf  ||
                        this->mode == FilterMode::HighShelf)
                    {
                        A = std::pow (FloatType (10), this->gainDb / FloatType (40));
                    }

                    FloatType b0, b1, b2, a0, a1, a2;

                    switch (this->mode)
                    {
                        case FilterMode::LowPass:
                        {
                            b0 = (one - c) * half;
                            b1 = one - c;
                            b2 = (one - c) * half;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                        case FilterMode::HighPass:
                        {
                            b0 = (one + c) * half;
                            b1 = -(one + c);
                            b2 = (one + c) * half;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                        case FilterMode::BandPass:
                        {
                            b0 = alpha;
                            b1 = FloatType (0);
                            b2 = -alpha;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                        case FilterMode::Notch:
                        {
                            b0 = one;
                            b1 = -two * c;
                            b2 = one;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                        case FilterMode::Peak:
                        {
                            b0 = one + alpha * A;
                            b1 = -two * c;
                            b2 = one - alpha * A;
                            a0 = one + alpha / A;
                            a1 = -two * c;
                            a2 = one - alpha / A;
                            break;
                        }
                        case FilterMode::LowShelf:
                        {
                            const FloatType sqrtA = std::sqrt (A);
                            const FloatType Ap1   = A + one;
                            const FloatType Am1   = A - one;

                            b0 = A * (Ap1 - Am1 * c + two * sqrtA * alpha);
                            b1 = two * A * (Am1 - Ap1 * c);
                            b2 = A * (Ap1 - Am1 * c - two * sqrtA * alpha);
                            a0 = Ap1 + Am1 * c + two * sqrtA * alpha;
                            a1 = -two * (Am1 + Ap1 * c);
                            a2 = Ap1 + Am1 * c - two * sqrtA * alpha;
                            break;
                        }
                        case FilterMode::HighShelf:
                        {
                            const FloatType sqrtA = std::sqrt (A);
                            const FloatType Ap1   = A + one;
                            const FloatType Am1   = A - one;

                            b0 = A * (Ap1 + Am1 * c + two * sqrtA * alpha);
                            b1 = -two * A * (Am1 + Ap1 * c);
                            b2 = A * (Ap1 + Am1 * c - two * sqrtA * alpha);
                            a0 = Ap1 - Am1 * c + two * sqrtA * alpha;
                            a1 = two * (Am1 - Ap1 * c);
                            a2 = Ap1 - Am1 * c - two * sqrtA * alpha;
                            break;
                        }
                        case FilterMode::AllPass:
                        {
                            b0 = one - alpha;
                            b1 = -two * c;
                            b2 = one + alpha;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                        default:
                        {
                            b0 = (one - c) * half;
                            b1 = one - c;
                            b2 = (one - c) * half;
                            a0 = one + alpha;
                            a1 = -two * c;
                            a2 = one - alpha;
                            break;
                        }
                    }

                    // Normalise and store for DF2T (a1, a2 negated for subtraction)
                    const FloatType invA0 = one / a0;
                    typename Base::AtomicCoefficientsType::CoeffArray arr;
                    arr[0] = b0 * invA0;
                    arr[1] = b1 * invA0;
                    arr[2] = b2 * invA0;
                    arr[3] = -a1 * invA0;
                    arr[4] = -a2 * invA0;
                    this->coeffs.swap (arr);
                }

                /**
                 * @brief Process one input sample through the DF2T biquad.
                 *
                 * Audio thread safe. Reads the current coefficient set and
                 * updates the two delay registers.
                 *
                 * @param x  Input sample.
                 * @return   Filtered output (mode baked into coefficients).
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    const auto& c = this->coeffs.get();

                    FloatType& z1 = this->states[0];
                    FloatType& z2 = this->states[1];

                    const FloatType y = c[0] * x + z1;
                    z1 = c[1] * x + c[3] * y + z2;
                    z2 = c[2] * x + c[4] * y;

                    return y;
                }

                /**
                 * @brief Compute the analytic magnitude response |H(f)|.
                 *
                 * Evaluates the RBJ biquad transfer function on the unit
                 * circle using the current committed coefficients.
                 *
                 * @param freq  Frequency in Hz. Must be > 0.
                 * @return      Magnitude at freq (linear, not dB).
                 */
                CASPI_NO_DISCARD FloatType getFrequencyResponse (FloatType freq) const noexcept CASPI_NON_BLOCKING
                {
                    CASPI_ASSERT (freq > FloatType (0), "Frequency must be positive");

                    const FloatType fs = this->getSampleRate();
                    if (fs <= FloatType (0))
                    {
                        return FloatType (0);
                    }

                    const FloatType w  = FloatType (2) * Constants::PI<FloatType> * freq / fs;
                    const auto& c      = this->coeffs.get();

                    using C = std::complex<FloatType>;
                    const C z { std::cos (w), std::sin (w) };
                    const C z1 = C (1) / z;        // z^-1
                    const C z2 = z1 * z1;           // z^-2

                    // H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
                    // But we stored -a1', -a2' (pre-negated for DF2T).
                    // Recover a1' = -c[3], a2' = -c[4].
                    const C num = C (c[0]) + C (c[1]) * z1 + C (c[2]) * z2;
                    const C den = C (1)  - C (c[3]) * z1 - C (c[4]) * z2; // since -(-a1') = a1'

                    if (std::abs (den) < std::numeric_limits<FloatType>::epsilon())
                    {
                        return FloatType (0);
                    }

                    return std::abs (num / den);
                }
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_BIQUAD_H