#ifndef CASPI_FMGRAPH_SEPARATED_H
#define CASPI_FMGRAPH_SEPARATED_H
/*************************************************************************
 * @file caspi_FMGraph.h
 * @brief Clean separation: FMGraphBuilder (config) → FMGraphDSP (runtime)
 *
 * ARCHITECTURE:
 *
 * FMGraphBuilder (Mutable, Non-RT)    →  FMGraphDSP (Immutable, RT-Safe)
 * - Add/remove operators                  - renderSample()
 * - Connect/disconnect                    - noteOn/Off()
 * - Validate topology                     - Parameter updates
 * - Compile to DSP object                 - No allocations
 *
 * Builder pattern ensures:
 * 1. Configuration errors caught before real-time
 * 2. DSP object is immutable and RT-safe
 * 3. Clear ownership and lifetime semantics
 ************************************************************************/

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
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

    struct ModulationConnection
    {
            size_t sourceOperator;
            size_t targetOperator;
            float modulationDepth;

            bool operator== (const ModulationConnection& other) const
            {
                return sourceOperator == other.sourceOperator && targetOperator == other.targetOperator;
            }
    };

    template <typename FloatType>
    /**
     * @brief Operator configuration (builder-side representation)
     */
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
    // FMGraphBuilder: Mutable graph construction and validation
    // ============================================================================

    /**
     * @class FMGraphBuilder
     * @brief Mutable graph builder for FM synthesis topology
     *
     * NOT REAL-TIME SAFE: This class is for configuration only
     * Use compile() to create a real-time safe FMGraphDSP object
     *
     * @example
     * @code
     * FMGraphBuilder<float> builder;
     *
     * // Configure topology
     * size_t mod = builder.addOperator();
     * size_t car = builder.addOperator();
     * builder.connect(mod, car, 3.0f);
     * builder.setOutputOperators({car});
     *
     * // Compile to RT-safe object
     * auto result = builder.compile(48000.0f);
     * if (result.has_value())
     * {
     *     FMGraphDSP<float> dsp = std::move(result.value());
     *     // Use dsp in real-time thread
     * }
     * @endcode
     */
    template <typename FloatType>
    class FMGraphBuilder
    {
        public:
            using Error  = FMGraphError;
            using Result = expected<void, Error, NonRealTimeSafe>;
            template <typename T>
            using ResultValue = expected<T, Error, NonRealTimeSafe>;

            /**
             * @brief Construct empty graph builder
             */
            FMGraphBuilder() = default;

            // ====================================================================
            // Graph Construction
            // ====================================================================

            /**
             * @brief Add a new operator to the graph
             * @return Index of newly added operator
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
             * @brief Remove an operator
             */
            Result removeOperator (size_t operatorIndex)
            {
                if (operatorIndex >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidOperatorIndex);
                }

                operators_.erase (operators_.begin() + operatorIndex);

                // Remove connections involving this operator
                connections_.erase (
                    std::remove_if (connections_.begin(), connections_.end(), [operatorIndex] (const ModulationConnection& conn)
                                    { return conn.sourceOperator == operatorIndex || conn.targetOperator == operatorIndex; }),
                    connections_.end());

                // Adjust indices
                for (auto& conn : connections_)
                {
                    if (conn.sourceOperator > operatorIndex)
                    {
                        conn.sourceOperator--;
                    }
                    if (conn.targetOperator > operatorIndex)
                    {
                        conn.targetOperator--;
                    }
                }

                outputOperators_.erase (
                    std::remove (outputOperators_.begin(), outputOperators_.end(), operatorIndex),
                    outputOperators_.end());

                for (auto& outIdx : outputOperators_)
                {
                    if (outIdx > operatorIndex)
                    {
                        outIdx--;
                    }
                }

                return Result();
            }

            /**
             * @brief Connect two operators
             */
            Result connect (size_t source, size_t target, FloatType modulationDepth)
            {
                if (source >= operators_.size() || target >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidOperatorIndex);
                }

                if (source == target)
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidConnection);
                }

                ModulationConnection conn { source, target, static_cast<float> (modulationDepth) };

                auto it = std::find (connections_.begin(), connections_.end(), conn);
                if (it != connections_.end())
                {
                    it->modulationDepth = static_cast<float> (modulationDepth);
                }
                else
                {
                    connections_.push_back (conn);
                }

                return Result();
            }

            /**
             * @brief Disconnect two operators
             */
            Result disconnect (size_t source, size_t target)
            {
                ModulationConnection conn { source, target, 0.0f };
                connections_.erase (
                    std::remove (connections_.begin(), connections_.end(), conn),
                    connections_.end());
                return Result();
            }

            /**
             * @brief Set output operators
             */
            Result setOutputOperators (const std::vector<size_t>& indices)
            {
                for (size_t idx : indices)
                {
                    if (idx >= operators_.size())
                    {
                        return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidOperatorIndex);
                    }
                }
                outputOperators_ = indices;
                return Result();
            }

            // ====================================================================
            // Operator Configuration
            // ====================================================================

            /**
             * @brief Configure operator parameters
             */
            Result configureOperator (size_t index,
                                      FloatType frequency,
                                      FloatType modulationIndex,
                                      FloatType modulationDepth)
            {
                if (index >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidOperatorIndex);
                }

                operators_[index].frequency       = frequency;
                operators_[index].modulationIndex = modulationIndex;
                operators_[index].modulationDepth = modulationDepth;

                return Result();
            }

            /**
             * @brief Set operator modulation mode
             */
            Result setOperatorMode (size_t index, ModulationMode mode)
            {
                if (index >= operators_.size())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::InvalidOperatorIndex);
                }

                operators_[index].modulationMode = mode;
                return Result();
            }

            // ====================================================================
            // Validation
            // ====================================================================

            /**
             * @brief Validate graph topology (check for cycles)
             * @return Success or error describing problem
             */
            Result validate() const
            {
                const size_t n = operators_.size();

                if (n == 0)
                {
                    return Result();
                } // Empty graph is valid

                if (outputOperators_.empty())
                {
                    return make_unexpected<Error, NonRealTimeSafe> (Error::NoOutputOperators);
                }

                // Check for cycles using topological sort
                std::vector<int> inDegree (n, 0);
                std::vector<std::vector<size_t>> adjacencyList (n);

                for (const auto& conn : connections_)
                {
                    adjacencyList[conn.sourceOperator].push_back (conn.targetOperator);
                    inDegree[conn.targetOperator]++;
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
                    size_t current = queue.front();
                    queue.pop();
                    processed++;

                    for (size_t neighbor : adjacencyList[current])
                    {
                        inDegree[neighbor]--;
                        if (inDegree[neighbor] == 0)
                        {
                            queue.push (neighbor);
                        }
                    }
                }

                if (processed != n)
                    return make_unexpected<Error, NonRealTimeSafe> (Error::CycleDetected);

                return Result();
            }

            // ====================================================================
            // Compilation
            // ====================================================================

            /**
             * @brief Compile graph into real-time safe DSP object
             * @param sampleRate Sample rate for the DSP object
             * @return FMGraphDSP object or error
             *
             * This method:
             * 1. Validates the graph topology
             * 2. Computes topological execution order
             * 3. Creates and initializes operators
             * 4. Returns immutable, RT-safe DSP object
             */
            ResultValue<FMGraphDSP<FloatType>> compile (FloatType sampleRate) const
            {
                // Validate first
                auto validationResult = validate();
                if (! validationResult.has_value())
                {
                    return make_unexpected<FMGraphDSP<FloatType>, Error, NonRealTimeSafe> (
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
                    return make_unexpected<FMGraphDSP<FloatType>, Error, NonRealTimeSafe> (
                        Error::AllocationFailure);
                }
            }

            // ====================================================================
            // Inspection
            // ====================================================================
            CASPI_NO_DISCARD CASPI_NON_BLOCKING
            size_t getNumOperators() const { return operators_.size(); }

            CASPI_NO_DISCARD CASPI_NON_BLOCKING
            const std::vector<ModulationConnection>& getConnections() const { return connections_; }

            CASPI_NO_DISCARD CASPI_NON_BLOCKING
            const std::vector<size_t>& getOutputOperators() const { return outputOperators_; }

        private:
            std::vector<OperatorConfig<FloatType>> operators_;
            std::vector<ModulationConnection> connections_;
            std::vector<size_t> outputOperators_;
    };

    // ============================================================================
    // FMGraphDSP: Immutable, real-time safe DSP object
    // ============================================================================

    /**
     * @class FMGraphDSP
     * @brief Immutable, real-time safe FM synthesis engine
     *
     * REAL-TIME SAFE: All rendering methods are non-blocking and non-allocating
     *
     * This class is created by FMGraphBuilder::compile() and represents
     * a validated, optimized FM synthesis graph ready for real-time use.
     *
     * The graph structure is IMMUTABLE after construction.
     * Only runtime parameters (frequency, envelopes, etc.) can be changed.
     */
    template <typename FloatType>
    class FMGraphDSP : public Core::Producer<FloatType>,
                       public Core::SampleRateAware<FloatType>
    {
        public:
            /**
             * @brief Construct DSP object from validated graph
             *
             * NOT REAL-TIME SAFE: Constructor allocates memory
             * Called by FMGraphBuilder::compile()
             */
            FMGraphDSP (const std::vector<OperatorConfig<FloatType>>& operatorConfigs,
                        const std::vector<ModulationConnection>& connections,
                        const std::vector<size_t>& outputOperators,
                        FloatType sampleRate)
                : connections_ (connections), outputOperators_ (outputOperators), baseFrequency_ (FloatType (440))
            {
                const size_t n = operatorConfigs.size();

                // Create operators
                operators_.reserve (n);
                for (const auto& config : operatorConfigs)
                {
                    auto op = std::make_unique<Operator<FloatType>>();
                    op->setSampleRate (sampleRate);
                    op->setFrequency (config.frequency);
                    op->setModulationIndex (config.modulationIndex);
                    op->setModulationDepth (config.modulationDepth);
                    op->setModulationFeedback (config.modulationFeedback);
                    op->setModulationMode (config.modulationMode);
                    operators_.push_back (std::move (op));
                }

                // Pre-allocate working buffers
                modulationSignals_.resize (n, FloatType (0));
                operatorOutputs_.resize (n, FloatType (0));

                // Compute execution order
                computeExecutionOrder();

                // Build adjacency list for O(V+E) rendering
                buildAdjacencyList();

                this->setSampleRate (sampleRate);
            }

            // Move-only (no copy - expensive and unnecessary)
            FMGraphDSP (const FMGraphDSP&)            = delete;
            FMGraphDSP& operator= (const FMGraphDSP&) = delete;
            FMGraphDSP (FMGraphDSP&&)                 = default;
            FMGraphDSP& operator= (FMGraphDSP&&)      = default;

            // ====================================================================
            // Real-Time Parameter Control
            // ====================================================================

            /**
             * @brief Get operator for parameter control
             * REAL-TIME SAFE: Returns pointer (no allocation)
             */
            CASPI_NON_ALLOCATING
            Operator<FloatType>* getOperator (size_t index)
            {
                return (index < operators_.size()) ? operators_[index].get() : nullptr;
            }

            CASPI_NON_ALLOCATING
            const Operator<FloatType>* getOperator (size_t index) const
            {
                return (index < operators_.size()) ? operators_[index].get() : nullptr;
            }

            /**
             * @brief Set base frequency for all operators
             * REAL-TIME SAFE: No allocations
             */
            CASPI_NON_ALLOCATING
            void setFrequency (FloatType frequency)
            {
                baseFrequency_ = frequency;
                for (auto& op : operators_)
                {
                    op->setFrequency (frequency);
                }
            }

            CASPI_NON_ALLOCATING
            FloatType getFrequency() const { return baseFrequency_; }

            /**
             * @brief Update connection modulation depth at runtime
             * REAL-TIME SAFE: Atomic write (floats are atomic on most platforms)
             */
            CASPI_NON_ALLOCATING
            void setConnectionDepth (size_t connectionIndex, FloatType depth)
            {
                if (connectionIndex < connections_.size())
                {
                    connections_[connectionIndex].modulationDepth = static_cast<float> (depth);
                }
            }

            // ====================================================================
            // Envelope Control
            // ====================================================================

            CASPI_NON_ALLOCATING
            void noteOn()
            {
                for (auto& op : operators_)
                {
                    op->noteOn();
                }
            }

            CASPI_NON_ALLOCATING
            void noteOff()
            {
                for (auto& op : operators_)
                {
                    op->noteOff();
                }
            }

            CASPI_NON_ALLOCATING
            void setADSR (FloatType attack, FloatType decay, FloatType sustain, FloatType release)
            {
                for (auto& op : operators_)
                {
                    op->setADSR (attack, decay, sustain, release);
                }
            }

            CASPI_NON_ALLOCATING
            void enableEnvelopes()
            {
                for (auto& op : operators_)
                {
                    op->enableEnvelope();
                }
            }

            CASPI_NON_ALLOCATING
            void disableEnvelopes()
            {
                for (auto& op : operators_)
                {
                    op->disableEnvelope();
                }
            }

            CASPI_NON_ALLOCATING
            void reset()
            {
                for (auto& op : operators_)
                {
                    op->reset();
                }

                std::fill (modulationSignals_.begin(), modulationSignals_.end(), FloatType (0));
            }

            // ====================================================================
            // Real-Time Rendering
            // ====================================================================

            /**
             * @brief Render one sample
             * REAL-TIME SAFE: No allocations, bounded execution time
             */
            CASPI_NON_BLOCKING
            CASPI_NON_ALLOCATING
            FloatType renderSample() override
            {
                const size_t numOps = operators_.size();

                // Clear modulation buffers
                std::fill (modulationSignals_.begin(), modulationSignals_.end(), FloatType (0));

                // Render in topological order
                for (size_t opIndex : executionOrder_)
                {
                    // Set modulation input
                    operators_[opIndex]->setModulationInput (modulationSignals_[opIndex]);

                    // Render operator
                    operatorOutputs_[opIndex] = operators_[opIndex]->renderSample();

                    // Flush denormals
                    operatorOutputs_[opIndex] = Core::flushToZero (operatorOutputs_[opIndex]);

                    // Distribute to targets (optimized with adjacency list)
                    for (auto [target, connIdx] : adjacencyList_[opIndex])
                    {
                        modulationSignals_[target] += operatorOutputs_[opIndex]
                                                      * connections_[connIdx].modulationDepth;
                    }
                }

                // Mix outputs
                if (outputOperators_.empty())
                {
                    return FloatType (0);
                }

                FloatType output = FloatType (0);
                for (size_t outIdx : outputOperators_)
                {
                    output += operatorOutputs_[outIdx];
                }

                return Core::flushToZero (output);
            }

            /**
             * @brief Render block of samples
             * REAL-TIME SAFE: No allocations
             */
            CASPI_NON_BLOCKING
            CASPI_NON_ALLOCATING
            void renderBlock (FloatType* buffer, size_t numSamples)
            {
                for (size_t i = 0; i < numSamples; ++i)
                {
                    buffer[i] = renderSample();
                }
            }

            // ====================================================================
            // Inspection
            // ====================================================================

            CASPI_NON_ALLOCATING
            size_t getNumOperators() const { return operators_.size(); }

            CASPI_NON_ALLOCATING
            const std::vector<size_t>* getExecutionOrder() const
            {
                return &executionOrder_;
            }

        private:
            // ====================================================================
            // Initialization (called from constructor)
            // ====================================================================

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
                    adjacencyList[conn.sourceOperator].push_back (conn.targetOperator);
                    inDegree[conn.targetOperator]++;
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
                    size_t current = queue.front();
                    queue.pop();
                    executionOrder_.push_back (current);

                    for (size_t neighbor : adjacencyList[current])
                    {
                        --inDegree[neighbor];
                        if (inDegree[neighbor] == 0)
                        {
                            queue.push (neighbor);
                        }
                    }
                }
            }

            /**
             * @brief Build adjacency list for O(V+E) rendering
             * Avoids O(V×E) connection search in hot path
             */
            void buildAdjacencyList()
            {
                adjacencyList_.resize (operators_.size());

                for (size_t i = 0; i < connections_.size(); ++i)
                {
                    const auto& conn = connections_[i];
                    adjacencyList_[conn.sourceOperator].emplace_back (
                        conn.targetOperator,
                        i);
                }
            }

            // ====================================================================
            // Member Variables
            // ====================================================================

            // Graph structure (immutable after construction)
            std::vector<std::unique_ptr<Operator<FloatType>>> operators_;
            std::vector<ModulationConnection> connections_;
            std::vector<size_t> outputOperators_;
            std::vector<size_t> executionOrder_;

            // Optimized rendering data structures
            std::vector<std::vector<std::pair<size_t, size_t>>> adjacencyList_;

            // Working buffers (pre-allocated)
            std::vector<FloatType> modulationSignals_;
            std::vector<FloatType> operatorOutputs_;

            // Runtime parameters
            FloatType baseFrequency_;
    };

} // namespace CASPI

#endif // CASPI_FMGRAPH_SEPARATED_H