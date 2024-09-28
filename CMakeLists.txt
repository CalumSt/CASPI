cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME "CASPI")

project(${PROJECT_NAME} LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)

add_library(CASPI STATIC caspi.h)
message(STATUS "Configuring CASPI")

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else()
    message(STATUS "This project is a top-level one")
endif()

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*/*.h")
target_sources("${PROJECT_NAME}" PRIVATE ${SourceFiles})

## Testing
add_subdirectory(Tests)
add_subdirectory(Libs/googletest)
enable_testing()

