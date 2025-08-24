cmake_minimum_required(VERSION 3.29)
set(CMAKE_CXX_STANDARD 20)
# Set up the testing project
project(UnitTests)
# allow testing from this directory
enable_testing()

message(STATUS "Configuring CASPI Tests")

include(FetchContent)

# Define download location for third-party libs
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs CACHE PATH "Base directory for FetchContent downloads")

# Prevent GoogleTest from overriding your compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Fetch GoogleTest
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Make it available
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# --------------------------------------------------------------------------
# ADD TEST FILES HERE
set(SOURCES
        base/Phase_test.cpp
        base/Expected_test.cpp
        base/AudioBuffer_test.cpp
        base/DelayLine_test.cpp
        base/Expected_test.cpp
        base/BaseClass_test.cpp
        sources/Oscillator_test.cpp
        base/Utilities_test.cpp
        processors/Filter_test.cpp
        processors/Envelope_test.cpp
        sources/PMOperator_test.cpp
        synthesizers/PMAlgorithm_test.cpp
        processors/Gain_test.cpp
        maths/FFT_test.cpp
        maths/Maths_test.cpp
)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})
# --------------------------------------------------------------------------
    
target_link_libraries(${PROJECT_NAME} PRIVATE
        gtest_main
        CASPI
)

gtest_discover_tests(${PROJECT_NAME})

add_test(NAME MyUnitTests COMMAND ${PROJECT_NAME})
set_tests_properties(MyUnitTests PROPERTIES LABELS "test")