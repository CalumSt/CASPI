/**
 * @file caspi_Traits.h
 * @brief Type traits and tag types for CASPI.
 * @ingroup base
 *
 * Provides RealTimeSafe/NonRealTimeSafe tag types, type traits for
 * span concepts, SIMD width detection, and other compile-time utilities.
 */

#ifndef CASPI_TRAITS_H
#define CASPI_TRAITS_H

#include <type_traits> // For std::is_nothrow_swappable, integral_constant
#include <utility>     // For std::declval

namespace CASPI {
// clang-format off
    /** @brief Tag type indicating real-time safe operations. */
    struct RealTimeSafe {};

    /** @brief Tag type indicating operations that may not be real-time safe. */
    struct NonRealTimeSafe {};

    struct SampleRateAwareTag {};

    // === Trait detection ===

    template<typename T>
    struct is_real_time_safe : std::false_type {};

    template<>
    struct is_real_time_safe<RealTimeSafe> : std::true_type {};

    template<typename T>
    struct is_non_real_time_safe : std::false_type {};

    template<>
    struct is_non_real_time_safe<NonRealTimeSafe> : std::true_type {};

    template<typename T>
    struct is_sample_rate_aware : std::false_type {};

    template<>
    struct is_sample_rate_aware<SampleRateAwareTag> : std::true_type {};

    template <typename Layout>
    struct is_interleaved : std::false_type {};

    template <typename Layout>
    struct is_channel_major : std::false_type {};


#if defined(CASPI_FEATURES_HAS_NOTHROW_SWAPPABLE)

    template <typename T>
    using is_nothrow_swappable = std::is_nothrow_swappable<T>;

    template <typename T>
    constexpr bool is_nothrow_swappable_v = std::is_nothrow_swappable_v<T>;



#else

    // Fallback implementation for pre-C++17
    template<typename T>
    struct is_nothrow_swappable {
    private:
        template<typename U>
        static auto test(int) -> decltype(
            (void) swap(std::declval<U &>(), std::declval<U &>()),
            std::integral_constant<bool, noexcept(swap(std::declval<U &>(), std::declval<U &>()))>{}
        );

        template<typename>
        static std::false_type test(...);

    public:
        static constexpr bool value = decltype(test<T>(0))::value;
    };
#endif // CASPI_CPP_17

#if defined(CASPI_FEATURES_HAS_NOTHROW_SWAPPABLE)
#define CASPI_IS_NOTHROW_SWAPPABLE(T) CASPI::is_nothrow_swappable_v<T>
#else
#define CASPI_IS_NOTHROW_SWAPPABLE(T) CASPI::is_nothrow_swappable<T>::value
#endif


    // === Variable templates for other traits (C++17) ===
#if defined(CASPI_FEATURES_HAS_TRAIT_VARIABLE_TEMPLATES)

    template <typename T>
    constexpr bool is_real_time_safe_v = is_real_time_safe<T>::value;

    template <typename T>
    constexpr bool is_non_real_time_safe_v = is_non_real_time_safe<T>::value;

#endif // CASPI_CPP_17
//clang-format on
} // namespace CASPI

#endif // CASPI_TRAITS_H
