#ifndef CASPI_FILTER_SELECTOR_H
#define CASPI_FILTER_SELECTOR_H

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
 * @file   filters/caspi_FilterSelector.h
 * @author CS Islay
 * @brief  FilterSelector -- runtime-selectable filter topology.
 *
 * Pre-allocates one Filter<F, Topology> for each topology in the pack
 * and dispatches processSample() to the active one with zero-allocation
 * overhead.
 *
 * Dispatch strategies:
 *   C++17 -- if constexpr chain (zero misprediction if topology stable).
 *   C++11 -- member-function-pointer table (3-4 cycle indirect call cost).
 *
 * Usage:
 *   FilterSelector<float, StateVariable, Biquad, Ladder> sel(48000.f);
 *   sel.setCutoff(1000.f);
 *   float out = sel.processSample(in);
 *   sel.setTopology(FilterTopology::Biquad);
 *
 ************************************************************************/

#include <atomic>
#include <tuple>
#include <utility>

#include "base/caspi_Assert.h"
#include "base/caspi_Features.h"
#include "filters/caspi_Filter.h"

namespace CASPI
{
    namespace Filters
    {

        template <typename FloatType, FilterTopology... Topologies>
        class FilterSelector
        {
            static_assert (sizeof...(Topologies) > 0,
                           "FilterSelector requires at least one topology");

            /*------------------------------------------------------------------
             * Helper: capture the first topology from the pack.
             *-----------------------------------------------------------------*/
            template <FilterTopology First, FilterTopology...>
            struct FirstTopology
            {
                static constexpr FilterTopology value = First;
            };

            public:
                using FilterTuple = std::tuple<Filter<FloatType, Topologies>...>;

                /*--------------------------------------------------------------
                 * Construction
                 *-------------------------------------------------------------*/

                /**
                 * @brief Default constructor. Uses the project default sample
                 *        rate and sets the active topology to the first in the
                 *        pack.
                 */
                FilterSelector() noexcept CASPI_NON_ALLOCATING
                {
                    initFilters (Constants::DEFAULT_SAMPLE_RATE<FloatType>);
                    active.store (FirstTopology<Topologies...>::value,
                                  std::memory_order_release);
                }

                /**
                 * @brief Construct with a sample rate and optional initial
                 *        topology.
                 *
                 * Initialises every filter to the given sample rate and resets
                 * their state.
                 *
                 * @param sampleRate  Sample rate in Hz. Must be > 0.
                 * @param initial     Initial active topology. Defaults to
                 *                    the first topology in the pack.
                 */
                explicit FilterSelector (
                    FloatType sampleRate,
                    FilterTopology initial = FirstTopology<Topologies...>::value) noexcept CASPI_NON_ALLOCATING
                {
                    initFilters (sampleRate);
                    active.store (initial, std::memory_order_release);
                }

                /*--------------------------------------------------------------
                 * Audio-thread hot path
                 *-------------------------------------------------------------*/

                /**
                 * @brief Process one input sample through the active filter.
                 *
                 * Dispatches to the currently selected filter topology with
                 * no allocation. Audio thread safe.
                 *
                 * @param in  Input sample.
                 * @return    Filtered output from the active filter.
                 */
                FloatType processSample (FloatType in) noexcept CASPI_NON_BLOCKING
                {
#if defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                    return dispatchIfConstexpr (in,
                                                std::integral_constant<std::size_t, 0>{});
#else
                    return dispatchMemberFn (in);
#endif
                }

                /*--------------------------------------------------------------
                 * Topology switching — setup thread only
                 *-------------------------------------------------------------*/

                /**
                 * @brief Switch the active filter topology at runtime.
                 *
                 * Resets the target filter before activating it so the
                 * new topology starts from a clean state. Setup thread only.
                 *
                 * @param t  The topology to activate. Must be one of the
                 *           topologies in the pack.
                 */
                void setTopology (FilterTopology t) noexcept
                {
                    CASPI_ASSERT (static_cast<std::size_t> (t) < sizeof...(Topologies),
                                  "Topology index out of range");
                    resetFilterAt (static_cast<std::size_t> (t));
                    active.store (t, std::memory_order_release);
                }

                /**
                 * @brief Read the currently active topology.
                 * @return The active FilterTopology value.
                 */
                CASPI_NO_DISCARD FilterTopology getTopology() const noexcept
                {
                    return active.load (std::memory_order_acquire);
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
                 * Tuple iteration — C++17 fold vs C++11 recursion
                 *-------------------------------------------------------------*/

                template <typename Fn>
                void forEachFilter (Fn&& fn) noexcept
                {
#if defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                    (fn (std::get<Filter<FloatType, Topologies>> (filters)), ...);
#else
                    forEachFilterImpl<0> (std::forward<Fn> (fn));
#endif
                }

#if !defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                template <std::size_t I, typename Fn>
                typename std::enable_if<I == sizeof...(Topologies), void>::type
                forEachFilterImpl (Fn&&) noexcept
                {
                }

                template <std::size_t I, typename Fn>
                typename std::enable_if<I < sizeof...(Topologies), void>::type
                forEachFilterImpl (Fn&& fn) noexcept
                {
                    fn (std::get<I> (filters));
                    forEachFilterImpl<I + 1> (std::forward<Fn> (fn));
                }
#endif

                /*--------------------------------------------------------------
                 * Reset a single filter by pack index
                 *-------------------------------------------------------------*/

                void resetFilterAt (std::size_t idx) noexcept
                {
                    resetFilterAtImpl<0> (idx);
                }

#if defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                template <std::size_t I>
                void resetFilterAtImpl (std::size_t idx) noexcept
                {
                    if (I == idx)
                    {
                        std::get<I> (filters).reset();
                    }
                    else if constexpr (I + 1 < sizeof...(Topologies))
                    {
                        resetFilterAtImpl<I + 1> (idx);
                    }
                }
#else
                /* Base case: I == sizeof...(Topologies) — do nothing. */
                template <std::size_t I>
                typename std::enable_if<I == sizeof...(Topologies), void>::type
                resetFilterAtImpl (std::size_t) noexcept
                {
                }

                /* Recursive case: check index I, then advance. */
                template <std::size_t I>
                typename std::enable_if<I < sizeof...(Topologies), void>::type
                resetFilterAtImpl (std::size_t idx) noexcept
                {
                    if (I == idx)
                    {
                        std::get<I> (filters).reset();
                    }
                    else
                    {
                        resetFilterAtImpl<I + 1> (idx);
                    }
                }
#endif

                /*--------------------------------------------------------------
                 * Dispatch — C++17 if constexpr chain
                 *-------------------------------------------------------------*/

#if defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                template <std::size_t I>
                FloatType dispatchIfConstexpr (
                    FloatType in,
                    std::integral_constant<std::size_t, I>) noexcept CASPI_NON_BLOCKING
                {
                    if (active.load (std::memory_order_relaxed) ==
                        static_cast<FilterTopology> (I))
                    {
                        return std::get<I> (filters).processSample (in);
                    }
                    if constexpr (I + 1 < sizeof...(Topologies))
                    {
                        return dispatchIfConstexpr (
                            in, std::integral_constant<std::size_t, I + 1>{});
                    }
                    return FloatType (0);
                }
#endif

                /*--------------------------------------------------------------
                 * Dispatch — C++11 member-function-pointer table
                 *-------------------------------------------------------------*/

#if !defined(CASPI_FEATURES_HAS_IF_CONSTEXPR)
                using DispatchFn =
                    FloatType (FilterSelector::*) (FloatType);

                template <FilterTopology T>
                FloatType dispatchMember (FloatType in) noexcept CASPI_NON_BLOCKING
                {
                    return std::get<Filter<FloatType, T>> (filters)
                        .processSample (in);
                }

                static const DispatchFn* buildDispatchTable() noexcept CASPI_NON_BLOCKING
                {
                    static const DispatchFn table[sizeof...(Topologies)] = {
                        &FilterSelector::dispatchMember<Topologies>...
                    };
                    return table;
                }

                FloatType dispatchMemberFn (FloatType in) noexcept CASPI_NON_BLOCKING
                {
                    return (this->*buildDispatchTable()
                                          [static_cast<std::size_t> (
                                              active.load (std::memory_order_relaxed))]) (in);
                }
#endif

                /*--------------------------------------------------------------
                 * State
                 *-------------------------------------------------------------*/

                FilterTuple filters;
                std::atomic<FilterTopology> active { FirstTopology<Topologies...>::value };
                FloatType sampleRate = FloatType (48000);
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_FILTER_SELECTOR_H
