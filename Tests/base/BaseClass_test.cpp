#include <gtest/gtest.h>
#include "core/caspi_Core.h"
#include "core/caspi_AudioBuffer.h"

using namespace CASPI::Core;
using CASPI::AudioBuffer;
using CASPI::ChannelMajorLayout;
using CASPI::InterleavedLayout;

// ----------------------
// Test Producers
// ----------------------

// Per-sample producer
class SampleProducer : public Producer<double, Traversal::PerSample> {
public:
    double renderSample() override {
        return 1.0;
    }
};

// Per-frame producer
class FrameProducer : public Producer<double, Traversal::PerFrame> {
public:
    double renderFrame() override {
        return 2.0;
    }
};

// Per-channel producer
class ChannelProducer : public Producer<double, Traversal::PerChannel> {
public:
    void renderChannel(double* chData, std::size_t nFrames) override {
        for (std::size_t f{0}; f < nFrames; ++f) {
            chData[f] = 3.0;
        }
    }
};

// ----------------------
// Test Processors
// ----------------------

// Per-sample processor: increment by 1
class SampleProcessor : public Processor<double, Traversal::PerSample> {
public:
    double processSample(double x) override {
        return x + 1.0;
    }
};

// Per-frame processor: multiply all channels in a frame by 10
class FrameProcessor : public Processor<double, Traversal::PerFrame> {
public:
    void processFrame(double* frame, std::size_t nChannels) override {
        for (std::size_t i{0}; i < nChannels; ++i) {
            frame[i] *= 10.0;
        }
    }
};

// Per-channel processor: leaves the channel unchanged
class ChannelProcessor : public Processor<double, Traversal::PerChannel> {
public:
    void processChannel(double* channel, std::size_t nFrames) override {
        for (std::size_t i{0}; i < nFrames; ++i) {
            channel[i] = channel[i];
        }
    }
};

// ----------------------
// Producer Tests (Interleaved)
// ----------------------

TEST(ProducerTest_Interleaved, PerSampleWritesEachSample) {
    AudioBuffer<double, InterleavedLayout> buf(2, 4);
    buf.clear();

    SampleProducer p;
    p.render(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 1.0);
        }
    }
}

TEST(ProducerTest_Interleaved, PerFrameReplicatesAcrossChannels) {
    AudioBuffer<double, InterleavedLayout> buf(2, 4);
    buf.clear();

    FrameProducer p;
    p.render(buf);

    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 2.0);
        }
    }
}

TEST(ProducerTest_Interleaved, PerChannelFillsChannelBlocks) {
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.clear();

    ChannelProducer p;
    p.render(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 3.0);
        }
    }
}

// ----------------------
// Producer Tests (ChannelMajor)
// ----------------------

TEST(ProducerTest_ChannelMajor, PerSampleWritesEachSample) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 4);
    buf.clear();

    SampleProducer p;
    p.render(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 1.0);
        }
    }
}

TEST(ProducerTest_ChannelMajor, PerFrameReplicatesAcrossChannels) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 4);
    buf.clear();

    FrameProducer p;
    p.render(buf);

    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 2.0);
        }
    }
}

TEST(ProducerTest_ChannelMajor, PerChannelFillsChannelBlocks) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.clear();

    ChannelProducer p;
    p.render(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 3.0);
        }
    }
}

// ----------------------
// Processor Tests (Interleaved)
// ----------------------

TEST(ProcessorTest_Interleaved, PerSampleIncrementsAllSamples) {
    AudioBuffer<double, InterleavedLayout> buf(2, 3);
    buf.fill(0.0);

    SampleProcessor proc;
    proc.process(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 1.0);
        }
    }
}

TEST(ProcessorTest_Interleaved, PerFrameMultipliesWholeFrames) {
    AudioBuffer<double, InterleavedLayout> buf(2, 2);

    // Fill with increasing numbers
    int val{1};
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            buf.sample(ch, f) = val++;
        }
    }

    FrameProcessor proc;
    proc.process(buf);

    // Expect each value multiplied by 10
    val = 1;
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), static_cast<double>(val * 10));
            ++val;
        }
    }
}

TEST(ProcessorTest_Interleaved, PerChannelProcessesChannelWise) {
    AudioBuffer<double, InterleavedLayout> buf(2, 2);

    // Fill ch0 with 5s, ch1 with 9s
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        buf.sample(0, f) = 5.0;
        buf.sample(1, f) = 9.0;
    }

    ChannelProcessor proc;
    proc.process(buf);

    // Expect ch0 unchanged (still 5), ch1 unchanged (still 9)
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample(0, f), 5.0);
        EXPECT_DOUBLE_EQ(buf.sample(1, f), 9.0);
    }
}

// ----------------------
// Processor Tests (ChannelMajor)
// ----------------------

TEST(ProcessorTest_ChannelMajor, PerSampleIncrementsAllSamples) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 3);
    buf.fill(0.0);

    SampleProcessor proc;
    proc.process(buf);

    for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
        for (std::size_t f{0}; f < buf.numFrames(); ++f) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), 1.0);
        }
    }
}

TEST(ProcessorTest_ChannelMajor, PerFrameMultipliesWholeFrames) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 2);

    // Fill with increasing numbers
    int val{1};
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            buf.sample(ch, f) = val++;
        }
    }

    FrameProcessor proc;
    proc.process(buf);

    // Expect each value multiplied by 10
    val = 1;
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        for (std::size_t ch{0}; ch < buf.numChannels(); ++ch) {
            EXPECT_DOUBLE_EQ(buf.sample(ch, f), static_cast<double>(val * 10));
            ++val;
        }
    }
}

TEST(ProcessorTest_ChannelMajor, PerChannelProcessesChannelWise) {
    AudioBuffer<double, ChannelMajorLayout> buf(2, 2);

    // Fill ch0 with 5s, ch1 with 9s
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        buf.sample(0, f) = 5.0;
        buf.sample(1, f) = 9.0;
    }

    ChannelProcessor proc;
    proc.process(buf);

    // Expect ch0 unchanged (still 5), ch1 unchanged (still 9)
    for (std::size_t f{0}; f < buf.numFrames(); ++f) {
        EXPECT_DOUBLE_EQ(buf.sample(0, f), 5.0);
        EXPECT_DOUBLE_EQ(buf.sample(1, f), 9.0);
    }
}
