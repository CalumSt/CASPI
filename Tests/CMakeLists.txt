message(STATUS "Configuring CASPI Tests")

include(FetchContent)

set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs CACHE PATH "FetchContent download dir")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ============================================================
# Sources
# ============================================================

set(SOURCES
        base/SIMD_test.cpp
        base/SIMD/SIMD_complex_test.cpp
        base/SIMD/SIMD_operations_test.cpp
        base/SIMD/SIMD_block_test.cpp
        base/SIMD/SIMD_LoadStore_test.cpp
        base/SIMD/SIMD_PolyKernel_test.cpp
        base/SIMD/SIMD_strategy_test.cpp
        core/Phase_test.cpp
        core/Expected_test.cpp
        core/AudioBuffer_test.cpp
        core/DelayLine_test.cpp
        core/BaseClass_test.cpp
        base/Utilities_test.cpp
        controls/Envelope_test.cpp
        sources/Oscillator_test.cpp
        sources/Operator_test.cpp
        processors/Filter_test.cpp
        processors/Envelope_test.cpp
        processors/Gain_test.cpp
        synthesizers/FMGraph_test.cpp
        maths/Spectral_test.cpp
        maths/FMTheory_test.cpp
        maths/FFT_test.cpp
        maths/Maths_test.cpp
)

add_executable(UnitTests ${SOURCES})

target_include_directories(UnitTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(UnitTests PRIVATE cxx_std_11)

target_link_libraries(UnitTests PRIVATE
        gtest_main
        CASPI
)

target_compile_definitions(UnitTests PRIVATE
        CASPI_RT_SAFE_EXPECTS
)

caspi_apply_sanitizer(UnitTests)

caspi_apply_debug_flags(UnitTests)

gtest_discover_tests(UnitTests)