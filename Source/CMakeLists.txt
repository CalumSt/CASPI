cmake_minimum_required(VERSION 3.22)

set(caspi_headers
    Oscillators/caspi_Oscillators.h
    Utilities/caspi_Utilities.h)

include_directories(${PROJECT_NAME} PUBLIC ${caspi_headers})
