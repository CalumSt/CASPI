#ifndef CASPI_FEATURES_H
#define CASPI_FEATURES_H
/************************************************************************
 .d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888


* @file caspi_Features.h
* @author CS Islay
* @brief A collection of macros to define features based on platform.
*
************************************************************************/

#include "caspi_Platform.h"

/**
* The following features are defined based on the platform:
*    CASPI_FEATURES_HAS_CONCEPTS
*    CASPI_FEATURES_HAS_RANGES
*    CASPI_FEATURES_HAS_TYPE_TRAITS (it is very likely that this will always be defined)
*/

#if defined(CASPI_CPP_VERSION)

    #if defined(CASPI_CPP_20)

        #if defined(__cpp_concepts) && (__cpp_concepts >= 201907L)
            #define CASPI_FEATURES_HAS_CONCEPTS
            #include <concepts>
        #endif
        #if defined(__cpp_lib_ranges) && (__cpp_lib_ranges >= 201911L)
            #define CASPI_FEATURES_HAS_RANGES
        #endif
        #if defined(__cpp_lib_span) && (__cpp_lib_span >= 202002L)
            #define CASPI_FEATURES_HAS_SPAN
            #include <span>
        #endif

        #define CASPI_CPP20_CONSTEXPR constexpr

    #else

        #define CASPI_CPP20_CONSTEXPR

    #endif

    #if defined(CASPI_CPP_17)

        #define CASPI_NO_DISCARD [[nodiscard]]
        #define CASPI_CPP17_IF_CONSTEXPR if constexpr
        #define CASPI_FEATURES_HAS_NOTHROW_SWAPPABLE
        #define CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES

    #else

        #define CASPI_NO_DISCARD
        #define CASPI_CPP17_IF_CONSTEXPR if

    #endif

    #if defined(CASPI_CPP_14)

    #endif
    #if defined(CASPI_CPP_11)

        #define CASPI_FEATURES_HAS_TYPE_TRAITS

    #endif

#endif

// Non-blocking
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonblocking)
#    define CASPI_NON_BLOCKING [[clang::nonblocking]]
#  else
#    define CASPI_NON_BLOCKING
#  endif
#else
#  define CASPI_NON_BLOCKING
#endif

// blocking
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::blocking)
#    define CASPI_BLOCKING [[clang::blocking]]
#  else
#    define CASPI_BLOCKING
#  endif
#else
#  define CASPI_BLOCKING
#endif

// Non-allocating
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonallocating)
#    define CASPI_NON_ALLOCATING [[clang::nonallocating]]
#  else
#    define CASPI_NON_ALLOCATING
#  endif
#else
#  define CASPI_NON_ALLOCATING
#endif

// allocating
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::allocating)
#    define CASPI_ALLOCATING [[clang::allocating]]
#  else
#    define CASPI_ALLOCATING
#  endif
#else
#  define CASPI_ALLOCATING
#endif

// SSE2 enables FLUSH_ZERO (FZ)
#if defined(CASPI_HAS_SSE2) && (defined(CASPI_ARCH_X86_64) || defined(CASPI_ARCH_X86_32))
#define CASPI_FEATURES_HAS_FLUSH_ZERO
#endif

// SSE3 enables DENORMALS_ZERO (DAZ)
#if defined(CASPI_HAS_SSE2) && defined(CASPI_HAS_SSE3) && (defined(CASPI_ARCH_X86_64) || defined(CASPI_ARCH_X86_32))
    #define CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS
#endif

#endif //CASPI_FEATURES_H
