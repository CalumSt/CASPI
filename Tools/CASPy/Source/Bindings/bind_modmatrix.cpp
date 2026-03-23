#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // REQUIRED for std::vector conversion

namespace py = pybind11;
using namespace CASPI;

void bind_modmatrix(py::module_& m)
{
    using MM      = Controls::ModMatrix<float>;
    using Routing = Controls::ModulationRouting<float>;
    using Curve   = Controls::ModulationCurve;
    using ModParam = Core::ModulatableParameter<float>;

    py::enum_<Curve>(m, "ModulationCurve",
        "Curve shaping applied to a source value before depth scaling.")
        .value("Linear",      Curve::Linear)
        .value("Exponential", Curve::Exponential)
        .value("Logarithmic", Curve::Logarithmic)
        .value("SCurve",      Curve::SCurve)
        .export_values();

    py::class_<Routing>(m, "ModulationRouting",
        R"pbdoc(
            Single routing: one source → one parameter destination.

            Depth is clamped to [-1,1] by ModMatrix.add_routing().
        )pbdoc")
        .def(py::init<>())
        .def(py::init<size_t, size_t, float>(),
             py::arg("source_id"), py::arg("destination_id"), py::arg("depth"))
        .def_readwrite("source_id",      &Routing::sourceId)
        .def_readwrite("destination_id", &Routing::destinationId)
        .def_readwrite("depth",          &Routing::depth)
        .def_readwrite("curve",          &Routing::curve)
        .def_readwrite("enabled",        &Routing::enabled);

    py::enum_<MM::ParamRegistrationError>(m, "ParamRegistrationError")
        .value("NullParameter",    MM::ParamRegistrationError::NullParameter)
        .value("CapacityExceeded", MM::ParamRegistrationError::CapacityExceeded)
        .export_values();

    py::class_<MM>(m, "ModMatrix",
        R"pbdoc(
            Lock-free modulation matrix routing sources to ModulatableParameters.

            Thread-safety:
                register_parameter()              — setup only, not thread-safe
                add_routing / remove_routing etc. — any thread (lock-free enqueue)
                set_source_value / process        — audio thread only

            Capacities (compile-time):
                Sources   : 64   (kMaxModSources)
                Routings  : 1024 per curve class (kMaxModRoutings)
                Parameters: 256  (kMaxModParams)

            Example:
                matrix = caspi.ModMatrix()
                dest = matrix.register_parameter(param)
                matrix.add_routing(caspi.ModulationRouting(src_id=0, destination_id=dest, depth=0.5))
                matrix.set_source_value(0, lfo_value)
                matrix.process()   # once per audio block
        )pbdoc")
        .def(py::init<>())

        // Setup
        .def("register_parameter",
             [](MM& self, ModParam* p) -> size_t {
                 auto result = self.registerParameter(p);
                 if (!result.has_value())
                 {
                     const auto err = result.error();
                     if (err == MM::ParamRegistrationError::NullParameter)
                         throw py::value_error("register_parameter: null parameter");
                     throw py::value_error("register_parameter: capacity exceeded (max 256)");
                 }
                 return result.value();
             },
             py::arg("parameter"),
             py::return_value_policy::reference,
             "Register a ModulatableParameter. Returns destination ID. Setup phase only.")

        // Routing mutations — any thread
        .def("add_routing",
             &MM::addRouting,
             py::arg("routing"),
             "Enqueue a routing for insertion on the next process() call.")

        .def("remove_routing",
             [](MM& self, size_t index, Curve curve) {
                 // Runtime dispatch to the correct template instantiation.
                 // Only Linear and non-linear distinction matters here.
                 if (curve == Curve::Linear)
                     self.removeRouting<Curve::Linear>(index);
                 else if (curve == Curve::Exponential)
                     self.removeRouting<Curve::Exponential>(index);
                 else if (curve == Curve::Logarithmic)
                     self.removeRouting<Curve::Logarithmic>(index);
                 else
                     self.removeRouting<Curve::SCurve>(index);
             },
             py::arg("index"),
             py::arg("curve") = Curve::Linear,
             "Enqueue removal of a routing by index from the specified curve list.")

        .def("clear_routings",
             &MM::clearRoutings,
             "Enqueue a command to clear all routings from both lists.")

        .def("set_routing_enabled",
             [](MM& self, size_t index, bool enabled, Curve curve) {
                 if (curve == Curve::Linear)
                     self.setRoutingEnabled<Curve::Linear>(index, enabled);
                 else if (curve == Curve::Exponential)
                     self.setRoutingEnabled<Curve::Exponential>(index, enabled);
                 else if (curve == Curve::Logarithmic)
                     self.setRoutingEnabled<Curve::Logarithmic>(index, enabled);
                 else
                     self.setRoutingEnabled<Curve::SCurve>(index, enabled);
             },
             py::arg("index"), py::arg("enabled"),
             py::arg("curve") = Curve::Linear,
             "Enqueue an enabled-state change for a routing in the specified curve list.")

        // Source values — audio thread
        .def("set_source_value", &MM::setSourceValue,
             py::arg("source_id"), py::arg("value"),
             "Write a source value. Audio thread only.")
        .def("get_source_value", &MM::getSourceValue,
             py::arg("source_id"),
             "Read a source value. Audio thread only.")

        // Audio thread processing
        .def("process", &MM::process,
             "Run one block: drain commands, accumulate, scatter. Audio thread only.")
        .def("reset",   &MM::reset,
             "Zero sources and clear parameter modulation. Routings preserved.")

        // Observers
        .def("get_num_parameters",        &MM::getNumParameters)
        .def("get_num_routings",          &MM::getNumRoutings)
        .def("get_num_linear_routings",   &MM::getNumLinearRoutings)
        .def("get_num_non_linear_routings", &MM::getNumNonLinearRoutings);
}
