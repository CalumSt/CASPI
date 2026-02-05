#ifndef CASPI_FMGRAPH_H
#define CASPI_FMGRAPH_H
/*************************************************************************
 * @file caspi_FMGraph.h
 * @brief FM synthesis graph with builder/runtime separation
 *
 * ARCHITECTURE OVERVIEW:
 *
 *   FMGraphBuilder (Mutable, Non-RT, Single-threaded)
 *        |
 *        v
 *   FMGraphDSP (Immutable topology, RT-safe rendering)
 *
 * FMGraphBuilder:
 *  - Intended for configuration and validation only
 *  - May allocate, resize, and throw
 *  - Not real-time safe
 *  - Not thread-safe
 *
 * FMGraphDSP:
 *  - Graph topology is immutable after construction
 *  - Rendering is real-time safe (no allocation, no locks)
 *  - Runtime parameters may be updated, but:
 *      * The class is NOT thread-safe
 *      * All mutation is assumed to occur on the audio thread
 *
 * DESIGN GOALS:
 *  1. Catch configuration errors before real-time use
 *  2. Guarantee bounded execution in render paths
 *  3. Make real-time assumptions explicit and auditable
 ************************************************************************/


#include "base/caspi_Compatibility.h"
#include "core/caspi_Core.h"
#include "core/caspi_Expected.h"
#include "oscillators/caspi_Operator.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <vector>

namespace CASPI
{

    // ============================================================================
    // Error Types
    // ============================================================================

    /**
     * @brief Errors reported during graph construction and compilation
     *
     * NOTE:
     * This is a CASPI-specific error domain and is used with the
     * CASPI expected<> type (not std::expected).
     */
    enum class FMGraphError
    {
        Success = 0,
        InvalidOperatorIndex,
        CycleDetected,
        InvalidConnection,
        NoOutputOperators,
        AllocationFailure,
        GraphNotCompiled
    };

    inline const char* errorToString (FMGraphError error)
    {
        switch (error)
        {
            case FMGraphError::Success:
                return "Success";
            case FMGraphError::InvalidOperatorIndex:
                return "Invalid operator index";
            case FMGraphError::CycleDetected:
                return "Cycle detected in graph";
            case FMGraphError::InvalidConnection:
                return "Invalid connection (self-modulation)";
            case FMGraphError::NoOutputOperators:
                return "No output operators specified";
            case FMGraphError::AllocationFailure:
                return "Memory allocation failure";
            case FMGraphError::GraphNotCompiled:
                return "Graph not compiled";
            default:
                return "Unknown error";
        }
    }

    // ============================================================================
    // Connection Representation
    // ============================================================================

    /**
     * @brief Directed modulation connection between two operators
     *
     * sourceOperator → targetOperator
     *
     * modulationDepth is applied at render time and may be updated
     * at runtime. No atomicity or cross-thread safety is guaranteed.
     */
    struct ModulationConnection
    {
            size_t sourceOperator;
            size_t targetOperator;
            float modulationDepth;

            bool operator== (const ModulationConnection& other) const
            {
                return sourceOperator == other.sourceOperator
                       && targetOperator == other.targetOperator;
            }
    };

    /**
     * @brief Builder-side operator configuration
     *
     * This struct exists only during graph construction and is copied
     * into the DSP object during compilation.
     */
    template <typename FloatType>
    struct OperatorConfig
    {
            FloatType frequency;
            FloatType modulationIndex;
            FloatType modulationDepth;
            FloatType modulationFeedback;
            ModulationMode modulationMode;
    };

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    template <typename FloatType>
    class FMGraphDSP;

    // ============================================================================
    // FMGraphBuilder
    // ============================================================================

    /**
     * @class FMGraphBuilder
     * @brief Mutable graph builder for FM synthesis topologies
     *
     * THREADING / RT NOTES:
     *  - Not real-time safe
     *  - Not thread-safe
     *  - Intended for offline or control-thread use only
     *
     * The builder validates graph structure and produces an
     * FMGraphDSP instance with an immutable topology.
     */
    template <typename FloatType>
    class FMGraphBuilder
    {
        public:
            using Error  = FMGraphError;
            using Result = expected<void, Error, NonRealTimeSafe>;

            template <typename T>
            using ResultValue = expected<T, Error, NonRealTimeSafe>;

            FMGraphBuilder() = default;

            // ====================================================================
            // Graph Construction
            // ====================================================================

            /**
             * @brief Add a new operator with default parameters
             * @return Index of the newly added operator
             */
            size_t addOperator()
            {
                OperatorConfig<FloatType> config;
                config.frequency          = FloatType (440);
                config.modulationIndex    = FloatType (1);
                config.modulationDepth    = FloatType (1);
                config.modulationFeedback = FloatType (0);
                config.modulationMode     = ModulationMode::Phase;

                operators_.push_back (config);
                return operators_.size() - 1;
            }

            /**
             * @brief Remove an operator and all associated connections
             */
            Result removeOperator (const size_t operatorIndex)
            {
                if (operatorIndex >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::InvalidOperatorIndex);
                }

                operators_.erase (operators_.begin() + operatorIndex);

                connections_.erase (
                    std::remove_if (connections_.begin(),
                                    connections_.end(),
                                    [operatorIndex] (const ModulationConnection& conn)
                                    {
                                        return conn.sourceOperator == operatorIndex
                                               || conn.targetOperator == operatorIndex;
                                    }),
                    connections_.end());

                for (auto& conn : connections_)
                {
                    if (conn.sourceOperator > operatorIndex)
                    {
                        --conn.sourceOperator;
                    }
                    if (conn.targetOperator > operatorIndex)
                    {
                        --conn.targetOperator;
                    }
                }

                outputOperators_.erase (
                    std::remove (outputOperators_.begin(),
                                 outputOperators_.end(),
                                 operatorIndex),
                    outputOperators_.end());

                for (auto& outIdx : outputOperators_)
                {
                    if (outIdx > operatorIndex)
                    {
                        --outIdx;
                    }
                }

                return {};
            }

            /**
             * @brief Connect source operator to target operator
             *
             * Self-modulation is explicitly disallowed.
             */
            Result connect (const size_t source,
                            const size_t target,
                            FloatType modulationDepth)
            {
                if (source >= operators_.size() || target >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::InvalidOperatorIndex);
                }

                if (source == target)
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::InvalidConnection);
                }

                const ModulationConnection conn {
                    source,
                    target,
                    static_cast<float> (modulationDepth)
                };

                auto it = std::find (connections_.begin(),
                                     connections_.end(),
                                     conn);

                if (it != connections_.end())
                {
                    it->modulationDepth = conn.modulationDepth;
                }
                else
                {
                    connections_.push_back (conn);
                }

                return {};
            }

            /**
             * @brief Remove a connection between two operators
             */
            Result disconnect (const size_t source, const size_t target)
            {
                const ModulationConnection conn { source, target, 0.0f };

                connections_.erase (
                    std::remove (connections_.begin(),
                                 connections_.end(),
                                 conn),
                    connections_.end());

                return {};
            }

            /**
             * @brief Define which operators contribute to final output
             */
            Result setOutputOperators (const std::vector<size_t>& indices)
            {
                for (size_t idx : indices)
                {
                    if (idx >= operators_.size())
                    {
                        return make_unexpected<Error, NonRealTimeSafe> (
                            Error::InvalidOperatorIndex);
                    }
                }

                outputOperators_ = indices;
                return {};
            }

            // ====================================================================
            // Operator Configuration
            // ====================================================================

            Result configureOperator (size_t index,
                                      FloatType frequency,
                                      FloatType modulationIndex,
                                      FloatType modulationDepth)
            {
                if (index >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::InvalidOperatorIndex);
                }

                operators_[index].frequency       = frequency;
                operators_[index].modulationIndex = modulationIndex;
                operators_[index].modulationDepth = modulationDepth;

                return {};
            }

            Result setOperatorMode (size_t index, ModulationMode mode)
            {
                if (index >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::InvalidOperatorIndex);
                }

                operators_[index].modulationMode = mode;
                return {};
            }

            // ====================================================================
            // Validation
            // ====================================================================

            /**
             * @brief Validate graph topology
             *
             * Checks:
             *  - At least one output operator
             *  - No cycles (DAG requirement)
             */
            CASPI_NO_DISCARD
            Result validate() const
            {
                const size_t n = operators_.size();

                if (n == 0)
                {
                    return {};
                }

                if (outputOperators_.empty())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::NoOutputOperators);
                }

                std::vector<int> inDegree (n, 0);
                std::vector<std::vector<size_t>> adjacencyList (n);

                for (const auto& conn : connections_)
                {
                    adjacencyList[conn.sourceOperator].push_back (
                        conn.targetOperator);
                    ++inDegree[conn.targetOperator];
                }

                std::queue<size_t> queue;
                for (size_t i = 0; i < n; ++i)
                {
                    if (inDegree[i] == 0)
                    {
                        queue.push (i);
                    }
                }

                size_t processed = 0;
                while (! queue.empty())
                {
                    const size_t current = queue.front();
                    queue.pop();
                    ++processed;

                    for (size_t neighbor : adjacencyList[current])
                    {
                        if (--inDegree[neighbor] == 0)
                        {
                            queue.push (neighbor);
                        }
                    }
                }

                if (processed != n)
                {
                    return make_unexpected<Error, NonRealTimeSafe> (
                        Error::CycleDetected);
                }

                return {};
            }

            // ====================================================================
            // Compilation
            // ====================================================================

            /**
             * @brief Compile into an immutable FMGraphDSP instance
             *
             * Allocates memory and must not be called on a real-time thread.
             */
            ResultValue<FMGraphDSP<FloatType>>
                compile (const FloatType sampleRate) const
            {
                auto validationResult = validate();
                if (! validationResult.has_value())
                {
                    return make_unexpected<FMGraphDSP<FloatType>,
                                           Error,
                                           NonRealTimeSafe> (
                        validationResult.error());
                }

                try
                {
                    return ResultValue<FMGraphDSP<FloatType>> (
                        in_place,
                        operators_,
                        connections_,
                        outputOperators_,
                        sampleRate);
                }
                catch (...)
                {
                    return make_unexpected<FMGraphDSP<FloatType>,
                                           Error,
                                           NonRealTimeSafe> (
                        Error::AllocationFailure);
                }
            }

            // ====================================================================
            // Inspection
            // ====================================================================

            CASPI_NON_BLOCKING CASPI_NO_DISCARD
            size_t getNumOperators() const { return operators_.size(); }

            CASPI_NON_BLOCKING CASPI_NO_DISCARD
            const std::vector<ModulationConnection>& getConnections() const { return connections_; }

            CASPI_NON_BLOCKING CASPI_NO_DISCARD const std::vector<size_t>&
                getOutputOperators() const { return outputOperators_; }

        private:
            std::vector<OperatorConfig<FloatType>> operators_;
            std::vector<ModulationConnection> connections_;
            std::vector<size_t> outputOperators_;
    };

    /**
     * @class FMGraphDSP
     * @brief Immutable-topology FM synthesis engine
     *
     * OVERVIEW:
     *  FMGraphDSP is the real-time rendering object produced by FMGraphBuilder.
     *  It owns all operator DSP instances and executes them in a precomputed,
     *  acyclic order derived from the modulation graph.
     *
     * IMMUTABILITY MODEL:
     *  - Graph topology (operators, connections, execution order) is immutable
     *    after construction.
     *  - Operator DSP state (phase, envelopes, feedback, etc.) is mutable.
     *  - Runtime parameters (frequency, modulation depth, index, feedback)
     *    may be updated after construction.
     *
     * REAL-TIME SAFETY:
     *  - renderSample() and renderBlock():
     *      * perform no dynamic allocation
     *      * perform no locking
     *      * perform no system calls
     *      * execute in bounded time proportional to operator count
     *  - Suitable for hard real-time audio threads
     *
     * THREAD SAFETY (IMPORTANT):
     *  - This class is NOT thread-safe
     *  - No internal synchronization is provided
     *  - All mutation (parameter updates, resets, rendering) is assumed
     *    to occur on the same audio thread
     *  - Concurrent access from control and audio threads is undefined behavior
     *
     * NUMERICAL NOTES:
     *  - FloatType is assumed to be float or double
     *  - No atomic operations are used on floating-point values
     *  - Denormal handling is the responsibility of the caller or platform
     *
     * SIMD / OPTIMIZATION NOTES:
     *  - Current implementation is scalar
     *  - Internal layout is intentionally contiguous to allow future SIMD
     *    or block-based vectorization without changing public API
     *
     * ERROR HANDLING:
     *  - Construction is expected to be validated by FMGraphBuilder
     *  - Runtime rendering functions do not report errors
     */
    template <typename FloatType>
    class FMGraphDSP : public Core::Producer<FloatType, Core::Traversal::PerFrame>,
                       public Core::SampleRateAware<FloatType>
    {
        public:
            /**
             * @brief Constructs an FMGraphDSP from a validated modulation graph.
             *
             * Allocates and initializes operator DSP instances, precomputes execution
             * order, and prepares all internal buffers required for real-time rendering.
             *
             * @param operatorConfigs Operator definitions and initial parameters.
             * @param connections Modulation connections between operators.
             * @param outputOperators Indices of operators whose outputs are mixed to final output.
             * @param sampleRate Initial sample rate in Hz.
             */
            FMGraphDSP (const std::vector<OperatorConfig<FloatType>>& operatorConfigs,
                        const std::vector<ModulationConnection>& connections,
                        const std::vector<size_t>& outputOperators,
                        FloatType sampleRate)
                : connections_ (connections),
                  outputOperators_ (outputOperators),
                  baseFrequency_ (FloatType (440)),
                  outputGain_ (FloatType (1)),
                  autoScaleOutputs_ (true)
            {
                const size_t n = operatorConfigs.size();

                operators_.reserve (n);
                for (const auto& config : operatorConfigs)
                {

                    auto op = CASPI::make_unique<Operator<FloatType>>(
                        sampleRate,
                        config.frequency,
                        config.modulationIndex,
                        config.modulationDepth,
                        config.modulationFeedback,
                        config.modulationMode
                    );

                    operators_.push_back (std::move (op));
                }

                modulationSignals_.resize (n, FloatType (0));
                operatorOutputs_.resize (n, FloatType (0));

                computeExecutionOrder();
                buildAdjacencyList();
                updateEffectiveGain();

                this->setSampleRate (sampleRate);
            }

            FMGraphDSP (const FMGraphDSP&)            = delete;
            FMGraphDSP& operator= (const FMGraphDSP&) = delete;
            FMGraphDSP (FMGraphDSP&&)                 = default;
            FMGraphDSP& operator= (FMGraphDSP&&)      = default;

            /**
             * @brief Returns a mutable pointer to an operator by index.
             *
             * Used for real-time parameter control of individual operators.
             *
             * @param index Operator index.
             * @return Pointer to operator, or nullptr if index is invalid.
             */
            CASPI_NON_BLOCKING CASPI_NO_DISCARD
                Operator<FloatType>*
                getOperator (const size_t index)
            {
                return (index < operators_.size()) ? operators_[index].get() : nullptr;
            }

            /**
             * @brief Returns a const pointer to an operator by index.
             *
             * @param index Operator index.
             * @return Const pointer to operator, or nullptr if index is invalid.
             */
            CASPI_NON_BLOCKING CASPI_NO_DISCARD
            const Operator<FloatType>* getOperator (const size_t index) const
            {
                return (index < operators_.size()) ? operators_[index].get() : nullptr;
            }

            /**
             * @brief Sets the base frequency applied to all operators.
             *
             * @param frequency Base frequency in Hz.
             */
            CASPI_NON_BLOCKING
            void setFrequency (const FloatType frequency)
            {
                baseFrequency_ = frequency;
                for (auto& op : operators_)
                {
                    op->setFrequency (frequency);
                }
            }

            /**
             * @brief Returns the current base frequency.
             *
             * @return Base frequency in Hz.
             */
            CASPI_NON_BLOCKING
            FloatType getFrequency() const
            {
                return baseFrequency_;
            }

            /**
             * @brief Updates the modulation depth of a connection.
             *
             * @param connectionIndex Index of the modulation connection.
             * @param depth New modulation depth value.
             */
            CASPI_NON_BLOCKING
            void setConnectionDepth (const size_t connectionIndex, const FloatType depth)
            {
                if (connectionIndex >= connections_.size())
                {
                    return;
                }

                const float depthFloat = static_cast<float> (depth);

                connections_[connectionIndex].modulationDepth = depthFloat;
                outgoingDepths_[connectionIndexToFlatIndex_[connectionIndex]] = depthFloat;
            }

        /**
 * @brief Updates the modulation depth between two operators.
 *
 * Alternative interface for updating depth by operator indices.
 * Requires O(n) search where n is the number of outgoing connections
 * from the source operator.
 *
 * @param sourceOperator Source operator index.
 * @param targetOperator Target operator index.
 * @param depth New modulation depth value.
 */
        CASPI_NON_BLOCKING
        void setModulationDepth (const size_t sourceOperator,
                                 const size_t targetOperator,
                                 const FloatType depth)
            {
                if (sourceOperator >= operators_.size())
                {
                    return;
                }

                const size_t start = outgoingOffsets_[sourceOperator];
                const size_t end = outgoingOffsets_[sourceOperator + 1];

                for (size_t i = start; i < end; ++i)
                {
                    if (outgoingTargets_[i] == targetOperator)
                    {
                        outgoingDepths_[i] = static_cast<float> (depth);

                        // Also update connections_ array for consistency
                        for (auto& conn : connections_)
                        {
                            if (conn.sourceOperator == sourceOperator
                                && conn.targetOperator == targetOperator)
                            {
                                conn.modulationDepth = static_cast<float> (depth);
                                break;
                            }
                        }
                        return;
                    }
                }
            }


            /**
             * @brief Triggers note-on for all operators.
             */
            CASPI_NON_BLOCKING
            void noteOn()
            {
                for (auto& op : operators_)
                {
                    op->noteOn();
                }
            }

            /**
             * @brief Triggers note-off for all operators.
             */
            CASPI_NON_BLOCKING
            void noteOff()
            {
                for (auto& op : operators_)
                {
                    op->noteOff();
                }
            }


            /**
             * @brief Sets the final output gain.
             *
             * @param gain Linear gain multiplier.
             */
            CASPI_NON_BLOCKING
            void setOutputGain (const FloatType gain)
            {
                outputGain_ = gain;
                updateEffectiveGain();
            }

            /**
             * @brief Returns the current output gain.
             *
             * @return Output gain value.
             */
            CASPI_NON_BLOCKING CASPI_NO_DISCARD
                FloatType
                getOutputGain() const
            {
                return outputGain_;
            }

            /**
             * @brief Enables or disables automatic output scaling.
             *
             * @param enable True to enable scaling, false to disable.
             */
            CASPI_NON_BLOCKING
            void setAutoScaleOutputs (const bool enable)
            {
                autoScaleOutputs_ = enable;
                updateEffectiveGain();
            }

            /**
             * @brief Returns whether automatic output scaling is enabled.
             *
             * @return True if enabled.
             */
            CASPI_NON_BLOCKING CASPI_NO_DISCARD
        bool getAutoScaleOutputs() const
            {
                return autoScaleOutputs_;
            }

            /**
             * @brief Resets all operator state and clears modulation buffers.
             */
            CASPI_NON_BLOCKING
            void reset()
            {
                for (auto& op : operators_)
                {
                    op->reset();
                }

                std::fill (modulationSignals_.begin(),
                           modulationSignals_.end(),
                           FloatType (0));

                std::fill (operatorOutputs_.begin(),
                           operatorOutputs_.end(),
                           FloatType (0));
            }

            /**
             * @brief Renders a single audio sample.
             *
             * Executes operators in precomputed topological order and propagates
             * modulation signals through the graph.
             *
             * @return Rendered audio sample.
             */
            CASPI_NON_BLOCKING CASPI_NO_DISCARD
            FloatType renderSample() override
            {
                Core::ScopedFlushDenormals flush{};

                std::fill (modulationSignals_.begin(),
                           modulationSignals_.end(),
                           FloatType (0));

                for (size_t opIndex : executionOrder_)
                {
                    operators_[opIndex]->setModulationInput (modulationSignals_[opIndex]);

                    operatorOutputs_[opIndex] = operators_[opIndex]->renderSample();

                    // Propagate output to all connected operators
                    const size_t start = outgoingOffsets_[opIndex];
                    const size_t end = outgoingOffsets_[opIndex + 1];

                    for (size_t i = start; i < end; ++i)
                    {
                        modulationSignals_[outgoingTargets_[i]] +=
                            operatorOutputs_[opIndex] * static_cast<FloatType> (outgoingDepths_[i]);
                    }
                }

                if (outputOperators_.empty())
                {
                    return FloatType (0);
                }

                // Mix output operators
                FloatType finalOutput = FloatType (0);
                for (size_t outIdx : outputOperators_)
                {
                    finalOutput += operatorOutputs_[outIdx];
                }

                // Apply pre-computed effective gain
                finalOutput *= effectiveGain_;

                return finalOutput;
            }

            /**
             * @brief Renders a block of audio samples.
             *
             * @param buffer Output buffer.
             * @param numSamples Number of samples to render.
             */
            CASPI_NON_BLOCKING
            void renderBlock (FloatType* buffer, const size_t numSamples)
            {
                for (size_t i = 0; i < numSamples; ++i)
                {
                    buffer[i] = renderSample();
                }
            }

            /**
             * @brief Producer interface override for per-frame rendering.
             *
             * @param channel Channel index (ignored).
             * @param frame Frame index (ignored).
             * @return Rendered audio sample.
             */
            CASPI_NO_DISCARD CASPI_NON_BLOCKING
                FloatType
                renderSample (const std::size_t channel,
                              const std::size_t frame) override
            {
                (void) channel;
                (void) frame;
                return renderSample();
            }

            /**
             * @brief Returns the number of operators in the graph.
             *
             * @return Operator count.
             */
            CASPI_NON_ALLOCATING CASPI_NO_DISCARD
            size_t getNumOperators() const
            {
                return operators_.size();
            }

            /**
            * @brief Returns the output operator indices.
            *
            * @return Reference to output operators vector.
            */
            CASPI_NON_ALLOCATING CASPI_NO_DISCARD
            const std::vector<size_t>& getOutputOperators() const
            {
                return outputOperators_;
            }


            /**
             * @brief Returns the execution order used for rendering.
             *
             * @return Pointer to execution order vector.
             */
            CASPI_NON_ALLOCATING CASPI_NO_DISCARD
            const std::vector<size_t>& getExecutionOrder() const
            {
                return executionOrder_;
            }

        private:
            /**
             * @brief Computes a topological execution order for the modulation graph.
             */
        void computeExecutionOrder()
        {
            const size_t n = operators_.size();
            if (n == 0)
            {
                return;
            }

            std::vector<int> inDegree (n, 0);
            std::vector<std::vector<size_t>> adjacencyList (n);

            for (const auto& conn : connections_)
            {
                adjacencyList[conn.sourceOperator].push_back (
                    conn.targetOperator);
                ++inDegree[conn.targetOperator];
            }

            std::queue<size_t> queue;
            for (size_t i = 0; i < n; ++i)
            {
                if (inDegree[i] == 0)
                {
                    queue.push (i);
                }
            }

            executionOrder_.clear();
            executionOrder_.reserve (n);

            while (! queue.empty())
            {
                const size_t current = queue.front();
                queue.pop();
                executionOrder_.push_back (current);

                for (size_t neighbor : adjacencyList[current])
                {
                    if (--inDegree[neighbor] == 0)
                    {
                        queue.push (neighbor);
                    }
                }
            }
        }

            /**
             * @brief Builds an adjacency list for fast modulation routing during rendering.
             */
            void buildAdjacencyList()
            {
                const size_t n = operators_.size();
                if (n == 0)
                {
                    return;
                }

                // Count outgoing connections per operator
                std::vector<size_t> counts (n, 0);
                for (const auto& conn : connections_)
                {
                    ++counts[conn.sourceOperator];
                }

                // Build offset array (prefix sum)
                outgoingOffsets_.resize (n + 1);
                outgoingOffsets_[0] = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    outgoingOffsets_[i + 1] = outgoingOffsets_[i] + counts[i];
                }

                // Allocate flat arrays
                const size_t totalConnections = connections_.size();
                outgoingTargets_.resize (totalConnections);
                outgoingDepths_.resize (totalConnections);
                connectionIndexToFlatIndex_.resize (totalConnections);

                // Fill flat arrays
                std::vector<size_t> positions = outgoingOffsets_; // Working copy for insertion
                for (size_t connIdx = 0; connIdx < connections_.size(); ++connIdx)
                {
                    const auto& conn = connections_[connIdx];
                    const size_t flatIdx = positions[conn.sourceOperator]++;

                    outgoingTargets_[flatIdx] = conn.targetOperator;
                    outgoingDepths_[flatIdx] = conn.modulationDepth;
                    connectionIndexToFlatIndex_[connIdx] = flatIdx;
                }
            }

        /**
 * @brief Recomputes the effective gain based on output gain and auto-scaling.
 */
        CASPI_NON_BLOCKING
        void updateEffectiveGain()
            {
                FloatType scale = FloatType (1);

                if (autoScaleOutputs_ && outputOperators_.size() > 1)
                {
                    scale = FloatType (1) / static_cast<FloatType> (outputOperators_.size());
                }

                effectiveGain_ = outputGain_ * scale;
            }

        // ====================================================================
        // Member Variables
        // ====================================================================

        // Operators
        std::vector<std::unique_ptr<Operator<FloatType>>> operators_;

        // Flat adjacency structure (render-optimized)
        std::vector<size_t> outgoingTargets_;           // All target operators, concatenated
        std::vector<float> outgoingDepths_;             // Corresponding modulation depths
        std::vector<size_t> outgoingOffsets_;           // Start index per operator (size = n+1)
        std::vector<size_t> connectionIndexToFlatIndex_; // Reverse index for O(1) depth updates

        // Topology metadata (for mutation/debugging)
        std::vector<ModulationConnection> connections_;
        std::vector<size_t> outputOperators_;

        // Execution
        std::vector<size_t> executionOrder_;

        // Render state
        std::vector<FloatType> modulationSignals_;
        std::vector<FloatType> operatorOutputs_;

        // Parameters
        FloatType baseFrequency_;
        FloatType outputGain_;
        FloatType effectiveGain_;
        bool autoScaleOutputs_;
    };

} // namespace CASPI

#endif // CASPI_FMGRAPH_H