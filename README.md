CASPI (C++ Audio Synthesis and Processing Interface) aims to provide modular building blocks for constructing Synthesisers, allowing for a drag-and-drop architecture.
This project is purely for personal use, and heavily based on The Computer Music Tutorial and Will Pirkle's books, but thought it would be valuable to try to make a library. It's a collection of useful audio processing classes from a variety of sources.

This project is header-only and freestanding, except for tests and benchmarks. These use the google set of toolings (googletest, googlebenchmark).

To bind it to your project, you can include the headers directly in your source files. You can also use CMake to include the library in your project, which will automatically handle the dependencies and include paths for you.
You can include the entire library by using `#include "caspi.h"` in your project, which will include all the necessary headers for you.
Alternatively, if you only need a little bit, you can use specific components, e.g. #include "oscillators/caspi_BlepOscillator.h"

**Building**

To use CASPI as a header-only library in your CMake project:  
Add CASPI as a subdirectory:  

```cmake
add_subdirectory(path/to/CASPI)
target_link_libraries(your_target PRIVATE CASPI)
```
Include the header in your code: 
```cpp
#include <caspi.h>
#include <filters/caspi_Ladder.h>
#include <oscillators/caspi_BlepOscillator.h>
etc...
```
CASPI will automatically provide the necessary include paths. No source files are compiled; simply link to the CASPI target and include the header.

You can also enable the tests and benchmarks by adding the following lines:
```
-DCASPI_BUILD_TESTS=ON
-DCASPI_BUILD_BENCHMARKS=ON
```
These will be built by default if CASPI is built standalone.

**Aims and Design**

CASPI has a very simple guiding principle: Everything should sound good. This is audio & music, after all!

The library is hierarchical and builds on itself. Including heavier classes (e.g. PMAlgorithms) will use pieces from other parts of the library, with base at the foundation. 

There is no specific framework that CASPI should work best with. It is likely to be used with JUCE, but should aim to be agnostic. Any framework adapters should be separate from this library.

Classes that produce audio should inherit the API from the "Producer" base class. Classes that process audio should inherit from the "Processor" base class. Everything either renders (produces) or processes audio.

Generally, classes and functions are in UpperCamelCase and lowerCamelCase respectively. If a class is compatible with the standard library (e.g. iterators, algorithms), it uses snake_case to denote this.


**TODO**
- Wavetables
- Samplers
- Granular Synthesis
- Additive Synthesis
- CASPI Explorer program
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
	

