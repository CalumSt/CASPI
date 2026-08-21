/*
 * bind_gain.cpp
 *
 * Binds:
 *   - Gain<float>            (plain ramped-gain struct, not a graph node)
 *   - WaveshapeType enum
 *   - Waveshaper<float>      (AudioNode)
 */

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Node.h"

namespace py = pybind11;

using F = float;
using NodeBase_t = CASPI::Graph::NodeBase<float>;
using Gain_t = CASPI::Gain<float>;
using Waveshaper_t = CASPI::Distortion::Waveshaper<float>;
using WaveshapeType = CASPI::Distortion::WaveshapeType;

void bind_gain (py::module_& m)
{
    /*----------------------------------------------------------------------
     * Gain -- plain ramped-gain struct, not graph-usable
     *--------------------------------------------------------------------*/
    py::class_<Gain_t> (m, "Gain", py::dynamic_attr(),
    R"pbdoc(
        Ramped gain processor. Not a graph node -- use Waveshaper (or a
        DynamicsBase-derived node) for graph-based level shaping.

        Usage:
            g = caspy.Gain()
            g.set_gain(0.5, 44100.0)
            out = g.process_block(samples)
    )pbdoc")

        .def (py::init<>())

        .def ("set_gain",
            [] (Gain_t& self, F gain, F sr, bool override_) { self.setGain (gain, sr, override_); },
            py::arg ("gain"), py::arg ("sample_rate"), py::arg ("override") = false)
        .def ("set_gain_db",
            [] (Gain_t& self, F gainDb, F sr, bool override_) { self.setGain_db (gainDb, sr, override_); },
            py::arg ("gain_db"), py::arg ("sample_rate"), py::arg ("override") = false)
        .def ("set_gain_ramp_duration",
            [] (Gain_t& self, F seconds, F sr) { self.setGainRampDuration (seconds, sr); },
            py::arg ("seconds"), py::arg ("sample_rate"))
        .def ("set_sample_rate", &Gain_t::setSampleRate, py::arg ("sample_rate"))
        .def ("reset", &Gain_t::reset)

        .def ("get_gain", &Gain_t::getGain)
        .def ("is_ramp_up", &Gain_t::isRampUp)
        .def ("is_ramp_down", &Gain_t::isRampDown)

        .def ("process_sample",
            [] (Gain_t& self, F x) { self.apply (x); return x; },
            py::arg ("x"))
        .def ("process_block",
            [] (Gain_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
            {
                const auto        in = arr.unchecked<1>();
                const py::ssize_t n  = arr.shape (0);
                py::array_t<F>   out (n);
                auto             ob  = out.mutable_unchecked<1>();
                for (py::ssize_t i = 0; i < n; ++i)
                {
                    F x = in (i);
                    self.apply (x);
                    ob (i) = x;
                }
                return out;
            },
            py::arg ("samples"),
            "Process a 1-D float32 numpy array. Returns output array.")
        .def ("process_block_inplace",
            [] (Gain_t& self, py::array_t<F, py::array::c_style> arr)
            {
                auto             buf = arr.mutable_unchecked<1>();
                const py::ssize_t n  = arr.shape (0);
                for (py::ssize_t i = 0; i < n; ++i)
                {
                    F x = buf (i);
                    self.apply (x);
                    buf (i) = x;
                }
            },
            py::arg ("samples"),
            "Process a 1-D float32 numpy array in-place, overwriting the input.");

    /*----------------------------------------------------------------------
     * WaveshapeType enum
     *--------------------------------------------------------------------*/
    py::enum_<WaveshapeType> (m, "WaveshapeType")
        .value ("Linear",      WaveshapeType::Linear)
        .value ("HardClip",    WaveshapeType::HardClip)
        .value ("SoftClip",    WaveshapeType::SoftClip)
        .value ("Cubic",       WaveshapeType::Cubic)
        .value ("Araya",       WaveshapeType::Araya)
        .value ("Sine",        WaveshapeType::Sine)
        .value ("Tan",         WaveshapeType::Tan)
        .value ("Arctan",      WaveshapeType::Arctan)
        .value ("Sigmoid",     WaveshapeType::Sigmoid)
        .value ("TanhDrive",   WaveshapeType::TanhDrive)
        .value ("ArctanDrive", WaveshapeType::ArctanDrive)
        .value ("AnalogKnee",  WaveshapeType::AnalogKnee)
        .value ("Custom",      WaveshapeType::Custom)
        .export_values();

    /*----------------------------------------------------------------------
     * Waveshaper
     *--------------------------------------------------------------------*/
    py::class_<Waveshaper_t, NodeBase_t, std::unique_ptr<Waveshaper_t, py::nodelete>> (m, "Waveshaper",
    py::dynamic_attr(),
    R"pbdoc(
        Stateless waveshaper/distortion node with a library of built-in
        transfer curves, plus custom curve registration.

        Standalone:
            w = caspy.Waveshaper()
            w.waveshape = caspy.WaveshapeType.SoftClip
            w.clip_limit = 0.8
            out = w.process_block(samples)

        Graph node:
            w_id = g.add_node(caspy.Waveshaper())
            g.get_node(w_id).waveshape = caspy.WaveshapeType.HardClip
    )pbdoc")

        .def (py::init ([] () { return new Waveshaper_t(); }))

        .def ("set_waveshape",          &Waveshaper_t::setWaveshape,         py::arg ("type"))
        .def ("set_negative_waveshape", &Waveshaper_t::setNegativeWaveshape, py::arg ("type"))
        .def ("set_asymmetry",          &Waveshaper_t::setAsymmetry,
              py::arg ("enabled"), py::arg ("asymmetry_point"))
        .def ("set_clip_limit",         &Waveshaper_t::setClipLimit,  py::arg ("limit"))
        .def ("set_drive",              &Waveshaper_t::setDrive,      py::arg ("drive"))
        .def ("set_drive_db",           &Waveshaper_t::setDriveDb,    py::arg ("drive_db"))
        .def ("set_analog_knee",        &Waveshaper_t::setAnalogKnee, py::arg ("amount"))

        .def ("register_custom_waveshape",
            [] (Waveshaper_t& self, const std::string& name, std::function<F (F)> fn)
            {
                self.registerCustomWaveshape (name, fn);
            },
            py::arg ("name"), py::arg ("fn"),
            "Register a Python callable (float -> float) as a named curve. "
            "Not real-time safe -- call before/between blocks, not from an "
            "audio callback.")
        .def ("set_custom_waveshape", &Waveshaper_t::setCustomWaveshape, py::arg ("name"),
              "Select a previously registered custom curve by name. Returns "
              "True on success; leaves the active curve unchanged otherwise.")

        .def_property ("waveshape",
            [] (const Waveshaper_t& s) { return s.getWaveshape(); },
            [] (Waveshaper_t& s, WaveshapeType t) { s.setWaveshape (t); })
        .def_property ("negative_waveshape",
            [] (const Waveshaper_t& s) { return s.getNegativeWaveshape(); },
            [] (Waveshaper_t& s, WaveshapeType t) { s.setNegativeWaveshape (t); })
        .def_property ("clip_limit",
            [] (const Waveshaper_t& s) { return s.getClipLimit(); },
            [] (Waveshaper_t& s, F v) { s.setClipLimit (v); })
        .def_property ("drive",
            [] (const Waveshaper_t& s) { return s.getDrive(); },
            [] (Waveshaper_t& s, F v) { s.setDrive (v); })
        .def_property ("analog_knee",
            [] (const Waveshaper_t& s) { return s.getAnalogKnee(); },
            [] (Waveshaper_t& s, F v) { s.setAnalogKnee (v); })
        .def_property_readonly ("is_asymmetric", &Waveshaper_t::getIsAsymmetric)

        .def ("process_sample", &Waveshaper_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Waveshaper_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
            {
                const auto        in = arr.unchecked<1>();
                const py::ssize_t n  = arr.shape (0);
                py::array_t<F>   out (n);
                auto             ob  = out.mutable_unchecked<1>();
                for (py::ssize_t i = 0; i < n; ++i)
                {
                    ob (i) = self.processSample (in (i));
                }
                return out;
            },
            py::arg ("samples"),
            "Process a 1-D float32 numpy array. Returns output array.")
        .def ("process_block_inplace",
            [] (Waveshaper_t& self, py::array_t<F, py::array::c_style> arr)
            {
                auto             buf = arr.mutable_unchecked<1>();
                const py::ssize_t n  = arr.shape (0);
                for (py::ssize_t i = 0; i < n; ++i)
                {
                    buf (i) = self.processSample (buf (i));
                }
            },
            py::arg ("samples"),
            "Process a 1-D float32 numpy array in-place, overwriting the input.");
}
