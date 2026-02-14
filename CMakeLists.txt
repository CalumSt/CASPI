cmake_minimum_required(VERSION 3.13)

set(PROJECT_NAME "CASPI")
option(CASPI_ENABLE_DEBUG "Enable CASPI debug features (asserts, checks)" OFF)
# =========================
# Sanitizer Options
# =========================

option(CASPI_ENABLE_ASAN    "Enable AddressSanitizer" OFF)
option(CASPI_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(CASPI_ENABLE_TSAN    "Enable ThreadSanitizer" OFF)
option(CASPI_ENABLE_RTSAN   "Enable Real-Time Sanitizer (RTSan)" OFF)

# =========================
# Sanitizer Validation
# =========================

set(_CASPI_SANITIZER_COUNT 0)

foreach(opt
        CASPI_ENABLE_ASAN
        CASPI_ENABLE_UBSAN
        CASPI_ENABLE_TSAN
        CASPI_ENABLE_RTSAN)
    if (${opt})
        math(EXPR _CASPI_SANITIZER_COUNT "${_CASPI_SANITIZER_COUNT} + 1")
    endif ()
endforeach()

message(STATUS "Configuring CASPI")
# Detect if standalone build (top-level project)
set(CASPI_IS_STANDALONE_BUILD OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CASPI_IS_STANDALONE_BUILD ON)
endif ()

# Optionally allow SANITIZE to be empty or not set
if (NOT DEFINED SANITIZE)
    set(SANITIZE "")
endif ()

# Define options with default depending on standalone or not,
# but only if the user hasn't set them already:
if (NOT DEFINED CASPI_BUILD_TESTING)
    set(CASPI_BUILD_TESTING ${CASPI_IS_STANDALONE_BUILD} CACHE BOOL "Build CASPI tests")
endif ()

if (NOT DEFINED CASPI_BUILD_BENCHMARKS)
    set(CASPI_BUILD_BENCHMARKS ${CASPI_IS_STANDALONE_BUILD} CACHE BOOL "Build CASPI benchmarks")
endif ()

# NEW: Python bindings option (off by default)
if (NOT DEFINED CASPI_BUILD_PYTHON)
    set(CASPI_BUILD_PYTHON OFF CACHE BOOL "Build Python bindings (CASPy)")
endif ()

message(STATUS "Standalone build: ${CASPI_IS_STANDALONE_BUILD}")
message(STATUS "Build testing: ${CASPI_BUILD_TESTING}")
message(STATUS "Build benchmarks: ${CASPI_BUILD_BENCHMARKS}")
message(STATUS "Build Python bindings: ${CASPI_BUILD_PYTHON}")

project(${PROJECT_NAME} LANGUAGES CXX VERSION 0.1)

add_library(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/CASPI/caspi.h)

target_include_directories(${PROJECT_NAME} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/CASPI>
        $<INSTALL_INTERFACE:include>
)
# explicitly list the unified header for IDEs (has no build effect)
target_sources(CASPI INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/CASPI/caspi.h
)

# Define CASPI_DEBUG if CASPI_ENABLE_DEBUG is ON
target_compile_definitions(CASPI INTERFACE
        $<$<BOOL:${CASPI_ENABLE_DEBUG}>:CASPI_DEBUG>
)

# Only do these if this is the main project, and not if it is included through add_subdirectory
if (CASPI_IS_STANDALONE_BUILD)

    # Let's ensure -std=c++xx instead of -std=g++xx
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Docs only available if this is the main app
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(doc_doxygen
                COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Generating documentation with Doxygen"
                VERBATIM
        )
    else()
        message(STATUS "Doxygen not found, not building docs")
    endif()

    # Common flags
    set(_CASPI_SAN_FLAGS "")
    set(_CASPI_LINK_FLAGS "")

    # -------------------------
    # AddressSanitizer
    # -------------------------
    if (CASPI_ENABLE_ASAN)
        message(STATUS "Enabling AddressSanitizer")

        set(_CASPI_SAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
        set(_CASPI_LINK_FLAGS "-fsanitize=address")

        # -------------------------
        # UndefinedBehaviorSanitizer
        # -------------------------
    elseif (CASPI_ENABLE_UBSAN)
        message(STATUS "Enabling UndefinedBehaviorSanitizer")

        set(_CASPI_SAN_FLAGS "-fsanitize=undefined")
        set(_CASPI_LINK_FLAGS "-fsanitize=undefined")

        # -------------------------
        # ThreadSanitizer
        # -------------------------
    elseif (CASPI_ENABLE_TSAN)
        message(STATUS "Enabling ThreadSanitizer")

        if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR "TSan requires GCC or Clang")
        endif ()

        set(_CASPI_SAN_FLAGS "-fsanitize=thread")
        set(_CASPI_LINK_FLAGS "-fsanitize=thread")

        # -------------------------
        # Real-Time Sanitizer (RTSan)
        # -------------------------
    elseif (CASPI_ENABLE_RTSAN)
        message(STATUS "Enabling Real-Time Sanitizer (RTSan)")

        if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "RTSan requires Clang")
        endif ()

        if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
            message(WARNING "RTSan is typically used with Release builds")
        endif ()

        set(_CASPI_SAN_FLAGS "-fsanitize=realtime")
        set(_CASPI_LINK_FLAGS "-fsanitize=realtime")
    endif ()

    # -------------------------
    # Apply flags if any sanitizer enabled
    # -------------------------
    if (NOT _CASPI_SAN_FLAGS STREQUAL "")
        set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_CASPI_SAN_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_CASPI_SAN_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_CASPI_LINK_FLAGS}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_CASPI_LINK_FLAGS}")
    endif ()
endif()

# Testing only available if this is the main app
if(CASPI_BUILD_TESTING)
    add_subdirectory(Tests)
endif()

if (CASPI_BUILD_BENCHMARKS)
    add_subdirectory(Benchmarks)
endif ()

# NEW: Python bindings (optional, off by default)
if (CASPI_BUILD_PYTHON)
    add_subdirectory(Tools/CASPy)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets)

install(DIRECTORY Library/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")





