#ifndef CASPI_MODULATION_MATRIX_H
#define CASPI_MODULATION_MATRIX_H

/*************************************************************************
 * @file caspi_ModMatrix.h
 *
 * ARCHITECTURE
 *
 * Routing data is split into two fixed-capacity sorted lists at
 * command-apply time:
 *
 *   linearRoutings[]    - curve == Linear, processed via FMA-eligible loop
 *   nonLinearRoutings[] - all other curves, processed scalar
 *
 * Both lists are sorted by destinationId ascending so writes into the
 * flat modulationAccum[] array are sequential, improving spatial locality
 * and exposing auto-vectorisation opportunities across adjacent writes.
 *
 * Per-block flow (process()):
 *
 *   drainCommands()
 *     try_dequeue -> applyCommand() until queue empty
 *
 *   accumulateLinear()
 *     ops::fill(modulationAccum, 0)         SIMD zero (NT stores if large)
 *     for each linearRouting (enabled):
 *       accum[dst] += source[src] * depth   FMA-eligible
 *
 *   accumulateNonLinear()
 *     for each nonLinearRouting (enabled):
 *       accum[dst] += applyCurve(source[src]) * depth   scalar
 *
 *   scatterToParameters()
 *     ops::clamp(modulationAccum, -1, 1)    SIMD clamp
 *     for each param:
 *       clearModulation() + addModulation(accum[i])
 *       one pointer chase per param, not per routing
 *
 * SIMD coverage:
 *   Zero pass    : ops::fill   (FillKernel, NT stores above L1 threshold)
 *   Clamp pass   : ops::clamp  (ClampKernel, min/max per lane)
 *   Linear accum : scalar loop, FMA auto-vectorised under /arch:AVX2 or -mavx2
 *
 * No heap allocation on the audio thread. RoutingList uses a fixed std::array.
 * Total routing storage: 2 x 1024 x 24B = 48KB (float), fits in L2.
 *
 * COMPILE-TIME CURVE SELECTION
 *
 * removeRouting and setRoutingEnabled are function templates parameterised
 * on ModulationCurve. The curve determines which internal list is targeted,
 * making the selection explicit and unspoofable at the call site:
 *
 *   matrix.removeRouting<ModulationCurve::Linear>(0);
 *   matrix.removeRouting<ModulationCurve::SCurve>(2);
 *   matrix.removeRouting(0);           // defaults to Linear
 *
 * The default (ModulationCurve::Linear) matches the dominant use case so
 * existing call sites that omit the template argument are unambiguous.
 *
 * Thread safety model:
 *   registerParameter()                    - setup phase only
 *   addRouting / removeRouting
 *   / clearRoutings / setRoutingEnabled    - any thread (lock-free enqueue)
 *   setSourceValue / process / reset       - audio thread only
 *   getSourceValue / getNumRoutings
 *   / getNumParameters                     - audio thread only
 *
 ************************************************************************/

/*------------------------------------------------------------------------------
 * Includes - System
 *----------------------------------------------------------------------------*/
#include <array>
#include <cmath>
#include <cstring>

/*------------------------------------------------------------------------------
 * Includes - Project
 *----------------------------------------------------------------------------*/
#include "base/caspi_Assert.h"
#include "base/SIMD/caspi_Blocks.h"
#include "core/caspi_Expected.h"
#include "core/caspi_Parameter.h"
#include "external/caspi_External.h"

namespace CASPI
{
    namespace Controls
    {
        /*======================================================================
         * Compile-time capacity constants
         *====================================================================*/

        /**
         * @brief Maximum number of distinct modulation sources (LFOs, envelopes, etc.).
         */
        static constexpr size_t kMaxModSources = 64;

        /**
         * @brief Maximum number of routings per curve class.
         *
         * Total maximum routing count = 2 x kMaxModRoutings (linear + non-linear).
         */
        static constexpr size_t kMaxModRoutings = 1024;

        /**
         * @brief Maximum number of registered modulatable parameters per matrix instance.
         */
        static constexpr size_t kMaxModParams = 256;

        /*======================================================================
         * ModulationCurve
         *====================================================================*/

        /**
         * @brief Curve shaping applied to a source value before depth scaling.
         *
         * Linear is the dominant case and takes the SIMD fast path internally.
         * All other values are handled on the scalar non-linear pass.
         *
         * This enum is also used as a non-type template parameter on
         * ModMatrix::removeRouting and ModMatrix::setRoutingEnabled to select
         * the target routing list at compile time.
         */
        enum class ModulationCurve
        {
            Linear, /**< y = x,                   SIMD fast path  */
            Exponential, /**< y = sign(x) * x^2,       scalar          */
            Logarithmic, /**< y = sign(x) * sqrt(|x|), scalar          */
            SCurve /**< smoothstep,               scalar          */
        };

        /*======================================================================
         * Detail: compile-time list selector
         *====================================================================*/

        namespace detail
        {
            /**
             * @brief Maps a ModulationCurve value to true if it uses the linear list.
             *
             * @tparam C  Curve to test.
             */
            template <ModulationCurve C>
            struct IsLinearCurve
            {
                    static constexpr bool value = (C == ModulationCurve::Linear);
            };
        } /* namespace detail */

        /*======================================================================
         * ModulationRouting
         *====================================================================*/

        /**
         * @brief Single modulation routing connecting one source to one parameter.
         *
         * POD-compatible struct: 2 x size_t + FloatType + enum + bool = 24 bytes (float).
         * Kept small so the full RoutingList fits within L2 cache.
         *
         * Depth is clamped to [-1, 1] at enqueue time (addRouting on the GUI thread)
         * so the audio-thread accumulation loop is unconditional.
         *
         * @tparam FloatType  Floating-point scalar type (float or double).
         */
        template <typename FloatType>
        struct ModulationRouting
        {
                /** @brief Index into ModMatrix::sourceValues[]. */
                size_t sourceId = 0;

                /** @brief Index into ModMatrix::parameters[] and modulationAccum[]. */
                size_t destinationId = 0;

                /** @brief Modulation depth, clamped to [-1, 1] at enqueue time. */
                FloatType depth = FloatType (0);

                /** @brief Curve shaping applied before depth scaling. */
                ModulationCurve curve = ModulationCurve::Linear;

                /** @brief When false the routing contributes zero modulation. */
                bool enabled = true;

                /**
                 * @brief Default constructor. All fields zero / default initialised.
                 */
                ModulationRouting() noexcept CASPI_NON_BLOCKING = default;

                /**
                 * @brief Construct a linear routing with the given source, destination and depth.
                 *
                 * @param src  Index into sourceValues[].
                 * @param dst  Index into parameters[] / modulationAccum[].
                 * @param d    Modulation depth. Clamped to [-1, 1] at enqueue time by addRouting().
                 */
                ModulationRouting (size_t src, size_t dst, FloatType d) noexcept CASPI_NON_BLOCKING
                    : sourceId (src),
                      destinationId (dst),
                      depth (d)
                {
                }

                /**
                 * @brief Apply curve shaping to a normalised source value.
                 *
                 * Only called from the non-linear accumulation pass.
                 * Linear routings never invoke this function; they take the FMA path directly.
                 *
                 * @param value  Source value, typically in [-1, 1] or [0, 1].
                 * @return       Curve-shaped value in the same normalised range.
                 */
                CASPI_NO_DISCARD FloatType applyCurve (FloatType value) const noexcept CASPI_NON_BLOCKING
                {
                    switch (curve)
                    {
                        case ModulationCurve::Linear:
                        {
                            return value;
                        }

                        case ModulationCurve::Exponential:
                        {
                            return (value >= FloatType (0)) ? value * value : -(value * value);
                        }

                        case ModulationCurve::Logarithmic:
                        {
                            return (value >= FloatType (0)) ? std::sqrt (value) : -std::sqrt (-value);
                        }

                        case ModulationCurve::SCurve:
                        {
                            FloatType x       = (value + FloatType (1)) / FloatType (2);
                            x                 = Maths::clamp (x, FloatType (0), FloatType (1));
                            const FloatType s = x * x * (FloatType (3) - FloatType (2) * x);
                            return s * FloatType (2) - FloatType (1);
                        }

                        default:
                        {
                            return value;
                        }
                    }
                }
        };

        /*======================================================================
         * RoutingList
         *====================================================================*/

        /**
         * @brief Fixed-capacity contiguous array of ModulationRouting, sorted by destinationId.
         *
         * Sorted on every insert so the accumulation pass produces sequential writes
         * into the flat modulationAccum[] array, keeping it hot in L1 cache.
         *
         * insert()   is O(log n) binary search + O(n) element shift.
         * removeAt() is O(n) element shift.
         * Both are acceptable: routing changes occur at GUI event rate, not per sample.
         *
         * No heap allocation. The entire list lives in a fixed std::array member.
         *
         * @tparam FloatType  Floating-point scalar type.
         * @tparam Capacity   Maximum number of entries. Debug-asserts if exceeded.
         */
        template <typename FloatType, size_t Capacity>
        class RoutingList
        {
            public:
                using Routing = ModulationRouting<FloatType>;

                /**
                 * @brief Insert a routing, maintaining ascending destinationId order.
                 *
                 * Uses binary search to locate the insertion point, then shifts
                 * existing elements right. Duplicate destinationIds are appended
                 * after existing entries with the same id (stable insert).
                 *
                 * @param r  Routing to insert. Debug-asserts if Capacity is reached.
                 */
                void insert (const Routing& r) noexcept CASPI_NON_BLOCKING
                {
                    if (count >= Capacity)
                    {
                        CASPI_RT_ASSERT (false);
                        return;
                    }

                    size_t lo = 0;
                    size_t hi = count;

                    while (lo < hi)
                    {
                        const size_t mid = lo + (hi - lo) / 2;

                        if (data[mid].destinationId < r.destinationId)
                        {
                            lo = mid + 1;
                        }
                        else
                        {
                            hi = mid;
                        }
                    }

                    for (size_t i = count; i > lo; --i)
                    {
                        data[i] = data[i - 1];
                    }

                    data[lo] = r;
                    ++count;
                }

                /**
                 * @brief Remove the routing at the given index.
                 *
                 * Silently returns if index >= count.
                 * Elements above index are shifted left to fill the gap.
                 *
                 * @param index  Zero-based position within this list.
                 */
                void removeAt (size_t index) noexcept CASPI_NON_BLOCKING
                {
                    if (index >= count)
                    {
                        return;
                    }

                    for (size_t i = index; i + 1 < count; ++i)
                    {
                        data[i] = data[i + 1];
                    }

                    --count;
                }

                /**
                 * @brief Remove all routings, setting count to zero.
                 */
                void clear() noexcept CASPI_NON_BLOCKING
                {
                    count = 0;
                }

                /**
                 * @brief Return the number of routings currently stored.
                 *
                 * @return Current routing count.
                 */
                CASPI_NO_DISCARD size_t size() const noexcept CASPI_NON_BLOCKING
                {
                    return count;
                }

                /**
                 * @brief Return true if no routings are stored.
                 *
                 * @return True when count == 0.
                 */
                CASPI_NO_DISCARD bool empty() const noexcept CASPI_NON_BLOCKING
                {
                    return count == 0;
                }

                /**
                 * @brief Mutable element access by index. No bounds checking in release.
                 *
                 * @param i  Zero-based index.
                 * @return   Reference to the routing at position i.
                 */
                CASPI_NO_DISCARD Routing& operator[] (size_t i) noexcept CASPI_NON_BLOCKING
                {
                    return data[i];
                }

                /**
                 * @brief Const element access by index. No bounds checking in release.
                 *
                 * @param i  Zero-based index.
                 * @return   Const reference to the routing at position i.
                 */
                CASPI_NO_DISCARD const Routing& operator[] (size_t i) const noexcept CASPI_NON_BLOCKING
                {
                    return data[i];
                }

                /**
                 * @brief Return a mutable pointer to the first routing.
                 *
                 * @return Pointer to data[0].
                 */
                CASPI_NO_DISCARD Routing* begin() noexcept CASPI_NON_BLOCKING
                {
                    return data.data();
                }

                /**
                 * @brief Return a mutable one-past-end pointer.
                 *
                 * @return Pointer to data[count].
                 */
                CASPI_NO_DISCARD Routing* end() noexcept CASPI_NON_BLOCKING
                {
                    return data.data() + count;
                }

                /**
                 * @brief Return a const pointer to the first routing.
                 *
                 * @return Const pointer to data[0].
                 */
                CASPI_NO_DISCARD const Routing* begin() const noexcept CASPI_NON_BLOCKING
                {
                    return data.data();
                }

                /**
                 * @brief Return a const one-past-end pointer.
                 *
                 * @return Const pointer to data[count].
                 */
                CASPI_NO_DISCARD const Routing* end() const noexcept CASPI_NON_BLOCKING
                {
                    return data.data() + count;
                }

            private:
                std::array<Routing, Capacity> data {};
                size_t count = 0;
        };

        /*======================================================================
         * ModMatrix
         *====================================================================*/

        /**
         * @brief Modulation matrix routing modulation sources to modulatable parameters.
         *
         * See file-level architecture comment for full design description.
         *
         * Routing mutations (add, remove, clear, enable) are enqueued lock-free
         * from any thread and applied at the start of each process() call on the
         * audio thread. No heap allocation occurs on the audio thread.
         *
         * removeRouting() and setRoutingEnabled() are function templates
         * parameterised on ModulationCurve. The curve selects the internal list
         * at compile time, eliminating a runtime bool argument and preventing
         * the caller from targeting the wrong list:
         *
         * @code
         *   matrix.removeRouting<ModulationCurve::Linear>(0);     // explicit
         *   matrix.removeRouting<ModulationCurve::SCurve>(2);     // explicit
         *   matrix.removeRouting(0);                              // defaults to Linear
         * @endcode
         *
         * @tparam FloatType  Floating-point scalar type (float or double).
         */
        template <typename FloatType>
        class ModMatrix
        {
            public:
                /*==============================================================
                 * Command queue types
                 *============================================================*/

                /**
                 * @brief Discriminates the mutation carried by a Command.
                 */
                enum class CommandType
                {
                    AddRouting, /**< Insert a routing into the appropriate list.    */
                    RemoveLinear, /**< Remove by index from linearRoutings.           */
                    RemoveNonLinear, /**< Remove by index from nonLinearRoutings.        */
                    ClearRoutings, /**< Clear both routing lists.                      */
                    SetEnabledLinear, /**< Toggle enabled flag in linearRoutings.         */
                    SetEnabledNonLinear /**< Toggle enabled flag in nonLinearRoutings.    */
                };

                /**
                 * @brief Payload carried through the lock-free command queue.
                 *
                 * Only the fields relevant to the CommandType are used;
                 * the rest are default-initialised.
                 */
                struct Command
                {
                        /** @brief Which mutation to perform when dequeued. */
                        CommandType type = CommandType::ClearRoutings;

                        /** @brief Routing data; used by AddRouting only. */
                        ModulationRouting<FloatType> routing {};

                        /** @brief Target index; used by Remove* and SetEnabled*. */
                        size_t index = 0;

                        /** @brief New enabled state; used by SetEnabled* only. */
                        bool enabled = false;
                };

                /*==============================================================
                 * Parameter registration (setup phase only - not thread-safe)
                 *============================================================*/

                /**
                 * @brief Error codes returned by registerParameter().
                 */
                enum class ParamRegistrationError
                {
                    NullParameter, /**< The supplied pointer was null.               */
                    CapacityExceeded /**< kMaxModParams registrations already reached. */
                };

                /**
                 * @brief Register a modulatable parameter with this matrix.
                 *
                 * Must be called during setup, before the audio thread starts.
                 * This function is not thread-safe and must not be called
                 * concurrently with any other method.
                 *
                 * The caller retains ownership; the pointer must remain valid for
                 * the entire lifetime of this ModMatrix instance.
                 *
                 * @param parameter  Non-null pointer to a ModulatableParameter.
                 * @return           Destination ID for use in addRouting(), or an error.
                 */
                expected<size_t, ParamRegistrationError> registerParameter (
                    Core::ModulatableParameter<FloatType>* parameter) CASPI_ALLOCATING
                {
                    CASPI_EXPECT (parameter != nullptr, "Cannot register null parameter");

                    if (parameter == nullptr)
                    {
                        return make_unexpected<size_t, ParamRegistrationError> (ParamRegistrationError::NullParameter);
                    }

                    if (numParameters >= kMaxModParams)
                    {
                        return make_unexpected<size_t, ParamRegistrationError> (
                            ParamRegistrationError::CapacityExceeded);
                    }

                    const size_t destId         = numParameters;
                    parameters[numParameters++] = parameter;
                    return make_expected<size_t, ParamRegistrationError> (destId);
                }

                /*==============================================================
                 * GUI / any-thread API - enqueues only, never mutates routing state
                 *============================================================*/

                /**
                 * @brief Enqueue a routing for insertion on the next process() call.
                 *
                 * May be called from any thread. The routing is dispatched to
                 * linearRoutings or nonLinearRoutings according to routing.curve
                 * when the command is dequeued by the audio thread.
                 *
                 * Depth is clamped to [-1, 1] here so the audio-thread accumulation
                 * loop is unconditional.
                 *
                 * @param routing  Routing to add. depth is clamped to [-1, 1].
                 */
                void addRouting (const ModulationRouting<FloatType>& routing) CASPI_NON_BLOCKING
                {
                    auto r  = routing;
                    r.depth = Maths::clamp (r.depth, FloatType (-1), FloatType (1));
                    pendingCommands.enqueue (producerToken,{ CommandType::AddRouting, r, 0, false });
                }

                /**
                 * @brief Enqueue removal of a routing by index, with the target list
                 *        selected at compile time via the Curve template parameter.
                 *
                 * May be called from any thread. The list targeted is determined
                 * entirely by the Curve template argument, not a runtime flag.
                 *
                 * When Curve == ModulationCurve::Linear (the default), the command
                 * targets linearRoutings. Any other value targets nonLinearRoutings.
                 *
                 * @code
                 *   matrix.removeRouting(0);                              // Linear (default)
                 *   matrix.removeRouting<ModulationCurve::Exponential>(2);
                 * @endcode
                 *
                 * @tparam Curve  Compile-time curve selector. Defaults to Linear.
                 * @param index   Zero-based position within the target list.
                 */
                template <ModulationCurve Curve = ModulationCurve::Linear>
                void removeRouting (size_t index) CASPI_NON_BLOCKING
                {
                    constexpr CommandType t =
                        detail::IsLinearCurve<Curve>::value ? CommandType::RemoveLinear : CommandType::RemoveNonLinear;
                    pendingCommands.enqueue (producerToken,{ t, {}, index, false });
                }

                /**
                 * @brief Enqueue a command to clear all routings from both lists.
                 *
                 * May be called from any thread.
                 * Effect is applied at the start of the next process() call.
                 */
                void clearRoutings() CASPI_NON_BLOCKING
                {
                    pendingCommands.enqueue (producerToken,{ CommandType::ClearRoutings, {}, 0, false });
                }

                /**
                 * @brief Enqueue a change to the enabled state of a routing, with the
                 *        target list selected at compile time via the Curve template parameter.
                 *
                 * May be called from any thread.
                 *
                 * @code
                 *   matrix.setRoutingEnabled(0, true);                             // Linear (default)
                 *   matrix.setRoutingEnabled<ModulationCurve::SCurve>(1, false);
                 * @endcode
                 *
                 * @tparam Curve    Compile-time curve selector. Defaults to Linear.
                 * @param index     Zero-based position within the target list.
                 * @param enabled   New enabled state for the routing at index.
                 */
                template <ModulationCurve Curve = ModulationCurve::Linear>
                void setRoutingEnabled (size_t index, bool enabled) CASPI_NON_BLOCKING
                {
                    constexpr CommandType t = detail::IsLinearCurve<Curve>::value ? CommandType::SetEnabledLinear
                                                                                  : CommandType::SetEnabledNonLinear;
                    pendingCommands.enqueue (producerToken,{ t, {}, index, enabled });
                }

                /*==============================================================
                 * Source values - audio thread only
                 *============================================================*/

                /**
                 * @brief Write a modulation source value.
                 *
                 * Must be called from the audio thread only.
                 * Silently ignored if sourceId >= kMaxModSources.
                 *
                 * @param sourceId  Index into sourceValues[]. Range: [0, kMaxModSources).
                 * @param value     Source value, typically in [-1, 1] or [0, 1].
                 */
                void setSourceValue (size_t sourceId, FloatType value) noexcept CASPI_NON_BLOCKING
                {
                    if (sourceId < kMaxModSources)
                    {
                        sourceValues[sourceId] = value;
                    }
                }

                /**
                 * @brief Read a modulation source value.
                 *
                 * Must be called from the audio thread only.
                 *
                 * @param sourceId  Index into sourceValues[]. Range: [0, kMaxModSources).
                 * @return          Stored source value, or FloatType(0) if out of range.
                 */
                CASPI_NO_DISCARD FloatType getSourceValue (size_t sourceId) const noexcept CASPI_NON_BLOCKING
                {
                    if (sourceId < kMaxModSources)
                    {
                        return sourceValues[sourceId];
                    }

                    return FloatType (0);
                }

                /*==============================================================
                 * Observers - audio thread only, reflect state after last process()
                 *============================================================*/

                /**
                 * @brief Return the number of registered parameters.
                 *
                 * @return Count of successfully registered ModulatableParameter pointers.
                 */
                CASPI_NO_DISCARD size_t getNumParameters() const noexcept CASPI_NON_BLOCKING
                {
                    return numParameters;
                }

                /**
                 * @brief Return the total number of active routings across both lists.
                 *
                 * @return linearRoutings.size() + nonLinearRoutings.size().
                 */
                CASPI_NO_DISCARD size_t getNumRoutings() const noexcept CASPI_NON_BLOCKING
                {
                    return linearRoutings.size() + nonLinearRoutings.size();
                }

                /**
                 * @brief Return the number of linear routings.
                 *
                 * @return Count of routings in linearRoutings.
                 */
                CASPI_NO_DISCARD size_t getNumLinearRoutings() const noexcept CASPI_NON_BLOCKING
                {
                    return linearRoutings.size();
                }

                /**
                 * @brief Return the number of non-linear routings.
                 *
                 * @return Count of routings in nonLinearRoutings.
                 */
                CASPI_NO_DISCARD size_t getNumNonLinearRoutings() const noexcept CASPI_NON_BLOCKING
                {
                    return nonLinearRoutings.size();
                }

                /*==============================================================
                 * Audio thread - main process
                 *============================================================*/

                /**
                 * @brief Process all modulation routings for one audio block.
                 *
                 * Must be called from the audio thread only, once per block.
                 *
                 * Execution order:
                 *   1. drainCommands()       - apply pending GUI mutations
                 *   2. accumulateLinear()    - zero accum, FMA scatter-accumulate
                 *   3. accumulateNonLinear() - scalar curved accumulation
                 *   4. scatterToParameters() - SIMD clamp, push to parameter objects
                 */
                void process() noexcept CASPI_NON_BLOCKING
                {
                    drainCommands();
                    accumulateLinear();
                    accumulateNonLinear();
                    scatterToParameters();
                }

                /**
                 * @brief Zero all source values and clear modulation on all parameters.
                 *
                 * Routings are preserved. Intended for transport stop or voice reset.
                 * Must be called from the audio thread only.
                 *
                 * Uses ops::fill which selects NT stores when the buffer exceeds
                 * the runtime L1 threshold.
                 */
                void reset() noexcept CASPI_NON_BLOCKING
                {
                    SIMD::ops::fill (sourceValues.data(), kMaxModSources, FloatType (0));
                    SIMD::ops::fill (modulationAccum.data(), numParameters, FloatType (0));

                    for (size_t i = 0; i < numParameters; ++i)
                    {
                        parameters[i]->clearModulation();
                    }
                }

            private:
                /*==============================================================
                 * Process steps (audio thread only)
                 *============================================================*/

                /**
                 * @brief Drain the pending command queue and apply each mutation.
                 *
                 * Called at the top of process() before any accumulation.
                 * try_dequeue is wait-free on the consumer side.
                 */
                void drainCommands() noexcept CASPI_NON_BLOCKING
                {
                    Command cmd;

                    while (pendingCommands.try_dequeue (cmd))
                    {
                        applyCommand (cmd);
                    }
                }

                /**
                 * @brief Zero the accumulator array then scatter-accumulate linear routings.
                 *
                 * Inner loop: accum[dst] += source[src] * depth
                 *
                 * FMA-eligible. Under /arch:AVX2 (MSVC) or -mavx2 -mfma (GCC/Clang)
                 * the compiler emits vfmadd231ss. Routings are sorted by destinationId
                 * so adjacent iterations tend to write to the same or neighbouring
                 * cache lines, keeping modulationAccum[] hot in L1.
                 *
                 * Indirect scatter with non-unit-stride indices cannot use
                 * block_op_ternary directly. Auto-vectorisation across sorted
                 * adjacent destinations is more effective than hand-rolled
                 * gather/scatter intrinsics for this access pattern.
                 */
                void accumulateLinear() noexcept CASPI_NON_BLOCKING
                {
                    Core::ScopedFlushDenormals flush;

                    SIMD::ops::fill (modulationAccum.data(), numParameters, FloatType (0));

                    const FloatType* CASPI_RESTRICT sources = sourceValues.data();
                    FloatType* CASPI_RESTRICT accum         = modulationAccum.data();

                    const auto* CASPI_RESTRICT routings = linearRoutings.begin();
                    const auto* CASPI_RESTRICT end      = linearRoutings.end();

                    for (; routings != end; ++routings)
                    {
                        if (! routings->enabled)
                        {
                            continue;
                        }

                        const size_t src = routings->sourceId;
                        const size_t dst = routings->destinationId;

                        CASPI_RT_ASSERT (src < kMaxModSources);
                        CASPI_RT_ASSERT (dst < numParameters);

                        accum[dst] += sources[src] * routings->depth;
                    }
                }

                /**
                 * @brief Scalar accumulation pass for non-linear (curve-shaped) routings.
                 *
                 * Adds curve-shaped, depth-scaled source values into the same
                 * modulationAccum[] array written by accumulateLinear().
                 * Non-linear routings are expected to be a minority in practice.
                 */
                void accumulateNonLinear() noexcept CASPI_NON_BLOCKING
                {
                    Core::ScopedFlushDenormals flush;

                    const FloatType* CASPI_RESTRICT sources = sourceValues.data();
                    FloatType* CASPI_RESTRICT accum         = modulationAccum.data();

                    const auto* CASPI_RESTRICT routings = nonLinearRoutings.begin();
                    const auto* CASPI_RESTRICT end      = nonLinearRoutings.end();

                    for (; routings != end; ++routings)
                    {
                        if (! routings->enabled)
                        {
                            continue;
                        }

                        const size_t src = routings->sourceId;
                        const size_t dst = routings->destinationId;

                        CASPI_RT_ASSERT (src < kMaxModSources);
                        CASPI_RT_ASSERT (dst < numParameters);

                        const FloatType curved  = routings->applyCurve (sources[src]);
                        accum[dst]             += curved * routings->depth;
                    }
                }

                /**
                 * @brief SIMD clamp the accumulator then scatter values to parameters.
                 *
                 * ops::clamp applies ClampKernel over the contiguous accumulator array,
                 * processing 4 or 8 floats per cycle (SSE / AVX). The subsequent
                 * parameter scatter performs one pointer dereference per parameter,
                 * not per routing.
                 */
                void scatterToParameters() noexcept CASPI_NON_BLOCKING
                {
                    SIMD::ops::clamp (modulationAccum.data(), FloatType (-1), FloatType (1), numParameters);

                    for (size_t i = 0; i < numParameters; ++i)
                    {
                        parameters[i]->clearModulation();
                        parameters[i]->addModulation (modulationAccum[i]);
                    }
                }

                /**
                 * @brief Apply a single command dequeued from pendingCommands.
                 *
                 * Called exclusively from drainCommands() on the audio thread.
                 *
                 * @param cmd  Command to execute.
                 */
                void applyCommand (const Command& cmd) noexcept CASPI_NON_BLOCKING
                {
                    switch (cmd.type)
                    {
                        case CommandType::AddRouting:
                        {
                            const auto& r = cmd.routing;

                            if (r.sourceId >= kMaxModSources || r.destinationId >= numParameters)
                            {
                                break;
                            }

                            if (r.curve == ModulationCurve::Linear)
                            {
                                linearRoutings.insert (r);
                            }
                            else
                            {
                                nonLinearRoutings.insert (r);
                            }

                            break;
                        }

                        case CommandType::RemoveLinear:
                        {
                            linearRoutings.removeAt (cmd.index);
                            break;
                        }

                        case CommandType::RemoveNonLinear:
                        {
                            nonLinearRoutings.removeAt (cmd.index);
                            break;
                        }

                        case CommandType::ClearRoutings:
                        {
                            linearRoutings.clear();
                            nonLinearRoutings.clear();
                            break;
                        }

                        case CommandType::SetEnabledLinear:
                        {
                            if (cmd.index < linearRoutings.size())
                            {
                                linearRoutings[cmd.index].enabled = cmd.enabled;
                            }

                            break;
                        }

                        case CommandType::SetEnabledNonLinear:
                        {
                            if (cmd.index < nonLinearRoutings.size())
                            {
                                nonLinearRoutings[cmd.index].enabled = cmd.enabled;
                            }

                            break;
                        }
                    }
                }

                /*==============================================================
                 * Data members
                 *============================================================*/

                /**
                 * @brief Lock-free command queue bridging any thread to the audio thread.
                 *
                 * Producers: any thread calling addRouting, removeRouting, etc.
                 * Consumer:  audio thread inside drainCommands().
                 */
                CASPI::external::ConcurrentQueue<Command> pendingCommands { 2048 };

                CASPI::external::ProducerToken producerToken { pendingCommands };

                /**
                 * @brief Source values written by the audio thread each block.
                 *
                 * Aligned to the SIMD boundary for potential future block-load operations.
                 * Indexed directly by ModulationRouting::sourceId.
                 */
                alignas (
                    SIMD::Strategy::simd_alignment<FloatType>()) std::array<FloatType, kMaxModSources> sourceValues {};

                /**
                 * @brief Flat per-parameter modulation accumulator.
                 *
                 * Zeroed at the start of each block by accumulateLinear(),
                 * filled by both accumulation passes, SIMD-clamped to [-1, 1],
                 * then scattered to parameter objects in scatterToParameters().
                 * Indexed by destination ID, which equals the parameters[] index.
                 */
                alignas (SIMD::Strategy::simd_alignment<FloatType>())
                    std::array<FloatType, kMaxModParams> modulationAccum {};

                /**
                 * @brief Non-owning pointers to registered ModulatableParameter instances.
                 *
                 * Populated during setup via registerParameter().
                 * Lifetime of pointed-to objects is the caller's responsibility.
                 */
                std::array<Core::ModulatableParameter<FloatType>*, kMaxModParams> parameters {};

                /**
                 * @brief Number of successfully registered parameters.
                 *
                 * Valid index range for parameters[] and modulationAccum[] is [0, numParameters).
                 */
                size_t numParameters = 0;

                /**
                 * @brief Fixed-capacity sorted list of linear (curve == Linear) routings.
                 *
                 * Processed in accumulateLinear() via the FMA-eligible scatter loop.
                 * Stack footprint: kMaxModRoutings x sizeof(ModulationRouting<float>) = 24KB.
                 */
                RoutingList<FloatType, kMaxModRoutings> linearRoutings;

                /**
                 * @brief Fixed-capacity sorted list of non-linear (curved) routings.
                 *
                 * Processed in accumulateNonLinear() via the scalar path.
                 * Stack footprint: kMaxModRoutings x sizeof(ModulationRouting<float>) = 24KB.
                 */
                RoutingList<FloatType, kMaxModRoutings> nonLinearRoutings;
        };

    } /* namespace Controls */
} /* namespace CASPI */

#endif /* CASPI_MODULATION_MATRIX_H */