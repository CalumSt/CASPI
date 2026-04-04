/************************************************************************
*  CASPy - Python Bindings for CASPI
*  bind_adsr.cpp
*
*  Bindings for ADSR Envelope components.
*  Exposes ADSR<float> and its state machine to Python via pybind11.
************************************************************************/

#include "caspi.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;
using ADSRFloat  = Envelope::ADSR<float>;
using NodeBase_t = Graph::NodeBase<float>;
using ADSRFloatPtr_t = std::shared_ptr<ADSRFloat>;
/**
 * @brief Render a full ADSR envelope to a NumPy array.
 *
 * This function generates `num_samples` of the envelope and returns
 * them as a contiguous NumPy array of floats. This is useful for
 * visualization or audio playback in Python.
 *
 * @param envelope The ADSR envelope instance to render.
 * @param num_samples The number of samples to generate.
 * @return py::array_t<float> A NumPy array containing the rendered samples.
 */
py::array_t<float> render_algorithm(ADSRFloat& envelope, size_t num_samples)
{
    py::array_t<float> result(num_samples);       // allocate NumPy array
    auto buf = result.request();                   // request buffer info
    float* ptr = static_cast<float*>(buf.ptr);    // pointer to data

    for (size_t i = 0; i < num_samples; ++i) {
        ptr[i] = envelope.render();               // render one sample at a time
    }

    return result;
}

/**
 * @brief Bind ADSR envelope classes and enums to a Python module.
 *
 * Creates a submodule `adsr` with:
 *   - `EnvelopeState` enum for the internal state of the envelope
 *   - `ADSR` class with full control over attack, decay, sustain, release
 *   - `render_algorithm` helper function for producing NumPy buffers
 *
 * @param m The parent pybind11 module.
 */
void bind_adsr(py::module_& m)
{
    // Create submodule
    auto adsr = m.def_submodule("adsr", "ADSR envelope generators for synthesis");

    // This connects the inheritance chain across file boundaries
    py::object node_base  = m.attr("NodeBase");

    // Bind envelope states enum
    py::enum_<Envelope::State>(adsr, "EnvelopeState",
        "States of the ADSR envelope")
        .value("Idle", Envelope::State::idle)
        .value("Attack", Envelope::State::attack)
        .value("Decay", Envelope::State::decay)
        .value("Slope", Envelope::State::slope)
        .value("Sustain", Envelope::State::sustain)
        .value("Release", Envelope::State::release)
        .value("NoteOn", Envelope::State::noteOn)
        .value("NoteOff", Envelope::State::noteOff)
        .export_values();

    // Bind ADSR class
    py::class_<ADSRFloat, NodeBase_t, ADSRFloatPtr_t>(adsr, "ADSR",
        R"pbdoc(
            ADSR (Attack-Decay-Sustain-Release) envelope generator.

            Features:
                - Attack: Rise from 0 to 1.0
                - Decay: Fall from 1.0 to sustain level
                - Sustain: Hold until note off
                - Release: Fall from sustain level to 0

            Example usage:
                env = caspy.adsr.ADSR()
                env.set_attack_time(0.01)
                env.note_on()
        )pbdoc")
        .def(py::init<>(), "Construct an ADSR envelope with default parameters")
        .def("note_on", &ADSRFloat::noteOn,
             "Trigger the attack phase of the envelope")
        .def("note_off", &ADSRFloat::noteOff,
             "Trigger the release phase of the envelope")
        .def("reset", &ADSRFloat::reset,
             "Reset the envelope to idle state")
        .def("render", &ADSRFloat::render,
             "Generate the next sample of the envelope (float)")
        .def("set_attack_time", &ADSRFloat::setAttackTime,
             py::arg("attack_time_s"),
             "Set attack time in seconds")
        .def("set_decay_time", &ADSRFloat::setDecayTime,
             py::arg("decay_time_s"),
             "Set decay time in seconds (call after set_sustain_level)")
        .def("set_sustain_level", &ADSRFloat::setSustainLevel,
             py::arg("sustain_level"),
             "Set sustain level in range [0.0, 1.0]")
        .def("set_release_time", &ADSRFloat::setReleaseTime,
             py::arg("release_time_s"),
             "Set release time in seconds")
        .def("set_sample_rate", &ADSRFloat::setSampleRate,
             py::arg("sample_rate"),
             "Set the envelope sample rate in Hz")
        .def("get_state", &ADSRFloat::getState,
             "Get the current state of the envelope as a string")
        .def("render_algorithm", &render_algorithm, py::arg("num_samples"),
             "Render `num_samples` of the envelope as a NumPy array for Python");
}
