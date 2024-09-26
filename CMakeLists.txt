cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME "CASPI")

project(${PROJECT_NAME} LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)

add_library(CASPI STATIC library.cpp)

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/../*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/../*.h")
target_sources("${PROJECT_NAME}" PRIVATE ${SourceFiles})

## Testing
add_subdirectory(Tests)
add_subdirectory(Libs/googletest)
enable_testing()

