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
* @brief A collection of macros to define features based on platform.
*
************************************************************************/

#ifndef CASPI_SPAN_H
#define CASPI_SPAN_H
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace CASPI::Core {

#if defined(CASPI_FEATURES_HAS_SPAN)
#include <span>
template <typename T>
using Span = std::span<T>;

#else

/**
 * @brief Minimal contiguous, non-owning span
 *
 * @tparam T Type of elements
 */
template <typename T>
class Span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;
    using iterator = T*;
    using const_iterator = const T*;

    CASPI_NON_BLOCKING Span() noexcept : ptr_(nullptr), sz_(0) {}
    CASPI_NON_BLOCKING Span(T* ptr, size_type count) noexcept : ptr_(ptr), sz_(count) {}

    CASPI_NO_DISCARD std::size_t size() const noexcept CASPI_NON_BLOCKING { return sz_; }
    CASPI_NO_DISCARD bool empty() const noexcept CASPI_NON_BLOCKING { return sz_ == 0; }

    CASPI_NO_DISCARD pointer data() const noexcept CASPI_NON_BLOCKING { return ptr_; }
    CASPI_NO_DISCARD iterator begin() const noexcept CASPI_NON_BLOCKING { return ptr_; }
    CASPI_NO_DISCARD iterator end()   const noexcept CASPI_NON_BLOCKING { return ptr_ + sz_; }

    CASPI_NO_DISCARD reference operator[](size_type i) const noexcept CASPI_NON_BLOCKING { return ptr_[i]; }

private:
    T* ptr_;
    size_type sz_;
};

#endif

/**
 * @brief Non-owning view over a strided array
 *
 * Allows iteration over elements with a fixed stride.
 *
 * @tparam T Element type
 */
template <typename T>
class StridedSpan {
public:
    /**
     * @brief Iterator for StridedSpan
     */
    class iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using pointer = T*;
        using iterator_category = std::forward_iterator_tag;

        CASPI_NON_BLOCKING iterator(pointer p, std::size_t stride) noexcept : p_(p), s_(stride) {}
        CASPI_NO_DISCARD reference operator*() const noexcept CASPI_NON_BLOCKING { return *p_; }
        CASPI_NON_BLOCKING iterator& operator++() noexcept { p_ += s_; return *this; }
        CASPI_NON_BLOCKING iterator operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        CASPI_NO_DISCARD bool operator==(const iterator& o) const noexcept CASPI_NON_BLOCKING { return p_ == o.p_; }
        CASPI_NO_DISCARD bool operator!=(const iterator& o) const noexcept CASPI_NON_BLOCKING { return p_ != o.p_; }

    private:
        pointer p_;
        std::size_t s_;
    };

    CASPI_NON_BLOCKING StridedSpan(T* base, std::size_t count, std::size_t stride) noexcept
        : base_(base), count_(count), stride_(stride) {}

    CASPI_NO_DISCARD iterator begin() const noexcept CASPI_NON_BLOCKING { return iterator(base_, stride_); }
    CASPI_NO_DISCARD iterator end()   const noexcept CASPI_NON_BLOCKING { return iterator(base_ + count_ * stride_, stride_); }

    CASPI_NO_DISCARD std::size_t size() const noexcept CASPI_NON_BLOCKING { return count_; }
    CASPI_NO_DISCARD T& operator[](std::size_t i) const noexcept CASPI_NON_BLOCKING { return *(base_ + i * stride_); }

    CASPI_NO_DISCARD T* data() const noexcept CASPI_NON_BLOCKING { return base_; }
    CASPI_NO_DISCARD std::size_t stride() const noexcept CASPI_NON_BLOCKING { return stride_; }

private:
    T* base_;
    std::size_t count_;
    std::size_t stride_;
};

/**
 * @brief A unified SpanView that can be contiguous or strided
 *
 * Provides random access and iteration over either contiguous or strided memory.
 *
 * @tparam T Element type
 */
template <typename T>
class SpanView {
public:
    enum class Type { Contiguous, Strided };

    // Contiguous constructor
    CASPI_NON_BLOCKING SpanView(T* ptr, std::size_t count) noexcept
        : type_(Type::Contiguous), contig_(ptr, count), stride_(1) {}

    // Strided constructor
    CASPI_NON_BLOCKING SpanView(T* ptr, std::size_t count, std::size_t stride) noexcept
        : type_(Type::Strided), strided_(ptr, count, stride), stride_(stride) {}

    CASPI_NO_DISCARD Type type() const noexcept CASPI_NON_BLOCKING { return type_; }
    CASPI_NO_DISCARD std::size_t size() const noexcept CASPI_NON_BLOCKING {
        return type_ == Type::Contiguous ? contig_.size() : strided_.size();
    }

    CASPI_NO_DISCARD T& operator[](std::size_t i) const noexcept CASPI_NON_BLOCKING {
        return type_ == Type::Contiguous ? contig_[i] : strided_[i];
    }

    CASPI_NO_DISCARD auto begin() const noexcept CASPI_NON_BLOCKING {
        return type_ == Type::Contiguous ? contig_.begin() : strided_.begin();
    }

    CASPI_NO_DISCARD auto end() const noexcept CASPI_NON_BLOCKING {
        return type_ == Type::Contiguous ? contig_.end() : strided_.end();
    }

private:
    Type type_;
    union {
        Span<T> contig_;
        StridedSpan<T> strided_;
    };
    std::size_t stride_;
};

} // namespace CASPI::Core

#endif
