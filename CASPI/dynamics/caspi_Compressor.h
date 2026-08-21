#ifndef CASPI_COMPRESSOR_H
#define CASPI_COMPRESSOR_H

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
 * @file   dynamics/caspi_Compressor.h
 * @author CS Islay
 * @brief  Feedforward VCA-style compressor built on DynamicsBase.
 * @ingroup dynamics
 *
 * Uses DynamicsBase::defaultGainReductionDb() unmodified — a standard
 * soft-knee, feedforward gain computer. Other topologies (FET, Optical,
 * ...) are expected to live alongside this file as their own Derived
 * class, each supplying its own computeGainReductionDb() curve while
 * reusing DynamicsBase's detector, sidechain input, and parameter API.
 *
 ************************************************************************/

#include "base/caspi_Features.h"
#include "dynamics/caspi_DynamicsBase.h"

namespace CASPI
{
    namespace Dynamics
    {
        template <CASPI_FLOAT_TYPE FloatType>
        class Compressor final : public DynamicsBase<Compressor<FloatType>, FloatType>
        {
            public:
                using Base = DynamicsBase<Compressor<FloatType>, FloatType>;

                Compressor() = default;

                /**
                 * @brief Gain computer — the standard soft-knee curve.
                 * @param levelDb  Detected (smoothed) level in dBFS.
                 * @return         Gain reduction in dB (>= 0).
                 */
                CASPI_NO_DISCARD FloatType computeGainReductionDb (FloatType levelDb) noexcept CASPI_NON_BLOCKING
                {
                    return this->defaultGainReductionDb (levelDb);
                }
        };

    } // namespace Dynamics
} // namespace CASPI

#endif // CASPI_COMPRESSOR_H
