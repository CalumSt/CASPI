/************************************************************************
*  CASPy - Python Bindings for CASPI
*  bind_fmgraph.cpp
*
*  Bindings for FM Graph synthesis components.
*  Exposes FMGraphBuilder and FMGraphDSP to Python via pybind11.
************************************************************************/

#include "caspi.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

/**
 * @brief Render audio from FMGraphDSP to a NumPy array.
 *
 * @param dsp The FMGraphDSP instance to render.
 * @param num_samples The number of samples to generate.
 * @return py::array_t<float> A NumPy array containing the rendered samples.
 */
template<typename FloatType>
py::array_t<FloatType> render_graph(FMGraphDSP<FloatType>& dsp, size_t num_samples)
{
    py::array_t<FloatType> result(num_samples);
    auto buf = result.request();
    FloatType* ptr = static_cast<FloatType*>(buf.ptr);

    for (size_t i = 0; i < num_samples; ++i)
    {
        ptr[i] = dsp.renderSample();
    }

    return result;
}

/**
 * @brief Render audio from FMGraphDSP to a multi-channel NumPy array.
 *
 * @param dsp The FMGraphDSP instance to render.
 * @param num_channels Number of output channels (duplicates mono output).
 * @param num_samples The number of samples per channel to generate.
 * @return py::array_t<FloatType> A 2D NumPy array [channels, samples].
 */
template<typename FloatType>
py::array_t<FloatType> render_graph_multichannel(
    FMGraphDSP<FloatType>& dsp,
    size_t num_channels,
    size_t num_samples)
{
    py::array_t<FloatType> result({num_channels, num_samples});
    auto buf = result.request();
    FloatType* ptr = static_cast<FloatType*>(buf.ptr);

    for (size_t frame = 0; frame < num_samples; ++frame)
    {
        FloatType sample = dsp.renderSample();

        // Replicate mono to all channels
        for (size_t ch = 0; ch < num_channels; ++ch)
        {
            ptr[ch * num_samples + frame] = sample;
        }
    }

    return result;
}

/**
 * @brief Python-friendly wrapper for FMGraphBuilder::compile()
 *
 * Returns a tuple (success: bool, dsp: FMGraphDSP or None, error: str or None)
 */
template<typename FloatType>
py::object compile_graph_py(const FMGraphBuilder<FloatType>& builder, FloatType sampleRate)
{
    auto result = builder.compile(sampleRate);

    if (result.has_value())
    {
        // Success: return (True, DSP object, None)
        return py::make_tuple(
            true,
            py::cast(std::move(result).value()),
            py::none()
        );
    }
    else
    {
        // Error: return (False, None, error message)
        return py::make_tuple(
            false,
            py::none(),
            py::str(errorToString(result.error()))
        );
    }
}

/**
 * @brief Bind FM Graph classes and enums to a Python module.
 */
void bind_fmgraph(py::module_& m)
{
    auto fmgraph = m.def_submodule("fmgraph", "FM synthesis graph construction and rendering");

    // ========================================================================
    // Enums
    // ========================================================================

    py::enum_<FMGraphError>(fmgraph, "FMGraphError",
        "Error codes for FM graph construction")
        .value("Success", FMGraphError::Success)
        .value("InvalidOperatorIndex", FMGraphError::InvalidOperatorIndex)
        .value("CycleDetected", FMGraphError::CycleDetected)
        .value("InvalidConnection", FMGraphError::InvalidConnection)
        .value("NoOutputOperators", FMGraphError::NoOutputOperators)
        .value("AllocationFailure", FMGraphError::AllocationFailure)
        .value("GraphNotCompiled", FMGraphError::GraphNotCompiled)
        .export_values();

    // ========================================================================
    // ModulationConnection
    // ========================================================================

    py::class_<ModulationConnection>(fmgraph, "ModulationConnection",
        R"pbdoc(
            Represents a connection between two FM operators.

            Attributes:
                source_operator: Index of the modulating operator
                target_operator: Index of the operator being modulated
                modulation_depth: Depth/amount of modulation
        )pbdoc")
        .def(py::init<>())
        .def_readwrite("source_operator", &ModulationConnection::sourceOperator)
        .def_readwrite("target_operator", &ModulationConnection::targetOperator)
        .def_readwrite("modulation_depth", &ModulationConnection::modulationDepth);

    // ========================================================================
    // FMGraphBuilder (float)
    // ========================================================================

    py::class_<FMGraphBuilder<float>>(fmgraph, "FMGraphBuilder",
        R"pbdoc(
            Builder for constructing FM synthesis graphs.

            Use this class to define operators, connections, and output routing.
            Once complete, call compile() to create a renderable FMGraphDSP instance.

            Example:
                builder = caspy.fmgraph.FMGraphBuilder()
                mod = builder.add_operator()
                car = builder.add_operator()
                builder.configure_operator(mod, freq=880.0, mod_index=2.0, mod_depth=1.0)
                builder.configure_operator(car, freq=440.0, mod_index=1.0, mod_depth=1.0)
                builder.connect(mod, car, depth=3.0)
                builder.set_output_operators([car])
                success, dsp, error = builder.compile(sample_rate=48000.0)
        )pbdoc")
        .def(py::init<>(), "Construct an empty FM graph builder")

        // Graph construction
        .def("add_operator", &FMGraphBuilder<float>::addOperator,
             "Add a new operator with default parameters. Returns operator index.")

        .def("remove_operator",
             [](FMGraphBuilder<float>& self, size_t index) {
                 auto result = self.removeOperator(index);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("operator_index"),
             "Remove an operator and all its connections")

        .def("connect",
             [](FMGraphBuilder<float>& self, size_t source, size_t target, float depth) {
                 auto result = self.connect(source, target, depth);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("source"), py::arg("target"), py::arg("modulation_depth"),
             "Connect source operator to target operator with given modulation depth")

        .def("disconnect",
             [](FMGraphBuilder<float>& self, size_t source, size_t target) {
                 auto result = self.disconnect(source, target);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("source"), py::arg("target"),
             "Remove connection between two operators")

        .def("set_output_operators",
             [](FMGraphBuilder<float>& self, const std::vector<size_t>& indices) {
                 auto result = self.setOutputOperators(indices);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("operator_indices"),
             "Set which operators contribute to final output")

        // Configuration
        .def("configure_operator",
             [](FMGraphBuilder<float>& self, size_t index, float freq,
                float mod_index, float mod_depth) {
                 auto result = self.configureOperator(index, freq, mod_index, mod_depth);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("operator_index"),
             py::arg("frequency"),
             py::arg("modulation_index"),
             py::arg("modulation_depth"),
             "Configure operator parameters")

        .def("set_operator_mode",
             [](FMGraphBuilder<float>& self, size_t index, ModulationMode mode) {
                 auto result = self.setOperatorMode(index, mode);
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             py::arg("operator_index"), py::arg("mode"),
             "Set modulation mode for an operator")

        // Validation
        .def("validate",
             [](const FMGraphBuilder<float>& self) {
                 auto result = self.validate();
                 if (!result.has_value())
                     throw py::value_error(errorToString(result.error()));
             },
             "Validate the graph topology (checks for cycles, output operators)")

        // Compilation
        .def("compile", &compile_graph_py<float>,
             py::arg("sample_rate"),
             R"pbdoc(
                 Compile the graph into a renderable DSP instance.

                 Returns:
                     tuple: (success: bool, dsp: FMGraphDSP or None, error: str or None)

                 Example:
                     success, dsp, error = builder.compile(48000.0)
                     if success:
                         audio = dsp.render(num_samples=48000)
                     else:
                         print(f"Compilation failed: {error}")
             )pbdoc")

        // Inspection
        .def("get_num_operators", &FMGraphBuilder<float>::getNumOperators,
             "Get the number of operators in the graph")
        .def("get_connections", &FMGraphBuilder<float>::getConnections,
             "Get all modulation connections")
        .def("get_output_operators", &FMGraphBuilder<float>::getOutputOperators,
             "Get indices of output operators");

    // ========================================================================
    // FMGraphDSP (float)
    // ========================================================================

    py::class_<FMGraphDSP<float>>(fmgraph, "FMGraphDSP",
        R"pbdoc(
            Real-time FM synthesis graph DSP engine.

            This is the compiled, renderable version of an FM graph.
            Once constructed (via FMGraphBuilder.compile()), the topology
            is immutable, but parameters can be updated in real-time.

            Example:
                dsp.set_frequency(440.0)
                dsp.note_on()
                audio = dsp.render(num_samples=48000)
                dsp.note_off()
        )pbdoc")

        // Note: Constructor is not exposed - use FMGraphBuilder.compile()

        // Operator access
        .def("get_operator",
             [](FMGraphDSP<float>& self, size_t index) -> Operator<float>* {
                 return self.getOperator(index);
             },
             py::arg("operator_index"),
             py::return_value_policy::reference_internal,
             "Get mutable access to an operator by index")

        .def("get_num_operators", &FMGraphDSP<float>::getNumOperators,
             "Get the number of operators in the graph")

        // Global frequency control
        .def("set_frequency", &FMGraphDSP<float>::setFrequency,
             py::arg("frequency"),
             "Set base frequency for all operators (Hz)")
        .def("get_frequency", &FMGraphDSP<float>::getFrequency,
             "Get current base frequency (Hz)")

        // Connection control
        .def("set_connection_depth", &FMGraphDSP<float>::setConnectionDepth,
             py::arg("connection_index"), py::arg("depth"),
             "Update modulation depth of a connection by index")
        .def("set_modulation_depth", &FMGraphDSP<float>::setModulationDepth,
             py::arg("source_operator"), py::arg("target_operator"), py::arg("depth"),
             "Update modulation depth between two operators")

        // Note control
        .def("note_on", &FMGraphDSP<float>::noteOn,
             "Trigger note-on for all operators")
        .def("note_off", &FMGraphDSP<float>::noteOff,
             "Trigger note-off for all operators")

        // Output control
        .def("set_output_gain", &FMGraphDSP<float>::setOutputGain,
             py::arg("gain"),
             "Set final output gain (linear)")
        .def("get_output_gain", &FMGraphDSP<float>::getOutputGain,
             "Get current output gain")
        .def("set_auto_scale_outputs", &FMGraphDSP<float>::setAutoScaleOutputs,
             py::arg("enable"),
             "Enable/disable automatic output scaling")
        .def("get_auto_scale_outputs", &FMGraphDSP<float>::getAutoScaleOutputs,
             "Check if auto-scaling is enabled")

        // State management
        .def("reset", &FMGraphDSP<float>::reset,
             "Reset all operator state and clear modulation buffers")

        // Rendering
    .def("render_sample",
     [](FMGraphDSP<float>& self) {
         return self.renderSample();
     },
     "Render a single audio sample (float)")
        .def("render", &render_graph<float>,
             py::arg("num_samples"),
             "Render multiple samples as a NumPy array")
        .def("render_multichannel", &render_graph_multichannel<float>,
             py::arg("num_channels"), py::arg("num_samples"),
             "Render to multi-channel NumPy array [channels, samples]")

        // Inspection
        .def("get_execution_order", &FMGraphDSP<float>::getExecutionOrder,
             "Get the topological execution order of operators")
        .def("get_output_operators", &FMGraphDSP<float>::getOutputOperators,
             "Get indices of output operators");


    // ========================================================================
    // Utility functions
    // ========================================================================

    fmgraph.def("error_to_string", &errorToString,
                py::arg("error"),
                "Convert an FMGraphError to a human-readable string");
}