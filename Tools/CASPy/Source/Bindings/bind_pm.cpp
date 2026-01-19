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

    py::enum_<Envelope::State>(pm, "EnvelopeState",
        "ADSR envelope state")
        .value("Idle", Envelope::State::idle)
        .value("Attack", Envelope::State::attack)
        .value("Decay", Envelope::State::decay)
        .value("Slope", Envelope::State::slope)
        .value("Sustain", Envelope::State::sustain)
        .value("Release", Envelope::State::release)
        .value("NoteOn", Envelope::State::noteOn)
        .value("NoteOff", Envelope::State::noteOff)
        .export_values();

    // ========================================================================
    // ADSR Envelope
    // ========================================================================

    py::class_<Envelope::ADSR<float>>(pm, "ADSR",
        R"pbdoc(
            ADSR (Attack-Decay-Sustain-Release) envelope generator.

            Uses analog-style exponential curves for natural-sounding envelopes.
            Based on Redmon's analog equations for accurate modeling of analog
            envelope behavior.

            Stages:
                - Attack: Rise from 0 to peak (1.0)
                - Decay: Fall from peak to sustain level
                - Sustain: Hold at sustain level until note off
                - Release: Fall from current level to 0
        )pbdoc")
        .def(py::init<>(),
             "Construct ADSR envelope with default parameters")
        .def("note_on",
             &Envelope::ADSR<float>::noteOn,
             "Trigger envelope attack phase")
        .def("note_off",
             &Envelope::ADSR<float>::noteOff,
             "Trigger envelope release phase")
        .def("reset",
             &Envelope::ADSR<float>::reset,
             "Reset envelope to idle state")
        .def("render",
             &Envelope::ADSR<float>::render,
             R"pbdoc(
                Generate next envelope sample.

                Returns:
                    float: Envelope amplitude [0.0, 1.0]
             )pbdoc")
        .def("set_attack_time",
             &Envelope::ADSR<float>::setAttackTime,
             py::arg("attack_time_s"),
             "Set attack time in seconds")
        .def("set_decay_time",
             &Envelope::ADSR<float>::setDecayTime,
             py::arg("decay_time_s"),
             "Set decay time in seconds (call after set_sustain_level)")
        .def("set_sustain_level",
             &Envelope::ADSR<float>::setSustainLevel,
             py::arg("sustain_level"),
             "Set sustain level [0.0, 1.0]")
        .def("set_release_time",
             &Envelope::ADSR<float>::setReleaseTime,
             py::arg("release_time_s"),
             "Set release time in seconds")
        .def("set_sample_rate",
             &Envelope::ADSR<float>::setSampleRate,
             py::arg("sample_rate"),
             "Set sample rate in Hz")
        .def("get_state",
             &Envelope::ADSR<float>::getState,
             "Get current envelope state as string");

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
             py::overload_cast<>(&PM::Operator<float>::render),
             "Render single sample (no external modulation)")
        .def("render",
             py::overload_cast<float>(&PM::Operator<float>::render),
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

    // ========================================================================
    // Algorithm Base Class (for reference/documentation)
    // ========================================================================

    py::class_<PM::AlgBase<float>>(pm, "AlgorithmBase",
        R"pbdoc(
            Base class for PM synthesis algorithms.

            An algorithm defines how multiple operators are connected:
            - Which operators modulate which
            - Signal flow topology
            - Output routing

            This is an abstract base - use concrete algorithm implementations.
        )pbdoc")
        .def("note_on",
             &PM::AlgBase<float>::noteOn,
             "Trigger all operator envelopes")
        .def("note_off",
             &PM::AlgBase<float>::noteOff,
             "Release all operator envelopes")
        .def("reset",
             &PM::AlgBase<float>::reset,
             "Reset algorithm and all operators")
        .def("set_frequency",
             &PM::AlgBase<float>::setFrequency,
             py::arg("frequency"),
             py::arg("sample_rate"),
             "Set carrier frequency for all operators")
        .def("set_sample_rate",
             &PM::AlgBase<float>::setSampleRate,
             py::arg("sample_rate"),
             "Set sample rate")
        .def("set_adsr",
             &PM::AlgBase<float>::setADSR,
             py::arg("attack_time_s"),
             py::arg("decay_time_s"),
             py::arg("sustain_level"),
             py::arg("release_time_s"),
             "Set ADSR for all operators")
        .def("enable_adsr",
             &PM::AlgBase<float>::enableADSR,
             "Enable envelopes for all operators")
        .def("disable_adsr",
             &PM::AlgBase<float>::disableADSR,
             "Disable envelopes for all operators")
        .def("prepare_to_play",
             &PM::AlgBase<float>::prepareToPlay,
             "Prepare algorithm for playback")
        .def("get_num_operators",
             &PM::AlgBase<float>::getNumOperators,
             "Get number of operators in this algorithm")
        .def("get_sample_rate",
             &PM::AlgBase<float>::getSampleRate,
             "Get current sample rate")
        .def("get_frequency",
             &PM::AlgBase<float>::getFrequency,
             "Get current carrier frequency");

    // Note: Concrete algorithm implementations would be bound here
    // For example, if you have a TwoOpAlgorithm class:
    //
    // py::class_<PM::TwoOpAlgorithm<float>, PM::AlgBase<float>>(pm, "TwoOpAlgorithm",
    //     "Two-operator FM algorithm")
    //     .def(py::init<>())
    //     .def("render", ...)
    //     .def("set_algorithm", ...);
}