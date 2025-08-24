/*
Unit Tests Plan
===============
1. Construction & Resizing
   - Default constructor produces 0 channels/frames.
   - Resizing updates numChannels, numFrames, numSamples correctly.
   - Resizing preserves no assumptions about data (we can re-fill after).

2. Sample Access
   - setSample then sample returns correct values.
   - Fill sets all samples to expected value.
   - Clear sets all samples to zero.
   - Const and non-const sample access yield same results.

3. Channel Data Pointer Offsets
   - ChannelMajor: channelData(c) points to contiguous frame block for that channel.
   - Interleaved: channelData(c) points to correct offset (interleaved step).

4. Memory Layout Verification
   - ChannelMajor: verify all channel samples are grouped by channel.
   - Interleaved: verify per-frame grouping of channels.

5. Edge Cases
   - 0 channels / 0 frames: no crash, numSamples == 0.
   - Single channel: both layouts behave same for sample order.
   - One frame: verify ordering in memory for both layouts.

6. Resize Behavior
   - Increase/decrease channels and frames, verify sizes and sample access.
*/

#include <gtest/gtest.h>
#include <vector>
#include <cstddef>

#include "core/caspi_AudioBuffer.h"

TEST(ChannelMajorLayoutTest, ConstructionAndResize) {
    CASPI::ChannelMajorLayout<float> buf;
    EXPECT_EQ(buf.numChannels(), 0u);
    EXPECT_EQ(buf.numFrames(), 0u);
    EXPECT_EQ(buf.numSamples(), 0u);

    buf.resize(2, 4);
    EXPECT_EQ(buf.numChannels(), 2u);
    EXPECT_EQ(buf.numFrames(), 4u);
    EXPECT_EQ(buf.numSamples(), 8u);
}

TEST(ChannelMajorLayoutTest, BasicSetAndGet) {
    CASPI::ChannelMajorLayout<float> buf(2, 3);
    buf.setSample(1, 2, 5.5f);
    EXPECT_FLOAT_EQ(buf.sample(1, 2), 5.5f);

    buf.fill(1.0f);
    for (size_t c = 0; c < buf.numChannels(); ++c)
        for (size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_FLOAT_EQ(buf.sample(c, f), 1.0f);

    buf.clear();
    for (size_t c = 0; c < buf.numChannels(); ++c)
        for (size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_FLOAT_EQ(buf.sample(c, f), 0.0f);
}

TEST(ChannelMajorLayoutTest, ChannelDataMemoryLayout) {
    CASPI::ChannelMajorLayout<int> buf(2, 4);
    // Fill with unique values: channel * 10 + frame
    for (size_t c = 0; c < 2; ++c)
        for (size_t f = 0; f < 4; ++f)
            buf.setSample(c, f, static_cast<int>(c * 10 + f));

    int* ch0 = buf.channelData(0);
    int* ch1 = buf.channelData(1);

    // ch0 should be contiguous for frames 0..3
    for (size_t f = 0; f < 4; ++f)
        EXPECT_EQ(ch0[f], 0 * 10 + f);

    // ch1 should be contiguous for frames 0..3
    for (size_t f = 0; f < 4; ++f)
        EXPECT_EQ(ch1[f], 1 * 10 + f);

    // Also check raw data memory grouping
    const int* raw = buf.data();
    EXPECT_EQ(raw[0], 0); // ch0, frame0
    EXPECT_EQ(raw[3], 3); // ch0, frame3
    EXPECT_EQ(raw[4], 10); // ch1, frame0
}

TEST(InterleavedLayoutTest, ConstructionAndResize) {
    CASPI::InterleavedLayout<float> buf;
    EXPECT_EQ(buf.numChannels(), 0u);
    EXPECT_EQ(buf.numFrames(), 0u);
    EXPECT_EQ(buf.numSamples(), 0u);

    buf.resize(2, 4);
    EXPECT_EQ(buf.numChannels(), 2u);
    EXPECT_EQ(buf.numFrames(), 4u);
    EXPECT_EQ(buf.numSamples(), 8u);
}

TEST(InterleavedLayoutTest, BasicSetAndGet) {
    CASPI::InterleavedLayout<float> buf(2, 3);
    buf.setSample(1, 2, 5.5f);
    EXPECT_FLOAT_EQ(buf.sample(1, 2), 5.5f);

    buf.fill(1.0f);
    for (size_t c = 0; c < buf.numChannels(); ++c)
        for (size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_FLOAT_EQ(buf.sample(c, f), 1.0f);

    buf.clear();
    for (size_t c = 0; c < buf.numChannels(); ++c)
        for (size_t f = 0; f < buf.numFrames(); ++f)
            EXPECT_FLOAT_EQ(buf.sample(c, f), 0.0f);
}

TEST(InterleavedLayoutTest, ChannelDataMemoryLayout) {
    CASPI::InterleavedLayout<int> buf(2, 4);
    for (size_t c = 0; c < 2; ++c)
        for (size_t f = 0; f < 4; ++f)
            buf.setSample(c, f, static_cast<int>(c * 10 + f));

    int* ch0 = buf.channelData(0);
    int* ch1 = buf.channelData(1);

    // To access channel samples for interleaved layout, step by the channel stride (numChannels).
    size_t stride = buf.numChannels();

    EXPECT_EQ(ch0[0], 0);                 // frame0, ch0 (raw[0])
    EXPECT_EQ(ch0[2 * stride], 2);        // frame2, ch0 (raw[4])
    EXPECT_EQ(ch1[0], 10);                // frame0, ch1 (raw[1])

    // Raw memory check: [0,10,1,11,2,12,3,13]
    const int* raw = buf.data();
    EXPECT_EQ(raw[0], 0);
    EXPECT_EQ(raw[1], 10);
    EXPECT_EQ(raw[2], 1);
    EXPECT_EQ(raw[3], 11);
    EXPECT_EQ(raw[4], 2);
    EXPECT_EQ(raw[5], 12);
}

TEST(AudioBufferTest, ResizeAndSampleAccess) {
    CASPI::AudioBuffer<float> buffer(2, 3);
    EXPECT_EQ(buffer.numChannels(), 2);
    EXPECT_EQ(buffer.numFrames(), 3);
    EXPECT_EQ(buffer.numSamples(), 6);

    buffer.fill(1.5f);
    for (size_t ch = 0; ch < buffer.numChannels(); ++ch) {
        for (size_t fr = 0; fr < buffer.numFrames(); ++fr) {
            EXPECT_FLOAT_EQ(buffer.sample(ch, fr), 1.5f);
        }
    }

    buffer.setSample(1, 2, 3.14f);
    EXPECT_FLOAT_EQ(buffer.sample(1, 2), 3.14f);
}

TEST(AudioBufferTest, ClearResetsToZero) {
    CASPI::AudioBuffer<int> buffer(1, 2);
    buffer.fill(5);
    buffer.clear();
    for (size_t ch = 0; ch < buffer.numChannels(); ++ch)
        for (size_t fr = 0; fr < buffer.numFrames(); ++fr)
            EXPECT_EQ(buffer.sample(ch, fr), 0);
}

TEST(InterleavedLayoutTest, GrowPreservesUnderlyingPrefix)
{
    CASPI::InterleavedLayout<float> buf(2, 2); // size = 4
    ASSERT_EQ(buf.numSamples(), 4u);

    // Fill raw memory directly so raw[i] == i
    float* raw = buf.data();
    for (size_t i = 0; i < buf.numSamples(); ++i) raw[i] = static_cast<float>(i);

    // Grow to 3x3 -> size 9
    buf.resize(3, 3);
    ASSERT_EQ(buf.numSamples(), 9u);

    // First 4 entries unchanged
    const float* r = buf.data();
    EXPECT_FLOAT_EQ(r[0], 0.0f);
    EXPECT_FLOAT_EQ(r[1], 1.0f);
    EXPECT_FLOAT_EQ(r[2], 2.0f);
    EXPECT_FLOAT_EQ(r[3], 3.0f);

    // New entries are value-initialized (zero)
    for (size_t i = 4; i < 9; ++i)
        EXPECT_FLOAT_EQ(r[i], 0.0f) << "index " << i;
}

TEST(ChannelMajorLayoutTest, GrowPreservesUnderlyingPrefix)
{
    CASPI::ChannelMajorLayout<float> buf(2, 2); // size = 4
    ASSERT_EQ(buf.numSamples(), 4u);

    float* raw = buf.data();
    for (size_t i = 0; i < buf.numSamples(); ++i) raw[i] = static_cast<float>(i);

    buf.resize(3, 3); // size = 9
    ASSERT_EQ(buf.numSamples(), 9u);

    const float* r = buf.data();
    EXPECT_FLOAT_EQ(r[0], 0.0f);
    EXPECT_FLOAT_EQ(r[1], 1.0f);
    EXPECT_FLOAT_EQ(r[2], 2.0f);
    EXPECT_FLOAT_EQ(r[3], 3.0f);
    for (size_t i = 4; i < 9; ++i)
        EXPECT_FLOAT_EQ(r[i], 0.0f) << "index " << i;
}

TEST(InterleavedLayoutTest, ResizeDoesNotPreserveSampleCoordinates)
{
    CASPI::InterleavedLayout<float> buf(2, 2);
    // Put a distinctive value at (1,1)
    buf.setSample(1, 1, 42.0f);
    // For 2x2 interleaved, (1,1) was at linear index oldIdx = 1*2 + 1 = 3
    const size_t oldIdx = 3;
    ASSERT_FLOAT_EQ(buf.data()[oldIdx], 42.0f);

    // Grow dimensions so the coordinate mapping changes
    buf.resize(3, 3);

    // Now (1,1) maps to index 1*3 + 1 = 4, which should NOT be 42
    EXPECT_NE(buf.sample(1, 1), 42.0f);
    // The old raw slot 3 should still hold the old value
    EXPECT_FLOAT_EQ(buf.data()[oldIdx], 42.0f);
}

TEST(ChannelMajorLayoutTest, ResizeDoesNotPreserveSampleCoordinates)
{
    CASPI::ChannelMajorLayout<float> buf(2, 2);
    buf.setSample(1, 1, 42.0f);
    // For 2x2 channel-major, (1,1) was at linear index oldIdx = 1*2 + 1 = 3
    const size_t oldIdx = 3;
    ASSERT_FLOAT_EQ(buf.data()[oldIdx], 42.0f);

    buf.resize(3, 3);

    // Now (1,1) maps to index 1*3 + 1 = 4, which should NOT be 42
    EXPECT_NE(buf.sample(1, 1), 42.0f);
    EXPECT_FLOAT_EQ(buf.data()[oldIdx], 42.0f);
}

TEST(InterleavedLayoutTest, ShrinkTruncatesUnderlyingMemory)
{
    CASPI::InterleavedLayout<float> buf(2, 4); // size = 8
    float* raw = buf.data();
    for (size_t i = 0; i < buf.numSamples(); ++i) raw[i] = static_cast<float>(i);

    buf.resize(2, 2); // size = 4
    ASSERT_EQ(buf.numSamples(), 4u);
    const float* r = buf.data();
    for (size_t i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(r[i], static_cast<float>(i));
}

TEST(ChannelMajorLayoutTest, ShrinkTruncatesUnderlyingMemory)
{
    CASPI::ChannelMajorLayout<float> buf(2, 4); // size = 8
    float* raw = buf.data();
    for (size_t i = 0; i < buf.numSamples(); ++i) raw[i] = static_cast<float>(i);

    buf.resize(2, 2); // size = 4
    ASSERT_EQ(buf.numSamples(), 4u);
    const float* r = buf.data();
    for (size_t i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(r[i], static_cast<float>(i));
}

TEST(AudioBufferTest, DefaultInterleaved_GrowPreservesUnderlyingPrefix)
{
    CASPI::AudioBuffer<float> buf(2, 2); // default Layout = InterleavedLayout
    ASSERT_EQ(buf.numSamples(), 4u);

    float* raw = buf.data();
    for (size_t i = 0; i < buf.numSamples(); ++i) raw[i] = static_cast<float>(i);

    buf.resize(3, 3);
    ASSERT_EQ(buf.numSamples(), 9u);

    const float* r = buf.data();
    EXPECT_FLOAT_EQ(r[0], 0.0f);
    EXPECT_FLOAT_EQ(r[1], 1.0f);
    EXPECT_FLOAT_EQ(r[2], 2.0f);
    EXPECT_FLOAT_EQ(r[3], 3.0f);
    for (size_t i = 4; i < 9; ++i)
        EXPECT_FLOAT_EQ(r[i], 0.0f);
}

TEST(AudioBufferTest, ResizeToZeroResultsInEmpty)
{
    CASPI::AudioBuffer<float> buf(2, 2);
    buf.resize(0, 5);
    EXPECT_EQ(buf.numSamples(), 0u);
    buf.resize(3, 0);
    EXPECT_EQ(buf.numSamples(), 0u);
}
// 2) Copy constructor and assignment
TEST(AudioBufferTest, CopyConstructorAndAssignment) {
    CASPI::AudioBuffer<int> buf1(2, 2);
    buf1.fill(42);

    CASPI::AudioBuffer<int> buf2 = buf1;  // Copy constructor
    EXPECT_EQ(buf2.sample(1, 1), 42);

    CASPI::AudioBuffer<int> buf3;
    buf3 = buf1;  // Assignment
    EXPECT_EQ(buf3.sample(0, 0), 42);
}

// ---------------------- Span access ----------------------
TEST(AudioBufferTest, ChannelFrameAllSpan) {
    CASPI::AudioBuffer<float> buf(2, 3);
    // fill with increasing numbers for testing
    for (size_t ch=0; ch<buf.numChannels(); ++ch)
        for (size_t f=0; f<buf.numFrames(); ++f)
            buf.setSample(ch, f, static_cast<float>(ch*10+f));

    // channel_span
    auto ch0 = buf.channel_span(0);
    EXPECT_EQ(ch0.size(), buf.numFrames());
    EXPECT_EQ(ch0[0], 0.0f);
    EXPECT_EQ(ch0[1], 1.0f);
    EXPECT_EQ(ch0[2], 2.0f);

    // frame_span
    auto f1 = buf.frame_span(1);
    EXPECT_EQ(f1.size(), buf.numChannels());
    EXPECT_EQ(f1[0], 1.0f);
    EXPECT_EQ(f1[1], 11.0f);

    // all_span
    auto all = buf.all_span();
    EXPECT_EQ(all.size(), buf.numSamples());
}

// ---------------------- StridedSpan test ----------------------
TEST(StridedSpanTest, IterationAndIndex) {
    float data[6] = {0,1,2,3,4,5};
    CASPI::StridedSpan<float> span(data, 3, 2); // elements at 0,2,4

    // operator[]
    EXPECT_EQ(span[0], 0);
    EXPECT_EQ(span[1], 2);
    EXPECT_EQ(span[2], 4);

    // iteration
    int expected[] = {0,2,4};
    size_t idx=0;
    for(auto x : span) {
        EXPECT_EQ(x, expected[idx++]);
    }
}

// ---------------------- Block operations ----------------------
TEST(BlocksTest, FillScaleCopyApply) {
    CASPI::AudioBuffer<float> buf(2,3);
    auto all = buf.all_span();

    // fill
    CASPI::block::fill(all, 1.0f);
    for(auto x : all) EXPECT_EQ(x, 1.0f);

    // scale
    CASPI::block::scale(all, 2.0f);
    for(auto x : all) EXPECT_EQ(x, 2.0f);

    // copy
    float tmp[6] = {};
    CASPI::block::copy(CASPI::Span<float>(tmp,6), all);
    for(size_t i=0;i<6;++i) EXPECT_EQ(tmp[i], 2.0f);

    // apply unary op
    CASPI::block::apply(all, [](float x){ return x+1.0f; });
    for(auto x : all) EXPECT_EQ(x, 3.0f);

    // apply2 binary op
    float src[6] = {1,1,1,1,1,1};
    CASPI::block::apply2(all, CASPI::Span<float>(src,6), [](float a,float b){ return a-b; });
    for(auto x : all) EXPECT_EQ(x, 2.0f);
}

// ---------------------- Channel / Frame views with block ops ----------------------
TEST(AudioBufferTest, ChannelFrameBlocks) {
    CASPI::AudioBuffer<float> buf(2,3);
    auto ch0 = buf.channel_span(0);
    auto ch1 = buf.channel_span(1);

    CASPI::block::fill(ch0, 1.0f);
    CASPI::block::fill(ch1, 2.0f);

    EXPECT_EQ(buf.sample(0,0), 1.0f);
    EXPECT_EQ(buf.sample(1,0), 2.0f);

    auto f2 = buf.frame_span(2);
    CASPI::block::scale(f2, 2.0f);
    EXPECT_EQ(buf.sample(0,2), 2.0f);
    EXPECT_EQ(buf.sample(1,2), 4.0f);
}

// 3) Move constructor and assignment
TEST(AudioBufferTest, MoveConstructorAndAssignment) {
    CASPI::AudioBuffer<int> buf1(1, 1);
    buf1.setSample(0, 0, 99);

    CASPI::AudioBuffer<int> buf2 = std::move(buf1);  // Move constructor
    EXPECT_EQ(buf2.sample(0, 0), 99);

    CASPI::AudioBuffer<int> buf3;
    buf3 = std::move(buf2);  // Move assignment
    EXPECT_EQ(buf3.sample(0, 0), 99);
}

// 4) Fill and clear tests
TEST(AudioBufferTest, FillAndClear) {
    CASPI::AudioBuffer<double> buf(3, 3);
    buf.fill(3.14);
    for (size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (size_t fr = 0; fr < buf.numFrames(); ++fr)
            EXPECT_DOUBLE_EQ(buf.sample(ch, fr), 3.14);

    buf.clear();
    for (size_t ch = 0; ch < buf.numChannels(); ++ch)
        for (size_t fr = 0; fr < buf.numFrames(); ++fr)
            EXPECT_DOUBLE_EQ(buf.sample(ch, fr), 0.0);
}

// 5) Testing different layouts (optional, if supported)
TEST(AudioBufferTest, DifferentLayouts) {
    CASPI::AudioBuffer<float, CASPI::ChannelMajorLayout> chBuf(2, 2);
    chBuf.fill(1.23f);
    EXPECT_FLOAT_EQ(chBuf.sample(1, 1), 1.23f);

    CASPI::AudioBuffer<float, CASPI::InterleavedLayout> intBuf(2, 2);
    intBuf.fill(4.56f);
    EXPECT_FLOAT_EQ(intBuf.sample(0, 0), 4.56f);
}



