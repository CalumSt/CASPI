/*
 * bind_dynamics.cpp
 *
 * Binds:
 *   - Compressor<float>  (AudioNode; DynamicsBase's parameter API is bound
 *                          through it since DynamicsBase itself is never
 *                          instantiated directly)
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Node.h"

namespace py = pybind11;

using F = float;
using NodeBase_t = CASPI::Graph::NodeBase<float>;
using Compressor_t = CASPI::Dynamics::Compressor<float>;

void bind_dynamics (py::module_& m)
{
    /*----------------------------------------------------------------------
     * Compressor
     *--------------------------------------------------------------------*/
    py::class_<Compressor_t, NodeBase_t, std::unique_ptr<Compressor_t, py::nodelete>> (m, "Compressor",
    py::dynamic_attr(),
    R"pbdoc(
        Feedforward VCA-style compressor with a soft-knee gain computer.

        Standalone:
            c = caspy.Compressor()
            c.set_sample_rate(44100.0)
            c.threshold = -18.0
            c.ratio = 4.0
            out = c.process_block(samples)

        Graph node (with sidechain on port 1):
            c_id = g.add_node(caspy.Compressor())
            g.connect(sidechain_id, 0, c_id, 1)
    )pbdoc")

        .def (py::init ([] () { return new Compressor_t(); }))

        .def ("set_threshold",     &Compressor_t::setThreshold,   py::arg ("threshold_db"))
        .def ("set_ratio",         &Compressor_t::setRatio,       py::arg ("ratio"))
        .def ("set_knee",          &Compressor_t::setKnee,        py::arg ("knee_db"))
        .def ("set_attack_time",   &Compressor_t::setAttackTime,  py::arg ("seconds"))
        .def ("set_release_time",  &Compressor_t::setReleaseTime, py::arg ("seconds"))
        .def ("set_makeup_gain",   &Compressor_t::setMakeupGain,  py::arg ("gain_db"))
        .def ("set_sample_rate",   &Compressor_t::setSampleRate,  py::arg ("sample_rate"))
        .def ("reset", &Compressor_t::reset)

        .def_property ("threshold",
            [] (const Compressor_t& s) { return s.getThreshold(); },
            [] (Compressor_t& s, F v) { s.setThreshold (v); })
        .def_property ("ratio",
            [] (const Compressor_t& s) { return s.getRatio(); },
            [] (Compressor_t& s, F v) { s.setRatio (v); })
        .def_property ("knee",
            [] (const Compressor_t& s) { return s.getKnee(); },
            [] (Compressor_t& s, F v) { s.setKnee (v); })
        .def_property ("attack_time",
            [] (const Compressor_t& s) { return s.getAttackTime(); },
            [] (Compressor_t& s, F v) { s.setAttackTime (v); })
        .def_property ("release_time",
            [] (const Compressor_t& s) { return s.getReleaseTime(); },
            [] (Compressor_t& s, F v) { s.setReleaseTime (v); })
        .def_property ("makeup_gain",
            [] (const Compressor_t& s) { return s.getMakeupGain(); },
            [] (Compressor_t& s, F v) { s.setMakeupGain (v); })
        .def_property_readonly ("gain_reduction_db", &Compressor_t::getGainReductionDb,
              "Most recently computed gain reduction, in dB (>= 0). For metering.")

        .def ("process_sample",
            static_cast<F (Compressor_t::*) (F)> (&Compressor_t::processSample),
            py::arg ("x"),
            "Process one mono sample, detecting on x itself.")
        .def ("process_sample_sidechain",
            static_cast<F (Compressor_t::*) (F, F)> (&Compressor_t::processSample),
            py::arg ("x"), py::arg ("sidechain"),
            "Process one mono sample, detecting on sidechain instead of x.")
        .def ("process_block",
            [] (Compressor_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (Compressor_t& self, py::array_t<F, py::array::c_style> arr)
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
