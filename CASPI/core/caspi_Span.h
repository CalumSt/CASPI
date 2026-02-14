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


* @file caspi_Span.h
* @author CS Islay
* @brief Span types with automatic SIMD acceleration for contiguous data
*
* Provides:
* - Span<T>: Contiguous, non-owning view (SIMD-eligible)
* - StridedSpan<T>: Strided view (scalar-only)
* - SpanView<T>: Unified view that can be either
* - SIMD-aware operations: fill, scale, copy, add
*
* SIMD is automatically used when:
* 1. Span is contiguous (not strided)
* 2. Element type is SIMD-supported (float or double)
* 3. Array is large enough to benefit from SIMD
*
************************************************************************/

#ifndef CASPI_SPAN_H
#define CASPI_SPAN_H

#include <cstddef>
#include <iterator>
#include <type_traits>

#include "base/caspi_SIMD.h"

namespace CASPI
{
    namespace Core
    {
#if defined(CASPI_FEATURES_HAS_SPAN)
#include <span>
        template <typename T>
        using Span = std::span<T>;

#else

        /**
         * @brief Minimal contiguous, non-owning span
         *
         * This is a lightweight view over contiguous memory. It does not own
         * the data and does not manage lifetime. Operations on contiguous
         * spans automatically use SIMD when beneficial.
         *
         * @tparam T Type of elements (can be const)
         */
        template <typename T>
        class Span
        {
            public:
                using element_type    = T;
                using value_type      = std::remove_cv_t<T>;
                using size_type       = std::size_t;
                using difference_type = std::ptrdiff_t;
                using pointer         = T*;
                using reference       = T&;
                using iterator        = T*;
                using const_iterator  = const T*;

                constexpr Span() noexcept : ptr_ (nullptr), sz_ (0) {}

                constexpr Span (T* ptr, size_type count) noexcept : ptr_ (ptr), sz_ (count) {}


                CASPI_NO_DISCARD constexpr std::size_t size() const noexcept CASPI_NON_BLOCKING { return sz_; }

                CASPI_NO_DISCARD constexpr bool empty() const noexcept CASPI_NON_BLOCKING { return sz_ == 0; }

                CASPI_NO_DISCARD constexpr pointer data() const noexcept CASPI_NON_BLOCKING { return ptr_; }

                CASPI_NO_DISCARD constexpr iterator begin() const noexcept CASPI_NON_BLOCKING { return ptr_; }

                CASPI_NO_DISCARD constexpr iterator end() const noexcept CASPI_NON_BLOCKING { return ptr_ + sz_; }

                CASPI_NO_DISCARD
                constexpr reference operator[] (size_type i) const noexcept
                {
                    return ptr_[i];
                }

            private:
                T* ptr_;
                size_type sz_;
        };

#endif

        /**
         * @brief Non-owning view over a strided array
         *
         * Allows iteration over elements with a fixed stride. This is used
         * for non-contiguous memory access patterns (e.g., accessing a channel
         * from an interleaved buffer, or a frame from a channel-major buffer).
         *
         * StridedSpan operations always use scalar code since the data is not
         * contiguous and cannot benefit from SIMD.
         *
         * @tparam T Element type (can be const)
         */
        template <typename T>
        class StridedSpan
        {
            public:
                /**
                 * @brief Iterator for StridedSpan with fixed stride
                 */
                class iterator
                {
                    public:
                        using value_type        = T;
                        using difference_type   = std::ptrdiff_t;
                        using reference         = T&;
                        using pointer           = T*;
                        using iterator_category = std::forward_iterator_tag;

                        constexpr iterator (pointer p, std::size_t stride) noexcept
                            : p_ (p), s_ (stride) {}

                        CASPI_NO_DISCARD
                        constexpr reference operator*() const noexcept { return *p_; }

                        constexpr iterator& operator++() noexcept
                        {
                            p_ += s_;
                            return *this;
                        }

                        constexpr iterator operator++ (int) noexcept
                        {
                            auto tmp = *this;
                            ++(*this);
                            return tmp;
                        }

                        CASPI_NO_DISCARD
                        constexpr bool operator== (const iterator& o) const noexcept
                        {
                            return p_ == o.p_;
                        }

            CASPI_NO_DISCARD
            bool operator!=(const iterator &o) const noexcept
            {
                return p_ != o.p_;
            }

                    private:
                        pointer p_;
                        std::size_t s_;
                };

                constexpr StridedSpan (T* base, std::size_t count, std::size_t stride) noexcept
                    : base_ (base), count_ (count), stride_ (stride) {}

        CASPI_NO_DISCARD
        constexpr iterator begin() const noexcept CASPI_NON_BLOCKING
        {
            return iterator(base_, stride_);
        }

         CASPI_NO_DISCARD
        constexpr iterator end() const noexcept CASPI_NON_BLOCKING
            {
            return iterator(base_ + count_ * stride_, stride_);
        }

        CASPI_NO_DISCARD constexpr std::size_t size() const noexcept CASPI_NON_BLOCKING { return count_; }

        CASPI_NO_DISCARD
        constexpr T &operator[](std::size_t i) const noexcept CASPI_NON_BLOCKING {
            return *(base_ + i * stride_);
        }
        CASPI_NO_DISCARD
        constexpr T *data() const noexcept CASPI_NON_BLOCKING { return base_; }

        CASPI_NO_DISCARD
        constexpr std::size_t stride() const noexcept CASPI_NON_BLOCKING { return stride_; }

    private:
        T *base_;
        std::size_t count_;
        std::size_t stride_;
    };

        /**
         * @brief A unified SpanView that can be contiguous or strided
         *
         * Provides random access and iteration over either contiguous or strided memory.
         * This is useful when you don't know at compile time whether the data will be
         * contiguous or strided (e.g., channels in different buffer layouts).
         *
         * @tparam T Element type
         * @note Operations dispatch to appropriate SIMD or scalar paths based on type
         */
        template <typename T>
        class SpanView
        {
            public:
                enum class Type
                {
                    Contiguous,
                    Strided
                };

                // Contiguous constructor
                constexpr SpanView (T* ptr, std::size_t count) noexcept
                    : type_ (Type::Contiguous), contig_ (ptr, count), stride_ (1) {}

                // Strided constructor
                constexpr SpanView (T* ptr, std::size_t count, std::size_t stride) noexcept
                    : type_ (Type::Strided), strided_ (ptr, count, stride), stride_ (stride) {}

                 CASPI_NO_DISCARD constexpr Type type() const noexcept CASPI_NON_BLOCKING
                { return type_; }

                CASPI_NO_DISCARD
                constexpr std::size_t size() const noexcept CASPI_NON_BLOCKING
                {
                    return type_ == Type::Contiguous ? contig_.size() : strided_.size();
                }

                CASPI_NO_DISCARD constexpr T& operator[] (std::size_t i) const noexcept
                {
                    return type_ == Type::Contiguous ? contig_[i] : strided_[i];
                }

                CASPI_NO_DISCARD
                constexpr auto begin() const noexcept CASPI_NON_BLOCKING
                {
                    return type_ == Type::Contiguous ? contig_.begin() : strided_.begin();
                }

                CASPI_NO_DISCARD
                constexpr auto end() const noexcept CASPI_NON_BLOCKING
                {
                    return type_ == Type::Contiguous ? contig_.end() : strided_.end();
                }

            private:
                Type type_;

                union
                {
                        Span<T> contig_;
                        StridedSpan<T> strided_;
                };

                std::size_t stride_;
        };

        /************************************************************************************************
          Span Type Detection
        ************************************************************************************************/

        /**
         * @brief Detect if a type is a contiguous span
         */
        template <typename T>
        struct is_contiguous_span : std::false_type
        {
        };

        template <typename T>
        struct is_contiguous_span<CASPI::Core::Span<T>> : std::true_type
        {
        };

        /**
         * @brief Detect if a type is a strided span
         */
        template <typename T>
        struct is_strided_span : std::false_type
        {
        };

        template <typename T>
        struct is_strided_span<CASPI::Core::StridedSpan<T>> : std::true_type
        {
        };

        /**
         * @brief Extract element type from span (removing const)
         */
        template <typename Span>
        struct span_element_type
        {
                using type = typename std::remove_const<
                    typename std::remove_reference<
                        decltype (*std::declval<Span>().data())>::type>::type;
        };

        /**
         * @brief Get SIMD width for a type
         */
        template <typename T>
        struct simd_width
        {
            static constexpr std::size_t value = SIMD::Strategy::min_simd_width<T>::value;
        };
        /**
         * @brief Check if span is SIMD-eligible at compile time
         *
         * A span is SIMD-eligible if:
         * 1. It's contiguous (not strided)
         * 2. Its element type supports SIMD (float or double)
         * 3. The element type is non-const (for write operations)
         */
        template <typename Span>
        struct is_simd_eligible
        {
            static constexpr bool value =
                is_contiguous_span<Span>::value &&
                (SIMD::Strategy::min_simd_width<typename span_element_type<Span>::type>::value > 1);
        };

        /**
         * @brief Helper to get non-const pointer from span
         */
        template <typename T>
        inline T* get_mutable_data (Span<T> span) noexcept
        {
            return span.data();
        }

        template <typename T>
        inline const T* get_const_data (Span<const T> span) noexcept
        {
            return span.data();
        }

        /************************************************************************************************
          SIMD-Aware Span Operations
        ************************************************************************************************

        These operations automatically use SIMD when beneficial:
        - Span is contiguous
        - Element type is float or double
        - Array size is large enough

        Otherwise, they fall back to scalar loops.
        ************************************************************************************************/

        /**
         * @brief Fill contiguous span with value, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param span Contiguous span to fill
         * @param value Value to fill with
         *
         * Automatically uses SIMD::ops::fill for eligible spans.
         */
        template <typename T>
        inline void fill (Span<T> span, T value) noexcept
        {
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && span.size() >= min_width)
            {
                SIMD::ops::fill (span.data(), span.size(), value);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < span.size(); ++i)
                {
                    span[i] = value;
                }
            }
        }

        /**
         * @brief Scale contiguous span by factor, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param span Contiguous span to scale
         * @param factor Scaling factor
         *
         * Computes: span[i] *= factor
         * Automatically uses SIMD::ops::scale for eligible spans.
         */
        template <typename T>
        inline void scale (Span<T> span, T factor) noexcept
        {
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && span.size() >= min_width)
            {
                SIMD::ops::scale (span.data(), span.size(), factor);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < span.size(); ++i)
                {
                    span[i] *= factor;
                }
            }
        }

        /**
         * @brief Copy from src to dst, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param dst Destination span
         * @param src Source span (can be const)
         *
         * Copies min(dst.size(), src.size()) elements.
         * Automatically uses SIMD::ops::copy for eligible spans.
         */
        template <typename T>
        inline void copy (Span<T> dst, Span<const T> src) noexcept
        {
            const std::size_t count         = (dst.size() < src.size()) ? dst.size() : src.size();
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && count >= min_width)
            {
                SIMD::ops::copy (dst.data(), src.data(), count);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[i] = src[i];
                }
            }
        }

        // Non-const source overload
        template <typename T>
        inline void copy (Span<T> dst, Span<T> src) noexcept
        {
            copy (dst, Span<const T> (src.data(), src.size()));
        }

        /**
         * @brief Add src to dst, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param dst Destination span (modified in-place)
         * @param src Source span (can be const)
         *
         * Computes: dst[i] += src[i]
         * Automatically uses SIMD::ops::add for eligible spans.
         */
        template <typename T>
        inline void add (Span<T> dst, Span<const T> src) noexcept
        {
            const std::size_t count         = (dst.size() < src.size()) ? dst.size() : src.size();
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && count >= min_width)
            {
                SIMD::ops::add (dst.data(), src.data(), count);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[i] += src[i];
                }
            }
        }

        // Non-const source overload
        template <typename T>
        inline void add (Span<T> dst, Span<T> src) noexcept
        {
            add (dst, Span<const T> (src.data(), src.size()));
        }

        /**
         * @brief Subtract src from dst, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param dst Destination span (modified in-place)
         * @param src Source span (can be const)
         *
         * Computes: dst[i] -= src[i]
         */
        template <typename T>
        inline void subtract (Span<T> dst, Span<const T> src) noexcept
        {
            const std::size_t count         = (dst.size() < src.size()) ? dst.size() : src.size();
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && count >= min_width)
            {
                SIMD::ops::sub (dst.data(), src.data(), count);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[i] -= src[i];
                }
            }
        }

        template <typename T>
        inline void subtract (Span<T> dst, Span<T> src) noexcept
        {
            subtract (dst, Span<const T> (src.data(), src.size()));
        }

        /**
         * @brief Element-wise multiply, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param dst Destination span (modified in-place)
         * @param src Source span (can be const)
         *
         * Computes: dst[i] *= src[i]
         */
        template <typename T>
        inline void multiply (Span<T> dst, Span<const T> src) noexcept
        {
            const std::size_t count         = (dst.size() < src.size()) ? dst.size() : src.size();
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && count >= min_width)
            {
                SIMD::ops::mul (dst.data(), src.data(), count);
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[i] *= src[i];
                }
            }
        }

        template <typename T>
        inline void multiply (Span<T> dst, Span<T> src) noexcept
        {
            multiply (dst, Span<const T> (src.data(), src.size()));
        }

        /**
         * @brief Clamp span to range, using SIMD when eligible
         * @tparam T Element type (float or double for SIMD)
         * @param span Span to clamp (modified in-place)
         * @param min_val Minimum value
         * @param max_val Maximum value
         *
         * Computes: span[i] = clamp(span[i], min_val, max_val)
         */
        template <typename T>
        inline void clamp (Span<T> span, T min_val, T max_val) noexcept
        {
            constexpr std::size_t min_width = SIMD::Strategy::min_simd_width<T>::value;

            if (is_simd_eligible<Span<T>>::value && span.size() >= min_width)
            {
                SIMD::ops::clamp (span.data(), min_val, max_val, span.size());
            }
            else
            {
                // Scalar fallback
                for (std::size_t i = 0; i < span.size(); ++i)
                {
                    if (span[i] < min_val)
                        span[i] = min_val;
                    if (span[i] > max_val)
                        span[i] = max_val;
                }
            }
        }

        /**
         * @brief Apply unary operation to span
         * @tparam T Element type
         * @tparam UnaryOp Operation type (callable)
         * @param span Span to process
         * @param op Unary operation
         *
         * @note Custom operations cannot be auto-vectorized, always uses scalar
         */
        template <typename T, typename UnaryOp>
        inline void apply (Span<T> span, UnaryOp op) noexcept
        {
            for (std::size_t i = 0; i < span.size(); ++i)
            {
                span[i] = op (span[i]);
            }
        }

        /**
         * @brief Apply binary operation between two spans
         * @tparam T Element type
         * @tparam BinaryOp Operation type (callable)
         * @param dst Destination span (modified in-place)
         * @param src Source span
         * @param op Binary operation
         *
         * @note Custom operations cannot be auto-vectorized, always uses scalar
         */
        template <typename T, typename BinaryOp>
        inline void apply2 (Span<T> dst, Span<const T> src, BinaryOp op) noexcept
        {
            const std::size_t count = (dst.size() < src.size()) ? dst.size() : src.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                dst[i] = op (dst[i], src[i]);
            }
        }

        template <typename T, typename BinaryOp>
        inline void apply2 (Span<T> dst, Span<T> src, BinaryOp op) noexcept
        {
            apply2 (dst, Span<const T> (src.data(), src.size()), op);
        }

        /************************************************************************************************
          Strided Span Operations (Always Scalar)
        ************************************************************************************************

        StridedSpan operations cannot use SIMD because the data is not contiguous.
        These always use iterator-based scalar loops.
        ************************************************************************************************/

        /**
         * @brief Fill strided span (always scalar - no SIMD)
         */
        template <typename T>
        void fill (StridedSpan<T> span, T value) noexcept
        {
            for (auto it = span.begin(); it != span.end(); ++it)
            {
                *it = value;
            }
        }

        /**
         * @brief Scale strided span (always scalar - no SIMD)
         */
        template <typename T>
        void scale (StridedSpan<T> span, T factor) noexcept
        {
            for (auto it = span.begin(); it != span.end(); ++it)
            {
                *it *= factor;
            }
        }

        /**
         * @brief Copy strided spans (always scalar - no SIMD)
         */
        template <typename T>
        void copy (StridedSpan<T> dst, StridedSpan<const T> src) noexcept
        {
            auto it_dst = dst.begin();
            auto it_src = src.begin();

            while (it_dst != dst.end() && it_src != src.end())
            {
                *it_dst = *it_src;
                ++it_dst;
                ++it_src;
            }
        }

        template <typename T>
        void copy (StridedSpan<T> dst, StridedSpan<T> src) noexcept
        {
            copy (dst, StridedSpan<const T> (src.data(), src.size(), src.stride()));
        }

        /**
         * @brief Add strided spans (always scalar - no SIMD)
         */
        template <typename T>
        void add (StridedSpan<T> dst, StridedSpan<const T> src) noexcept
        {
            auto it_dst = dst.begin();
            auto it_src = src.begin();

            while (it_dst != dst.end() && it_src != src.end())
            {
                *it_dst += *it_src;
                ++it_dst;
                ++it_src;
            }
        }

        template <typename T>
        void add (StridedSpan<T> dst, StridedSpan<T> src) noexcept
        {
            add (dst, StridedSpan<const T> (src.data(), src.size(), src.stride()));
        }

        /**
         * @brief Subtract strided spans (always scalar - no SIMD)
         */
        template <typename T>
        void subtract (StridedSpan<T> dst, StridedSpan<const T> src) noexcept
        {
            auto it_dst = dst.begin();
            auto it_src = src.begin();

            while (it_dst != dst.end() && it_src != src.end())
            {
                *it_dst -= *it_src;
                ++it_dst;
                ++it_src;
            }
        }

        template <typename T>
        void subtract (StridedSpan<T> dst, StridedSpan<T> src) noexcept
        {
            subtract (dst, StridedSpan<const T> (src.data(), src.size(), src.stride()));
        }

        /**
         * @brief Multiply strided spans (always scalar - no SIMD)
         */
        template <typename T>
        void multiply (StridedSpan<T> dst, StridedSpan<const T> src) noexcept
        {
            auto it_dst = dst.begin();
            auto it_src = src.begin();

            while (it_dst != dst.end() && it_src != src.end())
            {
                *it_dst *= *it_src;
                ++it_dst;
                ++it_src;
            }
        }

        template <typename T>
        void multiply (StridedSpan<T> dst, StridedSpan<T> src) noexcept
        {
            multiply (dst, StridedSpan<const T> (src.data(), src.size(), src.stride()));
        }

        /**
         * @brief Clamp strided span (always scalar - no SIMD)
         */
        template <typename T>
        void clamp (StridedSpan<T> span, T min_val, T max_val) noexcept
        {
            for (auto it = span.begin(); it != span.end(); ++it)
            {
                if (*it < min_val)
                    *it = min_val;
                if (*it > max_val)
                    *it = max_val;
            }
        }

    } // namespace Core
} // namespace CASPI

#endif // CASPI_SPAN_H