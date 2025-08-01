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


* @file caspi_CircularBuffer.h
* @author CS Islay
* @brief A collection of useful buffers for audio.
*
************************************************************************/

#ifndef CASPI_BUFFERS_H
#define CASPI_BUFFERS_H
#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "base/caspi_Traits.h"
#include "caspi_Expected.h"
#include <memory>
#include <vector>

namespace CASPI {
    /**
 * @class CircularBuffer
 * @brief A circular buffer for audio. Only contains one channel.
 */
    template<typename SampleType = double>
    class CircularBuffer {
    public:
        explicit CircularBuffer(std::size_t numSamples) noexcept
            : buffer(numSamples, static_cast<SampleType>(0)),
              numSamples(numSamples) {
        }

        explicit CircularBuffer(const std::vector<SampleType> &data) noexcept
            : buffer(data), numSamples(data.size()) {
        }

        CircularBuffer &operator=(CircularBuffer other) noexcept {
            if (this != &other) {
                swap(*this, other);
            }
            return *this;
        }

        friend void swap(CircularBuffer &a, CircularBuffer &b) noexcept {
            using std::swap;
            swap(a.buffer, b.buffer);
            swap(a.numSamples, b.numSamples);
            swap(a.writeIndex, b.writeIndex);
        }

        CircularBuffer(CircularBuffer &&other) noexcept = default;

        CircularBuffer(const CircularBuffer &other) noexcept = default;

        CASPI_NO_DISCARD std::size_t getNumSamples() const { return numSamples; }

        CASPI_NO_DISCARD SampleType read(const int delayInSamples) const {
            int readIndex = writeIndex - delayInSamples;
            if (readIndex < 0)
                readIndex += static_cast<int>(numSamples);
            return buffer[readIndex % numSamples];
        }

        CASPI_NO_DISCARD SampleType
        read(SampleType fractionalDelay, bool interpolate = true) const {
            const int index1 = static_cast<int>(fractionalDelay);
            const int index2 = index1 + 1;
            const SampleType y1 = read(index1);
            if (!interpolate)
                return y1;
            const SampleType y2 = read(index2);
            const SampleType frac = fractionalDelay - static_cast<int>(fractionalDelay);
            return linearInterpolation(y1, y2, frac);
        }

        void write(SampleType value) {
            buffer[writeIndex] = value;
            writeIndex = (writeIndex + 1) % numSamples;
        }

        void resize(std::size_t newSize) {
            std::vector<SampleType> newBuffer(newSize, static_cast<SampleType>(0));
            std::size_t minSize = std::min(newSize, numSamples);
            for (std::size_t i = 0; i < minSize; ++i) {
                newBuffer[i] = buffer[i];
            }
            buffer = std::move(newBuffer);
            numSamples = newSize;
            writeIndex %= numSamples;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), static_cast<SampleType>(0));
            writeIndex = 0;
        }

        CASPI_NO_DISCARD std::vector<SampleType> getBufferAsVector() const {
            return buffer;
        }

        CASPI_NO_DISCARD SampleType *ptr() {
            return buffer.data();
        }

        CASPI_NO_DISCARD const SampleType *ptr() const {
            return buffer.data();
        }

    private:
        std::vector<SampleType> buffer;
        std::size_t numSamples = 0;
        std::size_t writeIndex = 0;

        static SampleType linearInterpolation(SampleType y1, SampleType y2, SampleType frac) {
            if (frac >= 1.0)
                return y2;
            return frac * y2 + (1.0 - frac) * y1;
        }
    };

    enum class ReadError {
        DelayTooLarge,
        BufferEmpty
    };

    template<typename SampleType, typename PolicyTag>
    class CircularBufferBase {
    public:
        explicit CircularBufferBase(const std::size_t initialSize,
                                    std::size_t maxSize = Constants::DEFAULT_MAX_BUFFER_SIZE)
            : buffer_((maxSize == 0 ? initialSize : maxSize), SampleType(0)),
              activeSize_(initialSize),
              maxSize_((maxSize == 0) ? initialSize : maxSize),
              writeIndex_(0) {
            CASPI_ASSERT(initialSize > 0, "initialSize must be > 0");
            CASPI_ASSERT(maxSize_ >= initialSize, "maxSize must be >= initialSize");
        }

        void write(SampleType value) {
            buffer_[writeIndex_] = value;
            writeIndex_ = (writeIndex_ + 1) % activeSize_;
        }

        CASPI_NO_DISCARD expected<SampleType, ReadError> read(const std::size_t delay) const {
            if (delay >= activeSize_)
                return {unexpect, ReadError::DelayTooLarge}; // Invalid delay

            if (activeSize_ == 0)
                return {unexpect, ReadError::BufferEmpty};

            int readIndex = static_cast<int>(writeIndex_) - static_cast<int>(delay) - 1;
            if (readIndex < 0)
                readIndex += static_cast<int>(activeSize_);
            return expected<SampleType, ReadError>{buffer_[readIndex]};
        }

        CASPI_NO_DISCARD std::size_t getActiveSize() const { return activeSize_; }
        CASPI_NO_DISCARD std::size_t getMaxSize() const { return maxSize_; }

        // Resize active window (within preallocated space) — allowed for both tags
        CASPI_NO_DISCARD bool resize(const std::size_t newSize) {
            if (newSize > 0 && newSize <= maxSize_) {
                activeSize_ = newSize;
                writeIndex_ %= activeSize_;
                return true;
            }
            return false;
        }

        // Resize full buffer — only enabled for non-real-time-safe types
        template<typename T = PolicyTag>
        typename std::enable_if<is_non_real_time_safe<T>::value, void>::type
        resizeBeyondMax(std::size_t newMaxSize) {
            CASPI_ASSERT(newMaxSize > maxSize_,
                         "New max size must be greater than current max size");
            std::vector<SampleType> newBuffer(newMaxSize, SampleType(0));

            const std::size_t copySize = std::min(activeSize_, newMaxSize);
            const std::size_t start = (writeIndex_ + activeSize_ - copySize) % activeSize_;
            for (std::size_t i = 0; i < copySize; ++i)
                newBuffer[i] = buffer_[(start + i) % activeSize_];

            buffer_ = std::move(newBuffer);
            maxSize_ = newMaxSize;
            activeSize_ = copySize;
            writeIndex_ = copySize % activeSize_;
        }

        // Disallow resizing beyond max for real-time safe policy
        template<typename T = PolicyTag>
        typename std::enable_if<is_real_time_safe<T>::value, void>::type
        resizeBeyondMax(std::size_t) {
            CASPI_STATIC_ASSERT(! is_real_time_safe<T>::value,
                                "resizeBeyondMax() is disabled for real-time safe buffers.");
        }

        void clear() {
            std::fill(buffer_.begin(), buffer_.end(), SampleType(0));
            writeIndex_ = 0;
        }

    protected:
        std::vector<SampleType> buffer_;
        std::size_t activeSize_ = 0;
        std::size_t maxSize_ = 0;
        std::size_t writeIndex_ = 0;
    };

    //================================================================================
    // Interpolation Policies
    //================================================================================

    template<typename T>
    struct LinearInterpolation {
        static T apply(T a, T b, double frac) {
            return static_cast<T>((1.0 - frac) * a + frac * b);
        }
    };
} // namespace CASPI

#endif //CASPI_BUFFERS_H
