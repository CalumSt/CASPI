#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Core.h"
#include <gtest/gtest.h>

using namespace CASPI::Core;
using CASPI::AudioBuffer;
using CASPI::ChannelMajorLayout;
using CASPI::InterleavedLayout;

/*
Unit Test Summary:

PRODUCER TESTS
---------------
1. Producer_PerSample_Interleaved
   - Verifies that a per-sample producer fills an interleaved buffer correctly.
2. Producer_PerSample_ChannelMajor
   - Verifies that a per-sample producer fills a channel-major buffer correctly.
3. Producer_PerFrame_Interleaved
   - Verifies that a per-frame producer fills an interleaved buffer using renderSpan.
4. Producer_PerFrame_ChannelMajor
   - Verifies that a per-frame producer fills a channel-major buffer using renderSpan.
5. Producer_PerChannel_ChannelMajor
   - Verifies that a per-channel producer fills a channel-major buffer using renderSpan.
6. Producer_Edge_ZeroFramesOrChannels
   - Ensure rendering empty buffers does not crash.
7. Producer_Edge_SingleFrameOrChannel
   - Test minimal 1x1 buffer.
8. Producer_Edge_LargeBuffer
   - Test rendering large buffer without overflow.
9. Producer_ChannelAdder_PerSample
   - Verifies channel-only renderSample variant in PerSample traversal.
10. Producer_ChannelFrameAdder_PerSample
    - Verifies channel+frame renderSample variant in PerSample traversal.
11. Producer_ChannelAdder_PerChannel
    - Verifies channel-only renderSample variant in PerChannel traversal.
12. Producer_ChannelFrameAdder_PerFrame
    - Verifies channel+frame renderSample variant in PerFrame traversal.

PROCESSOR TESTS
----------------
13. Processor_PerSample_Interleaved
    - Verifies per-sample processor increments values in interleaved buffer.
14. Processor_PerSample_ChannelMajor
    - Verifies per-sample processor increments values in channel-major buffer.
15. Processor_PerFrame_Interleaved
    - Verifies per-frame processor multiplies values in interleaved buffer.
16. Processor_PerFrame_ChannelMajor
    - Verifies per-frame processor multiplies values in channel-major buffer.
17. Processor_PerChannel_ChannelMajor
    - Verifies per-channel processor adds offset in channel-major buffer.
18. Processor_Edge_ZeroFramesOrChannels
    - Ensure processing empty buffers does not crash.
19. Processor_Edge_SingleFrameOrChannel
    - Test minimal 1x1 buffer processing.
20. Processor_Edge_NegativeAndSpecialValues
    - Verify NaN, -inf, +inf, and normal values process safely.
21. Processor_Edge_InterleavedVsChannelMajor
    - Ensure identical results for both layouts.
22. Processor_Edge_MultipleCalls
    - Check repeated processing applies correctly.
23. Processor_ChannelAdder_PerSample
    - Channel-only processSample variant in PerSample traversal.
24. Processor_ChannelFrameAdder_PerSample
    - Channel+frame processSample variant in PerSample traversal.
25. Processor_ChannelAdder_PerChannel
    - Channel-only processSample variant in PerChannel traversal.
26. Processor_ChannelFrameAdder_PerFrame
    - Channel+frame processSample variant in PerFrame traversal.
*/

// ----------------------
// Base-class Test Producers
// ----------------------
class SampleProducer : public Producer<double, Traversal::PerSample> {
public:
    double renderSample() override { return 1.0; }
};

class FrameProducer : public Producer<double, Traversal::PerFrame> {
public:
    double renderSample() override { return 2.0; }
};

class ChannelProducer : public Producer<double, Traversal::PerChannel> {
public:
    double renderSample() override { return 3.0; }
};

// Channel/Frame index producers
class ChannelAdderProducer : public Producer<double, Traversal::PerSample> {
public:
    double renderSample(std::size_t channel) override { return static_cast<double>(channel); }
};

class ChannelFrameAdderProducer : public Producer<double, Traversal::PerSample> {
public:
    double renderSample(std::size_t channel, std::size_t frame) override {
        return static_cast<double>(channel + frame);
    }
};

// ----------------------
// Base-class Test Processors
// ----------------------
class SampleProcessor : public Processor<double, Traversal::PerSample> {
public:
    double processSample(double x) override {
        return x + 1.0;
    }
};

class FrameProcessor : public Processor<double, Traversal::PerFrame> {
public:
    double processSample(double x) override {
        return x * 10.0;
    }
};

class ChannelProcessor : public Processor<double, Traversal::PerChannel> {
public:
    double processSample(double x) override {
        return x + 5.0;
    }
};

// Channel/Frame index processors
class ChannelAdder : public Processor<double, Traversal::PerSample> {
public:
    double processSample(double in, const std::size_t channel) override {
        return in + static_cast<double>(channel);
    }
};

class ChannelFrameAdder : public Processor<double, Traversal::PerSample> {
public:
    double processSample(double in, std::size_t channel, std::size_t frame) override {
        return in + static_cast<double>(channel + frame);
    }
};

// ----------------------
// Producer Unit Tests
// ----------------------
TEST(Producer_PerSample_Interleaved, RendersCorrectly) {
    // 1.
    AudioBuffer<double, InterleavedLayout> buf(2, 4);
    buf.clear();
    SampleProducer p;
    p.render(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 1.0);
}

TEST(Producer_PerSample_ChannelMajor, RendersCorrectly) {
    // 2.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 4);
    buf.clear();
    SampleProducer p;
    p.render(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 1.0);
}

TEST(Producer_PerFrame_Interleaved, RendersCorrectly) {
    // 3.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.clear();
    FrameProducer p;
    p.render(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 2.0);
}

TEST(Producer_PerFrame_ChannelMajor, RendersCorrectly) {
    // 4.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.clear();
    FrameProducer p;
    p.render(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 2.0);
}

TEST(Producer_PerChannel_ChannelMajor, RendersCorrectly) {
    // 5.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.clear();
    ChannelProducer p;
    p.render(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 3.0);
}

// Edge cases
TEST(Producer_Edge_ZeroFramesOrChannels, HandlesEmptyBuffer) {
    // 6.
    AudioBuffer<double, InterleavedLayout> buf1(0, 5), buf2(5, 0);
    SampleProducer p;
    EXPECT_NO_THROW(p.render (buf1));
    EXPECT_NO_THROW(p.render (buf2));
}

TEST(Producer_Edge_SingleFrameOrChannel, Handles1x1Buffer) {
    // 7.
    AudioBuffer<double, InterleavedLayout> buf(1, 1);
    buf.clear();
    SampleProducer p;
    p.render(buf);
    EXPECT_DOUBLE_EQ(buf.sample (0, 0), 1.0);
}

TEST(Producer_Edge_LargeBuffer, HandlesLargeBuffer) {
    // 8.
    AudioBuffer<double, ChannelMajorLayout> buf(32, 1024);
    buf.clear();
    SampleProducer p;
    EXPECT_NO_THROW(p.render (buf));
}

// Channel/frame index tests
TEST(Producer_ChannelAdder_PerSample, SetsChannelIndex) {
    // 9.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);
    ChannelAdderProducer p;
    p.render(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), 0.0);
        EXPECT_DOUBLE_EQ(buf.sample (1, f), 1.0);
    }
}

TEST(Producer_ChannelFrameAdder_PerSample, SetsChannelPlusFrame) {
    // 10.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);
    ChannelFrameAdderProducer p;
    p.render(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), static_cast<double> (f));
        EXPECT_DOUBLE_EQ(buf.sample (1, f), static_cast<double> (1 + f));
    }
}

TEST(Producer_ChannelAdder_PerChannel, ChannelTraversal) {
    // 11.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 2);
    buf.fill(0.0);
    ChannelAdderProducer p;
    p.render(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), 0.0);
        EXPECT_DOUBLE_EQ(buf.sample (1, f), 1.0);
    }
}

TEST(Producer_ChannelFrameAdder_PerFrame, FrameTraversal) {
    // 12.
    AudioBuffer<double, InterleavedLayout> buf(2, 2);
    buf.fill(0.0);
    ChannelFrameAdderProducer p;
    p.render(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), static_cast<double> (f));
        EXPECT_DOUBLE_EQ(buf.sample (1, f), static_cast<double> (1 + f));
    }
}

// ----------------------
// Processor Unit Tests
// ----------------------
TEST(Processor_PerSample_Interleaved, IncrementsValues) {
    // 13.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);
    SampleProcessor p;
    p.process(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 1.0);
}

TEST(Processor_PerSample_ChannelMajor, IncrementsValues) {
    // 14.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.fill(0.0);
    SampleProcessor p;
    p.process(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 1.0);
}

TEST(Processor_PerFrame_Interleaved, MultipliesValues) {
    // 15.
    AudioBuffer<double, InterleavedLayout> buf(2, 2);
    int val = 1;
    for (std::size_t f = 0; f < buf.numFrames(); ++f)
        for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
            buf.sample(ch, f) = val++;
    FrameProcessor p;
    p.process(buf);
    val = 1;
    for (std::size_t f = 0; f < buf.numFrames(); ++f)
        for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), static_cast<double> (val++ * 10));
}

TEST(Processor_PerFrame_ChannelMajor, MultipliesValues) {
    // 16.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 2);
    int val = 1;
    for (std::size_t f = 0; f < buf.numFrames(); ++f)
        for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
            buf.sample(ch, f) = val++;
    FrameProcessor p;
    p.process(buf);
    val = 1;
    for (std::size_t f = 0; f < buf.numFrames(); ++f)
        for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), static_cast<double> (val++ * 10));
}

TEST(Processor_PerChannel_ChannelMajor, AddsOffset) {
    // 17.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.fill(0.0);
    ChannelProcessor p;
    p.process(buf);
    for (std::size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (std::size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 5.0);
}

// Edge cases
TEST(Processor_Edge_ZeroFramesOrChannels, HandlesEmptyBuffer) {
    // 18.
    AudioBuffer<double, InterleavedLayout> buf1(0, 3), buf2(2, 0);
    ChannelProcessor p;
    EXPECT_NO_THROW(p.process (buf1));
    EXPECT_NO_THROW(p.process (buf2));
}

TEST(Processor_Edge_SingleFrameOrChannel, Handles1x1Buffer) {
    // 19.
    AudioBuffer<double, ChannelMajorLayout> buf(1, 1);
    buf.fill(3.0);
    ChannelProcessor p;
    p.process(buf);
    EXPECT_DOUBLE_EQ(buf.sample (0, 0), 8.0);
}

TEST(Processor_Edge_NegativeAndSpecialValues, HandlesSpecialValues) {
    // 20.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.sample(0, 0) = -1.0;
    buf.sample(0, 1) = std::nan("");
    buf.sample(0, 2) = std::numeric_limits<double>::infinity();
    buf.sample(1, 0) = -std::numeric_limits<double>::infinity();
    buf.sample(1, 1) = 0.0;
    buf.sample(1, 2) = 2.5;
    ChannelProcessor p;
    EXPECT_NO_THROW(p.process (buf));
    EXPECT_DOUBLE_EQ(buf.sample (0, 0), 4.0);
    EXPECT_TRUE(std::isnan (buf.sample (0, 1)));
    EXPECT_TRUE(std::isinf (buf.sample (0, 2)));
    EXPECT_TRUE(std::isinf (buf.sample (1, 0)));
    EXPECT_DOUBLE_EQ(buf.sample (1, 1), 5.0);
    EXPECT_DOUBLE_EQ(buf.sample (1, 2), 7.5);
}

TEST(Processor_Edge_InterleavedVsChannelMajor, ConsistentResults) {
    // 21.
    AudioBuffer<double, InterleavedLayout> bufI(2, 3);
    AudioBuffer<double, ChannelMajorLayout> bufC(2, 3);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t f = 0; f < 3; ++f) {
            bufI.sample(ch, f) = static_cast<double>(ch + f);
            bufC.sample(ch, f) = static_cast<double>(ch + f);
        }
    ChannelProcessor p;
    p.process(bufI);
    p.process(bufC);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t f = 0; f < 3; ++f)
            EXPECT_DOUBLE_EQ(bufI.sample (ch, f), bufC.sample (ch, f));
}

TEST(Processor_Edge_MultipleCalls, AppliesIncrementRepeatedly) {
    // 22.
    AudioBuffer<double, InterleavedLayout> buf(2, 2);
    buf.fill(1.0);
    ChannelProcessor p;
    p.process(buf);
    p.process(buf);
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t f = 0; f < 2; ++f)
            EXPECT_DOUBLE_EQ(buf.sample (ch, f), 11.0);
}

// Channel/frame index tests
TEST(Processor_ChannelAdder_PerSample, AddsChannelIndex) {
    // 23.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);
    ChannelAdder p;
    p.process(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), 0.0);
        EXPECT_DOUBLE_EQ(buf.sample (1, f), 1.0);
    }
}

TEST(Processor_ChannelFrameAdder_PerSample, AddsChannelPlusFrame) {
    // 24.
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);
    ChannelFrameAdder p;
    p.process(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), static_cast<double> (f));
        EXPECT_DOUBLE_EQ(buf.sample (1, f), static_cast<double> (1 + f));
    }
}

TEST(Processor_ChannelAdder_PerChannel, ChannelTraversal) {
    // 25.
    AudioBuffer<double, ChannelMajorLayout> buf(2, 2);
    buf.fill(0.0);
    ChannelAdder p;
    p.process(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), 0.0);
        EXPECT_DOUBLE_EQ(buf.sample (1, f), 1.0);
    }
}

TEST(Processor_ChannelFrameAdder_PerFrame, FrameTraversal) {
    // 26.
    AudioBuffer<double, InterleavedLayout> buf(2, 2);
    buf.fill(0.0);
    ChannelFrameAdder p;
    p.process(buf);
    for (std::size_t f = 0; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample (0, f), static_cast<double> (f));
        EXPECT_DOUBLE_EQ(buf.sample (1, f), static_cast<double> (1 + f));
    }
}
