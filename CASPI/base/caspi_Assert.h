#ifndef CASPI_ASSERT_H
#define CASPI_ASSERT_H

/**
 * @file caspi_Assert.h
 * @brief Assertion, contract, and diagnostic utilities for CASPI
 *
 * OVERVIEW
 * ========
 * This header provides a lightweight, header-only assertion and contract
 * system designed for both real-time (RT) and non-real-time code paths.
 *
 * The system makes *semantic intent explicit* by separating:
 *  - internal invariants
 *  - caller preconditions
 *  - implementation postconditions
 *  - real-time safe assertions
 *
 * Unlike traditional assertions, all checks are routed through
 * user-overridable handlers, allowing applications to control logging,
 * abort behavior, telemetry, or silent handling without modifying CASPI
 * source code.
 *
 * SEMANTIC CATEGORIES
 * ===================
 * The following macros serve distinct roles and should not be used
 * interchangeably:
 *
 *  - CASPI_ASSERT(expr, msg)
 *      Internal invariants and programmer errors.
 *      Enabled only when CASPI_DEBUG is defined.
 *      Violations are considered fatal in debug builds.
 *
 *  - CASPI_EXPECT(expr, msg)
 *      Caller preconditions and contract violations.
 *      Intended for detecting incorrect API usage.
 *
 *  - CASPI_ENSURE(expr, msg)
 *      Postconditions and implementation guarantees.
 *      Indicates serious internal failures but may be recoverable
 *      in release builds depending on configuration.
 *
 *  - CASPI_RT_ASSERT(expr)
 *      Real-time safe assertion for audio/DSP threads.
 *      Guaranteed to perform no allocation, locking, or I/O.
 *
 *  - CASPI_STATIC_ASSERT(expr, msg)
 *      Compile-time assertion (always active).
 *
 * BUILD CONFIGURATION
 * ===================
 * Behavior is controlled entirely via preprocessor macros:
 *
 *  - CASPI_DEBUG
 *      Enables CASPI_ASSERT and verbose diagnostic output.
 *
 *  - CASPI_RT_SAFE_EXPECTS
 *      Forces EXPECT and ENSURE handlers to be real-time safe
 *      (no I/O, no backtrace). Failures are recorded via counters.
 *
 * Recommended configurations:
 *
 *  - Debug builds:
 *      -DCASPI_DEBUG
 *
 *  - Audio / DSP real-time builds:
 *      -DCASPI_RT_SAFE_EXPECTS
 *
 * HANDLER REDIRECTION
 * ===================
 * All assertion behavior is routed through user-overridable handlers.
 * Custom handlers must be defined *before* including this header.
 *
 * Available hooks:
 *  - CASPI_ASSERT_HANDLER
 *  - CASPI_EXPECT_HANDLER
 *  - CASPI_ENSURE_HANDLER
 *  - CASPI_RT_ASSERT_HANDLER
 *
 * Example:
 * @code
 * void myAssertHandler(const CASPI::AssertInfo& info) noexcept
 * {
 *     logError(info.file, info.line, info.expression, info.message);
 * }
 *
 * #define CASPI_ASSERT_HANDLER myAssertHandler
 * #include <caspi_Assert.h>
 * @endcode
 *
 * ASSERTION METADATA
 * ==================
 * Handlers receive a CASPI::AssertInfo struct containing:
 *  - expression string
 *  - optional message
 *  - source file
 *  - line number
 *  - function name
 *
 * This allows integration with logging systems, crash reporters,
 * telemetry pipelines, or custom debuggers.
 *
 * BACKTRACE SUPPORT
 * =================
 * Non-real-time handlers capture stack backtraces via
 * caspi_Backtrace.h when CASPI_DEBUG is enabled.
 *
 * Backtrace capture is:
 *  - enabled automatically in debug builds
 *  - never performed in RT-safe handlers
 *
 * Real-time safety is preserved by design.
 *
 * REAL-TIME ASSERTION COUNTERS
 * ============================
 * RT-safe assertions and RT-safe EXPECT/ENSURE failures increment
 * lock-free atomic counters that can be inspected from a non-RT thread:
 *
 * @code
 * uint32_t rtFailures = CASPI::rtAssertCounter().get();
 * uint32_t expectFailures = CASPI::expectCounter().get();
 * uint32_t ensureFailures = CASPI::ensureCounter().get();
 * @endcode
 *
 * USAGE EXAMPLES
 * ==============
 *
 * Internal invariant (debug-only):
 * @code
 * CASPI_ASSERT(buffer != nullptr, "Buffer must not be null");
 * @endcode
 *
 * Caller precondition:
 * @code
 * CASPI_EXPECT(sampleRate > 0, "Invalid sample rate");
 * @endcode
 *
 * Postcondition:
 * @code
 * CASPI_ENSURE(std::isfinite(output), "DSP produced NaN or Inf");
 * @endcode
 *
 * Real-time safe assertion (audio thread):
 * @code
 * CASPI_RT_ASSERT(modIndex >= 0);
 * @endcode
 *
 * CONTRACT PHILOSOPHY
 * ==================
 * This system is inspired by:
 *  - Eiffel Design by Contract
 *  - C++ Core Guidelines (Expects / Ensures)
 *  - LLVM and Abseil assertion models
 *
 * The intent is to make correctness assumptions explicit, auditable,
 * and configurable while preserving deterministic behavior in
 * performance-critical code paths.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "caspi_Backtrace.h"

namespace CASPI
{
    //==========================================================================
    // Assertion metadata
    //==========================================================================

    struct AssertInfo
    {
        const char* expression;
        const char* message;
        const char* file;
        int line;
        const char* function;
    };

    //==========================================================================
    // Assertion handler interface
    //==========================================================================

    using AssertHandler = void (*)(const AssertInfo&) noexcept;

    //==========================================================================
    // Lock-free RT assertion counters
    //==========================================================================

    struct RtAssertCounter
    {
        std::atomic<uint32_t> count { 0 };

        void increment() noexcept
        {
            count.fetch_add(1, std::memory_order_relaxed);
        }

        uint32_t get() const noexcept
        {
            return count.load(std::memory_order_relaxed);
        }

        void reset() noexcept
        {
            count.store(0, std::memory_order_relaxed);
        }
    };

    inline RtAssertCounter& rtAssertCounter() noexcept
    {
        static RtAssertCounter counter;
        return counter;
    }

    // Separate counter for EXPECT failures (can be checked separately)
    inline RtAssertCounter& expectCounter() noexcept
    {
        static RtAssertCounter counter;
        return counter;
    }

    // Separate counter for ENSURE failures
    inline RtAssertCounter& ensureCounter() noexcept
    {
        static RtAssertCounter counter;
        return counter;
    }

    //==========================================================================
    // Default handlers
    //==========================================================================

    /**
     * @brief Handler for unrecoverable assertions (CASPI_ASSERT, CASPI_ENSURE)
     *
     * Aborts in debug builds, does nothing in release.
     * NOT RT-SAFE due to IO and backtrace.
     */
    inline void defaultAssertHandler(const AssertInfo& info) noexcept
    {
#if defined(CASPI_DEBUG)
        std::fprintf(stderr,
            "CASPI ASSERT FAILED (FATAL)\n"
            "  Expr: %s\n"
            "  Msg : %s\n"
            "  File: %s:%d\n"
            "  Func: %s\n",
            info.expression,
            info.message ? info.message : "",
            info.file,
            info.line,
            info.function);

        auto bt = CASPI::Unwind::capture(7);
        std::fprintf(stderr, "Backtrace (most recent call first):\n");
        for (auto& f : bt)
            std::fprintf(stderr, "  %s\n", f.c_str());

        std::abort();
#else
        (void)info;
#endif
    }

    /**
     * @brief Handler for recoverable expectations (CASPI_EXPECT)
     *
     * Behavior depends on CASPI_RT_SAFE_EXPECTS:
     *   - If defined: Only increments counter (RT-safe)
     *   - If not defined: Logs to stderr with backtrace (NOT RT-safe)
     */
    inline void defaultExpectHandler(const AssertInfo& info) noexcept
    {
#if defined(CASPI_RT_SAFE_EXPECTS)
        // RT-SAFE: Only increment counter
        (void)info;
        expectCounter().increment();
#elif defined(CASPI_DEBUG)
        // NOT RT-SAFE: Log to stderr
        std::fprintf(stderr,
            "CASPI EXPECT FAILED (recoverable)\n"
            "  Expr: %s\n"
            "  Msg : %s\n"
            "  File: %s:%d\n"
            "  Func: %s\n",
            info.expression,
            info.message ? info.message : "",
            info.file,
            info.line,
            info.function);

        auto bt = CASPI::Unwind::capture(7);
        std::fprintf(stderr, "Backtrace (most recent call first):\n");
        for (auto& f : bt)
            std::fprintf(stderr, "  %s\n", f.c_str());

        // Also increment counter so it can be checked
        expectCounter().increment();
#else
        (void)info;
#endif
    }

    /**
     * @brief Handler for postconditions (CASPI_ENSURE)
     *
     * More severe than EXPECT but still recoverable in release.
     */
    inline void defaultEnsureHandler(const AssertInfo& info) noexcept
    {
#if defined(CASPI_RT_SAFE_EXPECTS)
        // RT-SAFE: Only increment counter
        (void)info;
        ensureCounter().increment();
#elif defined(CASPI_DEBUG)
        // NOT RT-SAFE: Log and abort
        std::fprintf(stderr,
            "CASPI ENSURE FAILED (serious)\n"
            "  Expr: %s\n"
            "  Msg : %s\n"
            "  File: %s:%d\n"
            "  Func: %s\n",
            info.expression,
            info.message ? info.message : "",
            info.file,
            info.line,
            info.function);

        auto bt = CASPI::Unwind::capture(7);
        std::fprintf(stderr, "Backtrace (most recent call first):\n");
        for (auto& f : bt)
            std::fprintf(stderr, "  %s\n", f.c_str());

        std::abort();
#else
        (void)info;
        ensureCounter().increment();
#endif
    }

    inline void defaultRtAssertHandler(const AssertInfo&) noexcept
    {
        // RT-safe: no IO, no locks, no allocation
        rtAssertCounter().increment();
    }

} // namespace CASPI

//==============================================================================
// User-overridable hooks
//==============================================================================

#ifndef CASPI_ASSERT_HANDLER
    #define CASPI_ASSERT_HANDLER ::CASPI::defaultAssertHandler
#endif

#ifndef CASPI_EXPECT_HANDLER
    #define CASPI_EXPECT_HANDLER ::CASPI::defaultExpectHandler
#endif

#ifndef CASPI_ENSURE_HANDLER
    #define CASPI_ENSURE_HANDLER ::CASPI::defaultEnsureHandler
#endif

#ifndef CASPI_RT_ASSERT_HANDLER
    #define CASPI_RT_ASSERT_HANDLER ::CASPI::defaultRtAssertHandler
#endif

//==============================================================================
// Assertion macros
//==============================================================================

#if defined(CASPI_DEBUG)
    #define CASPI_ASSERT(expr, msg)                                        \
        do {                                                              \
            if (!(expr))                                                  \
            {                                                             \
                CASPI_ASSERT_HANDLER(::CASPI::AssertInfo{                \
                    #expr, msg, __FILE__, __LINE__, __func__              \
                });                                                       \
            }                                                             \
        } while (0)
#else
    #define CASPI_ASSERT(expr, msg) ((void)0)
#endif

    #define CASPI_EXPECT(expr, msg)                                        \
        do {                                                              \
            if (!(expr))                                                  \
            {                                                             \
                CASPI_EXPECT_HANDLER(::CASPI::AssertInfo{                \
                    #expr, msg, __FILE__, __LINE__, __func__              \
                });                                                       \
            }                                                             \
        } while (0)

    #define CASPI_ENSURE(expr, msg)                                        \
        do {                                                              \
            if (!(expr))                                                  \
            {                                                             \
                CASPI_ENSURE_HANDLER(::CASPI::AssertInfo{                \
                    #expr, msg, __FILE__, __LINE__, __func__              \
                });                                                       \
            }                                                             \
        } while (0)


#define CASPI_RT_ASSERT(expr)                                              \
    do {                                                                  \
        if (!(expr))                                                      \
        {                                                                 \
            CASPI_RT_ASSERT_HANDLER(::CASPI::AssertInfo{                 \
                #expr, nullptr, __FILE__, __LINE__, __func__              \
            });                                                           \
        }                                                                 \
    } while (0)

#define CASPI_STATIC_ASSERT(expr, msg) static_assert(expr, msg)
#endif // CASPI_ASSERT_H
