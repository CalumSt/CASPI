/************************************************************************
 *  CASPy - Python Bindings for CASPI
 *  bind_wavetable.cpp
 ************************************************************************/

#include "oscillators/caspi_WavetableOscillator.h"

#include <pybind11/functional.h>  // REQUIRED for std::function<float(float)> lambdas
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>         // REQUIRED for std::vector / std::array conversion

namespace py = pybind11;
using namespace CASPI;

/*
 * Concrete instantiations exposed to Python.
 *
 * Template parameters are fixed at compile time; Python receives two variants:
 *   WaveTable1        — WaveTable<float, 2048>          (single table)
 *   WaveTableBank1    — WaveTableBank<float, 2048, 1>   (single-table bank, no morph)
 *   WavetableOsc1     — WavetableOscillator<float, 2048, 1>
 *
 *   WaveTableBank4    — WaveTableBank<float, 2048, 4>   (4-table morph bank)
 *   WavetableOsc4     — WavetableOscillator<float, 2048, 4>
 *
 * To add other sizes, replicate the using-declarations and registration blocks.
 */

using WaveTable1     = Oscillators::WaveTable<float, 2048>;
using WaveTableBank1 = Oscillators::WaveTableBank<float, 2048, 1>;
using WavetableOsc1  = Oscillators::WavetableOscillator<float, 2048, 1>;

using WaveTableBank4 = Oscillators::WaveTableBank<float, 2048, 4>;
using WavetableOsc4  = Oscillators::WavetableOscillator<float, 2048, 4>;

// ---------------------------------------------------------------------------
// render helpers — mirror render_blep() pattern exactly
// ---------------------------------------------------------------------------

/**
 * @brief Render num_samples from a WavetableOscillator<1> into a NumPy array.
 *
 * Uses renderBlock() to avoid repeated Python→C++ call overhead.
 * @param osc         Oscillator instance (single-table).
 * @param num_samples Number of samples to generate.
 * @return            float32 NumPy array of length num_samples.
 */
py::array_t<float> render_wavetable1 (WavetableOsc1& osc, int num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = result.request();
    osc.renderBlock (static_cast<float*> (buf.ptr), num_samples);
    return result;
}

/**
 * @brief Render num_samples from a WavetableOscillator<4> into a NumPy array.
 *
 * @param osc         Oscillator instance (4-table morph).
 * @param num_samples Number of samples to generate.
 * @return            float32 NumPy array of length num_samples.
 */
py::array_t<float> render_wavetable4 (WavetableOsc4& osc, int num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = result.request();
    osc.renderBlock (static_cast<float*> (buf.ptr), num_samples);
    return result;
}

// ---------------------------------------------------------------------------
// WaveTable fill helper — accepts a Python callable f(float) -> float
// ---------------------------------------------------------------------------

/**
 * @brief Wrap WaveTable::fillWith so a Python lambda can be used directly.
 *
 *   table.fill_with(lambda t: math.sin(2 * math.pi * t))
 *
 * pybind11/functional.h handles the std::function conversion automatically.
 */
static WaveTable1& fill_with_py (WaveTable1& self, std::function<float (float)> fn)
{
    return self.fillWith (fn);
}

/**
 * @brief Render num_samples of secondary synced to primary's phase wraps.
 *
 * Primary is rendered for timing only — its output is discarded.
 * On each primary phase wrap, secondary.forceSync() is called before
 * rendering the secondary sample.
 *
 * Returns a tuple: (audio: float32 ndarray, sync_indices: list[int])
 * sync_indices contains the sample index of each sync event, for plotting.
 */
static py::tuple render_hard_sync_1_4 (WavetableOsc1& primary,
                                        WavetableOsc4& secondary,
                                        int            num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = static_cast<float*> (result.request().ptr);

    std::vector<int> sync_points;
    sync_points.reserve (64);

    for (int i = 0; i < num_samples; ++i)
    {
        primary.renderSample();
        if (primary.phaseWrapped())
        {
            secondary.forceSync();
            sync_points.push_back (i);
        }
        buf[i] = secondary.renderSample();
    }

    return py::make_tuple (result, sync_points);
}

// Variant: both oscillators are WavetableOsc4
static py::tuple render_hard_sync_4_4 (WavetableOsc4& primary,
                                        WavetableOsc4& secondary,
                                        int            num_samples)
{
    py::array_t<float> result (num_samples);
    auto buf = static_cast<float*> (result.request().ptr);

    std::vector<int> sync_points;
    sync_points.reserve (64);

    for (int i = 0; i < num_samples; ++i)
    {
        primary.renderSample();
        if (primary.phaseWrapped())
        {
            secondary.forceSync();
            sync_points.push_back (i);
        }
        buf[i] = secondary.renderSample();
    }

    return py::make_tuple (result, sync_points);
}

// ---------------------------------------------------------------------------
// bind_wavetable
// ---------------------------------------------------------------------------

void bind_wavetable (py::module_& m)
{
    auto wt_m = m.def_submodule ("wavetable",
        "Single-cycle wavetable oscillator with morphing and per-sample modulation.");

    // -----------------------------------------------------------------------
    // InterpolationMode enum
    // -----------------------------------------------------------------------

    py::enum_<Oscillators::InterpolationMode> (wt_m, "InterpolationMode",
        "Table interpolation kernel for WavetableOscillator.")
        .value ("Linear",  Oscillators::InterpolationMode::Linear,
                "Branchless linear interpolation. Sufficient for TableSize >= 2048.")
        .value ("Hermite", Oscillators::InterpolationMode::Hermite,
                "4-point Catmull-Rom cubic. ~+6 dB SNR at ~4x FMA cost.")
        .export_values();

    // -----------------------------------------------------------------------
    // WaveTable  (shared by both bank variants — same type)
    // -----------------------------------------------------------------------

    py::class_<WaveTable1> (wt_m, "WaveTable",
        R"pbdoc(
            Single-cycle normalised waveform (float32, 2048 samples).

            Fill methods return self for chaining:

                table = caspi.wavetable.WaveTable()
                table.fill_sine()

                table.fill_with(lambda t: math.sin(2 * math.pi * t))

            Direct sample access:

                v = table[512]
                table[0] = 0.0
        )pbdoc")
        .def (py::init<>(), "Default constructor — zero-initialised.")

        // Fill methods — return self so Python can chain them
        .def ("fill_sine",     &WaveTable1::fillSine,
              py::return_value_policy::reference,
              "Fill with one period of a sine wave. Returns self.")
        .def ("fill_saw",      &WaveTable1::fillSaw,
              py::return_value_policy::reference,
              "Fill with a rising sawtooth in [-1, 1]. Returns self.")
        .def ("fill_triangle", &WaveTable1::fillTriangle,
              py::return_value_policy::reference,
              "Fill with a triangle wave in [-1, 1]. Returns self.")
        .def ("fill_with",     &fill_with_py,
              py::arg ("fn"),
              py::return_value_policy::reference,
              R"pbdoc(
                  Fill using a callable fn(normalised_phase: float) -> float.
                  Phase argument is in [0, 1). Returns self.
              )pbdoc")

        // Sample access
        .def ("__getitem__",
              [](const WaveTable1& self, std::size_t i) { return self[i]; },
              py::arg ("i"), "Read sample at index i.")
        .def ("__setitem__",
              [](WaveTable1& self, std::size_t i, float v) { self[i] = v; },
              py::arg ("i"), py::arg ("value"), "Write sample at index i.")
        .def ("__len__",
              [](const WaveTable1&) { return WaveTable1::size; },
              "Number of samples (always 2048).")

        // Read methods — useful for testing / inspection
        .def ("read_linear",
              &WaveTable1::readLinear, py::arg ("phase"),
              "Linear interpolated read at normalised phase in [0, 1).")
        .def ("read_hermite",
              &WaveTable1::readHermite, py::arg ("phase"),
              "Catmull-Rom cubic read at normalised phase in [0, 1).")

        // Numpy view — zero-copy if possible
        .def ("to_numpy",
              [](WaveTable1& self) {
                  return py::array_t<float> (
                      { WaveTable1::size },           // shape
                      { sizeof (float) },             // strides
                      self.data(),                    // ptr
                      py::cast (self));               // base object keeps WaveTable alive
              },
              "Return a float32 NumPy view of the table data (zero-copy).");

    // -----------------------------------------------------------------------
    // WaveTableBank1  (single table, no morph)
    // -----------------------------------------------------------------------

    py::class_<WaveTableBank1> (wt_m, "WaveTableBank1",
        R"pbdoc(
            Single-table bank (float32, 2048 samples, 1 table).

            The morph path is eliminated at compile time; morphPosition has no effect.

            Example:
                bank = caspi.wavetable.WaveTableBank1()
                bank[0].fill_sine()
                osc = caspi.wavetable.WavetableOscillator1(bank, 44100.0, 440.0)
        )pbdoc")
        .def (py::init<>(), "Default constructor — tables are zero-initialised.")

        .def ("__getitem__",
              [](WaveTableBank1& self, std::size_t i) -> WaveTable1& { return self[i]; },
              py::arg ("i"),
              py::return_value_policy::reference_internal,
              "Return reference to table i.")
        .def ("fill_all",
              [](WaveTableBank1& self, std::function<float (float)> fn) -> WaveTableBank1& {
                  return self.fillAll (fn);
              },
              py::arg ("fn"),
              py::return_value_policy::reference,
              "Fill all tables with fn(normalised_phase). Returns self.")
        .def ("fill_table",
              [](WaveTableBank1& self, std::size_t i, std::function<float (float)> fn) -> WaveTableBank1& {
                  return self.fillTable (i, fn);
              },
              py::arg ("i"), py::arg ("fn"),
              py::return_value_policy::reference,
              "Fill table i with fn(normalised_phase). Returns self.")
        .def_property_readonly_static ("num_tables",
              [](py::object) { return WaveTableBank1::numTables; },
              "Number of tables (1).")
        .def_property_readonly_static ("table_size",
              [](py::object) { return WaveTableBank1::tableSize; },
              "Samples per table (2048).");

    // -----------------------------------------------------------------------
    // WaveTableBank4  (4-table morph bank)
    // -----------------------------------------------------------------------

    py::class_<WaveTableBank4> (wt_m, "WaveTableBank4",
        R"pbdoc(
            Four-table morph bank (float32, 2048 samples per table).

            Adjacent tables are crossfaded by the oscillator's morph_position parameter.

            Example:
                bank = caspi.wavetable.WaveTableBank4()
                bank[0].fill_sine()
                bank[1].fill_saw()
                bank[2].fill_triangle()
                bank[3].fill_with(lambda t: math.sin(4 * math.pi * t))  # 2nd harmonic

                osc = caspi.wavetable.WavetableOscillator4(bank, 44100.0, 440.0)
                osc.set_morph_position(1.5)  # halfway between saw and triangle
        )pbdoc")
        .def (py::init<>(), "Default constructor — tables are zero-initialised.")

        .def ("__getitem__",
              [](WaveTableBank4& self, std::size_t i) -> WaveTable1& { return self[i]; },
              py::arg ("i"),
              py::return_value_policy::reference_internal,
              "Return reference to table i (0–3).")
        .def ("fill_all",
              [](WaveTableBank4& self, std::function<float (float)> fn) -> WaveTableBank4& {
                  return self.fillAll (fn);
              },
              py::arg ("fn"),
              py::return_value_policy::reference,
              "Fill all four tables with fn(normalised_phase). Returns self.")
        .def ("fill_table",
              [](WaveTableBank4& self, std::size_t i, std::function<float (float)> fn) -> WaveTableBank4& {
                  return self.fillTable (i, fn);
              },
              py::arg ("i"), py::arg ("fn"),
              py::return_value_policy::reference,
              "Fill table i with fn(normalised_phase). Returns self.")
        .def_property_readonly_static ("num_tables",
              [](py::object) { return WaveTableBank4::numTables; },
              "Number of tables (4).")
        .def_property_readonly_static ("table_size",
              [](py::object) { return WaveTableBank4::tableSize; },
              "Samples per table (2048).");

    // -----------------------------------------------------------------------
    // WavetableOscillator1  (single-table, no morph)
    // -----------------------------------------------------------------------

    py::class_<WavetableOsc1> (wt_m, "WavetableOscillator1",
        R"pbdoc(
            Wavetable oscillator — single table (float32, 2048 samples).

            No morph; set_morph_position() has no audible effect.
            Matches BlepOscillator API: set_frequency(), set_sample_rate(),
            phase_wrapped(), force_sync(), render().

            Example:
                bank = caspi.wavetable.WaveTableBank1()
                bank[0].fill_sine()

                osc = caspi.wavetable.WavetableOscillator1(bank, 44100.0, 440.0)
                audio = osc.render(4410)   # 0.1 s at 44.1 kHz

            Per-sample modulation (same pattern as BlepOscillator):
                osc.frequency.add_modulation(lfo_out * depth)
                sample = osc.render_sample()
                osc.frequency.clear_modulation()

            Hard sync:
                primary.render_sample()
                if primary.phase_wrapped():
                    secondary.force_sync()
        )pbdoc")
        .def (py::init<>(),
              "Default constructor. Call set_bank() and set_sample_rate() before rendering.")
        .def (py::init<WaveTableBank1&> (),
              py::arg ("bank"),
              "Construct with a bank. Call set_sample_rate() and set_frequency() before rendering.")
        .def (py::init<WaveTableBank1&, float, float> (),
              py::arg ("bank"), py::arg ("sample_rate"), py::arg ("frequency"),
              "Construct with bank, sample rate (Hz), and frequency (Hz).")

        // Configuration
        .def ("set_frequency",        &WavetableOsc1::setFrequency,    py::arg ("hz"),
              "Set frequency (Hz), bypassing parameter smoothing. Use for initialisation.")
        .def ("set_amplitude",        &WavetableOsc1::setAmplitude,    py::arg ("amplitude"),
              "Set amplitude in [0, 1], bypassing parameter smoothing.")
        .def ("set_morph_position",   &WavetableOsc1::setMorphPosition, py::arg ("pos"),
              "Set morph position in [0, num_tables-1]. No effect for single-table bank.")
        .def ("set_sample_rate",      &WavetableOsc1::setSampleRate,   py::arg ("sample_rate"),
              "Set sample rate (Hz). Must be called before rendering.")
        .def ("set_phase_offset",     &WavetableOsc1::setPhaseOffset,  py::arg ("offset"),
              "Set phase offset in [0, 1). Applied on reset_phase() and force_sync().")
        .def ("set_phase_mod_depth",  &WavetableOsc1::setPhaseModDepth, py::arg ("depth"),
              "Phase modulation depth in [-1, 1]. Added to phase before each table lookup.")
        .def ("set_interpolation_mode", &WavetableOsc1::setInterpolationMode, py::arg ("mode"),
              "Select Linear or Hermite interpolation kernel.")
        .def ("set_bank",
              [](WavetableOsc1& self, WaveTableBank1& bank) { self.setBank (bank); },
              py::arg ("bank"),
              "Swap the wavetable bank at runtime. Thread-safety is caller's responsibility.")

        // Phase control
        .def ("reset_phase",  &WavetableOsc1::resetPhase,
              "Reset phase to phase_offset.")
        .def ("force_sync",   &WavetableOsc1::forceSync,
              "Immediately reset phase to phase_offset. No discontinuity correction.")
        .def ("phase_wrapped", &WavetableOsc1::phaseWrapped,
              "True if phase wrapped on the most recent render_sample() call.")

        // Rendering
        .def ("render_sample", &WavetableOsc1::renderSample,
              "Render one sample. Advances parameter smoothers and phase.")
        .def ("render",        &render_wavetable1,
              py::arg ("num_samples"),
              R"pbdoc(
                  Render num_samples via renderBlock(). Returns a float32 NumPy array.
                  Preferred over calling render_sample() in a Python loop.
              )pbdoc")

        // Modulatable parameters — same pattern as BlepOscillator bindings
        .def_property_readonly ("amplitude",
              [](WavetableOsc1& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
              py::return_value_policy::reference_internal,
              "Output amplitude [0, 1]. ModulatableParameter<float>.")
        .def_property_readonly ("frequency",
              [](WavetableOsc1& self) -> Core::ModulatableParameter<float>& { return self.frequency; },
              py::return_value_policy::reference_internal,
              "Frequency [20, 20000 Hz], log scale. ModulatableParameter<float>.")
        .def_property_readonly ("morph_position",
              [](WavetableOsc1& self) -> Core::ModulatableParameter<float>& { return self.morphPosition; },
              py::return_value_policy::reference_internal,
              "Morph position [0, 1] normalised. No effect on single-table bank.");

    // -----------------------------------------------------------------------
    // WavetableOscillator4  (4-table morph)
    // -----------------------------------------------------------------------

    py::class_<WavetableOsc4> (wt_m, "WavetableOscillator4",
        R"pbdoc(
            Wavetable oscillator — 4-table morph (float32, 2048 samples per table).

            morph_position crossfades between adjacent tables:
              0.0  → table[0]
              1.0  → table[1]
              2.0  → table[2]
              3.0  → table[3]
            Fractional values produce linear crossfades between adjacent pairs.

            Example:
                bank = caspi.wavetable.WaveTableBank4()
                bank[0].fill_sine()
                bank[1].fill_saw()
                bank[2].fill_triangle()
                bank[3].fill_with(lambda t: math.sin(4 * math.pi * t))

                osc = caspi.wavetable.WavetableOscillator4(bank, 44100.0, 220.0)
                osc.set_morph_position(0.5)   # halfway sine→saw
                audio = osc.render(44100)

            Modulate morph in real time:
                osc.morph_position.add_modulation(lfo_out)
                sample = osc.render_sample()
                osc.morph_position.clear_modulation()
        )pbdoc")
        .def (py::init<>(),
              "Default constructor. Call set_bank() and set_sample_rate() before rendering.")
        .def (py::init<WaveTableBank4&> (),
              py::arg ("bank"),
              "Construct with a 4-table bank.")
        .def (py::init<WaveTableBank4&, float, float> (),
              py::arg ("bank"), py::arg ("sample_rate"), py::arg ("frequency"),
              "Construct with bank, sample rate (Hz), and frequency (Hz).")

        // Configuration
        .def ("set_frequency",        &WavetableOsc4::setFrequency,    py::arg ("hz"),
              "Set frequency (Hz), bypassing parameter smoothing.")
        .def ("set_amplitude",        &WavetableOsc4::setAmplitude,    py::arg ("amplitude"),
              "Set amplitude in [0, 1], bypassing parameter smoothing.")
        .def ("set_morph_position",   &WavetableOsc4::setMorphPosition, py::arg ("pos"),
              "Set morph position in [0, 3]. 0=table[0], 3=table[3], 1.5=halfway 1→2.")
        .def ("set_sample_rate",      &WavetableOsc4::setSampleRate,   py::arg ("sample_rate"),
              "Set sample rate (Hz). Must be called before rendering.")
        .def ("set_phase_offset",     &WavetableOsc4::setPhaseOffset,  py::arg ("offset"),
              "Set phase offset in [0, 1). Applied on reset_phase() and force_sync().")
        .def ("set_phase_mod_depth",  &WavetableOsc4::setPhaseModDepth, py::arg ("depth"),
              "Phase modulation depth in [-1, 1]. PM/FM input; not smoothed.")
        .def ("set_interpolation_mode", &WavetableOsc4::setInterpolationMode, py::arg ("mode"),
              "Select Linear or Hermite interpolation kernel.")
        .def ("set_bank",
              [](WavetableOsc4& self, WaveTableBank4& bank) { self.setBank (bank); },
              py::arg ("bank"),
              "Swap the wavetable bank at runtime. Thread-safety is caller's responsibility.")

        // Phase control
        .def ("reset_phase",   &WavetableOsc4::resetPhase,
              "Reset phase to phase_offset.")
        .def ("force_sync",    &WavetableOsc4::forceSync,
              "Immediately reset phase to phase_offset. No discontinuity correction.")
        .def ("phase_wrapped", &WavetableOsc4::phaseWrapped,
              "True if phase wrapped on the most recent render_sample() call.")

        // Rendering
        .def ("render_sample", &WavetableOsc4::renderSample,
              "Render one sample. Advances parameter smoothers and phase.")
        .def ("render",        &render_wavetable4,
              py::arg ("num_samples"),
              R"pbdoc(
                  Render num_samples via renderBlock(). Returns a float32 NumPy array.
                  Preferred over calling render_sample() in a Python loop.
              )pbdoc")

        // Modulatable parameters
        .def_property_readonly ("amplitude",
              [](WavetableOsc4& self) -> Core::ModulatableParameter<float>& { return self.amplitude; },
              py::return_value_policy::reference_internal,
              "Output amplitude [0, 1]. ModulatableParameter<float>.")
        .def_property_readonly ("frequency",
              [](WavetableOsc4& self) -> Core::ModulatableParameter<float>& { return self.frequency; },
              py::return_value_policy::reference_internal,
              "Frequency [20, 20000 Hz], log scale. ModulatableParameter<float>.")
        .def_property_readonly ("morph_position",
              [](WavetableOsc4& self) -> Core::ModulatableParameter<float>& { return self.morphPosition; },
              py::return_value_policy::reference_internal,
              "Morph position [0, 1] normalised (maps to [0, 3] at render time). ModulatableParameter<float>.");

    wt_m.def ("render_hard_sync",
          &render_hard_sync_1_4,
          py::arg ("primary"), py::arg ("secondary"), py::arg ("num_samples"),
          R"pbdoc(
                  Render secondary synced to primary (WavetableOscillator1 driving WavetableOscillator4).
                  Returns (audio: float32 ndarray, sync_indices: list[int]).
                  Primary output is discarded; only its phase wraps are used.
              )pbdoc");

    wt_m.def ("render_hard_sync",
              &render_hard_sync_4_4,
              py::arg ("primary"), py::arg ("secondary"), py::arg ("num_samples"),
              R"pbdoc(
                  Render secondary synced to primary (both WavetableOscillator4).
                  Returns (audio: float32 ndarray, sync_indices: list[int]).
              )pbdoc");
}