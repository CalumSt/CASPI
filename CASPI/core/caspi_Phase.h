//
// Created by calum on 25/07/2025.
//

#ifndef CASPI_PHASE_H
#define CASPI_PHASE_H

#include "base/caspi_Assert.h"
#include "core/caspi_Core.h"

#include <cmath>

namespace CASPI
{
    /// Structure for holding phase information as a dumb accumulator
    template <typename FloatType>
    struct Phase
    {
            /// Reset phase to zero
            void resetPhase() { phase = FloatType (0); }

            /// Advance phase by increment and wrap to specified limit
            /// Returns the phase value before advancement
            FloatType advanceAndWrap (const FloatType wrapLimit)
            {
                Core::ScopedFlushDenormals flush{};
                CASPI_ASSERT (wrapLimit > FloatType (0), "Wrap limit must be larger than 0.");
                /// take previous phase value
                auto phaseInternal = phase;
                /// update phase counter
                phase += increment;
                /// wrap to the limit
                phase = std::fmod (phase, wrapLimit);

                return phaseInternal;
            } /// wrap limit is 2pi for sine, 1 for others

            /// Current phase value
            FloatType phase = FloatType (0);

            /// Phase increment per sample (set externally based on frequency/sample rate)
            FloatType increment = FloatType (0);
    };
} // namespace CASPI

#endif // CASPI_PHASE_H
