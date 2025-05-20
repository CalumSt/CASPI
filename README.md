CASPI (C++ Audio Synthesis and Processing Interface) aims to provide modular building blocks for constructing Synthesisers, allowing for a drag-and-drop architecture.
This project is purely for personal use, and heavily based on The Computer Music Tutorial and Will Pirkle's books, but thought it would be valuable to try to make a library. It's a collection of useful audio processing classes from a variety of sources.
This project is header-only (except for tests) and has a dependency on googletest for testing.
Hopefully, including this should be as simple as including it as a submodule and adding #include "caspi.h".
Alternatively, if you only need a little bit, you can use specific components, e.g. #include "caspi_BlepOscillator.h"

CASPI has a very simple guiding principle: Everything should sound good. 

Current Features and Modules:
- BLEP Oscillators
- Envelope Generator
- Basic Filters
- Phase Modulation Operator

TODO:
- Ladder Filters
- Mod Matrix
- Synth Engine
- Waveshaper
- Compressor & Mixers

Further down the line...
- JUCE Bindings (and iPlug2, hopefully!)
- Drag-And-Drop Explorer
- 


See also:
- The Computer Music Tutorial
	

