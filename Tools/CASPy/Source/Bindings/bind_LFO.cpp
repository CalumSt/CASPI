/************************************************************************
 *  CASPy — Python bindings for CASPI
 *  bind_lfo.cpp
 ************************************************************************/

#include "oscillators/caspi_LFO.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace CASPI;

using LfoF = Oscillators::LFO<float>;

static py::array_t<float> render_lfo (LfoF& osc, int num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = result.request();
    osc.renderBlock (static_cast<float*> (buf.ptr), num_samples);
    return result;
}

void bind_lfo (py::module_& m)
{
    auto lfo_m = m.def_submodule ("lfo",
        "Low-frequency oscillator with unipolar/bipolar output, tempo sync, and one-shot mode.");

    py::enum_<Oscillators::LfoShape> (lfo_m, "LfoShape")
        .value ("Sine",       Oscillators::LfoShape::Sine)
        .value ("Triangle",   Oscillators::LfoShape::Triangle)
        .value ("Saw",        Oscillators::LfoShape::Saw,        "Rising: -1 at phase=0, +1 at phase=1.")
        .value ("ReverseSaw", Oscillators::LfoShape::ReverseSaw, "Falling: +1 at phase=0, -1 at phase=1.")
        .value ("Square",     Oscillators::LfoShape::Square)
        .export_values();

    py::enum_<Oscillators::LfoOutputMode> (lfo_m, "LfoOutputMode")
        .value ("Bipolar",  Oscillators::LfoOutputMode::Bipolar,  "Output in [-1, 1].")
        .value ("Unipolar", Oscillators::LfoOutputMode::Unipolar, "Output in [0, 1].")
        .export_values();

    py::class_<LfoF> (lfo_m, "LFO",
        R"pbdoc(
            Low-frequency oscillator (float32).

            Rate range: 0.01–20 Hz. Shapes: Sine, Triangle, Saw, ReverseSaw, Square.
            Output modes: Bipolar [-1, 1], Unipolar [0, 1].

            Example:
                lfo = caspi.lfo.LFO(44100.0, 2.0)          # 2 Hz sine
                lfo.set_shape(caspi.lfo.LfoShape.Triangle)
                lfo.set_output_mode(caspi.lfo.LfoOutputMode.Unipolar)
                mod = lfo.render(4410)                       # 0.1 s

            Tempo sync:
                lfo.set_tempo_sync(120.0, 4.0)              # 1 cycle per bar at 120 BPM

            One-shot (e.g. pitch bend on note-on):
                lfo.set_one_shot(True)
                lfo.reset_phase()
                samples = lfo.render(44100)
        )pbdoc")
        .def (py::init<>())
        .def (py::init<float, float> (), py::arg ("sample_rate"), py::arg ("rate_hz"))
        .def (py::init<float, float, Oscillators::LfoShape> (),
         py::arg ("sample_rate"), py::arg ("rate_hz"), py::arg ("shape"))
        .def (py::init<float, float, Oscillators::LfoShape, Oscillators::LfoOutputMode> (),
         py::arg ("sample_rate"), py::arg ("rate_hz"), py::arg ("shape"), py::arg ("mode"))

        .def ("set_rate",        &LfoF::setRate,        py::arg ("hz"))
        .def ("set_amplitude",   &LfoF::setAmplitude,   py::arg ("amplitude"))
        .def ("set_shape",       &LfoF::setShape,       py::arg ("shape"))
        .def ("set_output_mode", &LfoF::setOutputMode,  py::arg ("mode"))
        .def ("set_one_shot",    &LfoF::setOneShot,     py::arg ("enable"))
        .def ("set_phase_offset",&LfoF::setPhaseOffset, py::arg ("offset"))
        .def ("set_sample_rate", &LfoF::setSampleRate,  py::arg ("sample_rate"))
        .def ("set_tempo_sync",  &LfoF::setTempoSync,
              py::arg ("bpm"), py::arg ("beats_per_cycle"),
              "Compute rate from BPM. Re-call on tempo change.")

        .def ("reset_phase",   &LfoF::resetPhase)
        .def ("force_sync",    &LfoF::forceSync)
        .def ("phase_wrapped", &LfoF::phaseWrapped)
        .def ("is_halted",     &LfoF::isHalted)

        .def ("render_sample", &LfoF::renderSample)
        .def ("render",        &render_lfo, py::arg ("num_samples"),
              "Render num_samples. Returns float32 NumPy array.")

        .def_property_readonly ("rate",
              [] (LfoF& self) -> Core::ModulatableParameter<float>& { return self.rate; },
              py::return_value_policy::reference_internal)
        .def_property_readonly ("amplitude",
              [] (LfoF& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
              py::return_value_policy::reference_internal);
}