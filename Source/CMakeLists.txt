cmake_minimum_required(VERSION 3.22)

include_directories(${PROJECT_NAME} INTERFACE
        Oscillators
        Utilities
        Filters
        Envelopes
        Synthesizers
        Gain
        AudioUtilities
)