CASPI (C++ Audio Synthesis and Processing Interface) aims to provide modular building blocks for constructing Synthesisers, allowing for a drag-and-drop architecture.
This project is purely for personal use, but thought it would be valuable to try to make a library. It's a collection of useful audio processing classes from a variety of sources.
This project is header-only (except for tests) and has a dependency on googletest for testing.
Hopefully, including this should be as simple as including it as a submodule and adding #include "caspi.h".
Alternatively, if you only need a little bit, you can use specific components, e.g. #include "caspi_oscillators.h"


Wishlist:
	Full code test coverage
	Doxygen-code generation
	Automatic UML diagrams
	Framework-independent, but JUCE-forward
	Machine Learning integration for synth parameter estimation
	
See also:
	Will Pirkle Book
	The Computer Music Tutorial
	

