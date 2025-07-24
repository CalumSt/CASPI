cmake_minimum_required(VERSION 3.29)

option(CASPI_BUILD_TESTING "Build CASPI tests" ON)
option(CASPI_BUILD_BENCHMARKS "Build CASPI benchmarks" OFF)

set(PROJECT_NAME "CASPI")

message(STATUS "Configuring CASPI")

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
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)

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
endif()

# Testing only available if this is the main app
if(CASPI_BUILD_TESTING)
    add_subdirectory(Tests)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets)

install(DIRECTORY Library/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")





