#ifndef CASPI_STATE_VARIABLE_H
#define CASPI_STATE_VARIABLE_H

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
 * @file   filters/caspi_StateVariable.h
 * @author CS Islay
 * @brief  Cytomic SVF topology (two-integrator state variable).
 *
 * Full specialisation for the Cytomic SVF topology (Simper 2013). Two-integrator
 * state-variable filter with simultaneous LP / BP / HP / Notch / Peak / AllPass outputs.
 *
 * STATE LAYOUT (NumStates = 2)
 *
 *   states[0] = ic1eq  (first integrator output, z^-1)
 *   states[1] = ic2eq  (second integrator output, z^-1)
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 5)
 *
 *   coeffs[0] = a1
 *   coeffs[1] = a2
 *   coeffs[2] = a3
 *   coeffs[3] = g   (pre-warped angular frequency = tan(pi*fc/fs))
 *   coeffs[4] = k   (damping coefficient = 1/Q)
 *
 * THREAD SAFETY
 *
 *   setSampleRate / setCutoff / setQ / setMode / setParameters -- setup thread.
 *   processSample / getFrequencyResponse                       -- audio thread.
 *
 * REFERENCE
 *
 *   Cytomic SVF design by Andy Simper:
 *   https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
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
        class StateVariable : public FilterBase<StateVariable<FloatType>, FloatType, 2u, 5u>
        {
            public:
                using Base = FilterBase<StateVariable<FloatType>, FloatType, 2u, 5u>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default.
                 */
                StateVariable() noexcept CASPI_NON_ALLOCATING
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Construct with sample rate, cutoff, Q, and mode.
                 *
                 * Computes coefficients immediately via updateCoefficients().
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff frequency in Hz. Must be > 0.
                 * @param q             Quality factor. Must be > 0.
                 *                     Default: 1/sqrt(2) (Butterworth).
                 * @param m             Filter mode. Default: LowPass.
                 */
                StateVariable (FloatType sampleRateHz,
                               FloatType cutoffHz,
                               FloatType q  = FloatType (0.7071067811865476),
                               FilterMode m = FilterMode::LowPass) noexcept CASPI_NON_ALLOCATING
                {
                    CASPI_ASSERT (sampleRateHz > FloatType (0), "Sample rate must be positive");
                    CASPI_ASSERT (cutoffHz > FloatType (0), "Cutoff must be positive");
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");

                    this->cutoff = cutoffHz;
                    this->Q      = q;
                    this->mode   = m;
                    Graph::NodeBase<FloatType>::setSampleRate (sampleRateHz);
                }

                StateVariable (const StateVariable&)            = delete;
                StateVariable& operator= (const StateVariable&) = delete;
                StateVariable (StateVariable&&)                 = default;
                StateVariable& operator= (StateVariable&&)      = default;

                /**
                 * @brief Override the sample rate and recompute coefficients.
                 *
                 * Called by NodeBase or directly by the user when the
                 * audio driver sample rate changes.
                 *
                 * @param fs  New sample rate in Hz. Must be > 0.
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
                 * @brief Compute the five SVF coefficients from the current
                 *        (cutoff, Q, sampleRate).
                 *
                 * Writes the result into the atomic double buffer via swap()
                 * so the audio thread sees a consistent set.
                 */
                void updateCoefficients() noexcept
                {
                    const FloatType fs = this->getSampleRate();

                    if (fs <= FloatType (0) || this->cutoff <= FloatType (0))
                    {
                        return;
                    }

                    const FloatType one = FloatType (1);
                    const FloatType g_  = std::tan (Constants::PI<FloatType> * this->cutoff / fs);
                    const FloatType k_  = one / this->Q;
                    const FloatType a1_ = one / (one + g_ * (g_ + k_));
                    const FloatType a2_ = g_ * a1_;
                    const FloatType a3_ = g_ * a2_;

                    typename Base::AtomicCoefficientsType::CoeffArray arr;
                    arr[0] = a1_;
                    arr[1] = a2_;
                    arr[2] = a3_;
                    arr[3] = g_;
                    arr[4] = k_;
                    this->coeffs.swap (arr);
                }

                /**
                 * @brief Process one input sample through the SVF.
                 *
                 * Audio thread safe. Reads the current coefficient set and
                 * the two state variables, then writes back updated state.
                 *
                 * @param x  Input sample.
                 * @return   Filtered output (mode-dependent).
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    const auto& c = this->coeffs.get();

                    const FloatType a1_ = c[0];
                    const FloatType a2_ = c[1];
                    const FloatType a3_ = c[2];

                    FloatType& ic1eq = this->states[0];
                    FloatType& ic2eq = this->states[1];

                    const FloatType v3 = x - ic2eq;
                    const FloatType v1 = a1_ * ic1eq + a2_ * v3;
                    const FloatType v2 = ic2eq + a2_ * ic1eq + a3_ * v3;

                    ic1eq = FloatType (2) * v1 - ic1eq;
                    ic2eq = FloatType (2) * v2 - ic2eq;

                    return selectOutput (x, v1, v2, c[4]);
                }

                /**
                 * @brief Compute the analytic magnitude response |H(f)|.
                 *
                 * Uses the bilinear-transform transfer function of the SVF
                 * for the current mode and the latest committed coefficients.
                 * Safe to call from any thread.
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
                    const FloatType zr = std::cos (w);
                    const FloatType zi = std::sin (w);

                    const auto& c      = this->coeffs.get();
                    const FloatType g_ = c[3];
                    const FloatType k_ = c[4];

                    using C = std::complex<FloatType>;

                    const C z { zr, zi };
                    const C one { FloatType (1), FloatType (0) };
                    const C s_hat = (z - one) / ((z + one) * g_);
                    const C denom = s_hat * s_hat + C (k_) * s_hat + one;

                    C H;
                    switch (this->mode)
                    {
                        case FilterMode::LowPass:
                        {
                            H = one / denom;
                            break;
                        }
                        case FilterMode::BandPass:
                        {
                            H = s_hat / denom;
                            break;
                        }
                        case FilterMode::HighPass:
                        {
                            H = (s_hat * s_hat) / denom;
                            break;
                        }
                        case FilterMode::Notch:
                        {
                            H = (s_hat * s_hat + one) / denom;
                            break;
                        }
                        case FilterMode::Peak:
                        {
                            H = (s_hat * s_hat - one) / denom;
                            break;
                        }
                        case FilterMode::AllPass:
                        {
                            H = (s_hat * s_hat - C (k_) * s_hat + one) / denom;
                            break;
                        }
                        default:
                        {
                            H = one / denom;
                            break;
                        }
                    }

                    return std::abs (H);
                }

            private:
                CASPI_ALWAYS_INLINE FloatType selectOutput (FloatType x,
                                                            FloatType v1,
                                                            FloatType v2,
                                                            FloatType k_) const noexcept CASPI_NON_BLOCKING
                {
                    switch (this->mode)
                    {
                        case FilterMode::LowPass:
                        {
                            return v2;
                        }
                        case FilterMode::BandPass:
                        {
                            return v1;
                        }
                        case FilterMode::HighPass:
                        {
                            return x - k_ * v1 - v2;
                        }
                        case FilterMode::Notch:
                        {
                            return x - k_ * v1;
                        }
                        case FilterMode::Peak:
                        {
                            return x - k_ * v1 - FloatType (2) * v2;
                        }
                        case FilterMode::AllPass:
                        {
                            return x - FloatType (2) * k_ * v1;
                        }
                        default:
                        {
                            return v2;
                        }
                    }
                }
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_STATE_VARIABLE_H