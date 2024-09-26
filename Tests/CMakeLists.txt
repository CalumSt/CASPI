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

)
# --------------------------------------------------------------------------


add_executable(${PROJECT_NAME} ${SOURCES})

# --------------------------------------------------------------------------
# ADD DEPENDENCIES HERE
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ../Libs/googletest/googletest/include
        ../Source

    )    
# --------------------------------------------------------------------------
    
target_link_libraries(${PROJECT_NAME} 
PRIVATE
PUBLIC
    CASPI
    GTest::gtest_main)


gtest_discover_tests(${PROJECT_NAME})