#ifndef CASPI_SVF_FILTER_H
#define CASPI_SVF_FILTER_H

/*
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
 * @file   filters/caspi_SvfFilter.h
 * @author CS Islay
 * @brief  State-variable filter (Cytomic SVF topology) integrated with
 *         FilterBase and Processor.
 *
 * TOPOLOGY
 *
 * Implements the Cytomic SVF design by Andy Simper:
 *   https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
 *
 * All four simultaneous outputs (LP, BP, HP, Notch) are computed from the
 * same two integrator states (ic1eq, ic2eq) at no extra cost. Additional
 * outputs (Peak, AllPass) are derived from linear combinations.
 * Switching FilterMode costs no additional computation.
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 5)
 *
 *   coeffs[0] = a1
 *   coeffs[1] = a2
 *   coeffs[2] = a3
 *   coeffs[3] = g   (pre-warped angular frequency = tan(pi*fc/fs))
 *   coeffs[4] = k   (damping coefficient = 1/Q)
 *
 * STATE LAYOUT (NumStates = 2)
 *
 *   states[0] = ic1eq  (first integrator output, z^-1)
 *   states[1] = ic2eq  (second integrator output, z^-1)
 *
 * FREQUENCY RESPONSE
 *
 * getFrequencyResponse(freq) evaluates the analytic |H(f)| at the given
 * frequency for the current FilterMode, computed from the bilinear
 * s-domain SVF transfer function. Suitable for drawing response curves
 * without rendering audio.
 *
 * Reference: Simper, A. (2013). "Solving the Continuous SVF Equations
 * Using Trapezoidal Integration and Equivalent Circuits." Cytomic.
 *
 * THREAD SAFETY
 *
 *   setSampleRate / setCutoff / setQ / setMode / setParameters — setup thread.
 *   processSample / getFrequencyResponse                       — audio thread.
 *
 * COPY / MOVE
 *
 * SvfFilter is non-copyable because AtomicCoefficients contains std::atomic.
 * Construct in-place with the full constructor rather than using a factory
 * function returning by value.
 */

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

        /*
         * SvfFilter<FloatType>
         *
         * Cytomic state-variable filter with all-mode output selection.
         *
         * @tparam FloatType  float or double.
         *
         * Usage:
         *
         *   SvfFilter<float> f (44100.f, 800.f, 0.707f, FilterMode::LowPass);
         *
         *   float out = f.processSample (in);
         *   float mag = f.getFrequencyResponse (1000.f);
         *
         *   f.setMode (FilterMode::HighPass);
         *   f.setCutoff (2000.f);
         */
        template <CASPI_FLOAT_TYPE FloatType>
        class SvfFilter : public FilterBase<SvfFilter<FloatType>,
                                            FloatType,
                                            /*NumStates=*/2u,
                                            /*NumCoeffs=*/5u>
        {
            public:
                using Base = FilterBase<SvfFilter<FloatType>, FloatType, 2u, 5u>;

                /*
                 * Default constructor.
                 *
                 * Sample rate defaults to Constants::DEFAULT_SAMPLE_RATE.
                 * Coefficients are not computed until setSampleRate() and
                 * setCutoff() / setParameters() are called.
                 */
                SvfFilter()
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /*
                 * Full constructor.
                 *
                 * Computes coefficients immediately. Prefer this over the default
                 * constructor + separate calls when all parameters are known at
                 * construction time. This is the required pattern because
                 * SvfFilter is non-copyable (std::atomic member) and cannot be
                 * returned from a factory function by value.
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff frequency in Hz. Must be in (0, sampleRateHz/2).
                 * @param q             Quality factor. Must be > 0.
                 * @param m             Initial filter mode.
                 */
                SvfFilter (FloatType sampleRateHz,
                           FloatType cutoffHz,
                           FloatType q  = FloatType (0.7071067811865476),
                           FilterMode m = FilterMode::LowPass)
                {
                    CASPI_ASSERT (sampleRateHz > FloatType (0), "Sample rate must be positive");
                    CASPI_ASSERT (cutoffHz > FloatType (0), "Cutoff must be positive");
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");

                    Graph::NodeBase<FloatType>::setSampleRate (sampleRateHz);
                    this->cutoff = cutoffHz;
                    this->Q      = q;
                    this->mode   = m;
                    updateCoefficients();
                }

                SvfFilter (const SvfFilter&)            = delete;
                SvfFilter& operator= (const SvfFilter&) = delete;
                SvfFilter (SvfFilter&&)                 = default;
                SvfFilter& operator= (SvfFilter&&)      = default;

                /*
                 * Set sample rate and recompute coefficients.
                 *
                 * Mirrors NodeBase::setSampleRate for standalone (non-graph) use.
                 * In graph mode, onPrepare() handles this automatically.
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

                /*
                 * CRTP hook — recompute and publish coefficients.
                 *
                 * Called automatically by setCutoff(), setQ(), setParameters(),
                 * and onSampleRateChanged(). Must not allocate.
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

                /*
                 * Process one sample.
                 *
                 * Implements the Cytomic trapezoidal-integration SVF equations.
                 * Output topology selected by this->mode.
                 *
                 * @param x  Input sample.
                 * @return   Filtered output sample.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING override
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

                /*
                 * Compute the analytic magnitude response |H(f)|.
                 *
                 * Not real-time safe (uses std::complex arithmetic).
                 * Mode is read from this->mode at call time.
                 *
                 * The SVF transfer functions are expressed as rational functions of
                 * s_hat = (z-1)/((z+1)*g), the bilinear-transformed normalised
                 * frequency variable evaluated on the unit circle z = e^{jwT}.
                 *
                 * H_LP = 1 / (s^2 + k*s + 1) and so on per Simper (2013), eqs. (11)-(16),
                 * cross-verified with Pirkle (2019) "Designing Audio Effect Plugins in C++",
                 * 2nd ed., ch. 11.
                 *
                 * @param freq  Frequency in Hz. Must be > 0 and < sampleRate / 2.
                 * @return      Linear magnitude |H(f)|.
                 */
                CASPI_NO_DISCARD FloatType getFrequencyResponse (FloatType freq) const noexcept
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

#endif // CASPI_SVF_FILTER_H