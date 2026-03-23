# TODO

## Features

---

### Graph Class
  * It should be possible to build a graph of audio nodes and control nodes, and then render the graph in real-time. The graph should support feedback loops and dynamic changes to the topology.

### MIDI Support
* MIDI input should be supported, allowing for note triggering, pitch bending, modulation, and other MIDI events to control the synthesis parameters.

### Synthesizer Voice

### Synth Engine
* Perhaps the most complex feature. This is possible as a standalone engine, and an MPE Engine could be built alongside it.
* Will need some design work to work out how to map JUCE audio processor functionality to the engine. Similar for iPlug2. E.g. MIDI, audio buffers.

### Filter Engine
* A suite of filters, with a base class, and a filter designer class?
 
### Compressor
* A compressor with pluggable algorithms (e.g. VCA, FET, Optical) and a flexible sidechain input.

### Delay Lines & Circular Buffers

* A set of delay line classes, including circular buffers for efficient delay implementations, and support for fractional delay times with interpolation.
* This may be tricky with the multi-channel audio buffer.

### Reverb

* Plugable, flexible reverb algorithms
* [Let's Write a Reverb - Geraint Luff - ADC21](https://www.youtube.com/watch?v=6ZK2Goiyotk)

### Effect Module
* A base class for audio effects, with support for user-defined processing functions, and a library of common effects (e.g. chorus, flanger, phaser, tremolo).
* Basically, everything in the Computer Music Tutorial and Will Pirkle's books that isn't already listed.
* Some quirkier effects would be good too, e.g. bitcrushers, sample rate reducers, ring modulators, frequency shifters.

### Waveshaper

* Flesh out existing waveshaper class, with support for user-defined transfer functions, and a library of common distortion algorithms (e.g. hard clipping, soft clipping, tube distortion).

### Samplers

* Sampler classes for sample playback, as Audio Producers

### Granular Synthesis

* A granular synthesis engine, allowing for real-time manipulation of audio samples by dividing them into small grains and processing them individually. This could include features like grain size control, density control, pitch shifting, and spatialization.

### Additive Synthesis

* An additive synthesis engine, allowing for the creation of complex sounds by summing multiple sine wave oscillators. This could include features like harmonic control, inharmonicity, and dynamic control over the number of active oscillators.

### Drum Synthesis

* Provide a set of drum synthesis algos
* [Zion Jaymes, Drum Synthesis series on YouTube](https://www.youtube.com/watch?v=ndG-6-vONNc)

### Physical Modelling Synthesis

---
## Repo health

---

### Coverage

* It would be good to have code coverage reports for the library, which are automatically generated and updated with each commit. This would help ensure that we have good test coverage and can identify any areas of the code that may need additional testing.

### Doxygen Docs Page

* It would be good to have a Doxygen-generated documentation page, which is automatically updated with each commit and accessible from the repo. This would make it easier for users to understand the API and how to use the library.
* This could be set up using GitHub Actions to generate the documentation on each push and deploy it to GitHub Pages, or a similar hosting service.

### Benchmarks

* It would be good to have benchmarks for common systems, incorporated into the CI pipeline and displayed in the repo. Perhaps a weekly benchmark run, which runs a suite of benchmarks and posts the results to a dashboard, or top-level file (e.g. BENCHMARKS.md) in the repo.
* If taking the dashboard approach, it would be good to have a historical record of benchmark results, so that we can track performance over time and identify any regressions. It would also be good to have audio demos within this dashboard.

---
## Misc

---
### Audio Demos

### A nice logo :)