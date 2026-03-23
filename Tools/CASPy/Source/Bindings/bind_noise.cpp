/************************************************************************
 *  CASPy - Python Bindings for CASPI
 *  bind_noise.cpp
 ************************************************************************/

#include "oscillators/caspi_Noise.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace CASPI;

using NoiseOscWhite = Oscillators::NoiseOscillator<float, Oscillators::NoiseAlgorithm::White>;
using NoiseOscPink  = Oscillators::NoiseOscillator<float, Oscillators::NoiseAlgorithm::Pink>;

// ---------------------------------------------------------------------------
// render helpers — mirror render_wavetable* pattern exactly
// ---------------------------------------------------------------------------

static py::array_t<float> render_white (NoiseOscWhite& osc, int num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = result.request();
    osc.renderBlock (static_cast<float*> (buf.ptr), num_samples);
    return result;
}

static py::array_t<float> render_pink (NoiseOscPink& osc, int num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = result.request();
    osc.renderBlock (static_cast<float*> (buf.ptr), num_samples);
    return result;
}

// ---------------------------------------------------------------------------
// bind_noise
// ---------------------------------------------------------------------------

void bind_noise (py::module_& m)
{
    auto n_m = m.def_submodule ("noise",
        "Noise oscillators (White: xoshiro128+; Pink: Voss-McCartney 8-stage IIR).");

    // -----------------------------------------------------------------------
    // NoiseAlgorithm enum
    // -----------------------------------------------------------------------

    py::enum_<Oscillators::NoiseAlgorithm> (n_m, "NoiseAlgorithm",
        "Selects the noise generation algorithm.")
        .value ("White", Oscillators::NoiseAlgorithm::White,
                "White noise via xoshiro128+. Flat power spectrum.")
        .value ("Pink",  Oscillators::NoiseAlgorithm::Pink,
                "Pink noise via Voss-McCartney 8-stage IIR. -3 dB/octave PSD.")
        .export_values();

    // -----------------------------------------------------------------------
    // NoiseOscillatorWhite
    // -----------------------------------------------------------------------

    py::class_<NoiseOscWhite> (n_m, "NoiseOscillatorWhite",
        R"pbdoc(
            White noise oscillator (xoshiro128+, float32).

            Power spectrum: flat.
            Cost: ~2 ns/sample (x86-64, no division).

            Example:
                osc = caspi.noise.NoiseOscillatorWhite(44100.0)
                audio = osc.render(4410)   # 0.1 s

            Modulation:
                osc.amplitude.add_modulation(env_out)
                sample = osc.render_sample()
                osc.amplitude.clear_modulation()
        )pbdoc")
        .def (py::init<>(), "Default constructor.")
        .def (py::init<float> (), py::arg ("sample_rate"),
              "Construct with sample rate (Hz). Noise is not SR-dependent but kept for API consistency.")

        .def ("set_amplitude", &NoiseOscWhite::setAmplitude, py::arg ("amplitude"),
              "Set amplitude in [0, 1], bypassing parameter smoothing.")
        .def ("seed",          &NoiseOscWhite::seed,         py::arg ("seed"),
              "Re-seed the PRNG. Use for reproducible test vectors.")
        .def ("reset",         &NoiseOscWhite::reset,
              "Reset PRNG state to defaults.")
        .def ("set_sample_rate", &NoiseOscWhite::setSampleRate, py::arg ("sample_rate"),
              "Set sample rate (Hz).")

        .def ("render_sample", &NoiseOscWhite::renderSample,
              "Render one sample. Advances amplitude smoother.")
        .def ("render",        &render_white, py::arg ("num_samples"),
              "Render num_samples via renderBlock(). Returns float32 NumPy array.")

        .def_property_readonly ("amplitude",
              [](NoiseOscWhite& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
              py::return_value_policy::reference_internal,
              "Output amplitude [0, 1]. ModulatableParameter<float>.");

    // -----------------------------------------------------------------------
    // NoiseOscillatorPink
    // -----------------------------------------------------------------------

    py::class_<NoiseOscPink> (n_m, "NoiseOscillatorPink",
        R"pbdoc(
            Pink noise oscillator (Voss-McCartney 8-stage IIR, float32).

            Power spectrum: -3 dB/octave (-10 dB/decade).
            Cost: ~5 ns/sample (8 FMAs + white noise generation, x86-64).
            Output is normalised to approx. [-1, 1] via empirical scale factor.

            Reference: McCartney (1999) https://www.firstpr.com.au/dsp/pink-noise/
                       Kellett coefficient refinement.

            Example:
                osc = caspi.noise.NoiseOscillatorPink(44100.0)
                osc.seed(42)
                audio = osc.render(44100)
        )pbdoc")
        .def (py::init<>(), "Default constructor.")
        .def (py::init<float> (), py::arg ("sample_rate"),
              "Construct with sample rate (Hz).")

        .def ("set_amplitude", &NoiseOscPink::setAmplitude, py::arg ("amplitude"),
              "Set amplitude in [0, 1], bypassing parameter smoothing.")
        .def ("seed",          &NoiseOscPink::seed,         py::arg ("seed"),
              "Re-seed the PRNG and reset IIR filter state.")
        .def ("reset",         &NoiseOscPink::reset,
              "Reset PRNG and filter state to defaults.")
        .def ("set_sample_rate", &NoiseOscPink::setSampleRate, py::arg ("sample_rate"),
              "Set sample rate (Hz).")

        .def ("render_sample", &NoiseOscPink::renderSample,
              "Render one sample.")
        .def ("render",        &render_pink, py::arg ("num_samples"),
              "Render num_samples via renderBlock(). Returns float32 NumPy array.")

        .def_property_readonly ("amplitude",
              [](NoiseOscPink& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
              py::return_value_policy::reference_internal,
              "Output amplitude [0, 1]. ModulatableParameter<float>.");
}