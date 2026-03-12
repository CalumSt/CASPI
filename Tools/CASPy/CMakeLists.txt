cmake_minimum_required(VERSION 3.15...3.31)

project(CASPy VERSION 0.1.0 LANGUAGES CXX)

message(STATUS "Configuring CASPy (CASPI Python Bindings)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Python ---
# We only need Interpreter + dev headers for extension modules
find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)

# --- pybind11: use system first, fallback to FetchContent ---
find_package(pybind11 QUIET)
if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# --- CASPI: always consume from source tree ---
if(NOT TARGET CASPI)
    message(STATUS "Using CASPI from parent source tree")
    add_library(CASPI INTERFACE)
    target_include_directories(CASPI INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../CASPI"
    )
endif()

# --- Build Python module ---
pybind11_add_module(caspy
        Source/caspy_module.cpp
        Source/Bindings/bind_core.cpp
        Source/Bindings/bind_oscillators.cpp
        Source/Bindings/bind_adsr.cpp
        Source/Bindings/bind_operator.cpp
        Source/Bindings/bind_fmgraph.cpp
        Source/Bindings/bind_modmatrix.cpp
        Source/Bindings/bind_wavetable.cpp
        Source/Bindings/bind_parameters.cpp
)

target_link_libraries(caspy PRIVATE CASPI)

# --- Module properties (pybind11 handles suffix/prefix automatically) ---
set_target_properties(caspy PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        INTERPROCEDURAL_OPTIMIZATION ON
)

# Optional: output in build dir for easy `import caspy`
set_target_properties(caspy PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Install the .pyd/.so file to the Python package directory
# scikit-build-core expects this to go to the package name directory
if(NOT DEFINED SKBUILD_PLATLIB_DIR)
    message(STATUS "SKBUILD_PLATLIB_DIR not defined, setting to default")
    message(STATUS "Build through scikit-build-core is recommended for proper installation")
    set(SKBUILD_PLATLIB_DIR "${CMAKE_BINARY_DIR}/python_package")
endif()

install(TARGETS caspy
        LIBRARY DESTINATION ${SKBUILD_PLATLIB_DIR}
        RUNTIME DESTINATION ${SKBUILD_PLATLIB_DIR}
)
