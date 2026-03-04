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

* @file caspi_Strategy.h
* @author CS Islay
* @brief Compile-time SIMD strategy and trait definitions.
*
* Provides compile-time traits and utilities for SIMD operations:
*   - Type mappings (scalar to SIMD vector)
*   - Minimum SIMD widths per type
*   - Alignment checking and calculations
*   - Non-temporal store thresholds based on cache size
*
* These traits are used internally by block operations and help adapt
* SIMD code to different platform capabilities.
*
* USAGE
*
* @code
* // Get the SIMD type for a given scalar type
* using SimdVec = CASPI::SIMD::Strategy::simd_type<float, 4>::type;
*
* // Check alignment
* bool aligned = CASPI::SIMD::Strategy::is_aligned<16>(data_ptr);
*
* // Get SIMD alignment requirement
* constexpr size_t align = CASPI::SIMD::Strategy::simd_alignment<float>();
*
* // Get minimum SIMD width
* constexpr size_t width = CASPI::SIMD::Strategy::min_simd_width<float>::value;
* @endcode
*
*
* CACHE CONFIGURATION
*
* The L1 cache size is used to determine when non-temporal stores are beneficial.
* Override at compile time:
*   -DCASPI_L1_CACHE_BYTES=32768 (default: 32KB)
*
 ************************************************************************/

#ifndef CASPI_STRATEGY_H
#define CASPI_STRATEGY_H

#include "base/caspi_Assert.h"
#include "caspi_Intrinsics.h"

#include <cstddef>
#include <cstdint>

#if defined(CASPI_PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
  #include <windows.h>
#endif

#if defined(CASPI_PLATFORM_APPLE)
  #include <sys/sysctl.h>
#endif

#if defined(CASPI_PLATFORM_LINUX)
  #include <cstdio>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define CASPI_PLATFORM_X86 1
  #if defined(_MSC_VER)
    #include <intrin.h>
  #else
    #include <cpuid.h>
  #endif
#endif

#ifndef CASPI_L1_CACHE_BYTES
  #define CASPI_L1_CACHE_BYTES 32768
#endif

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Compile-time strategy namespace for SIMD traits.
         *
         * Contains type traits and utility functions for determining optimal
         * SIMD processing parameters at compile time.
         */
        namespace Strategy
        {
            /**
             * @brief Maps a scalar type and lane count to the corresponding SIMD vector type.
             *
             * Specializations exist for:
             *   - float with 4 lanes  → float32x4
             *   - double with 2 lanes → float64x2
             *   - float with 8 lanes  → float32x8 (AVX only)
             *   - double with 4 lanes → float64x4 (AVX only)
             *
             * @tparam T         Scalar type (float or double)
             * @tparam Width     Number of SIMD lanes
             */
            template <typename T, std::size_t Width>
            struct simd_type;

            /**
             * @brief SIMD type for 4-lane float vectors.
             */
            template <>
            struct simd_type<float, 4>
            {
                    using type = float32x4;
            };
            /**
             * @brief SIMD type for 2-lane double vectors.
             */
            template <>
            struct simd_type<double, 2>
            {
                    using type = float64x2;
            };

#if defined(CASPI_HAS_AVX)
            /**
             * @brief SIMD type for 8-lane float vectors (AVX).
             */
            template <>
            struct simd_type<float, 8>
            {
                    using type = float32x8;
            };
            /**
             * @brief SIMD type for 4-lane double vectors (AVX).
             */
            template <>
            struct simd_type<double, 4>
            {
                    using type = float64x4;
            };
#endif

            /**
             * @brief Minimum SIMD width for a given scalar type.
             *
             * Returns the smallest practical SIMD width available for the type.
             * Used as default when automatic detection is needed.
             *
             * @tparam T     Scalar type (float or double)
             *
             * Specializations:
             *   - float  → 4 lanes (SSE/NEON/WASM) or 1 (scalar fallback)
             *   - double → 2 lanes (SSE2/NEON64/WASM) or 1 (scalar fallback)
             */
            template <typename T>
            struct min_simd_width
            {
                    static constexpr std::size_t value = 1;
            };

            /**
             * @brief Minimum SIMD width for float (4 with SIMD, 1 scalar).
             */
            template <>
            struct min_simd_width<float>
            {
#if defined(CASPI_HAS_SSE) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_WASM_SIMD)
                    static constexpr std::size_t value = 4;
#else
                    static constexpr std::size_t value = 1;
#endif
            };

            /**
             * @brief Minimum SIMD width for double (2 with SIMD, 1 scalar).
             */
            template <>
            struct min_simd_width<double>
            {
#if defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_NEON64) || defined(CASPI_HAS_WASM_SIMD)
                    static constexpr std::size_t value = 2;
#else
                    static constexpr std::size_t value = 1;
#endif
            };

            /**
             * @brief Check if a pointer is aligned to a specified number of bytes.
             *
             * @tparam N         Alignment boundary (must be power of 2)
             * @param ptr        Pointer to check
             * @return           true if pointer is aligned to N bytes
             *
             * @code
             * float* data = ...;
             * if (Strategy::is_aligned<16>(data)) {
             *     // Use aligned loads
             * }
             * @endcode
             */
            template <std::size_t N>
            inline bool is_aligned (const void* ptr) noexcept
            {
                return (reinterpret_cast<std::uintptr_t> (ptr) % N) == 0;
            }

            /**
             * @brief Calculate the number of elements until the next alignment boundary.
             *
             * Used internally for prologue calculations in block operations.
             *
             * @tparam Alignment     Desired alignment in bytes
             * @tparam T             Element type
             * @param ptr            Pointer to check
             * @return               Number of elements to advance to reach alignment
             *
             * @code
             * float* data = unaligned_ptr;
             * size_t prologue = Strategy::samples_to_alignment<16>(data);
             * // Process 'prologue' elements scalar, then use SIMD
             * @endcode
             */
            template <std::size_t Alignment, typename T>
            inline std::size_t samples_to_alignment (const T* ptr) noexcept
            {
                const auto addr                   = reinterpret_cast<std::uintptr_t> (ptr);
                const std::uintptr_t misalignment = addr % Alignment;

                if (misalignment == 0)
                {
                    return 0;
                }

                const std::uintptr_t bytes_to_align = Alignment - misalignment;

                return (bytes_to_align + sizeof (T) - 1) / sizeof (T);
            }

            /**
             * @brief Get preferred SIMD alignment for a given scalar type.
             *
             * @tparam T         Scalar type
             * @return           Alignment in bytes:
             *                     - 32 for AVX
             *                     - 16 for SSE/NEON/WASM
             *                     - alignof(T) for scalar fallback
             *
             * @code
             * constexpr size_t float_align = Strategy::simd_alignment<float>();   // 16 or 32
             * constexpr size_t double_align = Strategy::simd_alignment<double>(); // 16 or 32
             * @endcode
             */
            template <typename T>
            constexpr std::size_t simd_alignment()
            {
#if defined(CASPI_HAS_AVX)
                return 32;
#elif defined(CASPI_HAS_SSE) || defined(CASPI_HAS_SSE2) || defined(CASPI_HAS_NEON) || defined(CASPI_HAS_NEON64) || defined(CASPI_HAS_WASM_SIMD)
                return 16;
#else
                return alignof (T);
#endif
            }

            /**
             * @brief L1 data cache size in bytes.
             *
             * Used for non-temporal store threshold calculations.
             * Defaults to 32KB (smallest L1 on modern x86/ARM).
             *
             * Override at compile time:
             *   -DCASPI_L1_CACHE_BYTES=32768
             *
             * @note The NT threshold is set to 2x L1 so that a working set that
             *       no longer fits in L1 does not pollute it with streaming writes.
             */
#if ! defined(CASPI_L1_CACHE_BYTES)
#define CASPI_L1_CACHE_BYTES 32768
#endif

            static constexpr std::size_t L1_CACHE_BYTES = CASPI_L1_CACHE_BYTES;

            /**
             * @brief Number of elements above which non-temporal stores are preferred.
             *
             * At this point the working set has overflowed L1 and temporal stores
             * would cause unnecessary cache pollution.
             *
             * @tparam T         Element type
             * @return           Element count threshold
             *
             * @code
             * size_t threshold = Strategy::nt_store_threshold<float>();  // ~16384 for 32KB L1
             * if (count > threshold) {
             *     // Use stream_store for better performance
             * }
             * @endcode
             */
            template <typename T>
            constexpr std::size_t nt_store_threshold() noexcept
            {
                return (2 * L1_CACHE_BYTES) / sizeof (T);
            }

            namespace detail
{

// ---------------------------------------------------------------------------
// x86 CPUID helpers
// ---------------------------------------------------------------------------

#if defined(CASPI_PLATFORM_X86)

/* @brief Execute CPUID for the given leaf/subleaf and return registers.
 *
 * @param leaf      CPUID leaf
 * @param subleaf   CPUID subleaf (usually 0)
 * @param eax,ebx,ecx,edx Returned registers (output)
 */
inline void cpuid (uint32_t leaf, uint32_t subleaf,
                   uint32_t& eax, uint32_t& ebx,
                   uint32_t& ecx, uint32_t& edx) noexcept
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex (regs, static_cast<int> (leaf), static_cast<int> (subleaf));
    eax = static_cast<uint32_t> (regs[0]);
    ebx = static_cast<uint32_t> (regs[1]);
    ecx = static_cast<uint32_t> (regs[2]);
    edx = static_cast<uint32_t> (regs[3]);
#else
    __cpuid_count (leaf, subleaf, eax, ebx, ecx, edx);
#endif
}

/* @brief Query L1 data cache size via CPUID (Intel/AMD) on x86.
 *
 * @return L1 data cache size in bytes, or 0 if CPUID does not expose the info.
 */
inline std::size_t query_x86_l1d() noexcept
{
    uint32_t eax, ebx, ecx, edx;

    // Check max leaf
    cpuid (0, 0, eax, ebx, ecx, edx);
    const uint32_t max_leaf = eax;

    if (max_leaf >= 4)
    {
        // Leaf 4: iterate sub-leaves until EAX[4:0] == 0
        for (uint32_t sub = 0; ; ++sub)
        {
            cpuid (4, sub, eax, ebx, ecx, edx);
            const uint32_t cache_type = eax & 0x1F;
            if (cache_type == 0) break;         // no more caches
            const uint32_t cache_level = (eax >> 5) & 0x7;
            // cache_type 1 = data, 3 = unified; level 1 = L1
            if (cache_level == 1 && (cache_type == 1 || cache_type == 3))
            {
                const uint32_t line_size   = (ebx & 0xFFF) + 1;
                const uint32_t partitions  = ((ebx >> 12) & 0x3FF) + 1;
                const uint32_t assoc       = ((ebx >> 22) & 0x3FF) + 1;
                const uint32_t sets        = ecx + 1;
                return static_cast<std::size_t> (line_size * partitions * assoc * sets);
            }
        }
    }

    // AMD extended leaf fallback
    cpuid (0x80000000u, 0, eax, ebx, ecx, edx);
    if (eax >= 0x80000005u)
    {
        cpuid (0x80000005u, 0, eax, ebx, ecx, edx);
        // ECX[23:16] = L1 data cache size in KB
        const uint32_t kb = (ecx >> 16) & 0xFF;
        if (kb > 0)
            return static_cast<std::size_t> (kb) * 1024u;
    }

    return 0;
}

#endif // CASPI_PLATFORM_X86

// ---------------------------------------------------------------------------
// Apple sysctl helper
// ---------------------------------------------------------------------------

#if defined(CASPI_PLATFORM_APPLE)

/* @brief Query L1 data cache size via sysctl on Apple platforms.
 *
 * @return L1 data cache size in bytes, or 0 on failure.
 */
inline std::size_t query_apple_l1d() noexcept
{
    std::size_t size  = 0;
    std::size_t len   = sizeof (size);
    if (::sysctlbyname ("hw.l1dcachesize", &size, &len, nullptr, 0) == 0)
        return size;
    return 0;
}

#endif

// ---------------------------------------------------------------------------
// Linux sysfs helper
// ---------------------------------------------------------------------------

#if defined(CASPI_PLATFORM_LINUX)

/* @brief Query L1 data cache size via Linux sysfs (/sys/devices/.../size).
 *
 * @return L1 data cache size in bytes, or 0 on failure.
 */
inline std::size_t query_linux_l1d() noexcept
{
    // index0 = L1 data cache on all Linux platforms (ARM, x86, RISC-V, etc.)
    // The file contains a string like "32K" or "192K".
    FILE* f = std::fopen (
        "/sys/devices/system/cpu/cpu0/cache/index0/size", "r");
    if (!f) return 0;

    char buf[32] = {};
    if (std::fgets (buf, sizeof (buf), f) == nullptr)
    {
        std::fclose (f);
        return 0;
    }
    std::fclose (f);

    unsigned long val  = 0;
    char          unit = 0;
    if (std::sscanf (buf, "%lu%c", &val, &unit) >= 1)
    {
        if (unit == 'K' || unit == 'k') return static_cast<std::size_t> (val) * 1024u;
        if (unit == 'M' || unit == 'm') return static_cast<std::size_t> (val) * 1024u * 1024u;
        return static_cast<std::size_t> (val);
    }
    return 0;
}

#endif

// ---------------------------------------------------------------------------
// Windows GLPI helper
// ---------------------------------------------------------------------------

#if defined(CASPI_PLATFORM_WINDOWS)

/* @brief Query L1 data cache size via Windows GetLogicalProcessorInformation.
 *
 * @return L1 data cache size in bytes, or 0 on failure.
 */
inline std::size_t query_windows_l1d() noexcept
{
    DWORD len = 0;
    GetLogicalProcessorInformation (nullptr, &len);
    if (len == 0) return 0;

    // len is now the required buffer size
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buf =
        static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*> (
            ::HeapAlloc (::GetProcessHeap(), 0, len));
    if (!buf) return 0;

    std::size_t result = 0;
    if (GetLogicalProcessorInformation (buf, &len))
    {
        const std::size_t count = len / sizeof (*buf);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (buf[i].Relationship == RelationCache)
            {
                const CACHE_DESCRIPTOR& c = buf[i].Cache;
                if (c.Level == 1 &&
                    (c.Type == CacheData || c.Type == CacheUnified))
                {
                    result = static_cast<std::size_t> (c.Size);
                    break;
                }
            }
        }
    }
    ::HeapFree (::GetProcessHeap(), 0, buf);
    return result;
}

#endif

// ---------------------------------------------------------------------------
// Master probe — tries each platform in priority order.
// ---------------------------------------------------------------------------

/* @brief Probe the system to obtain the runtime L1 data cache size.
 *
 * Tries platform-specific queries in a sensible order and falls back to the
 * compile-time default if probing fails.
 *
 * @return L1 data cache size in bytes (guaranteed > 0).
 */
inline std::size_t probe_l1d() noexcept
{
    std::size_t result = 0;

#if defined(CASPI_PLATFORM_APPLE)
    result = query_apple_l1d();
#endif

    // On Linux/Apple-Silicon the sysfs/sysctl path is preferred over CPUID.
    // On Linux x86 the sysfs path usually works too, but CPUID is available
    // as a fallback if sysfs is absent (e.g. inside a container).
#if defined(CASPI_PLATFORM_LINUX)
    if (result == 0) result = query_linux_l1d();
#endif

#if defined(CASPI_PLATFORM_X86)
    if (result == 0) result = query_x86_l1d();
#endif

#if defined(CASPI_PLATFORM_WINDOWS)
    if (result == 0) result = query_windows_l1d();
#endif

    return (result > 0) ? result : static_cast<std::size_t> (CASPI_L1_CACHE_BYTES);
}

inline const std::size_t _caspi_kL1DBytes = probe_l1d();

} // namespace detail

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Returns the L1 data cache size in bytes at runtime.
 *
 * The first call probes the hardware (cheap: ~microsecond).
 * All subsequent calls return the cached value at zero cost.
 * Thread-safe under C++11 (static local init is atomic).
 *
 * @return L1 data cache size in bytes, or CASPI_L1_CACHE_BYTES if
 *         the hardware query fails.
 */
inline std::size_t l1_data_bytes() noexcept
{
    return detail::_caspi_kL1DBytes;
}

/**
 * @brief Compile-time L1 cache size constant.
 *
 * Used in template parameters and constexpr contexts.
 * Override at build time with -DCASPI_L1_CACHE_BYTES=<bytes>.
 *
 * @return Compile-time constant; does NOT query hardware.
 */
constexpr std::size_t l1_data_bytes_compile_time() noexcept
{
    return static_cast<std::size_t> (CASPI_L1_CACHE_BYTES);
}

/**
 * @brief Non-temporal store threshold in elements.
 *
 * NT stores are beneficial when the working set exceeds L1.
 * Uses the runtime L1 size to adapt to the actual hardware.
 *
 * @tparam T  Element type.
 * @return    Element count above which NT stores are preferred.
 */
template <typename T>
inline std::size_t nt_store_threshold_runtime() noexcept
{
    return (2 * l1_data_bytes()) / sizeof (T);
}


        } // namespace Strategy
    } // namespace SIMD
} // namespace CASPI
#endif // CASPI_STRATEGY_H
