#ifndef CASPI_REALTIMECONTEXT_H
#define CASPI_REALTIMECONTEXT_H

// Detection: __has_feature is clang-specific.
// __SANITIZE_REALTIME is defined by clang when -fsanitize=realtime is passed,
// analogous to __SANITIZE_ADDRESS for ASan.
#if defined(__SANITIZE_REALTIME)
  #define CASPI_RTSAN_ENABLED 1
#endif

#if defined(CASPI_RTSAN_ENABLED)
  // Prefer the system header if available (LLVM 20+ compiler-rt).
  // Fall back to forward declarations for environments where the header
  // exists in the runtime but not on the default include path.
  #if defined(__has_include) && __has_include(<sanitizer/rtsan_interface.h>)
    #include <sanitizer/rtsan_interface.h>
  #else
    // Forward declarations — these symbols are always present in the
    // RTSan runtime when -fsanitize=realtime is active.
    // Source: github.com/realtime-sanitizer/rtsan (rtsan_standalone.h)
    extern "C"
    {
        void __rtsan_realtime_enter(void);
        void __rtsan_realtime_exit(void);
        void __rtsan_disable(void);
        void __rtsan_enable(void);
    }
  #endif
#endif

namespace CASPI
{
    // Marks the enclosed scope as a real-time context.
    // RTSan will intercept and report any non-RT-safe calls within scope.
    // Compiles to zero overhead when RTSan is not active.
    struct ScopedRealtimeThread
    {
#if defined(CASPI_RTSAN_ENABLED)
        ScopedRealtimeThread()  noexcept { __rtsan_realtime_enter(); }
        ~ScopedRealtimeThread() noexcept { __rtsan_realtime_exit();  }
#else
        ScopedRealtimeThread()  noexcept = default;
        ~ScopedRealtimeThread() noexcept = default;
#endif
        ScopedRealtimeThread(const ScopedRealtimeThread&)            = delete;
        ScopedRealtimeThread& operator=(const ScopedRealtimeThread&) = delete;
    };

    // Temporarily disables RTSan within an active real-time context.
    // Use for intentional non-RT calls inside RT-annotated scopes
    // (e.g. assert handlers, emergency logging).
    struct ScopedRealtimeDisable
    {
#if defined(CASPI_RTSAN_ENABLED)
        ScopedRealtimeDisable()  noexcept { __rtsan_disable(); }
        ~ScopedRealtimeDisable() noexcept { __rtsan_enable();  }
#else
        ScopedRealtimeDisable()  noexcept = default;
        ~ScopedRealtimeDisable() noexcept = default;
#endif
        ScopedRealtimeDisable(const ScopedRealtimeDisable&)            = delete;
        ScopedRealtimeDisable& operator=(const ScopedRealtimeDisable&) = delete;
    };

} // namespace CASPI

#endif // CASPI_REALTIMECONTEXT_H