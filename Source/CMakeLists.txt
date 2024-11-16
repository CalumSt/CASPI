cmake_minimum_required(VERSION 3.22)

set(caspi_headers
        Oscillators/caspi_BlepOscillator.h
        Oscillators/caspi_PMOperator.h
        Utilities/caspi_Assert.h
        Utilities/caspi_CircularBuffer.h
        Utilities/caspi_Maths.h
        Utilities/caspi_Constants.h
        Utilities/caspi_ScopedLock.h
        Filters/caspi_SvfFilter.h
        Envelopes/caspi_EnvelopeGenerator.h)

include_directories(${PROJECT_NAME} PUBLIC ${caspi_headers})

add_library(CASPI INTERFACE)
target_include_directories(CASPI INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
