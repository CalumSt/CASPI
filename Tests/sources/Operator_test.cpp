#include "oscillators/caspi_Operator.h"
#include "maths/caspi_FFT.h"
#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include <vector>
#include <algorithm>
#include "../test_helpers.h"

using CASPI::ModulationMode;
using CASPI::Operator;
using CASPI::Complex;
using CASPI::CArray;

// ============================================================================
// Test Constants
// ============================================================================

constexpr double TEST_SAMPLE_RATE = 48000.0;
constexpr double TEST_FREQUENCY = 440.0;
constexpr size_t FFT_SIZE = 4096;


// ============================================================================
// Basic Functionality Tests
// ============================================================================

class OperatorBasicTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
    }
};

TEST_F(OperatorBasicTest, DefaultConstruction)
{
    Operator<double> newOp;
    EXPECT_EQ(newOp.getSampleRate(), CASPI::Constants::DEFAULT_SAMPLE_RATE<double>);
    EXPECT_DOUBLE_EQ(newOp.getFrequency(), 0.0);
    EXPECT_DOUBLE_EQ(newOp.getModulationDepth(), 1.0);
    EXPECT_DOUBLE_EQ(newOp.getModulationIndex(), 1.0);
    EXPECT_DOUBLE_EQ(newOp.getModulationFeedback(), 0.0);
    EXPECT_EQ(newOp.getModulationMode(), ModulationMode::Phase);
}

TEST_F(OperatorBasicTest, ParameterSettersAndGetters)
{
    op.setFrequency(TEST_FREQUENCY);
    EXPECT_DOUBLE_EQ(op.getFrequency(), TEST_FREQUENCY);

    op.setModulationIndex(2.5);
    EXPECT_DOUBLE_EQ(op.getModulationIndex(), 2.5);

    op.setModulationDepth(0.7);
    EXPECT_DOUBLE_EQ(op.getModulationDepth(), 0.7);

    op.setModulationFeedback(0.3);
    EXPECT_DOUBLE_EQ(op.getModulationFeedback(), 0.3);

    op.setModulationMode(ModulationMode::Frequency);
    EXPECT_EQ(op.getModulationMode(), ModulationMode::Frequency);
}

TEST_F(OperatorBasicTest, BatchModulationSetting)
{
    op.setModulation(2.0, 0.5, 0.3);
    EXPECT_DOUBLE_EQ(op.getModulationIndex(), 2.0);
    EXPECT_DOUBLE_EQ(op.getModulationDepth(), 0.5);
    EXPECT_DOUBLE_EQ(op.getModulationFeedback(), 0.3);
}

TEST_F(OperatorBasicTest, SampleRateUpdate)
{
    op.setSampleRate(96000.0);
    EXPECT_DOUBLE_EQ(op.getSampleRate(), 96000.0);

    op.setFrequency(1000.0);
    op.setSampleRate(48000.0);
    EXPECT_DOUBLE_EQ(op.getSampleRate(), 48000.0);
}

TEST_F(OperatorBasicTest, Reset)
{
    op.setFrequency(TEST_FREQUENCY);
    op.setModulation(2.0, 0.5, 0.3);
    op.setModulationMode(ModulationMode::Frequency);
    op.enableEnvelope();
    op.renderSample();

    op.reset();

    EXPECT_DOUBLE_EQ(op.getFrequency(), 0.0);
    EXPECT_DOUBLE_EQ(op.getModulationIndex(), 1.0);
    EXPECT_DOUBLE_EQ(op.getModulationDepth(), 1.0);
    EXPECT_DOUBLE_EQ(op.getModulationFeedback(), 0.0);
    EXPECT_EQ(op.getModulationMode(), ModulationMode::Phase);
}

// ============================================================================
// Pure Sine Wave Tests
// ============================================================================

class OperatorSineWaveTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(TEST_FREQUENCY);
        op.setModulationDepth(1.0);
    }
};

TEST_F(OperatorSineWaveTest, OutputBounds)
{
    for (int i = 0; i < 1000; ++i)
    {
        double sample = op.renderSample();
        EXPECT_LE(std::abs(sample), 1.0) << "Sample " << i << " out of bounds";
    }
}

TEST_F(OperatorSineWaveTest, AmplitudeRange)
{
    std::vector<double> samples;
    for (int i = 0; i < 2000; ++i)
        samples.push_back(op.renderSample());

    double minVal = *std::min_element(samples.begin(), samples.end());
    double maxVal = *std::max_element(samples.begin(), samples.end());

    EXPECT_NEAR(minVal, -1.0, 0.02);
    EXPECT_NEAR(maxVal, 1.0, 0.02);
}

TEST_F(OperatorSineWaveTest, DCOffset)
{
    op.renderSample(); // Skip first sample (sin(0) = 0)

    std::vector<double> samples;
    for (int i = 0; i < 4410; ++i)
        samples.push_back(op.renderSample());

    double dcOffset = TestHelpers::calculateDCOffset(samples);
    EXPECT_NEAR(dcOffset, 0.0, 0.02);
}

TEST_F(OperatorSineWaveTest, RMSValue)
{
    op.renderSample(); // Skip first

    std::vector<double> samples;
    for (int i = 0; i < 4800; ++i)
        samples.push_back(op.renderSample());

    double rms = TestHelpers::calculateRMS(samples);
    EXPECT_NEAR(rms, 0.707, 0.05) << "RMS should be ~0.707 for unit sine";
}

TEST_F(OperatorSineWaveTest, Periodicity)
{
    op.setFrequency(1000.0); // 48 samples per period at 48kHz

    std::vector<double> samples;
    for (int i = 0; i < 192; ++i) // 4 periods
        samples.push_back(op.renderSample());

    double correlation = TestHelpers::calculatePeriodCorrelation(samples, 48);
    EXPECT_GT(correlation, 0.95) << "Signal should be highly periodic";
}

TEST_F(OperatorSineWaveTest, FrequencyAccuracy)
{
    op.setFrequency(1000.0);

    std::vector<double> samples;
    for (int i = 0; i < 4800; ++i) // 100ms
        samples.push_back(op.renderSample());

    int zeroCrossings = TestHelpers::countZeroCrossings(samples);

    // 1000 Hz for 100ms = 100 periods = ~200 zero crossings
    // Allow ±10% tolerance for phase start and discrete sampling
    EXPECT_NEAR(zeroCrossings, 200, 20) << "Zero crossings indicate frequency";
}

TEST_F(OperatorSineWaveTest, ModulationDepthScaling)
{
    std::vector<double> depths = {0.25, 0.5, 0.75, 1.0};
    std::vector<double> rmsValues;

    for (double depth : depths)
    {
        op.reset();
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(TEST_FREQUENCY);
        op.setModulationDepth(depth);

        std::vector<double> samples;
        for (int i = 0; i < 1000; ++i)
            samples.push_back(op.renderSample());

        rmsValues.push_back(TestHelpers::calculateRMS(samples));
    }

    // RMS should increase monotonically with depth
    for (size_t i = 1; i < rmsValues.size(); ++i)
    {
        EXPECT_GT(rmsValues[i], rmsValues[i-1] * 0.9)
            << "RMS should increase with depth";
    }
}

// ============================================================================
// Phase Modulation Tests
// ============================================================================

class OperatorPhaseModulationTest : public ::testing::Test
{
protected:
    Operator<double> modulator;
    Operator<double> carrier;

    void SetUp() override
    {
        modulator.setSampleRate(TEST_SAMPLE_RATE);
        carrier.setSampleRate(TEST_SAMPLE_RATE);

        carrier.setFrequency(TEST_FREQUENCY);
        carrier.setModulationMode(ModulationMode::Phase);
        carrier.setModulationDepth(1.0);
    }

    std::vector<double> generateModulatedOutput(size_t numSamples, double modulatorFreq, double modulationIndex)
    {
        modulator.setFrequency(modulatorFreq);
        modulator.setModulationDepth(1.0);

        carrier.setModulationIndex(modulationIndex);

        std::vector<double> modSignal(numSamples);
        for (size_t i = 0; i < numSamples; ++i)
            modSignal[i] = modulator.renderSample();

        carrier.setModulationInput(modSignal.data(), modSignal.size());

        std::vector<double> output(numSamples);
        for (size_t i = 0; i < numSamples; ++i)
            output[i] = carrier.renderSample();

        return output;
    }
};

TEST_F(OperatorPhaseModulationTest, CreatesHarmonics)
{
    // Generate pure sine for comparison
    std::vector<double> pureSine;
    for (int i = 0; i < 4410; ++i)
        pureSine.push_back(carrier.renderSample());

    double pureRMS = TestHelpers::calculateRMS(pureSine);

    // Reset and apply PM
    carrier.reset();
    carrier.setSampleRate(TEST_SAMPLE_RATE);
    carrier.setFrequency(TEST_FREQUENCY);
    carrier.setModulationMode(ModulationMode::Phase);

    auto pmOutput = generateModulatedOutput(4410, 880.0, 5.0); // Strong modulation

    double pmRMS = TestHelpers::calculateRMS(pmOutput);

    // PM should create spectral changes (though not always more zero crossings)
    EXPECT_NE(pmRMS, pureRMS) << "PM should alter signal characteristics";

    // Output should remain bounded
    double maxVal = *std::max_element(pmOutput.begin(), pmOutput.end());
    double minVal = *std::min_element(pmOutput.begin(), pmOutput.end());
    EXPECT_LT(maxVal, 10.0);
    EXPECT_GT(minVal, -10.0);
}

TEST_F(OperatorPhaseModulationTest, ModulationIndexEffect)
{
    std::vector<double> indices = {1.0, 3.0, 5.0};
    std::vector<double> rmsValues;

    for (double index : indices)
    {
        carrier.reset();
        carrier.setSampleRate(TEST_SAMPLE_RATE);
        carrier.setFrequency(TEST_FREQUENCY);
        carrier.setModulationMode(ModulationMode::Phase);

        auto output = generateModulatedOutput(4410, 880.0, index);
        rmsValues.push_back(TestHelpers::calculateRMS(output));
    }

    // Different indices should produce different RMS
    EXPECT_TRUE(rmsValues[0] != rmsValues[1] || rmsValues[1] != rmsValues[2])
        << "Different modulation indices should affect output";
}

TEST_F(OperatorPhaseModulationTest, FFTShowsHarmonics)
{
    auto output = generateModulatedOutput(FFT_SIZE, 880.0, 3.0);

    auto windowed = TestHelpers::applyHannWindow(output);
    auto fftData = TestHelpers::realToComplex(windowed);
    CASPI::fft(fftData);
    auto spectrum = TestHelpers::getMagnitudeSpectrum(fftData);

    int peaks = TestHelpers::countSignificantPeaks(spectrum, 0.1);

    // PM with strong modulation should create multiple spectral components
    EXPECT_GT(peaks, 1) << "PM should create harmonic sidebands";
}

// ============================================================================
// Frequency Modulation Tests
// ============================================================================

class OperatorFrequencyModulationTest : public ::testing::Test
{
protected:
    Operator<double> carrier;

    void SetUp() override
    {
        carrier.setSampleRate(TEST_SAMPLE_RATE);
        carrier.setFrequency(TEST_FREQUENCY);
        carrier.setModulationMode(ModulationMode::Frequency);
        carrier.setModulationIndex(1.0);
    }
};

TEST_F(OperatorFrequencyModulationTest, VibratoBasic)
{
    // Create 5 Hz LFO with ±10 Hz deviation
    std::vector<double> lfo(4410);
    for (size_t i = 0; i < lfo.size(); ++i)
        lfo[i] = 10.0 * std::sin(CASPI::Constants::TWO_PI<double> * 5.0 * i / TEST_SAMPLE_RATE);

    carrier.setModulationInput(lfo.data(), lfo.size());

    std::vector<double> output;
    for (size_t i = 0; i < lfo.size(); ++i)
        output.push_back(carrier.renderSample());

    // Output should be bounded
    double maxVal = *std::max_element(output.begin(), output.end());
    double minVal = *std::min_element(output.begin(), output.end());
    EXPECT_LE(maxVal, 1.5);
    EXPECT_GE(minVal, -1.5);

    // Should have significant energy
    double rms = TestHelpers::calculateRMS(output);
    EXPECT_GT(rms, 0.5);
}

TEST_F(OperatorFrequencyModulationTest, ModulationIndexScaling)
{
    std::vector<double> modSignal(1000, 50.0); // Constant 50 Hz deviation
    std::vector<double> indices = {0.5, 1.0, 2.0};

    for (double index : indices)
    {
        carrier.reset();
        carrier.setSampleRate(TEST_SAMPLE_RATE);
        carrier.setFrequency(TEST_FREQUENCY);
        carrier.setModulationMode(ModulationMode::Frequency);
        carrier.setModulationIndex(index);

        carrier.setModulationInput(modSignal.data(), modSignal.size());

        std::vector<double> output;
        for (size_t i = 0; i < modSignal.size(); ++i)
            output.push_back(carrier.renderSample());

        double maxVal = *std::max_element(output.begin(), output.end());
        EXPECT_LE(maxVal, 2.0) << "Index " << index << " should produce bounded output";
    }
}

// ============================================================================
// Feedback Tests
// ============================================================================

class OperatorFeedbackTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(220.0);
        op.setModulationDepth(1.0);
    }
};

TEST_F(OperatorFeedbackTest, AddsHarmonicContent)
{
    // Without feedback
    op.setModulationFeedback(0.0);
    std::vector<double> noFeedback;
    for (int i = 0; i < 4410; ++i)
        noFeedback.push_back(op.renderSample());

    double rmsNoFb = TestHelpers::calculateRMS(noFeedback);

    // With feedback
    op.reset();
    op.setSampleRate(TEST_SAMPLE_RATE);
    op.setFrequency(220.0);
    op.setModulationFeedback(2.0);

    std::vector<double> withFeedback;
    for (int i = 0; i < 4410; ++i)
        withFeedback.push_back(op.renderSample());

    double rmsWithFb = TestHelpers::calculateRMS(withFeedback);

    // Feedback should alter characteristics
    EXPECT_NE(rmsNoFb, rmsWithFb);
}

TEST_F(OperatorFeedbackTest, RemainsStable)
{
    std::vector<double> feedbackAmounts = {0.0, 0.5, 1.0, 2.0, 5.0};

    for (double feedback : feedbackAmounts)
    {
        op.reset();
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(220.0);
        op.setModulationFeedback(feedback);

        for (int i = 0; i < 10000; ++i)
        {
            double sample = op.renderSample();
            EXPECT_FALSE(std::isnan(sample)) << "NaN at sample " << i << " with feedback=" << feedback;
            EXPECT_FALSE(std::isinf(sample)) << "Inf at sample " << i << " with feedback=" << feedback;
            EXPECT_LT(std::abs(sample), 10.0) << "Explosion at sample " << i << " with feedback=" << feedback;
        }
    }
}

// ============================================================================
// Envelope Tests
// ============================================================================

class OperatorEnvelopeTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(TEST_FREQUENCY);
        op.enableEnvelope();
    }
};

TEST_F(OperatorEnvelopeTest, AttackPhase)
{
    op.setADSR(0.1, 0.1, 0.7, 0.2);
    op.noteOn();

    // Skip a few samples to get past sin(0)
    for (int i = 0; i < 10; ++i)
        op.renderSample();

    double earlySample = std::abs(op.renderSample());
    EXPECT_LT(earlySample, 0.5);

    // Halfway through attack
    for (int i = 0; i < 2400; ++i)
        op.renderSample();

    double midAttack = std::abs(op.renderSample());
    EXPECT_GT(midAttack, 0.2);

    // After attack + some decay
    for (int i = 0; i < 3000; ++i)
        op.renderSample();

    double afterAttack = std::abs(op.renderSample());
    EXPECT_GT(afterAttack, 0.3);
}

TEST_F(OperatorEnvelopeTest, SustainPhase)
{
    op.setADSR(0.01, 0.01, 0.5, 0.1);
    op.noteOn();

    // Let attack/decay finish
    for (int i = 0; i < 1000; ++i)
        op.renderSample();

    // Measure sustain
    std::vector<double> sustainSamples;
    for (int i = 0; i < 4410; ++i)
        sustainSamples.push_back(std::abs(op.renderSample()));

    double rms = TestHelpers::calculateRMS(sustainSamples);
    EXPECT_GT(rms, 0.2);
    EXPECT_LT(rms, 0.6);
}

TEST_F(OperatorEnvelopeTest, ReleasePhase)
{
    op.setADSR(0.01, 0.01, 0.7, 0.1);
    op.noteOn();

    // Reach sustain
    for (int i = 0; i < 2000; ++i)
        op.renderSample();

    std::vector<double> beforeRelease;
    for (int i = 0; i < 100; ++i)
        beforeRelease.push_back(op.renderSample());
    double rmsBefore = TestHelpers::calculateRMS(beforeRelease);

    op.noteOff();

    // Mid-release
    for (int i = 0; i < 2400; ++i)
        op.renderSample();

    std::vector<double> midRelease;
    for (int i = 0; i < 100; ++i)
        midRelease.push_back(op.renderSample());
    double rmsMid = TestHelpers::calculateRMS(midRelease);

    EXPECT_LT(rmsMid, rmsBefore * 0.8);
}

TEST_F(OperatorEnvelopeTest, DisabledPassthrough)
{
    op.disableEnvelope();
    op.renderSample(); // Skip sin(0)

    std::vector<double> samples;
    for (int i = 0; i < 1000; ++i)
        samples.push_back(op.renderSample());

    double rms = TestHelpers::calculateRMS(samples);
    EXPECT_NEAR(rms, 0.707, 0.1);
}

// ============================================================================
// Buffer Management Tests
// ============================================================================

class OperatorBufferTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
        op.setFrequency(TEST_FREQUENCY);
    }
};

TEST_F(OperatorBufferTest, BoundsHandling)
{
    std::vector<double> modSignal(100, 0.5);
    op.setModulationInput(modSignal.data(), modSignal.size());

    // Render more than buffer size
    for (int i = 0; i < 200; ++i)
    {
        double sample = op.renderSample();
        EXPECT_FALSE(std::isnan(sample));
    }
}

TEST_F(OperatorBufferTest, ClearModulation)
{
    std::vector<double> modSignal(100, 0.5);
    op.setModulationInput(modSignal.data(), modSignal.size());
    op.clearModulationInput();

    double sample = op.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(OperatorBufferTest, NullBuffer)
{
    // Don't set any modulation
    std::vector<double> samples;
    for (int i = 0; i < 100; ++i)
        samples.push_back(op.renderSample());

    double dcOffset = TestHelpers::calculateDCOffset(samples);
    EXPECT_NEAR(dcOffset, 0.0, 0.1);
}

// ============================================================================
// Edge Cases
// ============================================================================

class OperatorEdgeCaseTest : public ::testing::Test
{
protected:
    Operator<double> op;

    void SetUp() override
    {
        op.setSampleRate(TEST_SAMPLE_RATE);
    }
};

TEST_F(OperatorEdgeCaseTest, ZeroFrequency)
{
    op.setFrequency(0.0);

    std::vector<double> samples;
    for (int i = 0; i < 100; ++i)
        samples.push_back(op.renderSample());

    // Should produce constant output
    double variance = 0.0;
    double mean = samples[0];
    for (double s : samples)
        variance += (s - mean) * (s - mean);
    variance /= samples.size();

    EXPECT_LT(variance, 0.01);
}

TEST_F(OperatorEdgeCaseTest, VeryHighFrequency)
{
    op.setFrequency(20000.0);

    for (int i = 0; i < 1000; ++i)
    {
        double sample = op.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_LE(std::abs(sample), 1.5);
    }
}

TEST_F(OperatorEdgeCaseTest, VeryLowFrequency)
{
    op.setFrequency(1.0);

    for (int i = 0; i < 1000; ++i)
    {
        double sample = op.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_LE(std::abs(sample), 1.5);
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(OperatorIntegrationTest, ComplexFMPatch)
{
    Operator<double> modulator, carrier;

    modulator.setSampleRate(TEST_SAMPLE_RATE);
    carrier.setSampleRate(TEST_SAMPLE_RATE);

    modulator.setFrequency(261.63);
    modulator.setModulationDepth(1.0);
    modulator.enableEnvelope();
    modulator.setADSR(0.001, 0.05, 0.3, 0.1);

    carrier.setFrequency(261.63);
    carrier.setModulationMode(ModulationMode::Phase);
    carrier.setModulationIndex(3.0);
    carrier.setModulationDepth(1.0);
    carrier.enableEnvelope();
    carrier.setADSR(0.001, 0.1, 0.5, 0.2);

    modulator.noteOn();
    carrier.noteOn();

    const size_t duration = 22050; // 500ms
    std::vector<double> modSignal(duration);
    std::vector<double> output(duration);

    for (size_t i = 0; i < duration; ++i)
        modSignal[i] = modulator.renderSample();

    carrier.setModulationInput(modSignal.data(), modSignal.size());
    for (size_t i = 0; i < duration; ++i)
        output[i] = carrier.renderSample();

    // Verify output quality
    double rms = TestHelpers::calculateRMS(output);
    EXPECT_GT(rms, 0.1);

    double maxVal = *std::max_element(output.begin(), output.end());
    double minVal = *std::min_element(output.begin(), output.end());
    EXPECT_LE(maxVal, 2.0);
    EXPECT_GE(minVal, -2.0);
}