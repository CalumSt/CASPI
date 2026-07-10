#ifndef CASPI_LADDER_H
#define CASPI_LADDER_H

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
 * @file   filters/caspi_Ladder.h
 * @author CS Islay
 * @brief  Four-pole transistor ladder filter (Moog VCF).
 *
 * Implements the classic Stilson/Smith discretisation of the Moog ladder
 * using tanh saturation at each stage for self-oscillation limiting.
 *
 * STATE LAYOUT (NumStates = 4)
 *
 *   states[0..3] = stage1..stage4 output (one-sample delayed)
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 2)
 *
 *   coeffs[0] = g   = tan(pi * fc / fs)   (pre-warped cutoff)
 *   coeffs[1] = k   = 4 * Q / (1 + Q)     (feedback amount, 0..4)
 *
 * PROCESS SAMPLE
 *
 *   x0 = tanh(x - k * stage4)       // input saturation + feedback
 *   for each stage i:
 *       y      = stage_i + g * (tanh(x0) - tanh(stage_i))
 *       stage_i = y
 *       x0     = y
 *   return stage4
 *
 * NONLINEARITY
 *
 *   This filter uses tanh saturation and IS NOT linear-time-invariant.
 *   It is marked CASPI_NON_BLOCKING (safe for the audio thread) but
 *   does NOT satisfy superposition or frequency-invariance.
 *   getFrequencyResponse() is not provided — no closed-form solution.
 *
 * REFERENCE
 *
 *   Stilson & Smith, "Analyzing the Moog VCF with Considerations for
 *   Digital Implementation", CCRMA, 1996.
 *   https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf
 *
 ************************************************************************/

#include <cmath>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "filters/caspi_Filter.h"

namespace CASPI
{
    namespace Filters
    {

        template <CASPI_FLOAT_TYPE FloatType>
        class Ladder : public FilterBase<Ladder<FloatType>, FloatType, 4u, 2u>
        {
            public:
                using Base = FilterBase<Ladder<FloatType>, FloatType, 4u, 2u>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default and zeroes all coefficients.
                 */
                Ladder() noexcept CASPI_NON_ALLOCATING
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Construct with sample rate, cutoff, and Q.
                 *
                 * The Moog ladder is LP only — mode parameter is accepted
                 * for API compatibility but ignored.
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff frequency in Hz. Must be > 0.
                 * @param q             Resonance (Q). Must be > 0.
                 *                      Q ~ 0.5..2 typical, >= 4 → self-oscillation.
                 */
                Ladder (FloatType sampleRateHz,
                        FloatType cutoffHz,
                        FloatType q = FloatType (0.7071067811865476)) noexcept CASPI_NON_ALLOCATING
                {
                    CASPI_ASSERT (sampleRateHz > FloatType (0), "Sample rate must be positive");
                    CASPI_ASSERT (cutoffHz > FloatType (0), "Cutoff must be positive");
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");

                    this->cutoff = cutoffHz;
                    this->Q      = q;
                    Graph::NodeBase<FloatType>::setSampleRate (sampleRateHz);
                }

                Ladder (const Ladder&)            = delete;
                Ladder& operator= (const Ladder&) = delete;
                Ladder (Ladder&&)                 = default;
                Ladder& operator= (Ladder&&)      = default;

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
                 * @brief Compute ladder coefficients from the current
                 *        (cutoff, Q, sampleRate).
                 *
                 *   g = tan(pi * fc / fs)    — pre-warped one-pole coefficient
                 *   k = 4 * Q / (1 + Q)     — feedback amount, 0..4
                 */
                void updateCoefficients() noexcept
                {
                    const FloatType fs = this->getSampleRate();

                    if (fs <= FloatType (0) || this->cutoff <= FloatType (0))
                    {
                        return;
                    }

                    const FloatType g_ = std::tan (Constants::PI<FloatType> * this->cutoff / fs);
                    const FloatType k_ = FloatType (4) * this->Q / (FloatType (1) + this->Q);

                    typename Base::AtomicCoefficientsType::CoeffArray arr;
                    arr[0] = g_;
                    arr[1] = k_;
                    this->coeffs.swap (arr);
                }

                /**
                 * @brief Process one input sample through the Moog ladder.
                 *
                 * Implements the Stilson/Smith one-pole-per-stage discretisation
                 * with tanh saturation at the input and each stage.
                 *
                 * NONLINEAR — does NOT satisfy superposition.
                 *
                 * @param x  Input sample.
                 * @return    Four-pole low-pass filtered output.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    const auto& c = this->coeffs.get();

                    const FloatType g_ = c[0];
                    const FloatType k_ = c[1];

                    FloatType s0 = this->states[0];
                    FloatType s1 = this->states[1];
                    FloatType s2 = this->states[2];
                    FloatType s3 = this->states[3];

                    // Feedback with input saturation
                    FloatType x0 = x - k_ * s3;
                    x0 = std::tanh (x0);

                    // Stage 1
                    FloatType y = s0 + g_ * (std::tanh (x0) - std::tanh (s0));
                    s0 = y;
                    x0 = y;

                    // Stage 2
                    y = s1 + g_ * (std::tanh (x0) - std::tanh (s1));
                    s1 = y;
                    x0 = y;

                    // Stage 3
                    y = s2 + g_ * (std::tanh (x0) - std::tanh (s2));
                    s2 = y;
                    x0 = y;

                    // Stage 4
                    y = s3 + g_ * (std::tanh (x0) - std::tanh (s3));
                    s3 = y;

                    this->states[0] = s0;
                    this->states[1] = s1;
                    this->states[2] = s2;
                    this->states[3] = s3;

                    return s3;
                }
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_LADDER_H