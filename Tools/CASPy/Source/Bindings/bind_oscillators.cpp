/************************************************************************
 *  CASPy - Python Bindings for CASPI
 *  bind_oscillators.cpp
 ************************************************************************/

#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // REQUIRED for std::vector conversion

namespace py = pybind11;
using namespace CASPI;

/**
 * @brief Generic render function that works for any oscillator type
 *
 * Renders samples from an oscillator instance while preserving phase state.
 * Returns a numpy array for efficient Python integration.
 *
 * @tparam OscType Oscillator type (must have renderSample() method)
 * @param osc Oscillator instance
 * @param num_samples Number of samples to generate
 * @return Numpy array of float32 samples
 */
template <typename OscType>
py::array_t<float> render_oscillator(OscType& osc, size_t num_samples)
{
    // Allocate numpy array directly (more efficient than vector)
    py::array_t<float> result(num_samples);
    auto buf = result.request();
    float* ptr = static_cast<float*>(buf.ptr);

    // Render samples, preserving phase continuity
    for (size_t i = 0; i < num_samples; ++i) {
        ptr[i] = osc.renderSample();
    }

    return result;
}

/**
 * @brief Bind a single oscillator type with all standard methods
 *
 * Template function to reduce code duplication across oscillator types.
 *
 * @tparam OscType Oscillator class to bind
 * @param m Python module
 * @param name Python class name
 * @param docstring Class documentation
 */
template <typename OscType>
void bind_oscillator_type(py::module_& m, const char* name, const char* docstring)
{
    py::class_<OscType>(m, name, docstring)
        .def(py::init<>(), "Construct oscillator with default state")
        .def("set_frequency",
             &OscType::setFrequency,
             py::arg("frequency"),
             py::arg("sample_rate"),
             "Set frequency (Hz) and sample rate (Hz)")
        .def("reset_phase",
             &OscType::resetPhase,
             "Reset phase to zero")
        .def("render_sample",
             &OscType::renderSample,
             "Generate and return a single sample")
        .def("render",
             &render_oscillator<OscType>,
             py::arg("num_samples"),
             "Render multiple samples as numpy array, preserving state");
}

void bind_oscillators(py::module_& m)
{
    // Bind all oscillator types using the template
    bind_oscillator_type<Oscillators::BLEP::Sine<float>>(
        m, "Sine",
        "Band-limited sine wave oscillator. Clean, pure tone with single fundamental frequency.");

    bind_oscillator_type<Oscillators::BLEP::Saw<float>>(
        m, "Saw",
        "Band-limited sawtooth oscillator. Rich in harmonics, ideal for subtractive synthesis.");

    bind_oscillator_type<Oscillators::BLEP::Square<float>>(
        m, "Square",
        "Band-limited square wave oscillator. Hollow tone with odd harmonics only.");

    bind_oscillator_type<Oscillators::BLEP::Triangle<float>>(
        m, "Triangle",
        "Band-limited triangle wave oscillator. Mellow tone, softer than square wave.");
}