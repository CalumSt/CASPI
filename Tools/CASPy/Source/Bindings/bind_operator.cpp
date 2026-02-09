/************************************************************************
*  CASPy - Python Bindings for CASPI
*  bind_operator.cpp
*
*  Bindings for FM/PM Operator components.
*  Exposes Operator<float> with modulation capabilities to Python.
************************************************************************/

#include "caspi.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

/**
 * @brief Render audio from an Operator to a NumPy array.
 *
 * This renders the operator without external modulation (uses internal state).
 *
 * @param op The Operator instance to render.
 * @param num_samples The number of samples to generate.
 * @return py::array_t<float> A NumPy array containing the rendered samples.
 */
py::array_t<float> render_operator(Operator<float>& op, size_t num_samples)
{
    py::array_t<float> result(num_samples);
    auto buf = result.request();
    float* ptr = static_cast<float*>(buf.ptr);

    for (size_t i = 0; i < num_samples; ++i)
    {
        ptr[i] = op.renderSample();
    }

    return result;
}

/**
 * @brief Render audio from an Operator with external modulation buffer.
 *
 * @param op The Operator instance to render.
 * @param modulation_input NumPy array of modulation values.
 * @return py::array_t<float> A NumPy array containing the rendered samples.
 */
py::array_t<float> render_operator_with_modulation(
    Operator<float>& op,
    py::array_t<float> modulation_input)
{
    auto mod_buf = modulation_input.request();
    if (mod_buf.ndim != 1)
        throw std::runtime_error("Modulation input must be a 1D array");

    size_t num_samples = mod_buf.shape[0];
    const float* mod_ptr = static_cast<const float*>(mod_buf.ptr);

    py::array_t<float> result(num_samples);
    auto out_buf = result.request();
    float* out_ptr = static_cast<float*>(out_buf.ptr);

    for (size_t i = 0; i < num_samples; ++i)
    {
        out_ptr[i] = op.renderSample(mod_ptr[i]);
    }

    return result;
}

/**
 * @brief Process a buffer of samples with per-sample modulation values.
 *
 * More efficient than calling renderSample() in a Python loop.
 *
 * @param op The Operator instance.
 * @param modulation NumPy array of modulation values (can be zeros for no modulation).
 * @return py::array_t<float> Rendered audio samples.
 */
py::array_t<float> process_buffer(
    Operator<float>& op,
    py::array_t<float> modulation)
{
    auto mod_buf = modulation.request();
    if (mod_buf.ndim != 1)
        throw std::runtime_error("Modulation buffer must be 1D");

    size_t num_samples = mod_buf.shape[0];
    const float* mod_ptr = static_cast<const float*>(mod_buf.ptr);

    py::array_t<float> result(num_samples);
    auto out_buf = result.request();
    float* out_ptr = static_cast<float*>(out_buf.ptr);

    // Process all samples
    for (size_t i = 0; i < num_samples; ++i)
    {
        op.setModulationInput(mod_ptr[i]);
        out_ptr[i] = op.renderSample();
    }

    return result;
}

/**
 * @brief Bind Operator class and related enums to a Python module.
 */
void bind_operator(py::module_& m)
{
    auto op_module = m.def_submodule("operator", "FM/PM synthesis operators");

    // ========================================================================
    // Enums
    // ========================================================================

    py::enum_<ModulationMode>(op_module, "ModulationMode",
        R"pbdoc(
            Modulation modes for FM/PM operators.

            Phase: Phase modulation (PM) - modulation affects phase directly
            Frequency: Frequency modulation (FM) - modulation affects instantaneous frequency
        )pbdoc")
        .value("Phase", ModulationMode::Phase,
               "Phase modulation - modulation signal offsets the carrier phase")
        .value("Frequency", ModulationMode::Frequency,
               "Frequency modulation - modulation signal varies the instantaneous frequency")
        .export_values();

    // ========================================================================
    // Operator<float>
    // ========================================================================

    py::class_<Operator<float>>(op_module, "Operator",
        R"pbdoc(
            FM/PM synthesis operator.

            A sine wave oscillator capable of phase or frequency modulation.
            Can function as either a modulator or carrier in FM synthesis.

            Key Features:
                - Phase Modulation (PM) or Frequency Modulation (FM)
                - Self-modulation via feedback
                - ADSR envelope control
                - External modulation input

            Parameters:
                - Modulation Index: Controls depth of modulation (0.0 to ~10.0)
                - Modulation Depth: Output amplitude multiplier (0.0 to 1.0)
                - Modulation Feedback: Amount of self-modulation (0.0 to ~5.0)

            Example - Simple sine wave:
                op = caspy.operator.Operator()
                op.set_sample_rate(48000.0)
                op.set_frequency(440.0)
                op.set_modulation_depth(1.0)
                audio = op.render(num_samples=48000)

            Example - Phase modulation:
                mod = caspy.operator.Operator()
                mod.set_sample_rate(48000.0)
                mod.set_frequency(880.0)

                car = caspy.operator.Operator()
                car.set_sample_rate(48000.0)
                car.set_frequency(440.0)
                car.set_modulation_mode(caspy.operator.ModulationMode.Phase)
                car.set_modulation_index(3.0)

                # Render modulator
                mod_signal = mod.render(num_samples=1024)

                # Render carrier with modulation
                audio = car.render_with_modulation(mod_signal)
        )pbdoc")

        // Constructors
        .def(py::init<>(),
             "Construct an operator with default parameters")

        .def(py::init<float, float, float, float, float, ModulationMode>(),
             py::arg("sample_rate"),
             py::arg("frequency"),
             py::arg("modulation_index"),
             py::arg("modulation_depth"),
             py::arg("modulation_feedback"),
             py::arg("modulation_mode"),
             "Construct an operator with specified parameters")

        // Frequency Control
        .def("set_frequency", &Operator<float>::setFrequency,
             py::arg("frequency"),
             "Set the oscillator frequency in Hz")

        .def("get_frequency", &Operator<float>::getFrequency,
             "Get the current frequency in Hz")

        // Modulation Control
        .def("set_modulation_index", &Operator<float>::setModulationIndex,
             py::arg("index"),
             R"pbdoc(
                 Set the modulation index.

                 Controls the depth/amount of modulation:
                 - In PM: scales the phase deviation (radians)
                 - In FM: scales the frequency deviation (Hz)

                 Typical range: 0.0 to 10.0
             )pbdoc")

        .def("set_modulation_depth", &Operator<float>::setModulationDepth,
             py::arg("depth"),
             R"pbdoc(
                 Set the modulation depth (output amplitude multiplier).

                 Range: 0.0 to 1.0
                 Does NOT affect modulation amount, only output level.
             )pbdoc")

        .def("set_modulation_feedback", &Operator<float>::setModulationFeedback,
             py::arg("feedback"),
             R"pbdoc(
                 Set the feedback amount (self-modulation).

                 Adds harmonic complexity and brightness.
                 Range: 0.0 (none) to ~5.0 (heavy distortion)
             )pbdoc")

        .def("set_modulation_mode", &Operator<float>::setModulationMode,
             py::arg("mode"),
             "Set the modulation mode (Phase or Frequency)")

        .def("set_modulation", &Operator<float>::setModulation,
             py::arg("index"),
             py::arg("depth"),
             py::arg("feedback") = 0.0f,
             "Set all modulation parameters at once")

        .def("set_modulation_input",
             py::overload_cast<float>(&Operator<float>::setModulationInput),
             py::arg("value"),
             "Set single modulation value for the next sample")

        .def("clear_modulation_input", &Operator<float>::clearModulationInput,
             "Clear all modulation input")

        // Getters
        .def("get_modulation_index", &Operator<float>::getModulationIndex,
             "Get the current modulation index")
        .def("get_modulation_depth", &Operator<float>::getModulationDepth,
             "Get the current modulation depth")
        .def("get_modulation_feedback", &Operator<float>::getModulationFeedback,
             "Get the current feedback amount")
        .def("get_modulation_mode", &Operator<float>::getModulationMode,
             "Get the current modulation mode")

        // Envelope Control
        .def("enable_envelope", &Operator<float>::enableEnvelope,
             py::arg("enabled") = true,
             "Enable or disable the ADSR envelope")

        .def("disable_envelope", &Operator<float>::disableEnvelope,
             "Disable the ADSR envelope")

        .def("set_adsr", &Operator<float>::setADSR,
             py::arg("attack_time"),
             py::arg("decay_time"),
             py::arg("sustain_level"),
             py::arg("release_time"),
             R"pbdoc(
                 Set ADSR envelope parameters.

                 Args:
                     attack_time: Attack time in seconds
                     decay_time: Decay time in seconds
                     sustain_level: Sustain level (0.0 to 1.0)
                     release_time: Release time in seconds
             )pbdoc")

        .def("note_on", &Operator<float>::noteOn,
             "Trigger note-on (start envelope attack phase)")

        .def("note_off", &Operator<float>::noteOff,
             "Trigger note-off (start envelope release phase)")

        // Sample Rate
        .def("set_sample_rate", &Operator<float>::setSampleRate,
             py::arg("sample_rate"),
             "Set the sample rate in Hz")

        .def("get_sample_rate", &Operator<float>::getSampleRate,
             "Get the current sample rate in Hz")

        // State Management
        .def("reset", &Operator<float>::reset,
             "Reset all operator state to defaults")

        // Rendering - Single Sample
        .def("render_sample",
             py::overload_cast<>(&Operator<float>::renderSample),
             "Render a single audio sample (uses stored modulation)")

        .def("render_sample",
             py::overload_cast<float>(&Operator<float>::renderSample),
             py::arg("modulation_input"),
             "Render a single sample with explicit modulation input")

        // Rendering - Buffers
        .def("render", &render_operator,
             py::arg("num_samples"),
             R"pbdoc(
                 Render multiple samples as a NumPy array.

                 Uses internal modulation state (from set_modulation_input).
                 For external modulation, use render_with_modulation().

                 Returns:
                     numpy.ndarray: Audio samples (1D float array)
             )pbdoc")

        .def("render_with_modulation", &render_operator_with_modulation,
             py::arg("modulation_input"),
             R"pbdoc(
                 Render with external modulation buffer.

                 Args:
                     modulation_input: NumPy array of modulation values

                 Returns:
                     numpy.ndarray: Audio samples (same length as modulation_input)
             )pbdoc")

        .def("process_buffer", &process_buffer,
             py::arg("modulation"),
             R"pbdoc(
                 Process a buffer of samples with per-sample modulation.

                 More efficient than calling render_sample() in a loop.

                 Args:
                     modulation: NumPy array of modulation values (use zeros for no modulation)

                 Returns:
                     numpy.ndarray: Rendered audio samples
             )pbdoc");

    // ========================================================================
    // Utility Functions
    // ========================================================================

    op_module.def("create_sine_wave",
        [](float frequency, float sample_rate, size_t num_samples) -> py::array_t<float> {
            Operator<float> op;
            op.setSampleRate(sample_rate);
            op.setFrequency(frequency);
            op.setModulationDepth(1.0f);
            return render_operator(op, num_samples);
        },
        py::arg("frequency"),
        py::arg("sample_rate"),
        py::arg("num_samples"),
        R"pbdoc(
            Convenience function to create a simple sine wave.

            Args:
                frequency: Frequency in Hz
                sample_rate: Sample rate in Hz
                num_samples: Number of samples to generate

            Returns:
                numpy.ndarray: Audio samples

            Example:
                sine = caspy.operator.create_sine_wave(440.0, 48000.0, 48000)
        )pbdoc");
}