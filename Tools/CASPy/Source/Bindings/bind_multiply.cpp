/************************************************************************
*  CASPy - Python Bindings for CASPI
*  bind_multiply.cpp
*
*  Bindings for the generic N-input Multiply graph node.
************************************************************************/

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Multiply.h"
#include "core/caspi_Node.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

using MultiplyFloat_t    = Multiply<float>;
using NodeBase_t         = Graph::NodeBase<float>;
using MultiplyFloatPtr_t = std::unique_ptr<MultiplyFloat_t, py::nodelete>;

void bind_multiply(py::module_& m)
{
    // This connects the inheritance chain across file boundaries
    py::object node_base = m.attr("NodeBase");

    py::class_<MultiplyFloat_t, NodeBase_t, MultiplyFloatPtr_t>(m, "Multiply",
        R"pbdoc(
            Generic N-input audio multiplying node for AudioGraph.

            Multiplies all connected audio input ports into one output port.
            Unconnected ports contribute the multiplicative identity (1), not
            silence. Its primary use is as a VCA: multiplying an oscillator's
            audio output by an envelope node's audio output to shape
            amplitude and note lifecycle from a plain graph connection.

            Example:
                vca_id = g.add_node(caspy.Multiply(2))
                g.connect(car_id, 0, vca_id, 0)
                g.connect(env_id, 0, vca_id, 1)
        )pbdoc")

        .def(py::init([](std::size_t numInputs) { return new MultiplyFloat_t(numInputs); }),
             py::arg("num_inputs"),
             "Construct a multiplier with a fixed number of input ports");
}
