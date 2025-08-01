cmake_minimum_required(VERSION 3.29)
set(CMAKE_CXX_STANDARD 20)
# Set up the benchmark project
project(Benchmarks)

message(STATUS "Configuring CASPI Benchmarks")

include(FetchContent)

# Define download location for third-party libs
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs CACHE PATH "Base directory for FetchContent downloads")

# Prevent GoogleTest from overriding your compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Fetch GoogleTest
FetchContent_Declare(
        google_benchmark
        URL https://github.com/google/benchmark/archive/refs/heads/main.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Make it available
FetchContent_MakeAvailable(google_benchmark)

# --------------------------------------------------------------------------
# ADD TEST FILES HERE
set(SOURCES
        base/Phase_bm.cpp
        base/Denormals_bm.cpp
        base/Expected_bm.cpp
)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})
# --------------------------------------------------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE
        benchmark::benchmark
        CASPI
)