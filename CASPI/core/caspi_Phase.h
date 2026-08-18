/**
 * @file caspi_Phase.h
 * @brief Phase accumulator for oscillators.
 * @ingroup core
 *
 * Provides a simple phase accumulator that advances by a configurable
 * increment each sample and wraps at a specified limit (e.g. 2*pi for
 * sine oscillators, 1.0 for normalised waveforms).
 */

#ifndef CASPI_PHASE_H
#define CASPI_PHASE_H

#include "base/caspi_Assert.h"
#include "base/caspi_Denormals.h"

#include <cmath>

namespace CASPI
{
    /**
     * @brief Phase accumulator for driving oscillator waveforms.
     *
     * Holds the current phase and increment. Use advanceAndWrap() to
     * step the phase forward and get the previous value in one call.
     */
    template <typename FloatType>
    struct Phase
    {
            /** @brief Reset phase to zero. */
            void resetPhase() { phase = FloatType (0); }

            /**
             * @brief Advance phase by increment and wrap to limit.
             *
             * Returns the phase value before advancement. The wrap limit
             * is typically 2*pi for trigonometric oscillators or 1.0 for
             * normalised wavetable indices.
             *
             * @param wrapLimit  Upper bound for phase wrapping (must be > 0).
             * @return Phase value before advancement.
             */
            FloatType advanceAndWrap (const FloatType wrapLimit)
            {
                Core::ScopedFlushDenormals flush{};
                CASPI_ASSERT (wrapLimit > FloatType (0), "Wrap limit must be larger than 0.");
                auto phaseInternal = phase;
                phase += increment;
                phase = std::fmod (phase, wrapLimit);

                return phaseInternal;
            }

            /** @brief Current phase value in range [0, wrapLimit). */
            FloatType phase = FloatType (0);

            /** @brief Phase increment per sample. Set externally based on frequency and sample rate. */
            FloatType increment = FloatType (0);
    };
} // namespace CASPI

#endif // CASPI_PHASE_H
