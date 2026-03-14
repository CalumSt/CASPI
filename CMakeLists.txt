cmake_minimum_required(VERSION 3.16)

project(CASPI VERSION 0.1 LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME}")

set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# Options
# ============================================================

option(CASPI_ENABLE_DEBUG     "Enable debug features"    OFF)
option(CASPI_BUILD_TESTING    "Build tests"              OFF)
option(CASPI_BUILD_BENCHMARKS "Build benchmarks"         OFF)
option(CASPI_BUILD_PYTHON     "Build Python bindings"    OFF)

set(CASPI_SANITIZER "None" CACHE STRING "Sanitizer (None, Address, Undefined, Thread, Realtime)")
set_property(CACHE CASPI_SANITIZER PROPERTY STRINGS None Address Undefined Thread Realtime)

message(STATUS "Sanitizer: ${CASPI_SANITIZER}")

# ============================================================
# Library (Header-only)
# ============================================================

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/CASPI>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_11)

target_compile_definitions(${PROJECT_NAME} INTERFACE
        $<$<BOOL:${CASPI_ENABLE_DEBUG}>:CASPI_DEBUG>
)

# ============================================================
# Sanitizer Helper
# ============================================================

function(caspi_apply_sanitizer TARGET)
    if(CASPI_SANITIZER STREQUAL "None")
        return()
    endif()

    if(CASPI_SANITIZER STREQUAL "Address")
        set(_flags -fsanitize=address -fno-omit-frame-pointer)
    elseif(CASPI_SANITIZER STREQUAL "Undefined")
        set(_flags -fsanitize=undefined)
    elseif(CASPI_SANITIZER STREQUAL "Thread")
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR "ThreadSanitizer requires GCC or Clang")
        endif()
        set(_flags -fsanitize=thread)
    elseif(CASPI_SANITIZER STREQUAL "Realtime")
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "RealtimeSanitizer requires Clang")
        endif()
        set(_flags -fsanitize=realtime)
    else()
        message(FATAL_ERROR "Unknown sanitizer: ${CASPI_SANITIZER}")
    endif()

    target_compile_options(${TARGET} PRIVATE ${_flags})
    target_link_options(${TARGET} PRIVATE ${_flags})
endfunction()

# ============================================================
# Extra flags for debug builds
# ============================================================
function(caspi_apply_debug_flags TARGET)
    if(NOT CASPI_ENABLE_DEBUG)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${TARGET} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET} PRIVATE
                /W4
        )
    endif()
endfunction()

# ============================================================
# Subdirectories
# ============================================================

if(CASPI_BUILD_TESTING)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(CASPI_BUILD_BENCHMARKS)
    add_subdirectory(Benchmarks)
endif()

if(CASPI_BUILD_PYTHON)
    add_subdirectory(Tools/CASPy)
    add_custom_target(caspy_wheel
            COMMAND uv build --wheel --out-dir dist
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Tools/CASPy
            COMMENT "Building CASPy wheel using uv"
    )
endif()

# ============================================================
# Installation
# ============================================================

include(CMakePackageConfigHelpers)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
)

install(DIRECTORY CASPI/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION lib/cmake/${PROJECT_NAME}
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION lib/cmake/${PROJECT_NAME}
)