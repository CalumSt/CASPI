#ifndef CASPI_FILTER_H
#define CASPI_FILTER_H

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
 * @file   filters/caspi_Filter.h
 * @author CS Islay
 * @brief  CRTP base class for digital filters integrating with Processor.
 *
 * @details
 * ### Overview
 *
 * FilterBase<Derived, FloatType, NumStates> inherits Core::Processor and
 * adds:
 *   - Per-channel state variable storage (z^-1 delays).
 *   - AtomicCoefficients double buffer for lock-free parameter updates.
 *   - Common parameter API: setCutoff(), setQ(), setGain().
 *   - CRTP hook: updateCoefficients() called whenever parameters change.
 *
 * ### CRTP contract
 *
 * Derived must override:
 *   FloatType processSample(FloatType in) noexcept  — scalar per-sample path.
 *   void updateCoefficients()                       — recompute coefficients
 *                                                     from cutoff/Q/gain.
 *
 * Derived may override:
 *   void onPrepare(numChannels, numFrames, sampleRate)  — called by AudioNode.
 *   void onSampleRateChanged(rate)                      — called by NodeBase.
 *   void resetState()                                   — clear internal state.
 *
 * ### Parameter update model
 *
 * Parameters (cutoff, Q, gain) are stored as plain FloatType members.
 * Coefficient computation is deferred to updateCoefficients() which is
 * called on the setup thread by setCutoff() / setQ() / setGain() and by
 * onSampleRateChanged().
 *
 * AtomicCoefficients<FloatType, NumCoeffs> double-buffers the computed
 * coefficient array and provides a lock-free swap so the audio thread
 * always reads a consistent set. The pattern:
 *
 *   // GUI / setup thread:
 *   filter.setCutoff(800.f);          // calls updateCoefficients()
 *                                     // which calls coeffs.swap(newArray)
 *
 *   // Audio thread:
 *   float s = filter.processSample(x); // reads coeffs.get() — consistent
 *
 * ### State layout
 *
 * states[NumStates] is a flat array shared across all channels for filters
 * that maintain separate state per channel (e.g. stereo processing), the
 * Derived class must manage per-channel state manually and ignore the base
 * states array, or use NumStates = 2 * numChannels and index accordingly.
 *
 * The simpler and more common case (mono or single-channel processing via
 * Processor's traversal) stores state in the flat array directly.
 *
 * ### FilterMode
 *
 * FilterMode is defined here so all filters share a common vocabulary.
 * Derived classes use it in processSample() to select output topology.
 *
 ************************************************************************/

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "core/caspi_Processor.h"

namespace CASPI
{
    namespace Filters
    {

        /*======================================================================
         * FilterMode
         *====================================================================*/

        /**
         * @brief Selects the filter topology output for multi-mode filters.
         *
         * Not all modes are valid for all filter types. Invalid modes
         * silently default to LowPass.
         */
        enum class FilterMode
        {
            LowPass, ///< Low-pass response.
            HighPass, ///< High-pass response.
            BandPass, ///< Band-pass response.
            Notch, ///< Band-reject (notch) response.
            Peak, ///< Peaking EQ response.
            AllPass, ///< All-pass response (phase shift only).
            LowShelf, ///< Low-shelf EQ.
            HighShelf, ///< High-shelf EQ.
        };

        /*======================================================================
         * AtomicCoefficients<FloatType, NumCoeffs>
         *====================================================================*/

        /**
         * @brief Lock-free double-buffered coefficient array.
         *
         * Allows a setup/GUI thread to write new coefficients while the audio
         * thread reads the current set without blocking. The active index is
         * stored in an atomic<size_t>; swap() writes to the inactive buffer
         * then publishes it atomically.
         *
         * ### Guarantee
         * The audio thread always reads a complete, consistent coefficient set.
         * There is no partial-update window. This is sufficient for sample-rate
         * or cutoff changes that happen between blocks.
         *
         * ### Limitation
         * Not safe for concurrent writers. All writes must come from a single
         * setup/GUI thread (or be serialised externally).
         *
         * @tparam FloatType  float or double.
         * @tparam NumCoeffs  Number of coefficients per set.
         */
        template <typename FloatType, std::size_t NumCoeffs>
        class AtomicCoefficients
        {
            public:
                using CoeffArray = std::array<FloatType, NumCoeffs>;

                AtomicCoefficients() noexcept
                {
                    current.store (0u, std::memory_order_release);
                    coeffs[0].fill (FloatType (0));
                    coeffs[1].fill (FloatType (0));
                }

                /**
                 * @brief Read the current active coefficient set. Audio thread safe.
                 * @return Const reference to the active CoeffArray.
                 */
                CASPI_NO_DISCARD const CoeffArray& get() const noexcept CASPI_NON_BLOCKING
                {
                    return coeffs[current.load (std::memory_order_acquire)];
                }

                /**
                 * @brief Read one coefficient by index. Audio thread safe.
                 * @param index  Zero-based coefficient index.
                 */
                CASPI_NO_DISCARD FloatType operator[] (std::size_t index) const noexcept CASPI_NON_BLOCKING
                {
                    CASPI_ASSERT (index < NumCoeffs, "Coefficient index out of bounds");
                    return get()[index];
                }

                /**
                 * @brief Atomically publish a new coefficient set. Setup thread only.
                 *
                 * Writes newCoeffs to the inactive buffer, then flips the active
                 * index. After this call, audio-thread reads will see newCoeffs.
                 *
                 * @param newCoeffs  New coefficient values.
                 */
                void swap (const CoeffArray& newCoeffs) noexcept
                {
                    const std::size_t cur  = current.load (std::memory_order_acquire);
                    const std::size_t next = 1u - cur;
                    coeffs[next]           = newCoeffs;
                    current.store (next, std::memory_order_release);
                }

                /** @brief Zero both coefficient buffers. Setup thread only. */
                void reset() noexcept
                {
                    coeffs[0].fill (FloatType (0));
                    coeffs[1].fill (FloatType (0));
                }

            private:
                std::atomic<std::size_t> current { 0u };
                CoeffArray coeffs[2];
        };

        /*======================================================================
         * FilterBase<Derived, FloatType, NumStates>
         *====================================================================*/

        /**
         * @brief CRTP base class for digital filters.
         *
         * Inherits Core::Processor<Derived, FloatType, Traversal::PerSample>.
         * Adds: state storage, AtomicCoefficients, common parameter API,
         * and sample-rate notification forwarding.
         *
         * @tparam Derived     Concrete filter class (CRTP).
         * @tparam FloatType   float or double.
         * @tparam NumStates   Number of z^-1 state variables (e.g. 2 for SVF).
         * @tparam NumCoeffs   Number of coefficients in the atomic double buffer.
         */
        template <typename Derived, typename FloatType, std::size_t NumStates, std::size_t NumCoeffs>
        class FilterBase : public Core::Processor<Derived, FloatType, Core::Traversal::PerSample>
        {
            public:
                /*------------------------------------------------------------------
                 * Public parameter API — setup / GUI thread
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Set the cutoff (or centre) frequency in Hz.
                 *
                 * Triggers updateCoefficients() via CRTP dispatch.
                 * Must be called from the setup thread or before streaming starts.
                 *
                 * @param hz  Frequency in Hz. Must be > 0 and < sampleRate / 2.
                 */
                void setCutoff (FloatType hz) noexcept
                {
                    CASPI_ASSERT (hz > FloatType (0), "Cutoff must be positive");
                    cutoff = hz;
                    static_cast<Derived*> (this)->updateCoefficients();
                }

                /**
                 * @brief Set the quality (resonance) factor.
                 *
                 * Q = 0.5 = critically damped, Q = 0.707 = Butterworth,
                 * Q > 1 = resonant peak. Triggers updateCoefficients().
                 *
                 * @param q  Quality factor. Must be > 0.
                 */
                void setQ (FloatType q) noexcept
                {
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");
                    Q = q;
                    static_cast<Derived*> (this)->updateCoefficients();
                }

                /**
                 * @brief Set the gain in dB (used by peaking / shelf filters).
                 *
                 * Triggers updateCoefficients(). Default: 0 dB.
                 *
                 * @param dB  Gain in decibels.
                 */
                void setGain (FloatType dB) noexcept
                {
                    gainDb = dB;
                    static_cast<Derived*> (this)->updateCoefficients();
                }

                /**
                 * @brief Set the filter mode (LP, HP, BP, etc.).
                 *
                 * Mode selection takes effect on the next processSample() call.
                 * No coefficient recomputation required for mode-only changes on
                 * multi-mode topologies (e.g. SVF).
                 *
                 * @param m  New filter mode.
                 */
                void setMode (FilterMode m) noexcept
                {
                    mode = m;
                }

                /**
                 * @brief Set cutoff, Q, and mode in one call.
                 *
                 * Calls updateCoefficients() once. Prefer this over three
                 * separate calls when changing multiple parameters together.
                 */
                void setParameters (FloatType hz, FloatType q, FilterMode m = FilterMode::LowPass) noexcept
                {
                    CASPI_ASSERT (hz > FloatType (0), "Cutoff must be positive");
                    CASPI_ASSERT (q > FloatType (0), "Q must be positive");
                    cutoff = hz;
                    Q      = q;
                    mode   = m;
                    static_cast<Derived*> (this)->updateCoefficients();
                }

                /*------------------------------------------------------------------
                 * Parameter accessors
                 *-----------------------------------------------------------------*/

                CASPI_NO_DISCARD FloatType getCutoff() const noexcept
                {
                    return cutoff;
                }
                CASPI_NO_DISCARD FloatType getQ() const noexcept
                {
                    return Q;
                }
                CASPI_NO_DISCARD FloatType getGainDb() const noexcept
                {
                    return gainDb;
                }
                CASPI_NO_DISCARD FilterMode getMode() const noexcept
                {
                    return mode;
                }

                /**
                 * @brief Read one coefficient from the active set by index.
                 *
                 * Primarily for testing and diagnostics. Audio thread safe.
                 * @param index  Zero-based index into the NumCoeffs array.
                 */
                CASPI_NO_DISCARD FloatType getCoeffAt (std::size_t index) const noexcept CASPI_NON_BLOCKING
                {
                    return coeffs[index];
                }

                /*------------------------------------------------------------------
                 * State management
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Reset all state variables to zero and zero the coefficients.
                 *
                 * Call on note-off or transport stop to prevent clicks. Calls
                 * the Derived resetState() hook.
                 */
                void reset() noexcept CASPI_NON_BLOCKING
                {
                    states.fill (FloatType (0));
                    static_cast<Derived*> (this)->resetState();
                }

                /**
                 * @brief Default resetState() hook — does nothing.
                 *
                 * Override in Derived if additional state beyond states[] needs
                 * clearing (e.g. feedback registers, delay lines).
                 */
                void resetState() noexcept CASPI_NON_BLOCKING
                {
                }

                /*------------------------------------------------------------------
                 * State accessors (audio thread)
                 *-----------------------------------------------------------------*/

                CASPI_NO_DISCARD FloatType getState (std::size_t index) const noexcept CASPI_NON_BLOCKING
                {
                    CASPI_RT_ASSERT (index < NumStates);
                    return states[index];
                }

                void setState (std::size_t index, FloatType value) noexcept CASPI_NON_BLOCKING
                {
                    CASPI_RT_ASSERT (index < NumStates);
                    states[index] = value;
                }

                /*------------------------------------------------------------------
                 * Sample rate hook — NodeBase/AudioNode calls this
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Called by NodeBase when the sample rate changes.
                 *
                 * Stores the new rate and triggers updateCoefficients() via CRTP.
                 */
                void onSampleRateChanged (FloatType newRate) noexcept override
                {
                    CASPI_ASSERT (newRate > FloatType (0), "Sample rate must be positive");
                    // Store via the base NodeBase method.
                    if (cutoff > FloatType (0))
                    {
                        static_cast<Derived*> (this)->updateCoefficients();
                    }
                }

                /*------------------------------------------------------------------
                 * Graph hook — called by AudioNode::prepareToRender()
                 *-----------------------------------------------------------------*/

                void onPrepare (std::size_t, std::size_t, double sampleRateIn) noexcept
                {
                    const FloatType fs = static_cast<FloatType> (sampleRateIn);
                    Graph::NodeBase<FloatType>::setSampleRate (fs);
                    if (cutoff > FloatType (0))
                    {
                        static_cast<Derived*> (this)->updateCoefficients();
                    }
                }

            protected:
                /*------------------------------------------------------------------
                 * Construction — protected; only Derived constructs via CRTP
                 *-----------------------------------------------------------------*/

                FilterBase()
                    : Core::Processor<Derived, FloatType, Core::Traversal::PerSample> (1, 1)
                {
                    states.fill (FloatType (0));
                }

                /*------------------------------------------------------------------
                 * State and coefficient storage
                 *-----------------------------------------------------------------*/

                /** @brief z^-1 state variables. Indexed by convention of Derived. */
                std::array<FloatType, NumStates> states {};

                /** @brief Convenience typedef so Derived classes can name the array type. */
                using AtomicCoefficientsType = AtomicCoefficients<FloatType, NumCoeffs>;

                /** @brief Lock-free double-buffered coefficient array. */
                AtomicCoefficientsType coeffs;

                /** @brief Cutoff / centre frequency in Hz. */
                FloatType cutoff = FloatType (1000);

                /** @brief Quality factor. */
                FloatType Q = FloatType (0.7071067811865476); // 1/sqrt(2) = Butterworth

                /** @brief Gain in dB (peaking/shelf only). */
                FloatType gainDb = FloatType (0);

                /** @brief Current filter mode. */
                FilterMode mode = FilterMode::LowPass;
        };

    } // namespace Filters
} // namespace CASPI

#endif // CASPI_FILTER_H