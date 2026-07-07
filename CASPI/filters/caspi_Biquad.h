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
 * @brief  Stub: Filter<FloatType, FilterTopology::Biquad>
 *
 * Placeholder for the Direct Form II transposed biquad (RBJ cookbook).
 * Currently acts as an identity pass-through until implemented.
 *
 * STATE LAYOUT (NumStates = 4)
 *
 *   states[0..3]  z^-1 delay registers for DF2T
 *
 * COEFFICIENT LAYOUT (NumCoeffs = 5)
 *
 *   coeffs[0] = b0
 *   coeffs[1] = b1
 *   coeffs[2] = b2
 *   coeffs[3] = a1
 *   coeffs[4] = a2
 *
 ************************************************************************/

#include "base/caspi_Features.h"
#include "filters/caspi_Filter.h"

namespace CASPI
{
    namespace Filters
    {

        template <>
        struct FilterTraits<FilterTopology::Biquad>
        {
            static constexpr std::size_t NumStates = 4;
            static constexpr std::size_t NumCoeffs = 5;
        };

        template <CASPI_FLOAT_TYPE FloatType>
        class Filter<FloatType, FilterTopology::Biquad>
            : public FilterBase<Filter<FloatType, FilterTopology::Biquad>,
                                FloatType,
                                FilterTraits<FilterTopology::Biquad>::NumStates,
                                FilterTraits<FilterTopology::Biquad>::NumCoeffs>
        {
            public:
                using Base = FilterBase<Filter<FloatType, FilterTopology::Biquad>,
                                        FloatType,
                                        FilterTraits<FilterTopology::Biquad>::NumStates,
                                        FilterTraits<FilterTopology::Biquad>::NumCoeffs>;

                /**
                 * @brief Default constructor. Initialises sample rate to the
                 *        project default and zeroes all coefficients.
                 */
                Filter() noexcept
                {
                    Graph::NodeBase<FloatType>::setSampleRate (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                }

                /**
                 * @brief Compute or recompute all biquad coefficients from the
                 *        current (cutoff, Q, gain, sampleRate). Stub - no-op.
                 */
                void updateCoefficients() noexcept
                {
                    // Stub -- no coefficients computed yet.
                }

                /**
                 * @brief Process one input sample. Stub - identity pass-through.
                 * @param x  Input sample.
                 * @return   Unmodified input sample.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    return x;
                }
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_BIQUAD_H
