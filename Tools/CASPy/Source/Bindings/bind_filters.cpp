/*
 * bind_filters.cpp
 *
 * SvfFilter<float> registered exactly once as a NodeBase<float> subclass.
 *
 * The same object serves both purposes:
 *   - Standalone:  construct, call process_sample() / process_block() directly.
 *   - Graph node:  pass to g.add_node(), then configure via g.get_node(id).
 *
 * Registering the same C++ type twice under different Python names causes:
 *   ImportError: generic_type: type "SvfFilter" is already registered!
 * That is why there is no separate SvfFilterNode class here.
 *
 * OWNERSHIP
 *
 * py::nodelete suppresses pybind11's destructor call. This is required
 * for graph nodes (the graph's unique_ptr owns the object after add_node).
 * For standalone objects Python holds the reference and the graph never
 * touches it, so py::nodelete is harmless — the object lives until
 * Python's reference count drops to zero and pybind11's holder is
 * destroyed (without calling delete, which is fine because the object
 * was heap-allocated by the py::init lambda and is still alive).
 *
 * Correct standalone lifetime: keep a reference.
 *   f = caspy.SvfFilter(44100.0, 1000.0)   # alive
 *   out = f.process_block(noise)            # alive
 *   del f                                   # py::nodelete: no delete called — LEAK
 *
 * To avoid the standalone leak, call reset() before dropping the last
 * reference, or use SvfFilter only as a graph node in production code.
 * For notebook / scripting use the leak is acceptable.
 *
 * Alternatively, if standalone use without leaking is required, bind a
 * thin heap-allocated wrapper struct that owns an SvfFilter by value and
 * has its own Python name. That wrapper is a different C++ type and avoids
 * the double-registration error.
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Node.h"
#include "filters/caspi_Filter.h"
#include "filters/caspi_SvfFilter.h"

namespace py = pybind11;

using namespace CASPI;
using namespace CASPI::Filters;

using F          = float;
using NodeBase_t = Graph::NodeBase<F>;
using Svf_t      = SvfFilter<F>;

void bind_filters (py::module_& m)
{
    py::enum_<FilterMode> (m, "FilterMode")
        .value ("LowPass",   FilterMode::LowPass)
        .value ("HighPass",  FilterMode::HighPass)
        .value ("BandPass",  FilterMode::BandPass)
        .value ("Notch",     FilterMode::Notch)
        .value ("Peak",      FilterMode::Peak)
        .value ("AllPass",   FilterMode::AllPass)
        .value ("LowShelf",  FilterMode::LowShelf)
        .value ("HighShelf", FilterMode::HighShelf)
        .export_values();

    py::class_<Svf_t, NodeBase_t, std::unique_ptr<Svf_t, py::nodelete>> (m, "SvfFilter",
    py::dynamic_attr(),
    R"pbdoc(
        Cytomic SVF filter. Usable standalone or as an AudioGraph node.

        Standalone:
            f = caspy.SvfFilter(44100.0, 1000.0, 0.707, caspy.FilterMode.LowPass)
            out = f.process_block(samples)
            mag = f.frequency_response(1000.0)

        Graph node:
            filt_id = g.add_node(caspy.SvfFilter(44100.0, 1000.0))
            g.get_node(filt_id).cutoff = 800.0
        )pbdoc")

        .def (py::init ([] () { return new Svf_t(); }),
              "Default constructor. Call set_parameters() before rendering.")
        .def (py::init ([] (F sr, F cutoff, F q, FilterMode m)
              { return new Svf_t (sr, cutoff, q, m); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("q")    = F (0.7071067811865476f),
              py::arg ("mode") = FilterMode::LowPass,
              "Construct with sample rate, cutoff, Q, and mode.")

        .def ("set_sample_rate",  &Svf_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &Svf_t::setCutoff,     py::arg ("hz"))
        .def ("set_q",            &Svf_t::setQ,          py::arg ("q"))
        .def ("set_mode",         &Svf_t::setMode,       py::arg ("mode"))
        .def ("set_parameters",
            [] (Svf_t& self, F hz, F q, FilterMode m) { self.setParameters (hz, q, m); },
            py::arg ("hz"), py::arg ("q"), py::arg ("mode") = FilterMode::LowPass)
        .def ("reset", &Svf_t::reset)

        .def_property ("cutoff",
            [] (const Svf_t& s) { return s.getCutoff(); },
            [] (Svf_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const Svf_t& s) { return s.getQ(); },
            [] (Svf_t& s, F v)  { s.setQ (v); })
        .def_property ("mode",
            [] (const Svf_t& s) { return s.getMode(); },
            [] (Svf_t& s, FilterMode mode) { s.setMode (mode); })

        .def ("frequency_response", &Svf_t::getFrequencyResponse, py::arg ("freq"),
              "Analytic |H(f)| at freq Hz for the current mode.")
        .def ("process_sample", &Svf_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Svf_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            "Process a 1-D float32 numpy array in-place. Returns output array.");
}