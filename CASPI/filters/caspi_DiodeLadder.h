#ifndef CASPI_DIODE_LADDER_H
#define CASPI_DIODE_LADDER_H

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
 * @file   filters/caspi_DiodeLadder.h
 * @author CS Islay
 * @brief  Four-pole diode ladder filter (TB-303 style).
 *
 * Based on Huovilainen's non-linear digital model of the diode ladder,
 * using a cubic diode clipper approximation at each stage instead of
 * the transistor tanh saturation used by the Moog ladder.
 *
 * STATE LAYOUT (NumStates = 4)
 *
 *   states[0..3] = stage1..stage4 output (one-sample delayed)
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 2)
 *
 *   coeffs[0] = g   = tan(pi * fc / fs)   (pre-warped cutoff)
 *   coeffs[1] = k   = resonance feedback   (Huovilainen mapping)
 *
 * PROCESS SAMPLE
 *
 *   x0 = x - k * stage4
 *   for each stage i:
 *       y      = stage_i + g * (diodeClip(x0) - diodeClip(stage_i))
 *       stage_i = y
 *       x0     = y
 *   return stage4
 *
 * NONLINEARITY
 *
 *   Uses a diode clipper approximation (cubic saturation) instead of tanh.
 *   NOT linear-time-invariant. Marked CASPI_NON_BLOCKING.
 *
 * REFERENCE
 *
 *   Huovilainen, "Non-linear digital implementation of the Moog ladder
 *   filter", DAFx-04, 2004.
 *   https://www.dafx.de/paper-archive/2004/P_061.pdf
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
        class DiodeLadder : public FilterBase<DiodeLadder<FloatType>, FloatType, 4u, 2u>
        {
            public:
                using Base = FilterBase<DiodeLadder<FloatType>, FloatType, 4u, 2u>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default and zeroes all coefficients.
                 */
                DiodeLadder() noexcept CASPI_NON_ALLOCATING
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Construct with sample rate, cutoff, and Q.
                 *
                 * LP only — mode parameter is accepted for API compatibility but ignored.
                 *
                 * @param sampleRateHz  Sample rate in Hz. Must be > 0.
                 * @param cutoffHz      Cutoff frequency in Hz. Must be > 0.
                 * @param q             Resonance (Q). Must be > 0.
                 */
                DiodeLadder (FloatType sampleRateHz,
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

                DiodeLadder (const DiodeLadder&)            = delete;
                DiodeLadder& operator= (const DiodeLadder&) = delete;
                DiodeLadder (DiodeLadder&&)                 = default;
                DiodeLadder& operator= (DiodeLadder&&)      = default;

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
                 * @brief Compute diode ladder coefficients.
                 *
                 *   g = tan(pi * fc / fs)
                 *   k = Huovilainen resonance mapping from Q
                 */
                void updateCoefficients() noexcept
                {
                    const FloatType fs = this->getSampleRate();

                    if (fs <= FloatType (0) || this->cutoff <= FloatType (0))
                    {
                        return;
                    }

                    const FloatType g_ = std::tan (Constants::PI<FloatType> * this->cutoff / fs);

                    // Huovilainen-style resonance mapping:
                    // resonance in 0..1 from Q, then k = 3.6 * res - 1.6
                    // clamped so k >= 0.
                    const FloatType res = FloatType (1) - FloatType (1) / (FloatType (1) + this->Q);
                    FloatType k_ = FloatType (3.6) * res - FloatType (1.6);
                    if (k_ < FloatType (0))
                    {
                        k_ = FloatType (0);
                    }

                    typename Base::AtomicCoefficientsType::CoeffArray arr;
                    arr[0] = g_;
                    arr[1] = k_;
                    this->coeffs.swap (arr);
                }

                /**
                 * @brief Diode clipper approximation.
                 *
                 * Cubic saturation: f(x) = x - x^3 / 3, clipped to [-2/3, 2/3].
                 * Based on Huovilainen's diode ladder model.
                 */
                static FloatType diodeClip (FloatType x) noexcept
                {
                    // Clip input to keep the cubic well-behaved
                    if (x > FloatType (1))
                    {
                        return FloatType (2) / FloatType (3);
                    }
                    if (x < FloatType (-1))
                    {
                        return -FloatType (2) / FloatType (3);
                    }
                    return x - x * x * x / FloatType (3);
                }

                /**
                 * @brief Process one input sample through the diode ladder.
                 *
                 * Same four-stage structure as the Moog ladder but uses a
                 * cubic diode clipper instead of tanh saturation.
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

                    // Feedback (no input saturation — diode clipper in each stage)
                    FloatType x0 = x - k_ * s3;

                    // Stage 1
                    FloatType y = s0 + g_ * (diodeClip (x0) - diodeClip (s0));
                    s0 = y;
                    x0 = y;

                    // Stage 2
                    y = s1 + g_ * (diodeClip (x0) - diodeClip (s1));
                    s1 = y;
                    x0 = y;

                    // Stage 3
                    y = s2 + g_ * (diodeClip (x0) - diodeClip (s2));
                    s2 = y;
                    x0 = y;

                    // Stage 4
                    y = s3 + g_ * (diodeClip (x0) - diodeClip (s3));
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

#endif // CASPI_DIODE_LADDER_H