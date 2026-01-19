#include "caspi.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;

void bind_core (py::module_& m)
{
    // AudioBuffer binding with numpy integration
    py::class_<AudioBuffer<float, InterleavedLayout>> (m, "AudioBuffer", "Multi-channel audio buffer with numpy integration")
        .def (py::init<size_t, size_t>(),
              py::arg ("num_channels"),
              py::arg ("num_frames"),
              "Create an audio buffer\n\n"
              "Args:\n"
              "    num_channels: Number of audio channels\n"
              "    num_frames: Number of samples per channel")
        .def ("num_channels", &AudioBuffer<float, InterleavedLayout>::numChannels)
        .def ("num_frames", &AudioBuffer<float, InterleavedLayout>::numFrames)
        .def ("to_numpy", [] (AudioBuffer<float, InterleavedLayout>& buf)
              {
            // Return as (frames, channels) numpy array
            return py::array_t<float>(
                {buf.numFrames(), buf.numChannels()},
                {buf.numChannels() * sizeof(float), sizeof(float)},
                buf.data(),
                py::cast(buf)  // Keep buffer alive
            ); },
              "Convert buffer to numpy array (frames, channels)")
        .def_static ("from_numpy", [] (py::array_t<float> arr)
                     {
            py::buffer_info info = arr.request();
            if (info.ndim != 2) {
                throw std::runtime_error("Expected 2D array (frames, channels)");
            }
            size_t frames = info.shape[0];
            size_t channels = info.shape[1];
            
            AudioBuffer<float, InterleavedLayout> buf(channels, frames);
            float* src = static_cast<float*>(info.ptr);
            
            // Copy data
            for (size_t f = 0; f < frames; ++f) {
                for (size_t c = 0; c < channels; ++c) {
                    buf.sample(c, f) = src[f * channels + c];
                }
            }
            return buf; },
                     "Create buffer from numpy array (frames, channels)");

    // Phase wrapper
    py::class_<Phase<float>> (m, "Phase", "Phase accumulator for oscillators")
        .def (py::init<>())
        .def ("reset_phase", &Phase<float>::resetPhase)
        .def ("set_frequency", &Phase<float>::setFrequency, py::arg ("frequency"), py::arg ("sample_rate"));
}