/*
 * bind_filters.cpp
 *
 * Binds:
 *   - FilterTopology enum
 *   - FilterMode enum
 *   - StateVariableFilter<float>  (C++: Filter<float, FilterTopology::StateVariable>)
 *   - Filters<float, StateVariable, Biquad, Ladder>
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Node.h"

namespace py = pybind11;

using namespace CASPI;
using namespace CASPI::Filters;

using F          = float;
using NodeBase_t = Graph::NodeBase<F>;
using Svf_t      = Filter<F, FilterTopology::StateVariable>;
using Sel_t      = Filters<F, FilterTopology::StateVariable,
                              FilterTopology::Biquad,
                              FilterTopology::Ladder>;

void bind_filters (py::module_& m)
{
    /*----------------------------------------------------------------------
     * FilterTopology enum
     *--------------------------------------------------------------------*/
    py::enum_<FilterTopology> (m, "FilterTopology")
        .value ("StateVariable", FilterTopology::StateVariable)
        .value ("Biquad",        FilterTopology::Biquad)
        .value ("Ladder",        FilterTopology::Ladder)
        .export_values();

    /*----------------------------------------------------------------------
     * FilterMode enum
     *--------------------------------------------------------------------*/
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

    /*----------------------------------------------------------------------
     * StateVariableFilter<float>
     *
     * C++ type: Filter<float, FilterTopology::StateVariable>
     *--------------------------------------------------------------------*/
    py::class_<Svf_t, NodeBase_t, std::unique_ptr<Svf_t, py::nodelete>> (m, "StateVariableFilter",
    py::dynamic_attr(),
    R"pbdoc(
        State-variable filter (Cytomic SVF topology).

        Standalone:
            f = caspy.StateVariableFilter(44100.0, 1000.0)
            out = f.process_block(samples)
            mag = f.frequency_response(1000.0)

        Graph node:
            filt_id = g.add_node(caspy.StateVariableFilter(44100.0, 1000.0))
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
            "Process a 1-D float32 numpy array. Returns output array.")
        .def ("process_block_inplace",
            [] (Svf_t& self, py::array_t<F, py::array::c_style> arr)
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

    /*----------------------------------------------------------------------
     * Filters<float, StateVariable, Biquad, Ladder>
     *--------------------------------------------------------------------*/
    py::class_<Sel_t> (m, "Filters", py::dynamic_attr(),
        R"pbdoc(
            Runtime-selectable filter topology.

            Pre-allocates one filter per topology (StateVariable, Biquad,
            Ladder) and dispatches process_sample() to the active one with
            zero allocation overhead.

            Usage:
                f = caspy.Filters(48000.0)
                f.cutoff = 1000.0
                f.topology = caspy.FilterTopology.Biquad
                out = f.process_block(noise)
        )pbdoc")

        .def (py::init ([] (F sr, FilterTopology initial)
              {
                  return new Sel_t (sr, initial);
              }),
              py::arg ("sample_rate"),
              py::arg ("initial") = FilterTopology::StateVariable,
              "Construct with sample rate and optional initial topology.")

        .def ("process_sample", &Sel_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Sel_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (Sel_t& self, py::array_t<F, py::array::c_style> arr)
            {
                auto             buf = arr.mutable_unchecked<1>();
                const py::ssize_t n  = arr.shape (0);
                for (py::ssize_t i = 0; i < n; ++i)
                {
                    buf (i) = self.processSample (buf (i));
                }
            },
            py::arg ("samples"),
            "Process a 1-D float32 numpy array in-place, overwriting the input.")

        .def ("reset", &Sel_t::reset)

        .def_property ("topology",
            [] (const Sel_t& s) { return s.getTopology(); },
            [] (Sel_t& s, FilterTopology t) { s.setTopology (t); })
        .def_property ("cutoff",
            [] (const Sel_t& s) { return s.getCutoff(); },
            [] (Sel_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const Sel_t& s) { return s.getQ(); },
            [] (Sel_t& s, F v)  { s.setQ (v); })
        .def_property ("mode",
            [] (const Sel_t& s) { return s.getMode(); },
            [] (Sel_t& s, FilterMode m) { s.setMode (m); });
}
