#ifndef CASPI_TRAITS_H
#define CASPI_TRAITS_H

#include <type_traits> // For std::is_nothrow_swappable, integral_constant
#include <utility>     // For std::declval

namespace CASPI {
    // === Tag types ===
    struct RealTimeSafeTag {
    };

    struct NonRealTimeSafeTag {
    };

    struct SampleRateAwareTag {
    };

    // === Trait detection ===

    template<typename T>
    struct is_real_time_safe : std::false_type {
    };

    template<>
    struct is_real_time_safe<RealTimeSafeTag> : std::true_type {
    };

    template<typename T>
    struct is_non_real_time_safe : std::false_type {
    };

    template<>
    struct is_non_real_time_safe<NonRealTimeSafeTag> : std::true_type {
    };

    template<typename T>
    struct is_sample_rate_aware : std::false_type {
    };

    template<>
    struct is_sample_rate_aware<SampleRateAwareTag> : std::true_type {
    };


    // === is_nothrow_swappable ===

#if defined(CASPI_CPP_17)

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

#if defined(CASPI_CPP_14)
    template <typename T>
    constexpr bool is_nothrow_swappable_v = is_nothrow_swappable<T>::value;
#endif

#endif // CASPI_CPP_17

#if defined(CASPI_CPP_17)
#define CASPI_IS_NOTHROW_SWAPPABLE(T) CASPI::is_nothrow_swappable_v<T>
#else
#define CASPI_IS_NOTHROW_SWAPPABLE(T) CASPI::is_nothrow_swappable<T>::value
#endif


    // === Variable templates for other traits (C++17) ===
#if defined(CASPI_CPP_17)

    template <typename T>
    constexpr bool is_real_time_safe_v = is_real_time_safe<T>::value;

    template <typename T>
    constexpr bool is_non_real_time_safe_v = is_non_real_time_safe<T>::value;

    template <typename T>
    constexpr bool is_sample_rate_aware_v = is_sample_rate_aware<T>::value;

#endif // CASPI_CPP_17
} // namespace CASPI

#endif // CASPI_TRAITS_H
