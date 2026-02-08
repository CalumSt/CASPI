#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// Forward declarations of binding functions from other files
void bind_core (py::module_& m);
void bind_oscillators (py::module_& m);
void bind_adsr(py::module_& m);
void bind_operator(py::module_& m);
void bind_fmgraph(py::module_& m);


PYBIND11_MODULE (caspy, m)
{
    m.doc() = R"pbdoc(
        CASPy - Python bindings for CASPI Audio DSP Library
        ====================================================
        
        A high-performance, real-time safe audio DSP library with Python bindings.
        
        Modules:
            - oscillators: BLEP oscillators (Sine, Saw, Square, Triangle)
            - buffers: AudioBuffer for multichannel audio processing
            - core: Base classes (Producer, Processor)
        
        Example:
            >>> import caspy
            >>> import numpy as np
            >>> osc = caspy.Saw()
            >>> osc.set_frequency(440.0, 48000.0)
            >>> samples = osc.render(48000)  # 1 second at 48kHz
    )pbdoc";

    // Version info
    m.attr ("__version__") = "0.1.0";

    // Bind submodules
    bind_core (m);
    bind_oscillators (m);
    bind_adsr(m);
    bind_operator(m);
    bind_fmgraph(m);
}