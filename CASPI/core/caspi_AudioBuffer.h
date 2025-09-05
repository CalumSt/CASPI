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
#include "caspi_Span.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace CASPI {
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

    // clang-format on
    // ===== Error enums =====
    enum class ResizeError {
        InvalidChannels,
        InvalidFrames,
        OutOfMemory
    };

    enum class ReadError {
        OutOfRange
    };

    // ===== Layout Base =====
    template<typename Derived, typename T>
    class LayoutBase {
    public:
        using SampleType = T;

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numChannels()
        const noexcept {
            return numChannels_;
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numFrames() const noexcept {
            return numFrames_;
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numSamples() const noexcept {
            return numChannels_ * numFrames_;
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        SampleType *data() noexcept {
            return data_.data();
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        const SampleType *data() const noexcept {
            return data_.data();
        }

        CASPI_NON_BLOCKING

        void clear() noexcept {
            std::fill(data_.begin(), data_.end(), SampleType{});
        }

        CASPI_NON_BLOCKING

        void fill(SampleType value) noexcept {
            std::fill(data_.begin(), data_.end(), value);
        }

        CASPI_BLOCKING CASPI_NO_DISCARD

        expected<void, ResizeError> resize(const std::size_t channels, const std::size_t frames)
        {
            // Zero-sized is allowed and means "empty buffer"
            if (channels == 0 || frames == 0) {
                try {
                    data_.clear();
                    data_.shrink_to_fit(); // optional: keep or drop if you want capacity kept
                } catch (...) {
                    return make_unexpected(ResizeError::OutOfMemory);
                }
                numChannels_ = 0;
                numFrames_ = 0;
                return {};
            }

            // Normal resize
            try {
                data_.resize(channels * frames);
                numChannels_ = channels;
                numFrames_ = frames;
            } catch (...) {
                return make_unexpected(ResizeError::OutOfMemory);
            }
            return {};
        }

        CASPI_BLOCKING CASPI_NO_DISCARD

        expected<void, ResizeError> resizeAndClear(const std::size_t channels,
                                                   const std::size_t frames) {
            auto res = resize(channels, frames);
            if (!res) {
                return res;
            }
            clear();
            return res;
        }

        CASPI_NON_BLOCKING

        void setSample(const std::size_t channel, const std::size_t frame,
                       SampleType value) noexcept {
            derived().sample(channel, frame) = value;
        }

        // --- raw iterators over the underlying contiguous storage ---
        CASPI_NON_BLOCKING
        SampleType *begin() noexcept { return data_.data(); }

        CASPI_NON_BLOCKING
        SampleType *end() noexcept { return data_.data() + data_.size(); }

        CASPI_NON_BLOCKING
        const SampleType *begin() const noexcept { return data_.data(); }

        CASPI_NON_BLOCKING

        const SampleType *end() const noexcept {
            return data_.data() + data_.size();
        }

        CASPI_NON_BLOCKING

        const SampleType *cbegin() const noexcept {
            return data_.data();
        }

        CASPI_NON_BLOCKING

        const SampleType *cend() const noexcept {
            return data_.data() + data_.size();
        }

    protected:
        LayoutBase(const std::size_t channels = 0, const std::size_t frames = 0) {
            resize(channels, frames);
        }

        size_t numChannels_{0};
        size_t numFrames_{0};
        std::vector<T> data_;

        Derived &derived() noexcept { return static_cast<Derived &>(*this); }
        const Derived &derived() const noexcept { return static_cast<const Derived &>(*this); }
    };

    // ===== Channel Major Layout =====
    template<typename T>
    class ChannelMajorLayout : public LayoutBase<ChannelMajorLayout<T>, T> {
    public:
        using Base = LayoutBase<ChannelMajorLayout<T>, T>;
        using typename Base::SampleType;

        ChannelMajorLayout(const std::size_t channels = 0, const std::size_t frames = 0)
            : Base(channels, frames) {
        }

        CASPI_NON_BLOCKING

        SampleType &sample(const std::size_t channel, const std::size_t frame)
            noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            CASPI_ASSERT(frame < this->numFrames_, "Number of frames is out of range");
            return this->data_[channel * this->numFrames_ + frame];
        }

        CASPI_NON_BLOCKING

        const SampleType &sample(const std::size_t channel, const std::size_t frame)
        const noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            CASPI_ASSERT(frame < this->numFrames_, "Number of frames is out of range");
            return this->data_[channel * this->numFrames_ + frame];
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        expected<SampleType &, ReadError> sample_bounds_checked(
            const std::size_t channel, const std::size_t frame) noexcept {
            if (channel >= this->numChannels_ || frame >= this->numFrames_)
                return make_unexpected(ReadError::OutOfRange);

            return this->data_[channel * this->numFrames_ + frame];
        }

        CASPI_NON_BLOCKING

        SampleType *channelData(const std::size_t channel) noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            return &this->data_[channel * this->numFrames_];
        }

        CASPI_NON_BLOCKING

        const SampleType *channelData(const std::size_t channel) const noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            return &this->data_[channel * this->numFrames_];
        }

        // In ChannelMajorLayout<T> public section:
        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<SampleType> channel_span(std::size_t c) noexcept {
            CASPI_ASSERT(c < this->numChannels_, "channel out of range");
            return CASPI::Core::Span<SampleType>(this->data_.data() + c * this->numFrames_,
                                                 this->numFrames_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::StridedSpan<SampleType> frame_span(std::size_t f) noexcept {
            CASPI_ASSERT(f < this->numFrames_, "frame out of range");
            // one sample from each channel, stride = numFrames_
            return CASPI::Core::StridedSpan<SampleType>(this->data_.data() + f, this->numChannels_,
                                                        this->numFrames_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<SampleType> all_span() noexcept {
            return CASPI::Core::Span<SampleType>(this->data_.data(), this->data_.size());
        }

        // const overloads
        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<const SampleType> channel_span(std::size_t c) const noexcept {
            CASPI_ASSERT(c < this->numChannels_, "channel out of range");
            return CASPI::Core::Span<const SampleType>(this->data_.data() + c * this->numFrames_,
                                                       this->numFrames_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::StridedSpan<const SampleType> frame_span(std::size_t f) const noexcept {
            CASPI_ASSERT(f < this->numFrames_, "frame out of range");
            return CASPI::Core::StridedSpan<const SampleType>(
                this->data_.data() + f, this->numChannels_, this->numFrames_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<const SampleType> all_span() const noexcept {
            return CASPI::Core::Span<const SampleType>(this->data_.data(), this->data_.size());
        }
    };

    // ===== Interleaved Layout =====
    template<typename T>
    class InterleavedLayout : public LayoutBase<InterleavedLayout<T>, T> {
    public:
        using Base = LayoutBase<InterleavedLayout<T>, T>;
        using typename Base::SampleType;

        InterleavedLayout(size_t channels = 0, size_t frames = 0)
            : Base(channels, frames) {
        }

        CASPI_NON_BLOCKING

        SampleType &sample(size_t channel, size_t frame) noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            CASPI_ASSERT(frame < this->numFrames_, "Number of frames is out of range");
            return this->data_[frame * this->numChannels_ + channel];
        }

        CASPI_NON_BLOCKING

        const SampleType &sample(size_t channel, size_t frame) const noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            CASPI_ASSERT(frame < this->numFrames_, "Number of frames is out of range");
            return this->data_[frame * this->numChannels_ + channel];
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        expected<SampleType &, ReadError> sample_bounds_checked(size_t channel,
                                                                size_t frame) noexcept {
            if (channel >= this->numChannels_ || frame >= this->numFrames_)
                return make_unexpected(ReadError::OutOfRange);

            return this->data_[frame * this->numChannels_ + channel];
        }

        CASPI_NON_BLOCKING

        SampleType *channelData(size_t channel) noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            return &this->data_[channel];
        }

        CASPI_NON_BLOCKING

        const SampleType *channelData(size_t channel) const noexcept {
            CASPI_ASSERT(channel < this->numChannels_, "Number of channels is out of range");
            return &this->data_[channel];
        }

        // In InterleavedLayout<T> public section:
        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::StridedSpan<SampleType> channel_span(std::size_t c) noexcept {
            CASPI_ASSERT(c < this->numChannels_, "channel out of range");
            // samples of channel c at positions c, c+numChannels, ...
            return CASPI::Core::StridedSpan<SampleType>(this->data_.data() + c, this->numFrames_,
                                                        this->numChannels_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<SampleType> frame_span(std::size_t f) noexcept {
            CASPI_ASSERT(f < this->numFrames_, "frame out of range");
            // one contiguous block of all channels for this frame
            return CASPI::Core::Span<SampleType>(this->data_.data() + f * this->numChannels_,
                                                 this->numChannels_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<SampleType> all_span() noexcept {
            return CASPI::Core::Span<SampleType>(this->data_.data(), this->data_.size());
        }

        // const overloads
        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::StridedSpan<const SampleType> channel_span(std::size_t c) const noexcept {
            CASPI_ASSERT(c < this->numChannels_, "channel out of range");
            return CASPI::Core::StridedSpan<const SampleType>(
                this->data_.data() + c, this->numFrames_, this->numChannels_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<const SampleType> frame_span(std::size_t f) const noexcept {
            CASPI_ASSERT(f < this->numFrames_, "frame out of range");
            return CASPI::Core::Span<const SampleType>(this->data_.data() + f * this->numChannels_,
                                                       this->numChannels_);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        CASPI::Core::Span<const SampleType> all_span() const noexcept {
            return CASPI::Core::Span<const SampleType>(this->data_.data(), this->data_.size());
        }
    };

    template<typename T>
    struct is_interleaved<InterleavedLayout<T> > : std::true_type {
    };

    template<typename T>
    struct is_channel_major<ChannelMajorLayout<T> > : std::true_type {
    };

    // ===== AudioBuffer Wrapper =====
    template<typename SampleType, template <typename> class Layout = InterleavedLayout>
    class AudioBuffer {
        using LayoutType = Layout<SampleType>;
        CASPI_STATIC_ASSERT(CASPI::is_interleaved<LayoutType>::value || CASPI::is_channel_major<
                            LayoutType>::value, "Layout must be known type");

    public:
        AudioBuffer(size_t channels = 0, size_t frames = 0)
            : layout_(channels, frames) {
        }

        CASPI_NO_DISCARD auto resize(const std::size_t channels, const std::size_t frames)
        CASPI_BLOCKING {
            return layout_.resize(channels, frames);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numChannels() const noexcept {
            return layout_.numChannels();
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numFrames() const noexcept {
            return layout_.numFrames();
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        std::size_t numSamples() const noexcept {
            return layout_.numSamples();
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        SampleType &sample(size_t channel, size_t frame) noexcept {
            return layout_.sample(channel, frame);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        const SampleType &sample(size_t channel, size_t frame) const noexcept {
            return layout_.sample(channel, frame);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD

        expected<SampleType &, ReadError> sample_bounds_checked(
            size_t channel, size_t frame) const noexcept {
            return layout_.sample_bounds_checked(channel, frame);
        }

        CASPI_NON_BLOCKING

        void setSample(size_t channel, size_t frame, SampleType value) noexcept {
            layout_.setSample(channel, frame, value);
        }

        CASPI_NON_BLOCKING
        void clear() noexcept { layout_.clear(); }

        CASPI_NON_BLOCKING
        void fill(SampleType value) noexcept { layout_.fill(value); }

        CASPI_NON_BLOCKING
        SampleType *data() noexcept { return layout_.data(); }

        CASPI_NON_BLOCKING
        const SampleType *data() const noexcept { return layout_.data(); }

        CASPI_NON_BLOCKING

        SampleType *channelData(const std::size_t channel) noexcept {
            return layout_.channelData(channel);
        }

        CASPI_NON_BLOCKING

        const SampleType *channelData(const std::size_t channel) const noexcept {
            return layout_.channelData(channel);
        }

        // In AudioBuffer<SampleType, Layout> public section:
        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto channel_span(std::size_t c) noexcept {
            return layout_.channel_span(c);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto frame_span(std::size_t f) noexcept {
            return layout_.frame_span(f);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto all_span() noexcept {
            return layout_.all_span();
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto channel_span(std::size_t c) const noexcept {
            return layout_.channel_span(c);
        }

        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto frame_span(std::size_t f) const noexcept {
            return layout_.frame_span(f);
        }
        CASPI_NON_BLOCKING CASPI_NO_DISCARD
        auto all_span() const noexcept {
            return layout_.all_span();
        }

    private:
        LayoutType layout_;
    };

    namespace block {
        // --------------------------- fill ---------------------------
        template<typename View, typename T>
        CASPI_NON_BLOCKING
        void fill(View v, const T &value) noexcept {
            for (typename std::remove_reference<decltype (v.begin())>::type it = v.begin();
                 it != v.end();
                 ++it) {
                *it = value;
            }
        }

        // --------------------------- scale --------------------------
        template<typename View, typename T>
        CASPI_NON_BLOCKING
        void scale(View v, const T &factor) noexcept {
            for (auto &x: v) {
                using ValueType = typename std::remove_reference<decltype (x)>::type;
                x = static_cast<ValueType>(x * factor);
            }
        }

        // --------------------------- copy ---------------------------
        template<typename ViewDst, typename ViewSrc>
        CASPI_NON_BLOCKING
        void copy(ViewDst dst, ViewSrc src) noexcept {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS) {
                *itD = *itS;
            }
        }

        // --------------------------- add ----------------------------
        template<typename ViewDst, typename ViewSrc>
        CASPI_NON_BLOCKING
        void add(ViewDst dst, ViewSrc src) noexcept {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS) {
                *itD = static_cast<decltype (*itD)>(*itD + *itS);
            }
        }

        // --------------------------- apply (unary) ---------------------------
        template<typename View, typename UnaryOp>
        CASPI_NON_BLOCKING
        void apply(View v, UnaryOp op) noexcept (noexcept (op(*v.begin()))) {
            for (typename std::remove_reference<decltype (v.begin())>::type it = v.begin();
                 it != v.end();
                 ++it) {
                // op(x) -> assignable to element type
                *it = op(*it);
            }
        }

        // --------------------------- apply2 (binary) ---------------------------
        template<typename ViewDst, typename ViewSrc, typename BinaryOp>
        CASPI_NON_BLOCKING
        void apply2(ViewDst dst, ViewSrc src, BinaryOp op) noexcept (noexcept (op(
            *dst.begin(),
            *src.begin()))) {
            typename std::remove_reference<decltype (dst.begin())>::type itD = dst.begin();
            typename std::remove_reference<decltype (src.begin())>::type itS = src.begin();

            for (; itD != dst.end() && itS != src.end(); ++itD, ++itS) {
                *itD = op(*itD, *itS);
            }
        }
    } // namespace block
} // namespace CASPI

#endif // CASPI_AUDIOBUFFER_H
