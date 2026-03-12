#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // REQUIRED for std::vector conversion

namespace py = pybind11;
using namespace CASPI;

void bind_parameters(py::module_& m)
{
    using Param    = Core::Parameter<float>;
    using ModParam = Core::ModulatableParameter<float>;

    py::enum_<Core::ParameterScale>(m, "ParameterScale",
        "Scaling mode for parameter value mapping")
        .value("Linear",      Core::ParameterScale::Linear)
        .value("Logarithmic", Core::ParameterScale::Logarithmic)
        .value("Bipolar",     Core::ParameterScale::Bipolar)
        .export_values();

    py::class_<Param>(m, "Parameter",
        R"pbdoc(
            Thread-safe parameter with atomic base value and one-pole smoothing.

            GUI thread: set_base_normalised(), set_range(), set_smoothing_time()
            Audio thread: process(), value(), value_normalised()
        )pbdoc")
        .def(py::init<>())
        .def(py::init<float>(), py::arg("initial_normalised"))
        .def(py::init<float, float, float>(),
             py::arg("min"), py::arg("max"),
             py::arg("default_normalised") = 0.f)

        // GUI thread
        .def("set_base_normalised", &Param::setBaseNormalised,
             py::arg("value"),
             "Write normalised [0,1] base value. Thread-safe.")
        .def("get_base_normalised", &Param::getBaseNormalised,
             "Read normalised base value. Thread-safe.")
        .def("set_range", &Param::setRange,
             py::arg("min"), py::arg("max"),
             py::arg("scale") = Core::ParameterScale::Linear,
             "Set value range and scaling mode.")
        .def("set_smoothing_time", &Param::setSmoothingTime,
             py::arg("time_seconds"), py::arg("sample_rate"),
             "Configure one-pole smoother for ~99% convergence in time_seconds.")
        .def("get_min_value", &Param::getMinValue)
        .def("get_max_value", &Param::getMaxValue)
        .def("get_scale",     &Param::getScale)

        // Audio thread
        .def("process", &Param::process,
             "Advance smoother by one sample. Audio thread only.")
        .def("skip", &Param::skip,
             py::arg("num_samples"),
             "Advance smoother by N samples without rendering. Audio thread only.")
        .def("value",            &Param::value,
             "Scaled value in [min, max]. Audio thread only.")
        .def("value_normalised", &Param::valueNormalised,
             "Smoothed normalised value [0,1]. Audio thread only.")
        .def("__float__", [](const Param& p){ return static_cast<float>(p); });

    py::class_<ModParam, Param>(m, "ModulatableParameter",
        R"pbdoc(
            Extends Parameter with per-block modulation accumulation.

            Modulation API (audio thread only):
                clear_modulation()        — call at block start
                add_modulation(amount)    — accumulate modulation depth
                get_modulation_amount()   — read accumulated amount
                value_normalised()        — smoothed_base + modulation, clamped [0,1]
        )pbdoc")
        .def(py::init<>())
        .def(py::init<float>(), py::arg("initial_normalised"))
        .def(py::init<float, float, float>(),
             py::arg("min"), py::arg("max"),
             py::arg("default_normalised") = 0.f)

        .def("clear_modulation",     &ModParam::clearModulation,
             "Zero the modulation accumulator. Audio thread only.")
        .def("add_modulation",       &ModParam::addModulation,
             py::arg("amount"),
             "Add to modulation accumulator. Audio thread only.")
        .def("get_modulation_amount",&ModParam::getModulationAmount,
             "Read current accumulated modulation. Audio thread only.")
        .def("value_normalised",     &ModParam::valueNormalised,
             "Smoothed base + modulation, clamped [0,1]. Audio thread only.");
}
