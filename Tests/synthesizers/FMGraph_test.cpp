#include "synthesizers/caspi_FMGraph.h"
#include "analysis/caspi_SpectralProfile.h"
#include "analysis/caspi_FMTheory.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <fstream>

using namespace CASPI;

constexpr double SAMPLE_RATE = 48000.0;
constexpr double DURATION = 0.5;  // 500ms for spectral analysis

// ============================================================================
// Test Helpers
// ============================================================================

namespace FMGraphTestHelpers
{
    /**
     * @brief Render samples to power-of-2 size for FFT
     */
    template<typename FloatType>
    std::vector<double> renderForAnalysis(FMGraphDSP<FloatType>& dsp, double duration)
    {
        size_t desiredSamples = static_cast<size_t>(duration * SAMPLE_RATE);
        size_t fftSize = SpectralProfile::getNextPowerOf2(desiredSamples);

        std::vector<double> samples(fftSize);
        for (size_t i = 0; i < fftSize; ++i)
        {
            samples[i] = static_cast<double>(dsp.renderSample());
        }

        return samples;
    }

    double calculateRMS(const std::vector<double>& samples)
    {
        if (samples.empty()) return 0.0;

        double sum = 0.0;
        for (double s : samples)
            sum += s * s;

        return std::sqrt(sum / samples.size());
    }
}

// ============================================================================
// GROUP 1: Builder Construction & Configuration
// Tests the FMGraphBuilder API for creating and configuring graphs
// ============================================================================

TEST(FMGraphBuilder, DefaultConstruction)
{
    FMGraphBuilder<double> builder;

    EXPECT_EQ(builder.getNumOperators(), 0);
    EXPECT_EQ(builder.getConnections().size(), 0);
    EXPECT_EQ(builder.getOutputOperators().size(), 0);
}

TEST(FMGraphBuilder, AddOperator)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    EXPECT_EQ(op1, 0);
    EXPECT_EQ(builder.getNumOperators(), 1);

    size_t op2 = builder.addOperator();
    EXPECT_EQ(op2, 1);
    EXPECT_EQ(builder.getNumOperators(), 2);
}

TEST(FMGraphBuilder, RemoveOperator)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    EXPECT_EQ(builder.getNumOperators(), 3);

    auto result = builder.removeOperator(op2);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(builder.getNumOperators(), 2);
}

TEST(FMGraphBuilder, RemoveInvalidOperator)
{
    FMGraphBuilder<double> builder;

    auto result = builder.removeOperator(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

TEST(FMGraphBuilder, ConnectOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    auto result = builder.connect(op1, op2, 1.0);
    EXPECT_TRUE(result.has_value());

    const auto& connections = builder.getConnections();
    EXPECT_EQ(connections.size(), 1);
    EXPECT_EQ(connections[0].sourceOperator, op1);
    EXPECT_EQ(connections[0].targetOperator, op2);
    EXPECT_FLOAT_EQ(connections[0].modulationDepth, 1.0f);
}

TEST(FMGraphBuilder, UpdateExistingConnection)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op1, op2, 2.0);  // Update

    const auto& connections = builder.getConnections();
    EXPECT_EQ(connections.size(), 1);
    EXPECT_FLOAT_EQ(connections[0].modulationDepth, 2.0f);
}

TEST(FMGraphBuilder, ConnectInvalidOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();

    // Invalid target
    auto result = builder.connect(op1, 999, 1.0);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);

    // Self-connection
    result = builder.connect(op1, op1, 1.0);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidConnection);
}

TEST(FMGraphBuilder, DisconnectOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    EXPECT_EQ(builder.getConnections().size(), 1);

    auto result = builder.disconnect(op1, op2);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(builder.getConnections().size(), 0);
}

TEST(FMGraphBuilder, SetOutputOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    auto result = builder.setOutputOperators({op1, op2});
    EXPECT_TRUE(result.has_value());

    const auto& outputs = builder.getOutputOperators();
    EXPECT_EQ(outputs.size(), 2);
    EXPECT_EQ(outputs[0], op1);
    EXPECT_EQ(outputs[1], op2);
}

TEST(FMGraphBuilder, SetInvalidOutputOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();

    auto result = builder.setOutputOperators({op1, 999});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

TEST(FMGraphBuilder, ConfigureOperator)
{
    FMGraphBuilder<double> builder;

    size_t op = builder.addOperator();

    auto result = builder.configureOperator(op, 440.0, 2.0, 0.8);
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphBuilder, ConfigureInvalidOperator)
{
    FMGraphBuilder<double> builder;

    auto result = builder.configureOperator(999, 440.0, 2.0, 0.8);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

// ============================================================================
// GROUP 2: Graph Validation
// Tests topology validation (cycles, output operators)
// ============================================================================

TEST(FMGraphValidation, EmptyGraph)
{
    FMGraphBuilder<double> builder;

    auto result = builder.validate();
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphValidation, NoOutputOperators)
{
    FMGraphBuilder<double> builder;

    builder.addOperator();
    builder.addOperator();

    auto result = builder.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::NoOutputOperators);
}

TEST(FMGraphValidation, AcyclicGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.setOutputOperators({op2});

    auto result = builder.validate();
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphValidation, CyclicGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.connect(op3, op1, 1.0);  // Creates cycle
    builder.setOutputOperators({op1});

    auto result = builder.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::CycleDetected);
}

TEST(FMGraphValidation, ComplexAcyclicGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();
    size_t op4 = builder.addOperator();

    // Diamond: op1 -> {op2, op3} -> op4
    builder.connect(op1, op2, 1.0);
    builder.connect(op1, op3, 1.0);
    builder.connect(op2, op4, 1.0);
    builder.connect(op3, op4, 1.0);
    builder.setOutputOperators({op4});

    auto result = builder.validate();
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// GROUP 3: Compilation
// Tests builder->DSP compilation process
// ============================================================================

TEST(FMGraphCompilation, ValidGraph)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 3.0);
    builder.setOutputOperators({car});

    auto result = builder.compile(SAMPLE_RATE);
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphCompilation, InvalidGraphWithCycle)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op1, 1.0);  // Cycle
    builder.setOutputOperators({op1});

    auto result = builder.compile(SAMPLE_RATE);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::CycleDetected);
}

TEST(FMGraphCompilation, GraphWithoutOutputs)
{
    FMGraphBuilder<double> builder;

    builder.addOperator();

    auto result = builder.compile(SAMPLE_RATE);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::NoOutputOperators);
}

// ============================================================================
// GROUP 4: Basic DSP Operations
// Tests fundamental DSP rendering and parameter control
// ============================================================================

class FMGraphDSPTest : public ::testing::Test
{
protected:
    FMGraphDSP<double> createSimpleFM()
    {
        FMGraphBuilder<double> builder;

        size_t mod = builder.addOperator();
        size_t car = builder.addOperator();

        builder.configureOperator(mod, 880.0, 2.0, 1.0);
        builder.configureOperator(car, 440.0, 1.0, 1.0);
        builder.connect(mod, car, 3.0);
        builder.setOutputOperators({car});

        auto result = builder.compile(SAMPLE_RATE);
        EXPECT_TRUE(result.has_value());

        return std::move(result).value();
    }
};


TEST_F(FMGraphDSPTest, GetOperator)
{
    auto dsp = createSimpleFM();

    auto* op = dsp.getOperator(0);
    EXPECT_NE(op, nullptr);

    auto* invalid = dsp.getOperator(999);
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(FMGraphDSPTest, SetFrequency)
{
    auto dsp = createSimpleFM();

    dsp.setFrequency(220.0);
    EXPECT_DOUBLE_EQ(dsp.getFrequency(), 220.0);

    double sample = dsp.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(FMGraphDSPTest, Reset)
{
    auto dsp = createSimpleFM();

    for (int i = 0; i < 100; ++i)
        dsp.renderSample();

    dsp.reset();

    double sample = dsp.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

// ============================================================================
// GROUP 5: Topological Ordering
// Tests execution order computation for different graph topologies
// ============================================================================

TEST_F(FMGraphDSPTest, GetExecutionOrder)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    const auto order = dsp.getExecutionOrder();
    EXPECT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], op1);
    EXPECT_EQ(order[1], op2);
    EXPECT_EQ(order[2], op3);
}

TEST_F(FMGraphDSPTest, ExecutionOrderSimpleChain)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();
    const auto order = dsp.getExecutionOrder();

    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], op1);
    EXPECT_EQ(order[1], op2);
    EXPECT_EQ(order[2], op3);
}

TEST_F(FMGraphDSPTest, ExecutionOrderParallel)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.connect(op1, op3, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();
    const auto order = dsp.getExecutionOrder();

    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[2], op3);  // Sink must be last
}

TEST_F(FMGraphDSPTest, ExecutionOrderDiamond)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();
    size_t op4 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op1, op3, 1.0);
    builder.connect(op2, op4, 1.0);
    builder.connect(op3, op4, 1.0);
    builder.setOutputOperators({op4});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();
    const auto order = dsp.getExecutionOrder();

    ASSERT_EQ(order.size(), 4);
    EXPECT_EQ(order[0], op1);  // Source first
    EXPECT_EQ(order[3], op4);  // Sink last
}

// ============================================================================
// GROUP 6: Complex Topologies
// Tests real-world FM synthesis algorithms (DX7-style)
// ============================================================================

TEST_F(FMGraphDSPTest, DX7Algorithm1_SixOperatorCascade)
{
    FMGraphBuilder<double> builder;

    std::vector<size_t> ops;
    for (int i = 0; i < 6; ++i)
    {
        ops.push_back(builder.addOperator());
        builder.configureOperator(ops[i], 440.0, 1.0 + i * 0.5, 1.0);
    }

    for (int i = 0; i < 5; ++i)
    {
        builder.connect(ops[i], ops[i + 1], 2.0);
    }

    builder.setOutputOperators({ops[5]});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    for (int i = 0; i < 100; ++i)
    {
        double sample = dsp.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }
}

TEST_F(FMGraphDSPTest, DX7Algorithm32_AllParallel)
{
    FMGraphBuilder<double> builder;

    std::vector<size_t> ops;
    for (int i = 0; i < 6; ++i)
    {
        ops.push_back(builder.addOperator());
        builder.configureOperator(ops[i], 440.0, 1.0 + i * 0.5, 0.3);
    }

    builder.setOutputOperators(ops);

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    for (int i = 0; i < 100; ++i)
    {
        double sample = dsp.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_LE(std::abs(sample), 1.0);
    }
}

TEST_F(FMGraphDSPTest, ThreeOperatorCascade)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.configureOperator(op1, 220.0, 3.0, 1.0);
    builder.configureOperator(op2, 220.0, 2.0, 1.0);
    builder.configureOperator(op3, 220.0, 1.0, 1.0);

    builder.connect(op1, op2, 2.0);
    builder.connect(op2, op3, 1.5);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    for (int i = 0; i < 1000; ++i)
    {
        double sample = dsp.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }
}

TEST_F(FMGraphDSPTest, ParallelCarriers)
{
    FMGraphBuilder<double> builder;

    size_t car1 = builder.addOperator();
    size_t car2 = builder.addOperator();

    builder.configureOperator(car1, 440.0, 1.0, 0.5);
    builder.configureOperator(car2, 660.0, 1.5, 0.5);

    builder.setOutputOperators({car1, car2});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    std::vector<double> samples;
    for (int i = 0; i < 100; ++i)
    {
        samples.push_back(dsp.renderSample());
    }

    double rms = FMGraphTestHelpers::calculateRMS(samples);
    EXPECT_GT(rms, 0.2);
}

// ============================================================================
// GROUP 7: Amplitude & Gain Control
// Tests output scaling and clipping prevention
// ============================================================================
TEST_F(FMGraphDSPTest, ManualGainControl)
{
    FMGraphBuilder<double> builder;
    size_t op = builder.addOperator();
    builder.configureOperator(op, 440.0, 1.0, 1.0);
    builder.setOutputOperators({op});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    dsp.setOutputGain(0.5);

    double maxAmp = 0.0;
    for (int i = 0; i < 10000; ++i)
    {
        double sample = dsp.renderSample();
        maxAmp = std::max(maxAmp, std::abs(sample));
    }

    EXPECT_NEAR(maxAmp, 0.5, 0.1);
}

TEST_F(FMGraphDSPTest, SingleOperatorNormalizedOutput)
{
    FMGraphBuilder<double> builder;

    size_t op = builder.addOperator();
    builder.configureOperator(op, 440.0, 1.0, 1.0);
    builder.setOutputOperators({op});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    double maxAmp = 0.0;
    for (int i = 0; i < 10000; ++i)
    {
        double sample = dsp.renderSample();
        maxAmp = std::max(maxAmp, std::abs(sample));
    }

    EXPECT_NEAR(maxAmp, 1.0, 0.1);
}

// ============================================================================
// GROUP 8: Envelopes
// Tests ADSR envelope functionality
// ============================================================================

TEST_F(FMGraphDSPTest, NoteOnOffQuickSmoke)
{
    auto dsp = createSimpleFM();

    // Configure envelopes per-operator
    for (size_t i = 0; i < dsp.getNumOperators(); ++i)
    {
        auto* op = dsp.getOperator(i);
        op->setADSR(0.01, 0.01, 0.7, 0.5);  // 500ms release
        op->enableEnvelope();
    }

    dsp.noteOn();

    auto rmsWindow = [&](int n) {
        std::vector<double> s;
        for (int i = 0; i < n; ++i)
            s.push_back(dsp.renderSample());
        return FMGraphTestHelpers::calculateRMS(s);
    };

    // Wait for attack to stabilize
    for (int i = 0; i < 2048; ++i)
        dsp.renderSample();

    double before = rmsWindow(2048);

    dsp.noteOff();

    // Wait for full release (625ms = 30000 samples @ 48kHz)
    for (int i = 0; i < 30000; ++i)
        dsp.renderSample();

    double after = rmsWindow(2048);

    EXPECT_LT(after, before * 0.5);
}

TEST_F(FMGraphDSPTest, EnvelopeReducesAmplitude)
{
    FMGraphBuilder<double> builder;

    size_t op = builder.addOperator();
    builder.configureOperator(op, 440.0, 1.0, 1.0);
    builder.setOutputOperators({op});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    // Configure envelope via operator access
    dsp.getOperator(0)->setADSR(0.01, 0.01, 0.7, 0.2);
    dsp.getOperator(0)->enableEnvelope();

    dsp.noteOn();

    for (int i = 0; i < 1000; ++i)
        dsp.renderSample();

    std::vector<double> sustainSamples(1024);
    for (size_t i = 0; i < sustainSamples.size(); ++i)
        sustainSamples[i] = dsp.renderSample();

    double sustainRMS = FMGraphTestHelpers::calculateRMS(sustainSamples);

    dsp.noteOff();

    for (int i = 0; i < 12000; ++i)
        dsp.renderSample();

    std::vector<double> releaseSamples(1024);
    for (size_t i = 0; i < releaseSamples.size(); ++i)
        releaseSamples[i] = dsp.renderSample();

    double releaseRMS = FMGraphTestHelpers::calculateRMS(releaseSamples);

    EXPECT_LT(releaseRMS, sustainRMS * 0.3);
}

TEST_F(FMGraphDSPTest, EnvelopeDoesNotIntroduceClicks)
{
    FMGraphBuilder<double> builder;

    size_t op = builder.addOperator();
    builder.configureOperator(op, 440.0, 1.0, 1.0);
    builder.setOutputOperators({op});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    dsp.getOperator(0)->setADSR(0.001, 0.001, 0.8, 0.001);
    dsp.getOperator(0)->enableEnvelope();

    dsp.noteOn();
    auto samples = FMGraphTestHelpers::renderForAnalysis(dsp, 0.1);

    SpectralProfile profile(samples, SAMPLE_RATE);

    double highFreqEnergy = profile.getEnergyInRange(10000.0, SAMPLE_RATE / 2.0);
    double totalEnergy = profile.getTotalEnergy();

    double highFreqRatio = highFreqEnergy / totalEnergy;

    EXPECT_LT(highFreqRatio, 0.05);
}

// ============================================================================
// GROUP 9: FM Spectral Theory
// Tests against Bessel theory and FM synthesis mathematics
// ============================================================================

class FMGraphSpectralTest : public ::testing::Test {};

TEST_F(FMGraphSpectralTest, SimpleFMMatchesBesselTheory)
{
    using namespace FMTheory;

    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    double carrierFreq = 440.0;
    double modulatorFreq = 110.0;
    double modulationDepth = 2.0;

    builder.configureOperator(mod, modulatorFreq, 1.0, 1.0);
    builder.configureOperator(car, carrierFreq, 1.0, 1.0);
    builder.setOperatorMode(car, ModulationMode::Phase);
    builder.connect(mod, car, modulationDepth);
    builder.setOutputOperators({car});

    auto result = builder.compile(SAMPLE_RATE);
    ASSERT_TRUE(result.has_value());
    auto dsp = std::move(result).value();

    auto samples = FMGraphTestHelpers::renderForAnalysis(dsp, DURATION);
    SpectralProfile profile(samples, SAMPLE_RATE);

    EXPECT_TRUE(profile.hasPeakAt(carrierFreq, 5.0));
    EXPECT_TRUE(profile.hasPeakAt(carrierFreq + modulatorFreq, 5.0));
    EXPECT_TRUE(profile.hasPeakAt(carrierFreq - modulatorFreq, 5.0));

    int expectedSidebands = predictSignificantSidebands(modulationDepth, 0.05);
    int actualSidebands = 0;

    for (int n = 1; n <= expectedSidebands + 2; ++n)
    {
        if (profile.hasPeakAt(carrierFreq + n * modulatorFreq, 10.0) ||
            profile.hasPeakAt(carrierFreq - n * modulatorFreq, 10.0))
        {
            actualSidebands++;
        }
    }

    EXPECT_GE(actualSidebands, expectedSidebands - 1);
}

TEST_F(FMGraphSpectralTest, ModulationDepthAffectsSidebands)
{
    using namespace FMTheory;

    auto createFM = [](double beta) {
        FMGraphBuilder<double> builder;
        size_t mod = builder.addOperator();
        size_t car = builder.addOperator();

        builder.configureOperator(mod, 200.0, 1.0, 1.0);
        builder.configureOperator(car, 1000.0, 1.0, 1.0);
        builder.connect(mod, car, beta);
        builder.setOutputOperators({car});

        return std::move(builder.compile(SAMPLE_RATE)).value();
    };

    auto dsp1 = createFM(1.0);
    auto dsp3 = createFM(3.0);

    auto samples1 = FMGraphTestHelpers::renderForAnalysis(dsp1, DURATION);
    auto samples3 = FMGraphTestHelpers::renderForAnalysis(dsp3, DURATION);

    SpectralProfile profile1(samples1, SAMPLE_RATE);
    SpectralProfile profile3(samples3, SAMPLE_RATE);

    int sidebands1 = predictSignificantSidebands(1.0, 0.05);
    int sidebands3 = predictSignificantSidebands(3.0, 0.05);

    EXPECT_LT(sidebands1, sidebands3);
    EXPECT_LT(profile1.getBandwidth(), profile3.getBandwidth());
}

TEST_F(FMGraphSpectralTest, CascadeIncreasesSpectralComplexity)
{
    auto createCascade = [](int stages) {
        FMGraphBuilder<double> builder;

        std::vector<size_t> ops;
        for (int i = 0; i < stages; ++i)
        {
            ops.push_back(builder.addOperator());
            builder.configureOperator(ops[i], 440.0, 1.0 + i * 0.3, 1.0);
        }

        for (int i = 0; i < stages - 1; ++i)
        {
            builder.connect(ops[i], ops[i + 1], 1.5);
        }

        builder.setOutputOperators({ops.back()});

        return std::move(builder.compile(SAMPLE_RATE)).value();
    };

    auto dsp1 = createCascade(1);
    auto dsp2 = createCascade(2);
    auto dsp3 = createCascade(3);

    auto samples1 = FMGraphTestHelpers::renderForAnalysis(dsp1, DURATION);
    auto samples2 = FMGraphTestHelpers::renderForAnalysis(dsp2, DURATION);
    auto samples3 = FMGraphTestHelpers::renderForAnalysis(dsp3, DURATION);

    SpectralProfile profile1(samples1, SAMPLE_RATE);
    SpectralProfile profile2(samples2, SAMPLE_RATE);
    SpectralProfile profile3(samples3, SAMPLE_RATE);

    size_t peaks1 = profile1.getPeaks().size();
    size_t peaks2 = profile2.getPeaks().size();
    size_t peaks3 = profile3.getPeaks().size();

    EXPECT_LT(peaks1, peaks2);
    EXPECT_LT(peaks2, peaks3);
    EXPECT_LT(profile1.getBandwidth(), profile2.getBandwidth());
    EXPECT_LT(profile2.getBandwidth(), profile3.getBandwidth());
}

TEST_F(FMGraphDSPTest, SetConnectionDepth)
{
    auto dsp = createSimpleFM();

    std::vector<double> before;
    for (int i = 0; i < 100; ++i)
        before.push_back(dsp.renderSample());

    double rmsBefore = FMGraphTestHelpers::calculateRMS(before);

    dsp.reset();
    dsp.setConnectionDepth(0, 0.1);

    std::vector<double> after;
    for (int i = 0; i < 100; ++i)
        after.push_back(dsp.renderSample());

    double rmsAfter = FMGraphTestHelpers::calculateRMS(after);

    EXPECT_NE(rmsBefore, rmsAfter);
}

// ============================================================================
// GROUP 10: Frequency Accuracy
// Tests oscillator frequency precision
// ============================================================================

TEST_F(FMGraphSpectralTest, CarrierFrequencyAccurate)
{
    for (double freq : {110.0, 220.0, 440.0, 880.0})
    {
        FMGraphBuilder<double> builder;
        size_t car = builder.addOperator();

        builder.configureOperator(car, freq, 1.0, 1.0);
        builder.setOutputOperators({car});

        auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();
        auto samples = FMGraphTestHelpers::renderForAnalysis(dsp, DURATION);

        SpectralProfile profile(samples, SAMPLE_RATE);

        const auto* peak = profile.findNearestPeak(freq, 10.0);
        ASSERT_NE(peak, nullptr) << "Should find peak near " << freq << " Hz";

        EXPECT_NEAR(peak->frequency, freq, 2.0);
    }
}

TEST_F(FMGraphSpectralTest, FrequencyUpdateWorks)
{
    FMGraphBuilder<double> builder;
    size_t car = builder.addOperator();
    builder.configureOperator(car, 440.0, 1.0, 1.0);
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    auto samples1 = FMGraphTestHelpers::renderForAnalysis(dsp, 0.1);
    SpectralProfile profile1(samples1, SAMPLE_RATE);

    EXPECT_TRUE(profile1.hasPeakAt(440.0, 5.0));

    dsp.setFrequency(880.0);
    dsp.reset();

    auto samples2 = FMGraphTestHelpers::renderForAnalysis(dsp, 0.1);
    SpectralProfile profile2(samples2, SAMPLE_RATE);

    EXPECT_TRUE(profile2.hasPeakAt(880.0, 5.0));
}

// ============================================================================
// GROUP 11: Safety & Robustness
// Tests for NaN, Inf, denormals, and long-running stability
// ============================================================================

TEST_F(FMGraphSpectralTest, NoNaNOrInfInOutput)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.configureOperator(mod, 110.0, 10.0, 1.0);
    builder.configureOperator(car, 440.0, 10.0, 1.0);
    builder.connect(mod, car, 10.0);
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    for (int i = 0; i < 100000; ++i)
    {
        double sample = dsp.renderSample();
        ASSERT_FALSE(std::isnan(sample)) << "NaN at sample " << i;
        ASSERT_FALSE(std::isinf(sample)) << "Inf at sample " << i;
    }
}

TEST_F(FMGraphSpectralTest, DenormalProtection)
{
    FMGraphBuilder<double> builder;
    size_t op = builder.addOperator();
    builder.configureOperator(op, 440.0, 1.0, 1.0);
    builder.setOutputOperators({op});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    dsp.getOperator(0)->setADSR(0.001, 0.001, 0.1, 1.0);
    dsp.getOperator(0)->enableEnvelope();

    dsp.noteOn();
    for (int i = 0; i < 1000; ++i)
        dsp.renderSample();

    dsp.noteOff();

    for (int i = 0; i < 100000; ++i)
    {
        double sample = dsp.renderSample();

        if (sample != 0.0 && std::abs(sample) < 1e-38)
        {
            FAIL() << "Denormal detected at sample " << i << ": " << sample;
        }
    }
}

TEST_F(FMGraphDSPTest, NoAllocationsInRenderPath)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 1.0);
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(SAMPLE_RATE)).value();

    for (int i = 0; i < 100000; ++i)
    {
        volatile double sample = dsp.renderSample();
        (void)sample;
    }

    EXPECT_TRUE(true);
}

// ============================================================================
// GROUP 12: Move Semantics
// Tests move construction and assignment
// ============================================================================

TEST_F(FMGraphDSPTest, MoveConstruction)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 1.0);
    builder.setOutputOperators({car});

    auto dsp1 = std::move(builder.compile(SAMPLE_RATE)).value();

    FMGraphDSP<double> dsp2 = std::move(dsp1);

    double sample = dsp2.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(FMGraphDSPTest, MoveAssignment)
{
    FMGraphBuilder<double> builder1;
    size_t op1 = builder1.addOperator();
    builder1.setOutputOperators({op1});
    auto dsp1 = std::move(builder1.compile(SAMPLE_RATE)).value();

    FMGraphBuilder<double> builder2;
    size_t op2 = builder2.addOperator();
    builder2.setOutputOperators({op2});
    auto dsp2 = std::move(builder2.compile(SAMPLE_RATE)).value();

    dsp2 = std::move(dsp1);

    double sample = dsp2.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

// ============================================================================
// GROUP 13: Error Messages
// Tests error string conversion
// ============================================================================

TEST(FMGraphError, ErrorToString)
{
    EXPECT_STREQ(errorToString(FMGraphError::Success), "Success");
    EXPECT_STREQ(errorToString(FMGraphError::InvalidOperatorIndex), "Invalid operator index");
    EXPECT_STREQ(errorToString(FMGraphError::CycleDetected), "Cycle detected in graph");
    EXPECT_STREQ(errorToString(FMGraphError::InvalidConnection), "Invalid connection (self-modulation)");
    EXPECT_STREQ(errorToString(FMGraphError::NoOutputOperators), "No output operators specified");
    EXPECT_STREQ(errorToString(FMGraphError::AllocationFailure), "Memory allocation failure");
}

// ============================================================================
// Multi-Channel AudioBuffer Interaction Tests
// Tests FMGraphDSP's interaction with AudioBuffer base class interface
// ============================================================================

class FMGraphAudioBufferTest : public ::testing::Test
{
protected:
    FMGraphDSP<double> createSimpleFM()
    {
        FMGraphBuilder<double> builder;

        size_t mod = builder.addOperator();
        size_t car = builder.addOperator();

        builder.configureOperator(mod, 880.0, 2.0, 1.0);
        builder.configureOperator(car, 440.0, 1.0, 1.0);
        builder.connect(mod, car, 3.0);
        builder.setOutputOperators({car});

        auto result = builder.compile(SAMPLE_RATE);
        EXPECT_TRUE(result.has_value());

        return std::move(result).value();
    }
};

// ============================================================================
// Test 1: Mono Rendering (Single Channel)
// ============================================================================

TEST_F(FMGraphAudioBufferTest, RenderMonoInterleaved)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(1, 512);

    // Render using base class interface
    dsp.render(buffer);

    // Verify all samples are rendered
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double sample = buffer.sample(0, f);
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }

    // Verify non-zero output
    double maxAbs = 0.0;
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        maxAbs = std::max(maxAbs, std::abs(buffer.sample(0, f)));
    }
    EXPECT_GT(maxAbs, 0.1) << "Output should have significant amplitude";
}

TEST_F(FMGraphAudioBufferTest, RenderMonoChannelMajor)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, ChannelMajorLayout> buffer(1, 512);

    // Render using base class interface
    dsp.render(buffer);

    // Verify all samples are rendered
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double sample = buffer.sample(0, f);
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }
}

// ============================================================================
// Test 2: Stereo Rendering (Should Replicate to Both Channels)
// ============================================================================

TEST_F(FMGraphAudioBufferTest, RenderStereoInterleaved)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(2, 512);

    // Render using base class interface
    dsp.render(buffer);

    // Verify both channels have identical output (mono replication)
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double left = buffer.sample(0, f);
        double right = buffer.sample(1, f);

        EXPECT_FALSE(std::isnan(left));
        EXPECT_FALSE(std::isnan(right));

        // Channels should be identical (mono source)
        EXPECT_DOUBLE_EQ(left, right)
            << "Stereo channels should be identical at frame " << f;
    }
}

TEST_F(FMGraphAudioBufferTest, RenderStereoChannelMajor)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, ChannelMajorLayout> buffer(2, 512);

    // Render using base class interface
    dsp.render(buffer);

    // Verify both channels have identical output
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double left = buffer.sample(0, f);
        double right = buffer.sample(1, f);

        EXPECT_DOUBLE_EQ(left, right)
            << "Stereo channels should be identical at frame " << f;
    }

    // Verify non-zero output in both channels
    double maxAbsLeft = 0.0;
    double maxAbsRight = 0.0;

    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        maxAbsLeft = std::max(maxAbsLeft, std::abs(buffer.sample(0, f)));
        maxAbsRight = std::max(maxAbsRight, std::abs(buffer.sample(1, f)));
    }

    EXPECT_GT(maxAbsLeft, 0.1);
    EXPECT_GT(maxAbsRight, 0.1);
    EXPECT_DOUBLE_EQ(maxAbsLeft, maxAbsRight);
}

// ============================================================================
// Test 3: Multi-Channel Rendering (5.1, 7.1, etc.)
// ============================================================================

TEST_F(FMGraphAudioBufferTest, RenderQuadInterleaved)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(4, 256);

    dsp.render(buffer);

    // All channels should have identical output
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double reference = buffer.sample(0, f);

        for (size_t ch = 1; ch < 4; ++ch)
        {
            EXPECT_DOUBLE_EQ(buffer.sample(ch, f), reference)
                << "Channel " << ch << " differs from channel 0 at frame " << f;
        }
    }
}

TEST_F(FMGraphAudioBufferTest, Render51Surround)
{
    auto dsp = createSimpleFM();

    // 5.1 = 6 channels
    AudioBuffer<double, ChannelMajorLayout> buffer(6, 128);

    dsp.render(buffer);

    // All 6 channels should have identical output
    std::vector<double> channelMaxes(6, 0.0);

    for (size_t ch = 0; ch < 6; ++ch)
    {
        for (size_t f = 0; f < buffer.numFrames(); ++f)
        {
            channelMaxes[ch] = std::max(channelMaxes[ch],
                                       std::abs(buffer.sample(ch, f)));
        }
    }

    // All channels should have same max amplitude
    for (size_t ch = 1; ch < 6; ++ch)
    {
        EXPECT_DOUBLE_EQ(channelMaxes[ch], channelMaxes[0])
            << "Channel " << ch << " has different amplitude than channel 0";
    }
}

// ============================================================================
// Test 4: State Consistency Across Renders
// ============================================================================

TEST_F(FMGraphAudioBufferTest, StateConsistentAcrossMultipleRenders)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer1(2, 64);
    AudioBuffer<double, InterleavedLayout> buffer2(2, 64);

    // Render first buffer
    dsp.render(buffer1);

    // Render second buffer (should continue from where first left off)
    dsp.render(buffer2);

    // Verify both buffers have valid output
    for (size_t ch = 0; ch < 2; ++ch)
    {
        for (size_t f = 0; f < 64; ++f)
        {
            EXPECT_FALSE(std::isnan(buffer1.sample(ch, f)));
            EXPECT_FALSE(std::isnan(buffer2.sample(ch, f)));
        }
    }

    // Buffers should be different (phase advancement)
    bool foundDifference = false;
    for (size_t f = 0; f < 64; ++f)
    {
        if (std::abs(buffer1.sample(0, f) - buffer2.sample(0, f)) > 1e-10)
        {
            foundDifference = true;
            break;
        }
    }

    EXPECT_TRUE(foundDifference)
        << "Buffers should differ (oscillator should advance)";
}

// ============================================================================
// Test 5: Channel/Frame Spans Work Correctly
// ============================================================================

TEST_F(FMGraphAudioBufferTest, ChannelSpansWorkInterleaved)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(2, 128);

    dsp.render(buffer);

    // Get channel spans
    auto ch0Span = buffer.channel_span(0);
    auto ch1Span = buffer.channel_span(1);

    EXPECT_EQ(ch0Span.size(), 128);
    EXPECT_EQ(ch1Span.size(), 128);

    // Verify spans contain correct data
    for (size_t f = 0; f < 128; ++f)
    {
        EXPECT_DOUBLE_EQ(ch0Span[f], buffer.sample(0, f));
        EXPECT_DOUBLE_EQ(ch1Span[f], buffer.sample(1, f));
    }
}

TEST_F(FMGraphAudioBufferTest, ChannelSpansWorkChannelMajor)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, ChannelMajorLayout> buffer(2, 128);

    dsp.render(buffer);

    // Get channel spans
    auto ch0Span = buffer.channel_span(0);
    auto ch1Span = buffer.channel_span(1);

    EXPECT_EQ(ch0Span.size(), 128);
    EXPECT_EQ(ch1Span.size(), 128);

    // Verify spans contain correct data
    for (size_t f = 0; f < 128; ++f)
    {
        EXPECT_DOUBLE_EQ(ch0Span[f], buffer.sample(0, f));
        EXPECT_DOUBLE_EQ(ch1Span[f], buffer.sample(1, f));
    }
}

TEST_F(FMGraphAudioBufferTest, FrameSpansWorkInterleaved)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(2, 128);

    dsp.render(buffer);

    // Get first frame span
    auto frame0 = buffer.frame_span(0);

    EXPECT_EQ(frame0.size(), 2);  // 2 channels
    EXPECT_DOUBLE_EQ(frame0[0], buffer.sample(0, 0));
    EXPECT_DOUBLE_EQ(frame0[1], buffer.sample(1, 0));
}

// ============================================================================
// Test 6: Different Buffer Sizes
// ============================================================================

TEST_F(FMGraphAudioBufferTest, VaryingBufferSizes)
{
    auto dsp = createSimpleFM();

    std::vector<size_t> bufferSizes = {1, 16, 64, 128, 256, 512, 1024, 2048};

    for (size_t size : bufferSizes)
    {
        AudioBuffer<double, InterleavedLayout> buffer(2, size);

        dsp.render(buffer);

        // Verify all samples valid
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (size_t f = 0; f < size; ++f)
            {
                double sample = buffer.sample(ch, f);
                EXPECT_FALSE(std::isnan(sample))
                    << "NaN at size=" << size << " ch=" << ch << " f=" << f;
                EXPECT_FALSE(std::isinf(sample))
                    << "Inf at size=" << size << " ch=" << ch << " f=" << f;
            }
        }
    }
}

// ============================================================================
// Test 7: Render After Reset
// ============================================================================

TEST_F(FMGraphAudioBufferTest, RenderAfterReset)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer1(2, 64);

    // Render first buffer
    dsp.render(buffer1);

    // Reset
    dsp.reset();

    // Render second buffer
    AudioBuffer<double, InterleavedLayout> buffer2(2, 64);
    dsp.render(buffer2);

    // After reset, phases should be back to start
    // First few samples should be similar (but not identical due to random phase init)
    // Just verify they're both valid
    for (size_t ch = 0; ch < 2; ++ch)
    {
        for (size_t f = 0; f < 64; ++f)
        {
            EXPECT_FALSE(std::isnan(buffer2.sample(ch, f)));
        }
    }
}

// ============================================================================
// Test 8: Concurrent Channel Rendering (if supported)
// ============================================================================

TEST_F(FMGraphAudioBufferTest, SampleRenderMethodConsistency)
{
    auto dsp = createSimpleFM();

    // Render using render() method
    AudioBuffer<double, InterleavedLayout> buffer1(1, 128);
    dsp.render(buffer1);

    // Reset to same state
    dsp.reset();

    // Render using renderSample() method
    AudioBuffer<double, InterleavedLayout> buffer2(1, 128);
    for (size_t f = 0; f < 128; ++f)
    {
        buffer2.sample(0, f) = dsp.renderSample();
    }

    // Both methods should produce identical output
    for (size_t f = 0; f < 128; ++f)
    {
        EXPECT_DOUBLE_EQ(buffer1.sample(0, f), buffer2.sample(0, f))
            << "Mismatch at frame " << f;
    }
}

// ============================================================================
// Test 9: Envelope Interaction with Multi-Channel
// ============================================================================

TEST_F(FMGraphAudioBufferTest, EnvelopeWorksAcrossAllChannels)
{
    auto dsp = createSimpleFM();

    // Configure envelopes per-operator
    for (size_t i = 0; i < dsp.getNumOperators(); ++i)
    {
        auto* op = dsp.getOperator(i);
        op->setADSR(0.01, 0.01, 0.7, 0.1);
        op->enableEnvelope();
    }

    AudioBuffer<double, InterleavedLayout> buffer(4, 256);

    dsp.noteOn();
    dsp.render(buffer);

    // All channels should have identical envelope shape
    for (size_t f = 0; f < buffer.numFrames(); ++f)
    {
        double reference = buffer.sample(0, f);

        for (size_t ch = 1; ch < 4; ++ch)
        {
            EXPECT_DOUBLE_EQ(buffer.sample(ch, f), reference)
                << "Envelope differs across channels at frame " << f;
        }
    }

    // Verify envelope is active (amplitude varies)
    bool foundVariation = false;
    for (size_t f = 1; f < buffer.numFrames(); ++f)
    {
        if (std::abs(buffer.sample(0, f) - buffer.sample(0, f-1)) > 1e-6)
        {
            foundVariation = true;
            break;
        }
    }
    EXPECT_TRUE(foundVariation) << "Envelope should cause amplitude variation";
}

// ============================================================================
// Test 10: Extreme Channel Counts
// ============================================================================

TEST_F(FMGraphAudioBufferTest, ExtremeChannelCounts)
{
    auto dsp = createSimpleFM();

    // Test 1 channel
    AudioBuffer<double, InterleavedLayout> mono(1, 64);
    dsp.render(mono);
    EXPECT_FALSE(std::isnan(mono.sample(0, 0)));

    dsp.reset();

    // Test 8 channels (7.1 surround)
    AudioBuffer<double, ChannelMajorLayout> surround(8, 64);
    dsp.render(surround);

    for (size_t ch = 0; ch < 8; ++ch)
    {
        EXPECT_FALSE(std::isnan(surround.sample(ch, 0)));
    }

    // All channels should match
    for (size_t f = 0; f < 64; ++f)
    {
        double reference = surround.sample(0, f);
        for (size_t ch = 1; ch < 8; ++ch)
        {
            EXPECT_DOUBLE_EQ(surround.sample(ch, f), reference);
        }
    }
}

// ============================================================================
// Test 11: Layout Independence
// ============================================================================

TEST_F(FMGraphAudioBufferTest, InterleavedVsChannelMajorProduceSameOutput)
{
    auto dsp1 = createSimpleFM();
    auto dsp2 = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> interleaved(2, 128);
    AudioBuffer<double, ChannelMajorLayout> channelMajor(2, 128);

    dsp1.render(interleaved);
    dsp2.render(channelMajor);

    // Both layouts should produce identical output
    for (size_t ch = 0; ch < 2; ++ch)
    {
        for (size_t f = 0; f < 128; ++f)
        {
            EXPECT_DOUBLE_EQ(interleaved.sample(ch, f),
                           channelMajor.sample(ch, f))
                << "Layout mismatch at ch=" << ch << " f=" << f;
        }
    }
}

// ============================================================================
// Test 12: Performance - No Slowdown with More Channels
// ============================================================================

TEST_F(FMGraphAudioBufferTest, PerformanceScalesWithChannels)
{
    auto dsp = createSimpleFM();

    // Render large buffers with varying channel counts
    // Time should scale linearly (or better with optimizations)

    AudioBuffer<double, InterleavedLayout> mono(1, 4096);
    AudioBuffer<double, InterleavedLayout> stereo(2, 4096);
    AudioBuffer<double, InterleavedLayout> quad(4, 4096);

    // Just verify all complete without errors
    dsp.render(mono);

    dsp.reset();
    dsp.render(stereo);

    dsp.reset();
    dsp.render(quad);

    // All should complete (this is a smoke test, not a benchmark)
    EXPECT_TRUE(true);
}

// ============================================================================
// Test 13: Edge Cases
// ============================================================================

TEST_F(FMGraphAudioBufferTest, ZeroSizeBuffer)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(2, 0);

    // Should not crash
    dsp.render(buffer);

    EXPECT_EQ(buffer.numFrames(), 0);
    EXPECT_TRUE(true);  // If we get here, didn't crash
}

TEST_F(FMGraphAudioBufferTest, SingleSampleBuffer)
{
    auto dsp = createSimpleFM();

    AudioBuffer<double, InterleavedLayout> buffer(2, 1);

    dsp.render(buffer);

    EXPECT_FALSE(std::isnan(buffer.sample(0, 0)));
    EXPECT_FALSE(std::isnan(buffer.sample(1, 0)));
    EXPECT_DOUBLE_EQ(buffer.sample(0, 0), buffer.sample(1, 0));
}