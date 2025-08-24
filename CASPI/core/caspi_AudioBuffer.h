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

* @file caspi_AudioBuffer.h
* @author CS Islay
* @brief An audio buffer class for basic audio processing.
*
************************************************************************/

#ifndef CASPI_AUDIOBUFFER_H
#define CASPI_AUDIOBUFFER_H

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "base/caspi_Traits.h"
#include "caspi_Expected.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace CASPI
{
    // clang-format off
#if defined(CASPI_FEATURES_HAS_CONCEPTS)
template <typename L>
concept AudioLayout = requires (L layout, size_t ch, size_t fr, typename L::SampleType value)
{
    { layout.numChannels() } -> std::convertible_to<std::size_t>;
    { layout.numFrames() } -> std::convertible_to<std::size_t>;
    { layout.sample(ch, fr) } -> std::same_as<typename L::SampleType&>;
    { layout.setSample(ch, fr, value) };
    { layout.data() } -> std::same_as<typename L::SampleType*>;
};
#else
#include <type_traits>
template <typename, typename = void>
struct is_audio_layout : std::false_type {};

template <typename L>
struct is_audio_layout<L,
    std::void_t<
        decltype(std::declval<L>().numChannels()),
        decltype(std::declval<L>().numFrames()),
        decltype(std::declval<L>().sample(size_t{}, size_t{})),
        decltype(std::declval<L>().setSample(size_t{}, size_t{}, typename L::SampleType{})),
        decltype(std::declval<L>().data())
    >
> : std::true_type {};

template <typename L>
constexpr bool is_audio_layout_v = is_audio_layout<L>::value;
#endif // CASPI_FEATURES_HAS_CONCEPTS

    // ---------- Span detection / alias ----------
#if defined(CASPI_FEATURES_HAS_SPAN)
#define CASPI_FEATURES_HAS_SPAN
#include <span>
        template <typename T>
        using Span = std::span<T>;
#else
    // Minimal fallback Span (contiguous, non-owning)
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

            Span() noexcept : ptr_(nullptr), sz_(0) {}
            Span(T* ptr, size_type count) noexcept : ptr_(ptr), sz_(count) {}

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
#endif // span

    // ---------- StridedSpan (for non-contiguous views) ----------
        template <typename T>
        class StridedSpan {
        public:
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
                CASPI_NON_BLOCKING iterator operator++(int) noexcept { auto tmp=*this; ++(*this); return tmp; }
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

            // Helpers to expose raw information (useful for optimized block ops)
            CASPI_NO_DISCARD T* data() const noexcept CASPI_NON_BLOCKING { return base_; }
            CASPI_NO_DISCARD std::size_t stride() const noexcept CASPI_NON_BLOCKING { return stride_; }

        private:
            T* base_;
            std::size_t count_;
            std::size_t stride_;
        };

    // clang-format on
    // ===== Error enums =====
    enum class ResizeError
    {
        InvalidChannels,
        InvalidFrames,
        OutOfMemory
    };

    enum class ReadError
    {
        OutOfRange
    };

    // ===== Layout Base =====
    template <typename Derived, typename T>
    class LayoutBase
    {
        public:
            using SampleType = T;

            CASPI_NO_DISCARD std::size_t numChannels() const CASPI_NON_BLOCKING noexcept {
                return numChannels_;
            }

            CASPI_NO_DISCARD std::size_t numFrames() const CASPI_NON_BLOCKING noexcept {
                return numFrames_;
            }

            CASPI_NO_DISCARD std::size_t numSamples() const CASPI_NON_BLOCKING noexcept {
                return numChannels_ * numFrames_;
            }

            CASPI_NO_DISCARD SampleType* data() CASPI_NON_BLOCKING noexcept { return data_.data(); }
            CASPI_NO_DISCARD const SampleType *data() const CASPI_NON_BLOCKING noexcept {
                return data_.data();
            }

            void clear() CASPI_NON_BLOCKING noexcept
            {
                std::fill (data_.begin(), data_.end(), SampleType {});
            }

            void fill (SampleType value) CASPI_NON_BLOCKING noexcept
            {
                std::fill (data_.begin(), data_.end(), value);
            }

            CASPI_NO_DISCARD expected<void, ResizeError>
                resize (const std::size_t channels, const std::size_t frames) CASPI_BLOCKING
            {
                // Zero-sized is allowed and means "empty buffer"
                if (channels == 0 || frames == 0)
                {
                    try
                    {
                        data_.clear();
                        data_.shrink_to_fit(); // optional: keep or drop if you want capacity kept
                    }
                    catch (...)
                    {
                        return make_unexpected (ResizeError::OutOfMemory);
                    }
                    numChannels_ = 0;
                    numFrames_   = 0;
                    return {};
                }

                // Normal resize
                try
                {
                    data_.resize (channels * frames);
                    numChannels_ = channels;
                    numFrames_   = frames;
                }
                catch (...)
                {
                    return make_unexpected (ResizeError::OutOfMemory);
                }
                return {};
            }

            CASPI_NO_DISCARD expected<void, ResizeError> resizeAndClear (
                const std::size_t channels,
                const std::size_t frames) CASPI_BLOCKING
            {
                auto res = resize (channels, frames);
                if (! res)
                {
                    return res;
                }
                clear();
                return res;
            }

            void setSample (const std::size_t channel, const std::size_t frame, SampleType value) noexcept CASPI_NON_BLOCKING
            {
                derived().sample (channel, frame) = value;
            }

            // --- raw iterators over the underlying contiguous storage ---
            SampleType* begin() noexcept CASPI_NON_BLOCKING { return data_.data(); }
            SampleType* end() noexcept CASPI_NON_BLOCKING { return data_.data() + data_.size(); }
            const SampleType* begin() const noexcept CASPI_NON_BLOCKING { return data_.data(); }

            const SampleType* end() const noexcept CASPI_NON_BLOCKING
            {
                return data_.data() + data_.size();
            }

            const SampleType* cbegin() const noexcept CASPI_NON_BLOCKING { return data_.data(); }

            const SampleType* cend() const noexcept CASPI_NON_BLOCKING
            {
                return data_.data() + data_.size();
            }

        protected:
            LayoutBase (const std::size_t channels = 0, const std::size_t frames = 0)
            {
                resize (channels, frames);
            }

            size_t numChannels_ { 0 };
            size_t numFrames_ { 0 };
            std::vector<T> data_;

            Derived& derived() noexcept { return static_cast<Derived&> (*this); }
            const Derived& derived() const noexcept { return static_cast<const Derived&> (*this); }
    };

    // ===== Channel Major Layout =====
    template <typename T>
    class ChannelMajorLayout : public LayoutBase<ChannelMajorLayout<T>, T>
    {
        public:
            using Base = LayoutBase<ChannelMajorLayout<T>, T>;
            using typename Base::SampleType;

            ChannelMajorLayout (const std::size_t channels = 0, const std::size_t frames = 0)
                : Base (channels, frames)
            {
            }

            SampleType& sample (const std::size_t channel, const std::size_t frame) noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                CASPI_ASSERT (frame < this->numFrames_, "Number of frames is out of range");
                return this->data_[channel * this->numFrames_ + frame];
            }

            const SampleType& sample (const std::size_t channel, const std::size_t frame) const noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                CASPI_ASSERT (frame < this->numFrames_, "Number of frames is out of range");
                return this->data_[channel * this->numFrames_ + frame];
            }

            CASPI_NO_DISCARD expected<SampleType&, ReadError>
                sample_bounds_checked (const std::size_t channel, const std::size_t frame) noexcept
                CASPI_NON_BLOCKING
            {
                if (channel >= this->numChannels_ || frame >= this->numFrames_)
                    return make_unexpected (ReadError::OutOfRange);

                return this->data_[channel * this->numFrames_ + frame];
            }

            SampleType* channelData (const std::size_t channel) noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                return &this->data_[channel * this->numFrames_];
            }

            const SampleType* channelData (const std::size_t channel) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                return &this->data_[channel * this->numFrames_];
            }

            // In ChannelMajorLayout<T> public section:
            CASPI_NO_DISCARD CASPI::Span<SampleType> channel_span (std::size_t c) noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (c < this->numChannels_, "channel out of range");
                return CASPI::Span<SampleType> (this->data_.data() + c * this->numFrames_,
                                                this->numFrames_);
            }

            CASPI_NO_DISCARD CASPI::StridedSpan<SampleType> frame_span (std::size_t f) noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (f < this->numFrames_, "frame out of range");
                // one sample from each channel, stride = numFrames_
                return CASPI::StridedSpan<SampleType> (this->data_.data() + f, this->numChannels_, this->numFrames_);
            }

            CASPI_NO_DISCARD CASPI::Span<SampleType> all_span() noexcept CASPI_NON_BLOCKING
            {
                return CASPI::Span<SampleType> (this->data_.data(), this->data_.size());
            }

            // const overloads
            CASPI_NO_DISCARD CASPI::Span<const SampleType> channel_span (std::size_t c) const noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (c < this->numChannels_, "channel out of range");
                return CASPI::Span<const SampleType> (this->data_.data() + c * this->numFrames_,
                                                      this->numFrames_);
            }

            CASPI_NO_DISCARD CASPI::StridedSpan<const SampleType> frame_span (
                std::size_t f) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (f < this->numFrames_, "frame out of range");
                return CASPI::StridedSpan<const SampleType> (this->data_.data() + f, this->numChannels_, this->numFrames_);
            }

            CASPI_NO_DISCARD CASPI::Span<const SampleType> all_span() const noexcept
                CASPI_NON_BLOCKING
            {
                return CASPI::Span<const SampleType> (this->data_.data(), this->data_.size());
            }
    };

    // ===== Interleaved Layout =====
    template <typename T>
    class InterleavedLayout : public LayoutBase<InterleavedLayout<T>, T>
    {
        public:
            using Base = LayoutBase<InterleavedLayout<T>, T>;
            using typename Base::SampleType;

            InterleavedLayout (size_t channels = 0, size_t frames = 0)
                : Base (channels, frames)
            {
            }

            SampleType& sample (size_t channel, size_t frame) noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                CASPI_ASSERT (frame < this->numFrames_, "Number of frames is out of range");
                return this->data_[frame * this->numChannels_ + channel];
            }

            const SampleType& sample (size_t channel, size_t frame) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                CASPI_ASSERT (frame < this->numFrames_, "Number of frames is out of range");
                return this->data_[frame * this->numChannels_ + channel];
            }

            CASPI_NO_DISCARD expected<SampleType&, ReadError>
                sample_bounds_checked (size_t channel, size_t frame) noexcept CASPI_NON_BLOCKING
            {
                if (channel >= this->numChannels_ || frame >= this->numFrames_)
                    return make_unexpected (ReadError::OutOfRange);

                return this->data_[frame * this->numChannels_ + channel];
            }

            SampleType* channelData (size_t channel) noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                return &this->data_[channel];
            }

            const SampleType* channelData (size_t channel) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (channel < this->numChannels_, "Number of channels is out of range");
                return &this->data_[channel];
            }

            // In InterleavedLayout<T> public section:
            CASPI_NO_DISCARD CASPI::StridedSpan<SampleType> channel_span (std::size_t c) noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (c < this->numChannels_, "channel out of range");
                // samples of channel c at positions c, c+numChannels, ...
                return CASPI::StridedSpan<SampleType> (this->data_.data() + c, this->numFrames_, this->numChannels_);
            }

            CASPI_NO_DISCARD CASPI::Span<SampleType> frame_span (std::size_t f) noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (f < this->numFrames_, "frame out of range");
                // one contiguous block of all channels for this frame
                return CASPI::Span<SampleType> (this->data_.data() + f * this->numChannels_,
                                                this->numChannels_);
            }

            CASPI_NO_DISCARD CASPI::Span<SampleType> all_span() noexcept CASPI_NON_BLOCKING
            {
                return CASPI::Span<SampleType> (this->data_.data(), this->data_.size());
            }

            // const overloads
            CASPI_NO_DISCARD CASPI::StridedSpan<const SampleType> channel_span (
                std::size_t c) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (c < this->numChannels_, "channel out of range");
                return CASPI::StridedSpan<const SampleType> (this->data_.data() + c, this->numFrames_, this->numChannels_);
            }

            CASPI_NO_DISCARD CASPI::Span<const SampleType> frame_span (std::size_t f) const noexcept
                CASPI_NON_BLOCKING
            {
                CASPI_ASSERT (f < this->numFrames_, "frame out of range");
                return CASPI::Span<const SampleType> (this->data_.data() + f * this->numChannels_,
                                                      this->numChannels_);
            }

            CASPI_NO_DISCARD CASPI::Span<const SampleType> all_span() const noexcept
                CASPI_NON_BLOCKING
            {
                return CASPI::Span<const SampleType> (this->data_.data(), this->data_.size());
            }
    };

    // ===== AudioBuffer Wrapper =====
    template <typename SampleType, template <typename> class Layout = InterleavedLayout>
    class AudioBuffer
    {
        public:
            using LayoutType = Layout<SampleType>;

            AudioBuffer (size_t channels = 0, size_t frames = 0)
                : layout_ (channels, frames)
            {
            }

            CASPI_NO_DISCARD auto resize (const std::size_t channels, const std::size_t frames)
                CASPI_BLOCKING
            {
                return layout_.resize (channels, frames);
            }

            CASPI_NO_DISCARD std::size_t numChannels() const noexcept CASPI_NON_BLOCKING
            {
                return layout_.numChannels();
            }

            CASPI_NO_DISCARD std::size_t numFrames() const noexcept CASPI_NON_BLOCKING
            {
                return layout_.numFrames();
            }

            CASPI_NO_DISCARD std::size_t numSamples() const noexcept CASPI_NON_BLOCKING
            {
                return layout_.numSamples();
            }

            CASPI_NO_DISCARD SampleType& sample (size_t channel, size_t frame) noexcept
                CASPI_NON_BLOCKING
            {
                return layout_.sample (channel, frame);
            }

            CASPI_NO_DISCARD const SampleType& sample (size_t channel, size_t frame) const noexcept
                CASPI_NON_BLOCKING
            {
                return layout_.sample (channel, frame);
            }

            CASPI_NO_DISCARD expected<SampleType&, ReadError> sample_bounds_checked (
                size_t channel,
                size_t frame) const noexcept CASPI_NON_BLOCKING
            {
                return layout_.sample_bounds_checked (channel, frame);
            }

            void setSample (size_t channel, size_t frame, SampleType value) noexcept CASPI_NON_BLOCKING
            {
                layout_.setSample (channel, frame, value);
            }

            void clear() noexcept CASPI_NON_BLOCKING { layout_.clear(); }
            void fill (SampleType value) noexcept CASPI_NON_BLOCKING { layout_.fill (value); }

            SampleType* data() noexcept CASPI_NON_BLOCKING { return layout_.data(); }
            const SampleType* data() const noexcept CASPI_NON_BLOCKING { return layout_.data(); }

            // In AudioBuffer<SampleType, Layout> public section:
            CASPI_NO_DISCARD auto channel_span (std::size_t c) noexcept CASPI_NON_BLOCKING
            {
                return layout_.channel_span (c);
            }

            CASPI_NO_DISCARD auto frame_span (std::size_t f) noexcept CASPI_NON_BLOCKING
            {
                return layout_.frame_span (f);
            }

            CASPI_NO_DISCARD auto all_span() noexcept CASPI_NON_BLOCKING { return layout_.all_span(); }

            CASPI_NO_DISCARD auto channel_span (std::size_t c) const noexcept CASPI_NON_BLOCKING
            {
                return layout_.channel_span (c);
            }

            CASPI_NO_DISCARD auto frame_span (std::size_t f) const noexcept CASPI_NON_BLOCKING
            {
                return layout_.frame_span (f);
            }

            CASPI_NO_DISCARD auto all_span() const noexcept CASPI_NON_BLOCKING
            {
                return layout_.all_span();
            }

        private:
            LayoutType layout_;
    };

    namespace block
    {
        // --------------------------- fill ---------------------------
        template <typename View, typename T>
        CASPI_NON_BLOCKING void fill (View v, const T& value) noexcept
        {
            for (typename std::remove_reference<decltype (v.begin())>::type it = v.begin();
                 it != v.end();
                 ++it)
            {
                *it = value;
            }
        }

        // --------------------------- scale --------------------------
        template <typename View, typename T>
        CASPI_NON_BLOCKING void scale (View v, const T& factor) noexcept
        {
            for (auto& x : v)
            {
                using ValueType = typename std::remove_reference<decltype (x)>::type;
                x               = static_cast<ValueType> (x * factor);
            }
        }

        // --------------------------- copy ---------------------------
        template <typename ViewDst, typename ViewSrc>
        CASPI_NON_BLOCKING inline void copy (ViewDst dst, ViewSrc src) noexcept
        {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS)
            {
                *itD = *itS;
            }
        }

        // --------------------------- add ----------------------------
        template <typename ViewDst, typename ViewSrc>
        CASPI_NON_BLOCKING void add (ViewDst dst, ViewSrc src) noexcept
        {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS)
            {
                *itD = static_cast<decltype (*itD)> (*itD + *itS);
            }
        }

        // --------------------------- apply (unary) ---------------------------
        template <typename View, typename UnaryOp>
        CASPI_NON_BLOCKING void apply (View v, UnaryOp op) noexcept (noexcept (op (*v.begin())))
        {
            for (typename std::remove_reference<decltype (v.begin())>::type it = v.begin();
                 it != v.end();
                 ++it)
            {
                // op(x) -> assignable to element type
                *it = op (*it);
            }
        }

        // --------------------------- apply2 (binary) ---------------------------
        template <typename ViewDst, typename ViewSrc, typename BinaryOp>
        CASPI_NON_BLOCKING void apply2 (ViewDst dst, ViewSrc src, BinaryOp op) noexcept (noexcept (op (*dst.begin(), *src.begin())))
        {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS)
            {
                *itD = op (*itD, *itS);
            }
        }
    } // namespace block
} // namespace CASPI

#endif // CASPI_AUDIOBUFFER_H
