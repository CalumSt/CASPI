cmake_minimum_required(VERSION 3.22)

set(caspi_headers
        Oscillators/caspi_oscillators.h
        Utilities/caspi_utilities.h
        Filters/caspi_filters.h
        Envelopes/caspi_envelopes.h)

include_directories(${PROJECT_NAME} PUBLIC ${caspi_headers})
