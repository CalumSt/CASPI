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
*/

// clang-format off
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
        #if defined(__cpp_lib_math_special_functions)
            #define CASPI_HAS_STD_BESSEL
        #endif
        #define CASPI_NO_DISCARD [[nodiscard]]
        #define CASPI_MAYBE_UNUSED [[maybe_unused]]
        #define CASPI_FEATURES_HAS_IF_CONSTEXPR
        #define CASPI_CPP17_IF_CONSTEXPR if constexpr
        #define CASPI_FEATURES_HAS_NOTHROW_SWAPPABLE
        #define CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES
        #define CASPI_FEATURES_HAS_STRUCTURED_BINDINGS

    #else

        #define CASPI_NO_DISCARD
        #define CASPI_MAYBE_UNUSED
        #define CASPI_CPP17_IF_CONSTEXPR if

    #endif

    #if defined(CASPI_CPP_14)
        #if defined(__cpp_lib_make_unique) && (__cpp_lib_make_unique >= 201304L)
        #define CASPI_FEATURES_HAS_MAKE_UNIQUE
        #endif

#else

    #endif
    #if defined(CASPI_CPP_11)

        #endif

#endif

// Detect compiler support for restrict-like qualifiers
#if defined(CASPI_COMPILER_MSVC)
    // Microsoft Visual C++
    #define CASPI_RESTRICT __restrict

#elif defined(CASPI_COMPILER_CLANG)
    // Clang (also defines __GNUC__, so must come first)
    #if __has_extension(cxx_restrict)
        #define CASPI_RESTRICT __restrict__
    #else
        #define CASPI_RESTRICT
    #endif
#elif defined(CASPI_COMPILER_GCC)
    // GCC and compatible
    #define CASPI_RESTRICT __restrict__
#else
    // Unknown compiler
    #define CASPI_RESTRICT
#endif


// Concept for floating-point types (C++20)
#if defined(CASPI_FEATURES_HAS_CONCEPTS) && ! defined(CASPI_FEATURES_DISABLE_CONCEPTS)
#include <concepts>
template <typename T>
concept FloatingPoint = std::is_floating_point_v<T>;
#define CASPI_FLOAT_TYPE FloatingPoint
#else
#define CASPI_FLOAT_TYPE typename
#endif


// Non-blocking
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonblocking) && defined(CASPI_COMPILER_CLANG) && !defined(CASPI_PLATFORM_MACOS) && __clang_major__ >= 17
#    define CASPI_NON_BLOCKING [[clang::nonblocking]]
#  else
#    define CASPI_NON_BLOCKING
#  endif
#else
#  define CASPI_NON_BLOCKING
#endif

// blocking
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::blocking) && defined(CASPI_COMPILER_CLANG) && !defined(CASPI_PLATFORM_MACOS) && __clang_major__ >= 17
#    define CASPI_BLOCKING [[clang::blocking]]
#  else
#    define CASPI_BLOCKING
#  endif
#else
#  define CASPI_BLOCKING
#endif

// Non-allocating
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonallocating)  && defined(CASPI_COMPILER_CLANG) && !defined(CASPI_PLATFORM_MACOS) && __clang_major__ >= 17
#    define CASPI_NON_ALLOCATING [[clang::nonallocating]]
#  else
#    define CASPI_NON_ALLOCATING
#  endif
#else
#  define CASPI_NON_ALLOCATING
#endif

// allocating
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::allocating)  && defined(CASPI_COMPILER_CLANG) && !defined(CASPI_PLATFORM_MACOS) && __clang_major__ >= 17
#    define CASPI_ALLOCATING [[clang::allocating]]
#  else
#    define CASPI_ALLOCATING
#  endif
#else
#  define CASPI_ALLOCATING
#endif

// always inline
#if defined(__GNUC__) || defined(CASPI_COMPILER_CLANG)
    #define CASPI_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(CASPI_COMPILER_MSVC)
    #define CASPI_ALWAYS_INLINE __forceinline
#else
    #define CASPI_ALWAYS_INLINE inline
#endif

// SSE2 enables FLUSH_ZERO (FZ)
#if defined(CASPI_HAS_SSE2) && (defined(CASPI_ARCH_X86_64) || defined(CASPI_ARCH_X86_32))
#define CASPI_FEATURES_HAS_FLUSH_ZERO
#endif

// SSE3 enables DENORMALS_ZERO (DAZ)
#if defined(CASPI_HAS_SSE2) && defined(CASPI_HAS_SSE3) && (defined(CASPI_ARCH_X86_64) || defined(CASPI_ARCH_X86_32))
    #define CASPI_FEATURES_HAS_FLUSH_ZERO_DENORMALS
#endif
// clang-format on
#endif //CASPI_FEATURES_H
