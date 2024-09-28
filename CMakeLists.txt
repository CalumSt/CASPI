cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME "CASPI")

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "Configuring CASPI")
endif()

project(${PROJECT_NAME} LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)

add_library(${PROJECT_NAME} INTERFACE caspi.h
        Source/Oscillators/caspi_Oscillators.h
        Source/Oscillators/BLEP/caspi_BlepOscillator.h)
add_subdirectory(Source)
target_include_directories(${PROJECT_NAME} INTERFACE Source/.)


#file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "*.cpp" "*.h")

## Testing
add_subdirectory(Tests)
add_subdirectory(Libs/googletest)
enable_testing()

