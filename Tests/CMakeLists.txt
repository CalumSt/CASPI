cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 20)
# Set up the testing project
project(UnitTests)
# allow testing from this directory
enable_testing()

message(STATUS "Configuring CASPI Tests")

# set up sources
include(GoogleTest)
add_subdirectory(Libs/googletest/)

set(This Tests)
# --------------------------------------------------------------------------
# ADD TEST FILES HERE
set(SOURCES
        CircularBuffer_test.cpp
        Oscillator_test.cpp
        Utilities_test.cpp
        Filter_test.cpp
        Envelope_test.cpp
        PMOperator_test.cpp
        PMAlgorithm_test.cpp
        Gain_test.cpp
        FFT_test.cpp
        Maths_test.cpp
)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})

# --------------------------------------------------------------------------
# ADD DEPENDENCIES HERE
target_include_directories(${PROJECT_NAME}
    PRIVATE
        Libs/googletest/googletest/include
        ../Source
    )    
# --------------------------------------------------------------------------
    
target_link_libraries(${PROJECT_NAME} PRIVATE gtest_main)

gtest_discover_tests(${PROJECT_NAME})