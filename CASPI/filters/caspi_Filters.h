#ifndef CASPI_FILTERS_H
#define CASPI_FILTERS_H

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
 * @file   filters/caspi_Filters.h
 * @author CS Islay
 * @brief  Top-level filter module. Runtime-switchable multi-filter dispatcher.
 *
 * Usage:
 *   Filters<float, StateVariable, Biquad, Ladder> f(48000.f);
 *   f.setCutoff(1000.f);
 *   float out = f.processSample(in);
 *   f.setActiveIndex(1);              // runtime switch
 *   f.setActive<Biquad>();            // compile-time switch
 *
 ************************************************************************/

#include <atomic>
#include <tuple>
#include <utility>
#include <type_traits>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "filters/caspi_Filter.h"
#include "filters/caspi_StateVariable.h"
#include "filters/caspi_Biquad.h"
#include "filters/caspi_Ladder.h"
#include "filters/caspi_DiodeLadder.h"
#include "filters/caspi_OnePole.h"

namespace CASPI
{
    namespace Filters
    {

        template <typename FloatType, template <typename> class... FilterTs>
        class Filters
        {
            static_assert (sizeof...(FilterTs) > 0,
                           "Filters requires at least one filter type");

            public:
                using FilterTuple = std::tuple<FilterTs<FloatType>...>;

                /*--------------------------------------------------------------
                 * Construction
                 *-------------------------------------------------------------*/

                /**
                 * @brief Default constructor. Uses the project default sample
                 *        rate and sets the active filter to the first in the pack.
                 */
                Filters() noexcept CASPI_NON_ALLOCATING
                {
                    initFilters (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                    active.store (0, std::memory_order_release);
                }

                /**
                 * @brief Construct with a sample rate and optional initial
                 *        filter index.
                 *
                 * Initialises every filter to the given sample rate and resets
                 * their state.
                 *
                 * @param sampleRate  Sample rate in Hz. Must be > 0.
                 * @param initialIndex  Initial active filter index. Defaults to 0.
                 */
                explicit Filters (
                    FloatType sampleRate,
                    std::size_t initialIndex = 0) noexcept CASPI_NON_ALLOCATING
                {
                    initFilters (sampleRate);
                    active.store (initialIndex, std::memory_order_release);
                }

                /*--------------------------------------------------------------
                 * Audio-thread hot path
                 *-------------------------------------------------------------*/

                /**
                 * @brief Process one input sample through the active filter.
                 *
                 * Dispatches to the currently selected filter with
                 * no allocation. Audio thread safe.
                 *
                 * @param in  Input sample.
                 * @return    Filtered output from the active filter.
                 */
                FloatType processSample (FloatType in) noexcept CASPI_NON_BLOCKING
                {
                    const auto idx = active.load (std::memory_order_relaxed);
                    return dispatch (in, idx);
                }

                /*--------------------------------------------------------------
                 * Topology switching — setup thread only
                 *-------------------------------------------------------------*/

                /**
                 * @brief Switch the active filter at runtime by pack index.
                 *
                 * Resets the target filter before activating it so the
                 * new filter starts from a clean state. Setup thread only.
                 *
                 * @param idx  The filter index to activate. Must be < pack size.
                 */
                void setActiveIndex (std::size_t idx) noexcept
                {
                    CASPI_ASSERT (idx < sizeof...(FilterTs), "Filter index out of range");
                    resetFilterAt (idx);
                    active.store (idx, std::memory_order_release);
                }

                /**
                 * @brief Read the currently active filter index.
                 * @return The active filter pack index (0-based).
                 */
                CASPI_NO_DISCARD std::size_t getActiveIndex() const noexcept
                {
                    return active.load (std::memory_order_acquire);
                }

                /**
                 * @brief Compile-time switch by filter type.
                 *
                 * @tparam FilterT  Filter class template to activate (must be in pack).
                 */
                template <template <typename> class FilterT>
                void setActive() noexcept
                {
                    setActiveIndex (indexOf<FilterT>());
                }

                /*--------------------------------------------------------------
                 * Parameter delegation — setup thread (forwarded to all filters)
                 *-------------------------------------------------------------*/

                /**
                 * @brief Forward setCutoff to every stored filter.
                 *
                 * All filters receive the new cutoff so each has correct
                 * coefficients when it becomes active. Setup thread only.
                 *
                 * @param hz  Cutoff frequency in Hz.
                 */
                void setCutoff (FloatType hz) noexcept
                {
                    forEachFilter ([&] (auto& f) { f.setCutoff (hz); });
                }

                /**
                 * @brief Forward setQ to every stored filter.
                 *
                 * @param q  Quality factor. Must be > 0.
                 */
                void setQ (FloatType q) noexcept
                {
                    forEachFilter ([&] (auto& f) { f.setQ (q); });
                }

                /**
                 * @brief Forward setGain to every stored filter.
                 *
                 * @param dB  Gain in dB.
                 */
                void setGain (FloatType dB) noexcept
                {
                    forEachFilter ([&] (auto& f) { f.setGain (dB); });
                }

                /**
                 * @brief Forward setMode to every stored filter.
                 *
                 * @param m  New filter mode.
                 */
                void setMode (FilterMode m) noexcept
                {
                    forEachFilter ([&] (auto& f) { f.setMode (m); });
                }

                /**
                 * @brief Forward setParameters to every stored filter.
                 *
                 * @param hz  Cutoff frequency in Hz.
                 * @param q   Quality factor.
                 * @param m   Filter mode. Default: LowPass.
                 */
                void setParameters (FloatType hz,
                                    FloatType q,
                                    FilterMode m = FilterMode::LowPass) noexcept
                {
                    forEachFilter ([&] (auto& f) { f.setParameters (hz, q, m); });
                }

                /**
                 * @brief Reset all stored filters to their initial state.
                 *
                 * Audio thread safe. Called on note-off or transport stop
                 * to prevent clicks on topology switch.
                 */
                void reset() noexcept CASPI_NON_BLOCKING
                {
                    forEachFilter ([&] (auto& f) { f.reset(); });
                }

                /**
                 * @brief Read the stored sample rate.
                 * @return Active sample rate in Hz.
                 */
                CASPI_NO_DISCARD FloatType getSampleRate() const noexcept
                {
                    return sampleRate;
                }

                /**
                 * @brief Read the cutoff from the first stored filter.
                 *
                 * All filters share the same cutoff (set via delegation), so
                 * the first filter's value is representative.
                 */
                CASPI_NO_DISCARD FloatType getCutoff() const noexcept
                {
                    return std::get<0> (filters).getCutoff();
                }

                /**
                 * @brief Read the Q from the first stored filter.
                 *
                 * All filters share the same Q (set via delegation), so
                 * the first filter's value is representative.
                 */
                CASPI_NO_DISCARD FloatType getQ() const noexcept
                {
                    return std::get<0> (filters).getQ();
                }

                /**
                 * @brief Read the gain from the first stored filter.
                 */
                CASPI_NO_DISCARD FloatType getGainDb() const noexcept
                {
                    return std::get<0> (filters).getGainDb();
                }

                /**
                 * @brief Read the mode from the first stored filter.
                 *
                 * All filters share the same mode (set via delegation), so
                 * the first filter's value is representative.
                 */
                CASPI_NO_DISCARD FilterMode getMode() const noexcept
                {
                    return std::get<0> (filters).getMode();
                }

            private:
                /*--------------------------------------------------------------
                 * Initialisation
                 *-------------------------------------------------------------*/

                void initFilters (FloatType sr) noexcept
                {
                    sampleRate = sr;
                    forEachFilter ([&] (auto& f) { f.setSampleRate (sr); });
                }

                /*--------------------------------------------------------------
                 * Tuple iteration — C++17 fold expression
                 *-------------------------------------------------------------*/

                template <typename Fn>
                void forEachFilter (Fn&& fn) noexcept
                {
                    (fn (std::get<FilterTs<FloatType>> (filters)), ...);
                }

                /*--------------------------------------------------------------
                 * Reset a single filter by pack index
                 *-------------------------------------------------------------*/

                void resetFilterAt (std::size_t idx) noexcept
                {
                    resetFilterAtImpl<0> (idx);
                }

                template <std::size_t I>
                void resetFilterAtImpl (std::size_t idx) noexcept
                {
                    if (I == idx)
                    {
                        std::get<I> (filters).reset();
                    }
                    else if constexpr (I + 1 < sizeof...(FilterTs))
                    {
                        resetFilterAtImpl<I + 1> (idx);
                    }
                }

                /*--------------------------------------------------------------
                 * Dispatch by index
                 *-------------------------------------------------------------*/

                FloatType dispatch (FloatType in, std::size_t idx) noexcept CASPI_NON_BLOCKING
                {
                    return dispatchImpl<0> (in, idx);
                }

                template <std::size_t I>
                FloatType dispatchImpl (FloatType in, std::size_t idx) noexcept CASPI_NON_BLOCKING
                {
                    if (I == idx)
                    {
                        return std::get<I> (filters).processSample (in);
                    }
                    else if constexpr (I + 1 < sizeof...(FilterTs))
                    {
                        return dispatchImpl<I + 1> (in, idx);
                    }
                    return FloatType (0);
                }

                /*--------------------------------------------------------------
                 * Compile-time index lookup by type
                 *-------------------------------------------------------------*/

                template <template <typename> class Needle,
                          template <typename> class First,
                          template <typename> class... Rest>
                struct IndexOfImpl
                {
                    static const std::size_t value =
                        std::is_same<Needle<FloatType>, First<FloatType>>::value
                            ? 0
                            : 1 + IndexOfImpl<Needle, Rest...>::value;
                };

                template <template <typename> class Needle,
                          template <typename> class First>
                struct IndexOfImpl<Needle, First>
                {
                    static const std::size_t value =
                        std::is_same<Needle<FloatType>, First<FloatType>>::value
                            ? 0
                            : 1;
                };

                template <template <typename> class FilterT>
                static constexpr std::size_t indexOf() noexcept
                {
                    return IndexOfImpl<FilterT, FilterTs...>::value;
                }

                /*--------------------------------------------------------------
                 * State
                 *-------------------------------------------------------------*/

                FilterTuple filters;
                std::atomic<std::size_t> active { 0 };
                FloatType sampleRate = FloatType (48000);
        };

        /*--------------------------------------------------------------
         * Convenience aliases for common filter sets
         *-------------------------------------------------------------*/

        template <typename FloatType>
        using AllFilters = Filters<FloatType,
            StateVariable,
            Biquad,
            Ladder,
            DiodeLadder,
            OnePole>;

        template <typename FloatType>
        using LinearFilters = Filters<FloatType,
            StateVariable,
            Biquad,
            OnePole>;

        template <typename FloatType>
        using NonlinearFilters = Filters<FloatType,
            Ladder,
            DiodeLadder>;

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_FILTERS_H