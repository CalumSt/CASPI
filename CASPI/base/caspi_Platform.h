#ifndef CASPI_PLATFORM_H
#define CASPI_PLATFORM_H
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


* @file caspi_Platform.h
* @author CS Islay
* @brief A platform detection header.
*
************************************************************************/


/**
* This header defines macros for platform detection. The macros are defined as follows:
*
* Operating System:
*     CASPI_PLATFORM_WINDOWS
*     CASPI_PLATFORM_IOS
*     CASPI_PLATFORM_MACOS
*     CASPI_PLATFORM_APPLE (Fallback)
*     CASPI_PLATFORM_ANDROID
*     CASPI_PLATFORM_LINUX
*     CASPI_PLATFORM_WEBASSEMBLY
*     CASPI_PLATFORM_FREEBSD
*     CASPI_PLATFORM_UNIX
*     CASPI_PLATFORM_UNKNOWN
*     CASPI_PLATFORM_STRING
*
* Compilers:
*     CASPI_COMPILER_CLANG
*     CASPI_COMPILER_GCC
*     CASPI_COMPILER_MSVC
*     CASPI_COMPILER_INTEL
*     CASPI_COMPILER_MINGW
*     CASPI_COMPILER_UNKNOWN
*     CASPI_COMPILER_STRING
*
* Architecture:
*    CASPI_ARCHITECTURE_X86_64
*    CASPI_ARCHITECTURE_X86_32
*    CASPI_ARCHITECTURE_ARM32
*    CASPI_ARCHITECTURE_ARM64
*    CASPI_ARCHITECTURE_WASM
*    CASPI_ARCHITECTURE_PPC
*    CASPI_ARCHITECTURE_UNKNOWN
*    CASPI_ARCHITECTURE_STRING
*
* C++ Version:
*     CASPI_CPP_26
*     CASPI_CPP_23
*     CASPI_CPP_20
*     CASPI_CPP_17
*/
#if defined(_WIN32)
    #define CASPI_PLATFORM_WINDOWS
    #define CASPI_PLATFORM_STRING "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define CASPI_PLATFORM_IOS
        #define CASPI_PLATFORM_STRING "iOS"
    #elif TARGET_OS_MAC
        #define CASPI_PLATFORM_MACOS
        #define CASPI_PLATFORM_STRING "macOS"
    #else
        #define CASPI_PLATFORM_APPLE
        #define CASPI_PLATFORM_STRING "Unknown Apple Platform"
    #endif
#elif defined(__ANDROID__)
    #define CASPI_PLATFORM_ANDROID
    #define CASPI_PLATFORM_STRING "Android"
#elif defined(__linux__) && !defined(__ANDROID__)
    #define CASPI_PLATFORM_LINUX
    #define CASPI_PLATFORM_STRING "Linux"
#elif defined(__EMSCRIPTEN__)
    #define CASPI_PLATFORM_WEBASSEMBLY
    #define CASPI_PLATFORM_STRING "WebAssembly"
#elif defined(__FreeBSD__)
    #define CASPI_PLATFORM_FREEBSD
    #define CASPI_PLATFORM_STRING "FreeBSD"
#elif defined(__unix__) || defined(__unix)
    #define CASPI_PLATFORM_UNIX
    #define CASPI_PLATFORM_STRING "Unix"
#else
    #define CASPI_PLATFORM_UNKNOWN
    #define CASPI_PLATFORM_STRING "Unknown!"
#endif

#if defined(__clang__)
    #define CASPI_COMPILER_CLANG
    #define CASPI_COMPILER_STRING "Clang"
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define CASPI_COMPILER_MINGW
    #define CASPI_COMPILER_STRING "MinGW"
#elif defined(__GNUC__) && !defined(__clang__)
    #define CASPI_COMPILER_GCC
    #define CASPI_COMPILER_STRING "GCC"
#elif defined(_MSC_VER)
    #define CASPI_COMPILER_MSVC
    #define CASPI_COMPILER_STRING "MSVC"
#elif defined(__INTEL_COMPILER) || defined(__ICC)
    #define CASPI_COMPILER_INTEL
    #define CASPI_COMPILER_STRING "Intel Compiler"
#else
    #define CASPI_COMPILER_UNKNOWN
    #define CASPI_COMPILER_STRING "Unknown Compiler"
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define CASPI_ARCH_X86_64
    #define CASPI_ARCH_STRING "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
    #define CASPI_ARCH_X86_32
    #define CASPI_ARCH_STRING "x86_32"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define CASPI_ARCH_ARM64
    #define CASPI_ARCH_STRING "ARM64"
#elif defined(__arm__) || defined(_M_ARM)
    #define CASPI_ARCH_ARM32
    #define CASPI_ARCH_STRING "ARM32"
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
    #define CASPI_ARCH_WASM
    #define CASPI_ARCH_STRING "WebAssembly"
#elif defined(__powerpc__) || defined(__ppc__)
    #define CASPI_ARCH_PPC
    #define CASPI_ARCH_STRING "PowerPC"
#else
    #define CASPI_ARCH_UNKNOWN
    #define CASPI_ARCH_STRING "Unknown Architecture"
#endif

// Detect C++ standard version
#if defined(__cplusplus)
    #if __cplusplus >= 201103L
        #define CASPI_CPP_11
    #endif
    #if __cplusplus >= 201402L
        #define CASPI_CPP_14
    #endif
    #if __cplusplus >= 201703L
        #define CASPI_CPP_17
    #endif
    #if __cplusplus >= 202002L
        #define CASPI_CPP_20
    #endif
    #if __cplusplus >= 202302L
        #define CASPI_CPP_23
    #endif
    #if __cplusplus >= 202402L
        #define CASPI_CPP_26
    #endif

    // Set CASPI_CPP_VERSION and CASPI_CPP_STRING to the highest detected version
    #if __cplusplus >= 202402L
        #define CASPI_CPP_VERSION 202402L
        #define CASPI_CPP_STRING "C++26"
    #elif __cplusplus >= 202302L
        #define CASPI_CPP_VERSION 202302L
        #define CASPI_CPP_STRING "C++23"
    #elif __cplusplus >= 202002L
        #define CASPI_CPP_VERSION 202002L
        #define CASPI_CPP_STRING "C++20"
    #elif __cplusplus >= 201703L
        #define CASPI_CPP_VERSION 201703L
        #define CASPI_CPP_STRING "C++17"
    #elif __cplusplus >= 201402L
        #define CASPI_CPP_VERSION 201402L
        #define CASPI_CPP_STRING "C++14"
    #elif __cplusplus >= 201103L
        #define CASPI_CPP_VERSION 201103L
        #define CASPI_CPP_STRING "C++11"
    #else
        #define CASPI_CPP_VERSION 0L
        #define CASPI_CPP_STRING "Pre-C++11 or unknown"
    #endif
#else
    #define CASPI_CPP_VERSION 0L
    #define CASPI_CPP_STRING "Not C++"
#endif

// SSE support (x86/x86_64 only)
#if defined(__SSE__)
    #define CASPI_HAS_SSE
    #include <xmmintrin.h>
#endif

#if defined(__SSE2__) || (defined(_M_X64) || defined(_M_IX86))
    #define CASPI_HAS_SSE2
#include <emmintrin.h>
#endif

#if defined(__SSE3__)
    #define CASPI_HAS_SSE3
    #include <pmmintrin.h>
#endif

#if defined(__SSSE3__)
    #define CASPI_HAS_SSSE3
#endif

#if defined(__SSE4_1__)
    #define CASPI_HAS_SSE4_1
#endif

#if defined(__SSE4_2__)
    #define CASPI_HAS_SSE4_2
#endif

#if defined(__AVX__)
    #include <immintrin.h>
    #define CASPI_HAS_AVX
#endif

#if defined(__AVX2__)
    #define CASPI_HAS_AVX2
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define CASPI_HAS_NEON
#endif

#if defined(__FMA__) || defined(__FMA3__)
    #define CASPI_HAS_FMA
#endif


#endif //CASPI_PLATFORM_H
