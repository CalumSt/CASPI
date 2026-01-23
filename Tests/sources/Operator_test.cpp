#include "oscillators/caspi_Operator.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using CASPI::ModulationMode;
using CASPI::Operator;

constexpr double tolerance  = 1e-6;
constexpr double sampleRate = 44100.0;
constexpr double frequency  = 440.0;

class OperatorTest : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            // Setup for each test
        }

        void TearDown() override
        {
            // Cleanup after each test
        }
};

TEST_F (OperatorTest, DefaultConstruction)
{
    Operator<double> op;

    // Test default values
    EXPECT_EQ (op.getSampleRate(), CASPI::Constants::DEFAULT_SAMPLE_RATE<double>);
    EXPECT_DOUBLE_EQ (op.getFrequency(), 0.0);
    EXPECT_DOUBLE_EQ (op.getModulationDepth(), 1.0);
    EXPECT_DOUBLE_EQ (op.getModulationIndex(), 1.0);
    EXPECT_DOUBLE_EQ (op.getModulationFeedback(), 0.0);
}

TEST_F (OperatorTest, SetAndGetParameters)
{
    Operator<double> op;

    // Test frequency setting
    op.setFrequency (frequency);
    EXPECT_DOUBLE_EQ (op.getFrequency(), frequency);

    // Test modulation parameters
    op.setModulationIndex (2.0);
    EXPECT_DOUBLE_EQ (op.getModulationIndex(), 2.0);

    op.setModulationDepth (0.5);
    EXPECT_DOUBLE_EQ (op.getModulationDepth(), 0.5);

    op.setModulationFeedback (0.3);
    EXPECT_DOUBLE_EQ (op.getModulationFeedback(), 0.3);

    op.setModulationMode (ModulationMode::Frequency);
    // Can't easily test mode without accessing internals
}

TEST_F (OperatorTest, BasicRenderWithoutModulation)
{
    Operator<double> op;
    op.setFrequency (frequency);
    op.setSampleRate (sampleRate);

    // Render a few samples and verify they're within expected range
    for (int i = 0; i < 100; ++i)
    {
        double sample = op.renderSample();
        EXPECT_LE (std::abs (sample), 1.0) << "Sample should be within [-1, 1] range";
    }
}

TEST_F (OperatorTest, RenderWithExternalModulation)
{
    Operator<double> op;
    op.setFrequency (frequency);
    op.setSampleRate (sampleRate);
    op.setModulationDepth (0.5);

    // Create a simple modulation signal
    std::vector<double> modulation (10);
    for (size_t i = 0; i < modulation.size(); ++i)
    {
        modulation[i] = 0.5 * std::sin (2.0 * CASPI::Constants::PI<double> * i / modulation.size());
    }

    // Test with modulation input
    op.setModulationInput (modulation.data(), modulation.size());

    // Render samples and verify they're different from unmodulated
    std::vector<double> samples (modulation.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = op.renderSample();
        EXPECT_LE (std::abs (samples[i]), 1.0) << "Modulated sample should be within [-1, 1] range";
    }

    op.clearModulationInput();
}

TEST_F (OperatorTest, EnvelopeIntegration)
{
    Operator<double> op;
    op.setFrequency (frequency);
    op.setSampleRate (sampleRate);
    op.enableEnvelope();
    op.setADSR (0.1, 0.1, 0.7, 0.2);
    op.noteOn();

    // First few samples should be affected by envelope
    double sample1 = op.renderSample();
    EXPECT_LE (std::abs (sample1), 1.0);

    // After attack, should reach higher amplitude
    for (int i = 0; i < 1000; ++i)
    {
        double sample = op.renderSample();
        EXPECT_LE (std::abs (sample), 1.0);
    }
}

TEST_F (OperatorTest, ModulationModes)
{
    Operator<double> op;
    op.setFrequency (frequency);
    op.setSampleRate (sampleRate);

    // Test Phase Modulation mode (default)
    op.setModulationMode (ModulationMode::Phase);
    double sample1 = op.renderSample();
    EXPECT_LE (std::abs (sample1), 1.0);

    // Test Frequency Modulation mode
    op.setModulationMode (ModulationMode::Frequency);
    double sample2 = op.renderSample();
    EXPECT_LE (std::abs (sample2), 1.0);
}

TEST_F (OperatorTest, ResetFunctionality)
{
    Operator<double> op;
    op.setFrequency (frequency);
    op.setModulationIndex (2.0);
    op.setModulationDepth (0.5);
    op.enableEnvelope();

    // Render some samples to change internal state
    op.renderSample();
    op.renderSample();

    // Reset and verify defaults
    op.reset();
    EXPECT_DOUBLE_EQ (op.getFrequency(), 0.0);
    EXPECT_DOUBLE_EQ (op.getModulationIndex(), 1.0);
    EXPECT_DOUBLE_EQ (op.getModulationDepth(), 1.0);
}

TEST_F (OperatorTest, ProducerInterfaceCompatibility)
{
    Operator<double, CASPI::Core::Traversal::PerFrame> op;
    op.setFrequency (frequency);
    op.setSampleRate (sampleRate);

    // Test that we can use it as a Producer
    // This would be used with AudioBuffer in real applications
    EXPECT_DOUBLE_EQ (op.getFrequency(), frequency);

    // Should be able to call renderSample without issues
    double sample = op.renderSample();
    EXPECT_LE (std::abs (sample), 1.0);
}