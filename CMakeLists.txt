cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME "CASPI")
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

message(STATUS "Standalone build: ${CASPI_IS_STANDALONE_BUILD}")
message(STATUS "Build testing: ${CASPI_BUILD_TESTING}")
message(STATUS "Build benchmarks: ${CASPI_BUILD_BENCHMARKS}")

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

# Only do these if this is the main project, and not if it is included through add_subdirectory
if (CASPI_IS_STANDALONE_BUILD)

    # Let's ensure -std=c++xx instead of -std=g++xx
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Docs only available if this is the main app
    find_package(Doxygen)
    find_package(Doxygen REQUIRED)
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

    if (SANITIZE STREQUAL "address")
        message(STATUS "Enabling AddressSanitizer")
        # Add AddressSanitizer flags for GCC/Clang
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
    elseif (SANITIZE STREQUAL "undefined")
        message(STATUS "Enabling UndefinedBehaviorSanitizer")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
        set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=undefined")
    elseif (SANITIZE STREQUAL "")
        # No sanitizer: do nothing
    else ()
        message(FATAL_ERROR "Unknown sanitizer type: ${SANITIZE}")
    endif ()
endif()

# Testing only available if this is the main app
if(CASPI_BUILD_TESTING)
    add_subdirectory(Tests)
endif()

if (CASPI_BUILD_BENCHMARKS)
    add_subdirectory(Benchmarks)
endif ()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets)

install(DIRECTORY Library/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")





