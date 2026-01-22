/************************************************************************
 *  CASPy - Python Bindings for CASPI
 *  bind_pm.cpp
 *
 *  Bindings for Phase Modulation synthesis components:
 *  - PM Operators (sine oscillators with envelopes and feedback)
 *  - PM Algorithms (combinations of operators for FM/PM synthesis)
 *  - ADSR Envelopes
 ************************************************************************/

#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

/**
 * @brief Render PM operator to numpy array
 *
 * @tparam OpType Operator type
 * @param op Operator instance
 * @param num_samples Number of samples to generate
 * @return Numpy array of rendered samples
 */
template <typename OpType>
py::array_t<float> render_operator(OpType& op, size_t num_samples)
{
    py::array_t<float> result(num_samples);
    auto buf = result.request();
    float* ptr = static_cast<float*>(buf.ptr);

    for (size_t i = 0; i < num_samples; ++i) {
        ptr[i] = op.render();
    }

    return result;
}

/**
 * @brief Render PM algorithm to numpy array
 *
 * @tparam AlgType Algorithm type
 * @param alg Algorithm instance
 * @param num_samples Number of samples to generate
 * @return Numpy array of rendered samples
 */
template <typename AlgType>
py::array_t<float> render_algorithm(AlgType& alg, size_t num_samples)
{
    py::array_t<float> result(num_samples);
    auto buf = result.request();
    float* ptr = static_cast<float*>(buf.ptr);

    for (size_t i = 0; i < num_samples; ++i) {
        ptr[i] = alg.render();
    }

    return result;
}

void bind_pm(py::module_& m)
{
    // Create PM submodule
    auto pm = m.def_submodule("pm", "Phase Modulation synthesis components");

    // ========================================================================
    // Enums
    // ========================================================================

    py::enum_<PM::ModulationMode>(pm, "ModulationMode",
        "Modulation mode for PM operators")
        .value("Phase", PM::ModulationMode::Phase,
               "Phase modulation (traditional FM)")
        .value("Frequency", PM::ModulationMode::Frequency,
               "Frequency modulation")
        .export_values();

    py::enum_<PM::OpIndex>(pm, "OpIndex",
        "Operator index for addressing specific operators in algorithms")
        .value("OpA", PM::OpIndex::OpA)
        .value("OpB", PM::OpIndex::OpB)
        .value("OpC", PM::OpIndex::OpC)
        .value("OpD", PM::OpIndex::OpD)
        .value("OpE", PM::OpIndex::OpE)
        .value("OpF", PM::OpIndex::OpF)
        .value("OpG", PM::OpIndex::OpG)
        .value("OpH", PM::OpIndex::OpH)
        .value("OpI", PM::OpIndex::OpI)
        .value("OpJ", PM::OpIndex::OpJ)
        .value("OpK", PM::OpIndex::OpK)
        .value("OpL", PM::OpIndex::OpL)
        .value("OpM", PM::OpIndex::OpM)
        .value("All", PM::OpIndex::All)
        .export_values();

    // ========================================================================
    // PM Operator
    // ========================================================================

    py::class_<PM::Operator<float>>(pm, "Operator",
        R"pbdoc(
            Phase Modulation operator - sine oscillator with envelope and feedback.

            A PM operator is a sine wave generator that can:
            - Be modulated by other operators (FM/PM synthesis)
            - Modulate itself (self-feedback for added harmonics)
            - Apply an ADSR envelope to its output
            - Act as a carrier (audible) or modulator

            Key concepts:
                - Modulation Index: Ratio of modulator to carrier frequency
                - Modulation Depth: Amount of modulation/output level
                - Feedback: Self-modulation amount for added complexity
        )pbdoc")
        .def(py::init<>(),
             "Construct PM operator with default parameters")
        .def("set_frequency",
             &PM::Operator<float>::setFrequency,
             py::arg("frequency"),
             py::arg("sample_rate"),
             "Set carrier frequency (Hz) and sample rate")
        .def("set_sample_rate",
             &PM::Operator<float>::setSampleRate,
             py::arg("sample_rate"),
             "Set sample rate in Hz")
        .def("set_modulation",
             py::overload_cast<float, float>(&PM::Operator<float>::setModulation),
             py::arg("mod_index"),
             py::arg("mod_depth"),
             "Set modulation index and depth")
        .def("set_modulation",
             py::overload_cast<float, float, float>(&PM::Operator<float>::setModulation),
             py::arg("mod_index"),
             py::arg("mod_depth"),
             py::arg("mod_feedback"),
             "Set modulation index, depth, and feedback")
        .def("set_mod_index",
             &PM::Operator<float>::setModIndex,
             py::arg("mod_index"),
             "Set modulation index (frequency ratio)")
        .def("set_mod_depth",
             &PM::Operator<float>::setModDepth,
             py::arg("mod_depth"),
             "Set modulation depth/output level [0.0, 1.0]")
        .def("set_mod_feedback",
             &PM::Operator<float>::setModFeedback,
             py::arg("mod_feedback"),
             "Set self-modulation feedback amount")
        .def("enable_mod_feedback",
             &PM::Operator<float>::enableModFeedback,
             "Enable self-modulation feedback")
        .def("disable_mod_feedback",
             &PM::Operator<float>::disableModFeedback,
             "Disable self-modulation feedback")
        .def("enable_envelope",
             &PM::Operator<float>::enableEnvelope,
             "Enable ADSR envelope")
        .def("disable_envelope",
             &PM::Operator<float>::disableEnvelope,
             "Disable ADSR envelope")
        .def("note_on",
             &PM::Operator<float>::noteOn,
             "Trigger envelope attack")
        .def("note_off",
             &PM::Operator<float>::noteOff,
             "Trigger envelope release")
        .def("set_adsr",
             &PM::Operator<float>::setADSR,
             py::arg("attack_time_s"),
             py::arg("decay_time_s"),
             py::arg("sustain_level"),
             py::arg("release_time_s"),
             "Set all ADSR parameters at once")
        .def("set_attack_time",
             &PM::Operator<float>::setAttackTime,
             py::arg("attack_time_s"))
        .def("set_decay_time",
             &PM::Operator<float>::setDecayTime,
             py::arg("decay_time_s"))
        .def("set_sustain_level",
             &PM::Operator<float>::setSustainLevel,
             py::arg("sustain_level"))
        .def("set_release_time",
             &PM::Operator<float>::setReleaseTime,
             py::arg("release_time_s"))
     .def("render",
          static_cast<float (PM::Operator<float>::*)()>(&PM::Operator<float>::render),
          "Render single sample (no external modulation)")
     .def("render",
          static_cast<float (PM::Operator<float>::*)(float)>(&PM::Operator<float>::render),
          py::arg("modulation_signal"),
          "Render single sample with external modulation")
        .def("render",
             [](PM::Operator<float>& self, size_t num_samples) {
                 return render_operator(self, num_samples);
             },
             py::arg("num_samples"),
             "Render multiple samples as numpy array")
        .def("reset",
             &PM::Operator<float>::reset,
             "Reset operator to default state")
        .def("get_frequency",
             &PM::Operator<float>::getFrequency,
             "Get current carrier frequency")
        .def("get_sample_rate",
             &PM::Operator<float>::getSampleRate,
             "Get current sample rate")
        .def("get_mod_index",
             &PM::Operator<float>::getModulationIndex,
             "Get modulation index")
        .def("get_mod_depth",
             &PM::Operator<float>::getModulationDepth,
             "Get modulation depth")
        .def("get_mod_feedback",
             &PM::Operator<float>::getModulationFeedback,
             "Get feedback amount")
        .def_readwrite("modulation_mode",
                       &PM::Operator<float>::modulationMode,
                       "Modulation mode (Phase or Frequency)");
}