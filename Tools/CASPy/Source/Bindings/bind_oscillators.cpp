/************************************************************************
 *  CASPy - Python Bindings for CASPI
 *  bind_oscillators.cpp
 ************************************************************************/

#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // REQUIRED for std::vector conversion

namespace py = pybind11;
using namespace CASPI;
using BlepOscFloat = Oscillators::BlepOscillator<float>;
using NodeBase_t = Graph::NodeBase<float>;
using BlepOscFloatPtr_t = std::shared_ptr<BlepOscFloat>;

/**
 * @brief Render num_samples from a BlepOscillator into a NumPy array.
 *
 * Uses renderBlock() (preferred over per-sample renderSample() in Python
 * loops) to avoid repeated Python→C++ call overhead.
 * @tparam OscType Oscillator type (must have renderSample() method)
 * @param osc Oscillator instance
 * @param num_samples Number of samples to generate
 * @return Numpy array of float32 samples
 */
py::array_t<float> render_blep(BlepOscFloat& osc,
                                       int num_samples)
{
    py::array_t<float> result(num_samples);
    auto buf = result.request();
    osc.renderBlock(static_cast<float*>(buf.ptr), num_samples);
    return result;
}
void bind_oscillators(py::module_& m)
{
    auto osc_m = m.def_submodule("oscillators",
        "Band-limited oscillators using the PolyBLEP method.");

    py::enum_<Oscillators::WaveShape>(osc_m, "WaveShape",
        "Waveform selection for BlepOscillator.")
        .value("Sine",     Oscillators::WaveShape::Sine)
        .value("Saw",      Oscillators::WaveShape::Saw)
        .value("Square",   Oscillators::WaveShape::Square)
        .value("Triangle", Oscillators::WaveShape::Triangle)
        .value("Pulse",    Oscillators::WaveShape::Pulse)
        .export_values();

    // This connects the inheritance chain across file boundaries
    py::object node_base = m.attr("NodeBase");

    py::class_<BlepOscFloat, NodeBase_t, BlepOscFloatPtr_t>(osc_m, "BlepOscillator",
        R"pbdoc(
            Band-limited oscillator (PolyBLEP antialiasing).

            Supports Sine, Saw, Square, Triangle, Pulse at runtime via set_shape().
            Modulate amplitude, frequency, and pulse_width directly on the
            exposed ModulatableParameter attributes.

            Example:
                osc = caspi.oscillators.BlepOscillator()
                osc.set_sample_rate(48000.0)
                osc.set_frequency(440.0)
                osc.set_shape(caspi.oscillators.WaveShape.Saw)
                audio = osc.render(4800)   # 0.1 s at 48 kHz

            Hard sync (primary drives secondary):
                primary.render_sample()
                if primary.phase_wrapped():
                    secondary.force_sync()
        )pbdoc")
        .def(py::init<>(), "Default constructor — Sine, 440 Hz, no sample rate set.")
        .def(py::init<Oscillators::WaveShape, float, float>(),
             py::arg("shape"), py::arg("sample_rate"), py::arg("frequency"),
             "Construct with explicit shape, sample rate (Hz), and frequency (Hz).")

        // Configuration
        .def("set_shape",       &BlepOscFloat::setShape,       py::arg("shape"),
             "Select waveform at runtime. Switching to Triangle resets integrator.")
        .def("get_shape",       &BlepOscFloat::getShape,       "Get current WaveShape.")
        .def("set_frequency",   &BlepOscFloat::setFrequency,   py::arg("hz"),
             "Set frequency (Hz), bypassing parameter smoothing. Use for init.")
        .def("set_sample_rate", &BlepOscFloat::setSampleRate,  py::arg("sample_rate"),
             "Set sample rate (Hz). Must be called before rendering.")
        .def("set_phase_offset",&BlepOscFloat::setPhaseOffset, py::arg("offset"),
             "Set phase offset in [0,1). Applied on reset_phase() and force_sync().")
        .def("reset_phase",     &BlepOscFloat::resetPhase,
             "Reset phase to phase_offset. Does not clear the triangle integrator.")

        // Hard sync
        .def("force_sync",      &BlepOscFloat::forceSync,
             "Force phase reset with one-sample discontinuity correction.")
        .def("phase_wrapped",   &BlepOscFloat::phaseWrapped,
             "True if phase wrapped on the most recent render_sample() call.")

        // Rendering
        .def("render_sample",   &BlepOscFloat::renderSample,
             "Render one sample. Updates parameter smoothers and phase.")
        .def("render",          &render_blep,
             py::arg("num_samples"),
             R"pbdoc(
                 Render num_samples via renderBlock(). Returns a float32 NumPy array.
                 Preferred over calling render_sample() in a Python loop.
             )pbdoc")

        // Expose modulatable parameters as attributes so callers can:
        //   osc.amplitude.set_base_normalised(0.8)
        //   osc.frequency.add_modulation(0.1)
    .def_property_readonly("amplitude",
        [](BlepOscFloat& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
        py::return_value_policy::reference_internal,
        "Output amplitude [0,1]. ModulatableParameter<float>.")
    .def_property_readonly("frequency",
        [](BlepOscFloat& self) -> Core::ModulatableParameter<float>& { return self.frequency; },
        py::return_value_policy::reference_internal,
        "Frequency [20,20000 Hz], log scale. ModulatableParameter<float>.")
    .def_property_readonly("pulse_width",
        [](BlepOscFloat& self) -> Core::ModulatableParameter<float>& { return self.pulseWidth; },
        py::return_value_policy::reference_internal,
        "Pulse width [0.01,0.99]. Audible on Square/Pulse only.");
}