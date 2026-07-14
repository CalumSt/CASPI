/*
 * bind_filters.cpp
 *
 * Binds:
 *   - FilterMode enum
 *   - StateVariable<float>
 *   - Biquad<float>
 *   - Ladder<float>
 *   - DiodeLadder<float>
 *   - OnePole<float>
 *   - Filters<float, StateVariable, Biquad, Ladder, DiodeLadder, OnePole> (FilterBank)
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Node.h"

namespace py = pybind11;

using namespace CASPI;
using FiltNS = CASPI::Filters;

using F          = float;
using NodeBase_t = Graph::NodeBase<F>;
using Svf_t      = FiltNS::StateVariable<F>;
using Bq_t       = FiltNS::Biquad<F>;
using Lad_t      = FiltNS::Ladder<F>;
using DLad_t     = FiltNS::DiodeLadder<F>;
using Op_t       = FiltNS::OnePole<F>;
using FB_t       = FiltNS::Filters<F, FiltNS::StateVariable, FiltNS::Biquad, FiltNS::Ladder, FiltNS::DiodeLadder, FiltNS::OnePole>;

void bind_filters (py::module_& m)
{
    /*----------------------------------------------------------------------
     * FilterMode enum
     *--------------------------------------------------------------------*/
    py::enum_<FiltNS::FilterMode> (m, "FilterMode")
        .value ("LowPass",   FiltNS::FilterMode::LowPass)
        .value ("HighPass",  FiltNS::FilterMode::HighPass)
        .value ("BandPass",  FiltNS::FilterMode::BandPass)
        .value ("Notch",     FiltNS::FilterMode::Notch)
        .value ("Peak",      FiltNS::FilterMode::Peak)
        .value ("AllPass",   FiltNS::FilterMode::AllPass)
        .value ("LowShelf",  FiltNS::FilterMode::LowShelf)
        .value ("HighShelf", FiltNS::FilterMode::HighShelf)
        .export_values();

    /*----------------------------------------------------------------------
     * StateVariable
     *--------------------------------------------------------------------*/
    py::class_<Svf_t, NodeBase_t, std::unique_ptr<Svf_t, py::nodelete>> (m, "StateVariable",
    py::dynamic_attr(),
    R"pbdoc(
        State-variable filter (Cytomic SVF topology).

        Standalone:
            f = caspy.StateVariable(44100.0, 1000.0)
            out = f.process_block(samples)
            mag = f.frequency_response(1000.0)

        Graph node:
            filt_id = g.add_node(caspy.StateVariable(44100.0, 1000.0))
            g.get_node(filt_id).cutoff = 800.0
    )pbdoc")

        .def (py::init ([] () { return new Svf_t(); }),
              "Default constructor. Call set_parameters() before rendering.")
        .def (py::init ([] (F sr, F cutoff, F q, FiltNS::FilterMode m)
              { return new Svf_t (sr, cutoff, q, m); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("q")    = F (0.7071067811865476f),
              py::arg ("mode") = FiltNS::FilterMode::LowPass,
              "Construct with sample rate, cutoff, Q, and mode.")

        .def ("set_sample_rate",  &Svf_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &Svf_t::setCutoff,     py::arg ("hz"))
        .def ("set_q",            &Svf_t::setQ,          py::arg ("q"))
        .def ("set_mode",         &Svf_t::setMode,       py::arg ("mode"))
        .def ("set_parameters",
            [] (Svf_t& self, F hz, F q, FiltNS::FilterMode m) { self.setParameters (hz, q, m); },
            py::arg ("hz"), py::arg ("q"), py::arg ("mode") = FiltNS::FilterMode::LowPass,
            "Set all parameters at once.")
        .def ("reset", &Svf_t::reset)

        .def_property ("cutoff",
            [] (const Svf_t& s) { return s.getCutoff(); },
            [] (Svf_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const Svf_t& s) { return s.getQ(); },
            [] (Svf_t& s, F v)  { s.setQ (v); })
        .def_property ("mode",
            [] (const Svf_t& s) { return s.getMode(); },
            [] (Svf_t& s, FiltNS::FilterMode mode) { s.setMode (mode); })

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
            "Process a 1-D float32 numpy array in-place, overwriting the input.")

    /*----------------------------------------------------------------------
     * Biquad
     *--------------------------------------------------------------------*/
    py::class_<Bq_t, NodeBase_t, std::unique_ptr<Bq_t, py::nodelete>> (m, "Biquad",
    py::dynamic_attr(),
    R"pbdoc(
        Biquad filter (RBJ DF2T).

        Supports all 8 FilterModes including peaking/shelf with gain.
    )pbdoc")

        .def (py::init ([] () { return new Bq_t(); }),
              "Default constructor.")
        .def (py::init ([] (F sr, F cutoff, F q, FiltNS::FilterMode m, F gain)
              { return new Bq_t (sr, cutoff, q, m, gain); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("q")    = F (0.7071067811865476f),
              py::arg ("mode") = FiltNS::FilterMode::LowPass,
              py::arg ("gain") = F (0),
              "Construct with sample rate, cutoff, Q, mode, and gain (dB).")

        .def ("set_sample_rate",  &Bq_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &Bq_t::setCutoff,     py::arg ("hz"))
        .def ("set_q",            &Bq_t::setQ,          py::arg ("q"))
        .def ("set_mode",         &Bq_t::setMode,       py::arg ("mode"))
        .def ("set_gain",         &Bq_t::setGain,       py::arg ("gain_db"))
        .def ("set_parameters",
            [] (Bq_t& self, F hz, F q, FiltNS::FilterMode m, F gain)
            { self.setParameters (hz, q, m); self.setGain (gain); },
            py::arg ("hz"), py::arg ("q"), py::arg ("mode") = FiltNS::FilterMode::LowPass,
            py::arg ("gain") = F (0))
        .def ("reset", &Bq_t::reset)

        .def_property ("cutoff",
            [] (const Bq_t& s) { return s.getCutoff(); },
            [] (Bq_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const Bq_t& s) { return s.getQ(); },
            [] (Bq_t& s, F v)  { s.setQ (v); })
        .def_property ("gain",
            [] (const Bq_t& s) { return s.getGainDb(); },
            [] (Bq_t& s, F v)  { s.setGain (v); })
        .def_property ("mode",
            [] (const Bq_t& s) { return s.getMode(); },
            [] (Bq_t& s, FiltNS::FilterMode mode) { s.setMode (mode); })

        .def ("frequency_response", &Bq_t::getFrequencyResponse, py::arg ("freq"),
              "Analytic |H(f)| at freq Hz for the current mode/coeffs.")
        .def ("process_sample", &Bq_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Bq_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (Bq_t& self, py::array_t<F, py::array::c_style> arr)
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

    /*----------------------------------------------------------------------
     * Ladder (Moog)
     *--------------------------------------------------------------------*/
    py::class_<Lad_t, NodeBase_t, std::unique_ptr<Lad_t, py::nodelete>> (m, "Ladder",
    py::dynamic_attr(),
    R"pbdoc(
        Moog transistor ladder filter (Stilson/Smith).

        Four-pole LP with tanh saturation. Nonlinear — no frequency_response.
    )pbdoc")

        .def (py::init ([] () { return new Lad_t(); }),
              "Default constructor.")
        .def (py::init ([] (F sr, F cutoff, F q)
              { return new Lad_t (sr, cutoff, q); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("q") = F (0.7071067811865476f),
              "Construct with sample rate, cutoff, and Q.")

        .def ("set_sample_rate",  &Lad_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &Lad_t::setCutoff,     py::arg ("hz"))
        .def ("set_q",            &Lad_t::setQ,          py::arg ("q"))
        .def ("reset", &Lad_t::reset)

        .def_property ("cutoff",
            [] (const Lad_t& s) { return s.getCutoff(); },
            [] (Lad_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const Lad_t& s) { return s.getQ(); },
            [] (Lad_t& s, F v)  { s.setQ (v); })

        .def ("process_sample", &Lad_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Lad_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (Lad_t& self, py::array_t<F, py::array::c_style> arr)
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

    /*----------------------------------------------------------------------
     * DiodeLadder
     *--------------------------------------------------------------------*/
    py::class_<DLad_t, NodeBase_t, std::unique_ptr<DLad_t, py::nodelete>> (m, "DiodeLadder",
    py::dynamic_attr(),
    R"pbdoc(
        Diode ladder filter (Huovilainen).

        Four-pole LP with diode clipper saturation. Nonlinear.
    )pbdoc")

        .def (py::init ([] () { return new DLad_t(); }),
              "Default constructor.")
        .def (py::init ([] (F sr, F cutoff, F q)
              { return new DLad_t (sr, cutoff, q); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("q") = F (0.7071067811865476f),
              "Construct with sample rate, cutoff, and Q.")

        .def ("set_sample_rate",  &DLad_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &DLad_t::setCutoff,     py::arg ("hz"))
        .def ("set_q",            &DLad_t::setQ,          py::arg ("q"))
        .def ("reset", &DLad_t::reset)

        .def_property ("cutoff",
            [] (const DLad_t& s) { return s.getCutoff(); },
            [] (DLad_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const DLad_t& s) { return s.getQ(); },
            [] (DLad_t& s, F v)  { s.setQ (v); })

        .def ("process_sample", &DLad_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (DLad_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (DLad_t& self, py::array_t<F, py::array::c_style> arr)
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

    /*----------------------------------------------------------------------
     * OnePole
     *--------------------------------------------------------------------*/
    py::class_<Op_t, NodeBase_t, std::unique_ptr<Op_t, py::nodelete>> (m, "OnePole",
    py::dynamic_attr(),
    R"pbdoc(
        One-pole LP/HP filter.

        Cheapest filter on this API. No resonance control (set_q is a no-op).
    )pbdoc")

        .def (py::init ([] () { return new Op_t(); }),
              "Default constructor.")
        .def (py::init ([] (F sr, F cutoff, FiltNS::FilterMode m)
              { return new Op_t (sr, cutoff, m); }),
              py::arg ("sample_rate"),
              py::arg ("cutoff"),
              py::arg ("mode") = FiltNS::FilterMode::LowPass,
              "Construct with sample rate, cutoff, and mode (LP/HP).")

        .def ("set_sample_rate",  &Op_t::setSampleRate, py::arg ("sr"))
        .def ("set_cutoff",       &Op_t::setCutoff,     py::arg ("hz"))
        .def ("set_mode",         &Op_t::setMode,       py::arg ("mode"))
        .def ("reset", &Op_t::reset)

        .def_property ("cutoff",
            [] (const Op_t& s) { return s.getCutoff(); },
            [] (Op_t& s, F v)  { s.setCutoff (v); })
        .def_property ("mode",
            [] (const Op_t& s) { return s.getMode(); },
            [] (Op_t& s, FiltNS::FilterMode m) { s.setMode (m); })

        .def ("frequency_response", &Op_t::getFrequencyResponse, py::arg ("freq"),
              "Analytic |H(f)| at freq Hz.")
        .def ("process_sample", &Op_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (Op_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (Op_t& self, py::array_t<F, py::array::c_style> arr)
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

    /*----------------------------------------------------------------------
     * FilterBank (runtime-switchable multi-filter)
     *--------------------------------------------------------------------*/
    py::class_<FB_t> (m, "FilterBank", py::dynamic_attr(),
    R"pbdoc(
        Runtime-selectable filter topology.

        Pre-allocates one filter per topology and dispatches
        process_sample() to the active one.

        Usage:
            f = caspy.FilterBank(48000.0)
            f.cutoff = 1000.0
            f.set_active(Biquad)         # compile-time switch
            f.set_active_index(1)        # runtime switch
            out = f.process_block(noise)
    )pbdoc")

        .def (py::init ([] (F sr, std::size_t initialIndex)
              {
                  return new FB_t (sr, initialIndex);
              }),
              py::arg ("sample_rate"),
              py::arg ("initial_index") = 0,
              "Construct with sample rate and optional initial filter index.")
        .def (py::init ([] (F sr)
              {
                  return new FB_t (sr);
              }),
              py::arg ("sample_rate"),
              "Construct with sample rate; active filter is first in pack (StateVariable).")

        .def ("process_sample", &FB_t::processSample, py::arg ("x"))
        .def ("process_block",
            [] (FB_t& self, py::array_t<F, py::array::c_style> arr) -> py::array_t<F>
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
            [] (FB_t& self, py::array_t<F, py::array::c_style> arr)
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

        .def ("reset", &FB_t::reset)

        .def ("set_active_index", &FB_t::setActiveIndex, py::arg ("idx"),
              "Runtime switch by pack index.")
        .def ("get_active_index", &FB_t::getActiveIndex,
              "Read the active filter index.")
        .def ("set_active", [] (FB_t& self) { self.setActive<FiltNS::Biquad>(); },
              "Compile-time switch to Biquad (example).")

        .def ("set_cutoff",       &FB_t::setCutoff,       py::arg ("hz"))
        .def ("set_q",            &FB_t::setQ,            py::arg ("q"))
        .def ("set_gain",         &FB_t::setGain,         py::arg ("gain_db"))
        .def ("set_mode",         &FB_t::setMode,         py::arg ("mode"))
        .def ("set_parameters",
            [] (FB_t& self, F hz, F q, FiltNS::FilterMode m) { self.setParameters (hz, q, m); },
            py::arg ("hz"), py::arg ("q"), py::arg ("mode") = FiltNS::FilterMode::LowPass)

        .def_property ("cutoff",
            [] (const FB_t& s) { return s.getCutoff(); },
            [] (FB_t& s, F v)  { s.setCutoff (v); })
        .def_property ("q",
            [] (const FB_t& s) { return s.getQ(); },
            [] (FB_t& s, F v)  { s.setQ (v); })
        .def_property ("gain",
            [] (const FB_t& s) { return s.getGainDb(); },
            [] (FB_t& s, F v)  { s.setGain (v); })
        .def_property ("mode",
            [] (const FB_t& s) { return s.getMode(); },
            [] (FB_t& s, FiltNS::FilterMode m) { s.setMode (m); })

        .def_property_readonly ("active_index", &FB_t::getActiveIndex)
        .def_property_readonly ("sample_rate", &FB_t::getSampleRate)
        .def_property_readonly ("cutoff",      [] (const FB_t& s) { return s.getCutoff(); })
        .def_property_readonly ("q",           [] (const FB_t& s) { return s.getQ(); })
        .def_property_readonly ("gain",        [] (const FB_t& s) { return s.getGainDb(); })
        .def_property_readonly ("mode",        [] (const FB_t& s) { return s.getMode(); })
        .def_property_readonly ("num_filters", [] (const FB_t&) { return FB_t::FilterTuple::size(); });

    /*----------------------------------------------------------------------
     * Convenience type aliases at module level
     *--------------------------------------------------------------------*/
    m.attr ("FilterBank3") = py::cpp_function (
        [] (F sr, std::size_t idx = 0) { return new Filters<F, FiltNS::StateVariable, FiltNS::Biquad, FiltNS::Ladder> (sr, idx); },
        py::arg ("sample_rate"), py::arg ("initial_index") = 0,
        "FilterBank with StateVariable, Biquad, Ladder");

    m.attr ("FilterBank5") = py::cpp_function (
        [] (F sr, std::size_t idx = 0) { return new Filters<F, FiltNS::StateVariable, FiltNS::Biquad, FiltNS::Ladder, FiltNS::DiodeLadder, FiltNS::OnePole> (sr, idx); },
        py::arg ("sample_rate"), py::arg ("initial_index") = 0,
        "FilterBank with all 5 topologies");
}