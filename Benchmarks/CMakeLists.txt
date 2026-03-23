cmake_minimum_required(VERSION 3.13)
set(CMAKE_CXX_STANDARD 20)
# Set up the benchmark project
project(Benchmarks)

message(STATUS "Configuring CASPI Benchmarks")

include(FetchContent)

# Define download location for third-party libs
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs CACHE PATH "Base directory for FetchContent downloads")

# Prevent GoogleTest from overriding your compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

# Fetch GoogleTest
FetchContent_Declare(
        google_benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Make it available
FetchContent_MakeAvailable(google_benchmark)

# --------------------------------------------------------------------------
# ADD TEST FILES HERE
set(SOURCES
        base/Buffer_bm.cpp
        base/Phase_bm.cpp
        base/Expected_bm.cpp
        base/SIMD_bm.cpp
        base/AudioBuffer_SIMD_bm.cpp
        base/PolyKernel_bm.cpp
        Producers/Oscillator_bm.cpp
)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})
# --------------------------------------------------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        CASPI
)