cmake_minimum_required(VERSION 3.22)
# Set up the testing project
project(TESTING)
# allow testing from this directory
enable_testing()

# set up sources
include(GoogleTest)

set(This Tests)
# --------------------------------------------------------------------------
# ADD TEST FILES HERE
set(SOURCES
        Oscillator_test.cpp
        Utilities_test.cpp
        Filter_test.cpp
        Envelope_test.cpp
)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})

# --------------------------------------------------------------------------
# ADD DEPENDENCIES HERE
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ../Libs/googletest/googletest/include

    )    
# --------------------------------------------------------------------------
    
target_link_libraries(${PROJECT_NAME} 
PRIVATE
PUBLIC
    CASPI
    gtest_main)


gtest_discover_tests(${PROJECT_NAME})