/************************************************************************
*  CASPy - Python Bindings for CASPI
*  bind_mixer.cpp
*
*  Bindings for the generic N-input Mixer graph node.
************************************************************************/

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Mixer.h"
#include "core/caspi_Node.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

using MixerFloat_t    = Mixer<float>;
using NodeBase_t      = Graph::NodeBase<float>;
using MixerFloatPtr_t = std::unique_ptr<MixerFloat_t, py::nodelete>;

void bind_mixer(py::module_& m)
{
    // This connects the inheritance chain across file boundaries
    py::object node_base = m.attr("NodeBase");

    py::class_<MixerFloat_t, NodeBase_t, MixerFloatPtr_t>(m, "Mixer",
        R"pbdoc(
            Generic N-input audio summing node for AudioGraph.

            Sums all connected audio input ports into one output port.
            Used to combine several graph-native FM carriers (or any other
            audio nodes) into one output — the same mixing FMGraphDSP does
            internally for its designated output operators, expressed as a
            plain graph node instead.

            Example:
                mix_id = g.add_node(caspy.Mixer(2))
                g.connect(car1_id, 0, mix_id, 0)
                g.connect(car2_id, 0, mix_id, 1)
        )pbdoc")

        .def(py::init([](std::size_t numInputs) { return new MixerFloat_t(numInputs); }),
             py::arg("num_inputs"),
             "Construct a mixer with a fixed number of input ports")

        .def("set_auto_scale", &MixerFloat_t::setAutoScale,
             py::arg("enable"),
             "Enable/disable dividing the summed output by the declared input port count")
        .def("get_auto_scale", &MixerFloat_t::getAutoScale,
             "Check if auto-scale is enabled")

        .def("set_output_gain", &MixerFloat_t::setOutputGain,
             py::arg("gain"),
             "Set linear output gain applied after summing (and after auto-scale)")
        .def("get_output_gain", &MixerFloat_t::getOutputGain,
             "Get the current output gain");
}
