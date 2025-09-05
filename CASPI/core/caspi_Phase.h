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
    enum class SyncMode
    {
        None,
        Soft,
        Hard
    };
    /// Structure for holding phase information conveniently
    template <typename FloatType>
    struct Phase : public virtual Core::SampleRateAware<FloatType>
    {
            void resetPhase() { phase = 0; }

            void setFrequency (const FloatType frequency, const FloatType sampleRate)
            {
                CASPI_ASSERT ((sampleRate > 0 && frequency >= 0), "Sample Rate and Frequency must be larger than 0.");
                increment = frequency / sampleRate;
                this->setSampleRate (sampleRate);
            }

            void setHardSyncFrequency (const FloatType frequency)
            {
                CASPI_ASSERT (frequency >= 0, "Hard Sync Frequency cannot be negative.");
                hardSyncIncrement = frequency / this->getSampleRate();
                hardSyncPhase     = 0;
            }

            FloatType advanceAndWrap (const FloatType wrapLimit)
            {
                CASPI_ASSERT (wrapLimit > 0, "Wrap limit must be larger than 0.");
                /// take previous phase value
                auto phaseInternal = phase;
                /// update phase counter
                phase += increment;
                /// wrap to the limit
                phase = CASPI::Core::flushToZero (std::fmod (phase, wrapLimit));

                if (hardSyncIncrement > 0)
                {
                    hardSyncPhase += hardSyncIncrement;
                    hardSyncPhase  = CASPI::Core::flushToZero (std::fmod (hardSyncPhase, wrapLimit));

                    switch (syncMode)
                    {
                        case SyncMode::Hard:
                            if (hardSyncPhase < hardSyncIncrement)
                            {
                                phase = 0; // hard sync
                            }
                            break;

                        case SyncMode::Soft:
                            if (hardSyncPhase < hardSyncIncrement)
                            {
                                phase = wrapLimit - phase; // soft sync by reflecting phase
                            }
                            break;

                        case SyncMode::None:
                        default:
                            break;
                    }
                }

                // Final flush for phase in case sync produced denormal
                phase = CASPI::Core::flushToZero (phase);

                return phaseInternal;
            } /// wrap limit is 2pi for sine, 1 for others

            FloatType phase     = 0;
            FloatType increment = 0;

            FloatType hardSyncPhase     = 0;
            FloatType hardSyncIncrement = 0;

            SyncMode syncMode = SyncMode::None;
    };
} // namespace CASPI

#endif //CASPI_PHASE_H
