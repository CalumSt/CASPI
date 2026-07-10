#ifndef CASPI_ONE_POLE_H
#define CASPI_ONE_POLE_H

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
 * @file   filters/caspi_OnePole.h
 * @author CS Islay
 * @brief  Trivial one-pole LP/HP filter suitable for smoothing, DC blocking,
 *         or cheap modulation-rate filtering.
 *
 * STATE LAYOUT (NumStates = 1)
 *
 *   states[0] = z1 (one-sample delay / integrator state)
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 1)
 *
 *   coeffs[0] = a = g / (1 + g)    where g = tan(pi * fc / fs)
 *
 * PROCESS SAMPLE
 *
 *   LP: z1 = a * x + (1 - a) * z1;   return z1
 *   HP: z1 = a * x + (1 - a) * z1;   return x - z1
 *
 * LIMITATIONS
 *
 *   - Only LowPass and HighPass modes are valid. Other modes silently
 *     default to LowPass.
 *   - There is no resonance control — setQ() is a no-op.
 *   - setGain() has no effect.
 *
 * REFERENCE
 *
 *   Zölzer, DAFX, 2nd ed., ch. 2.2.1 (basic filters).
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
        class OnePole : public FilterBase<OnePole<FloatType>, FloatType, 1u, 1u>
        {
            public:
                using Base = FilterBase<OnePole<FloatType>, FloatType, 1u, 1u>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default and zeroes all coefficients.
                 */
                OnePole() noexcept CASPI_NON_ALLOCATING
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Construct with sample rate, cutoff, and mode.
                 *
                 * Q is accepted (for API compatibility with other filters)
                 * but has no effect. Gain is ignored.
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff frequency in Hz. Must be > 0.
                 * @param m             Filter mode (LP or HP). Default: LowPass.
                 */
                OnePole (FloatType sampleRateHz,
                         FloatType cutoffHz,
                         FilterMode m = FilterMode::LowPass) noexcept CASPI_NON_ALLOCATING
                {
                    CASPI_ASSERT (sampleRateHz > FloatType (0), "Sample rate must be positive");
                    CASPI_ASSERT (cutoffHz > FloatType (0), "Cutoff must be positive");

                    this->cutoff = cutoffHz;
                    this->mode   = m;
                    Graph::NodeBase<FloatType>::setSampleRate (sampleRateHz);
                }

                OnePole (const OnePole&)            = delete;
                OnePole& operator= (const OnePole&) = delete;
                OnePole (OnePole&&)                 = default;
                OnePole& operator= (OnePole&&)      = default;

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
                 * @brief No-op: one-pole filter has no resonance.
                 *
                 * Overrides the base class setQ to do nothing. The base
                 * member Q is never read by updateCoefficients.
                 */
                void setQ (FloatType) noexcept
                {
                    // No resonance in a one-pole filter.
                }

                /**
                 * @brief Compute the one-pole coefficient.
                 *
                 *   a = g / (1 + g)
                 *   g = tan(pi * fc / fs)
                 */
                void updateCoefficients() noexcept
                {
                    const FloatType fs = this->getSampleRate();

                    if (fs <= FloatType (0) || this->cutoff <= FloatType (0))
                    {
                        return;
                    }

                    const FloatType g_ = std::tan (Constants::PI<FloatType> * this->cutoff / fs);

                    typename Base::AtomicCoefficientsType::CoeffArray arr;
                    arr[0] = g_ / (FloatType (1) + g_);
                    this->coeffs.swap (arr);
                }

                /**
                 * @brief Process one input sample.
                 *
                 * LP: one-pole low-pass (exponential smoothing).
                 * HP: input minus LP output.
                 *
                 * @param x  Input sample.
                 * @return   Filtered output.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    const auto& c   = this->coeffs.get();
                    const FloatType a = c[0];
                    const FloatType b = FloatType (1) - a;

                    FloatType& z1 = this->states[0];
                    z1 = a * x + b * z1;

                    if (this->mode == FilterMode::HighPass)
                    {
                        return x - z1;
                    }

                    return z1;
                }

                /**
                 * @brief Compute the analytic magnitude response |H(f)|.
                 *
                 * One-pole LP: H(z) = a / (1 - (1 - a) * z^-1)
                 * One-pole HP: H(z) = (1 - a) * (1 - z^-1) / (1 - (1 - a) * z^-1)
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

                    const auto& c   = this->coeffs.get();
                    const FloatType a = c[0];
                    const FloatType b = FloatType (1) - a;

                    const FloatType w  = FloatType (2) * Constants::PI<FloatType> * freq / fs;

                    using C = std::complex<FloatType>;
                    const C z1 { std::cos (w), -std::sin (w) };  // e^-jw

                    C H;
                    if (this->mode == FilterMode::HighPass)
                    {
                        // HP: b * (1 - z^-1) / (1 - b * z^-1)
                        H = C (b) * (C (1) - z1) / (C (1) - C (b) * z1);
                    }
                    else
                    {
                        // LP: a / (1 - b * z^-1)
                        H = C (a) / (C (1) - C (b) * z1);
                    }

                    return std::abs (H);
                }
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_ONE_POLE_H