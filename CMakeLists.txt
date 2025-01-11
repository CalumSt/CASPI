cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME "CASPI")

message(STATUS "Configuring CASPI")

project(${PROJECT_NAME} LANGUAGES CXX VERSION 0.1)

include(GNUInstallDirs)

# Optional: Specify the headers that are part of the library
set(header_files
        caspi.h
        Source/Oscillators/caspi_BlepOscillator.h
        Source/Oscillators/caspi_PMOperator.h
        Source/Oscillators/caspi_PM.h
        Source/Filters/caspi_SvfFilter.h
        Source/Envelopes/caspi_EnvelopeGenerator.h
        Source/Utilities/caspi_Constants.h
        Source/Utilities/caspi_Gain.h
        Source/Utilities/caspi_Maths.h
        Source/Utilities/caspi_Assert.h
        Source/Utilities/caspi_FFT.h
        Source/Utilities/caspi_FFT_new.h
        Source/Utilities/caspi_CircularBuffer.h
        # ...
)

add_library(${PROJECT_NAME} INTERFACE ${header_files})

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR})

# Only do these if this is the main project, and not if it is included through add_subdirectory
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)

    # Let's ensure -std=c++xx instead of -std=g++xx
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Docs only available if this is the main app
    find_package(Doxygen)
    if(Doxygen_FOUND)
        add_subdirectory(docs)
    else()
        message(STATUS "Doxygen not found, not building docs")
    endif()
endif()


# Testing only available if this is the main app
# Emergency override MODERN_CMAKE_BUILD_TESTING provided as well
if((CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR MODERN_CMAKE_BUILD_TESTING)
        AND BUILD_TESTING)
    add_subdirectory(Tests)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include DESTINATION include COMPONENT ${PROJECT_NAME})







