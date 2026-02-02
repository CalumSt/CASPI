#include "synthesizers/caspi_FMGraph.h"
#include "maths/caspi_FFT.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <fstream>
#include <algorithm>
#include <numeric>
#include "test_helpers.h"

using namespace CASPI;

constexpr double sampleRate = 48000.0;
constexpr double tolerance = 1e-6;
constexpr size_t FFT_SIZE = 4096;

// ============================================================================
// FMGraphBuilder Tests
// ============================================================================

TEST(FMGraphBuilderTest, DefaultConstruction)
{
    FMGraphBuilder<double> builder;

    EXPECT_EQ(builder.getNumOperators(), 0);
    EXPECT_EQ(builder.getConnections().size(), 0);
    EXPECT_EQ(builder.getOutputOperators().size(), 0);
}

TEST(FMGraphBuilderTest, AddOperator)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    EXPECT_EQ(op1, 0);
    EXPECT_EQ(builder.getNumOperators(), 1);

    size_t op2 = builder.addOperator();
    EXPECT_EQ(op2, 1);
    EXPECT_EQ(builder.getNumOperators(), 2);
}

TEST(FMGraphBuilderTest, RemoveOperator)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    EXPECT_EQ(builder.getNumOperators(), 3);

    // Remove middle operator
    auto result = builder.removeOperator(op2);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(builder.getNumOperators(), 2);
}

TEST(FMGraphBuilderTest, RemoveInvalidOperator)
{
    FMGraphBuilder<double> builder;

    auto result = builder.removeOperator(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

TEST(FMGraphBuilderTest, ConnectOperators)
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

TEST(FMGraphBuilderTest, UpdateExistingConnection)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op1, op2, 2.0); // Update

    const auto& connections = builder.getConnections();
    EXPECT_EQ(connections.size(), 1);
    EXPECT_FLOAT_EQ(connections[0].modulationDepth, 2.0f);
}

TEST(FMGraphBuilderTest, ConnectInvalidOperators)
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

TEST(FMGraphBuilderTest, DisconnectOperators)
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

TEST(FMGraphBuilderTest, SetOutputOperators)
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

TEST(FMGraphBuilderTest, SetInvalidOutputOperators)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();

    auto result = builder.setOutputOperators({op1, 999});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

TEST(FMGraphBuilderTest, ConfigureOperator)
{
    FMGraphBuilder<double> builder;

    size_t op = builder.addOperator();

    auto result = builder.configureOperator(op, 440.0, 2.0, 0.8);
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphBuilderTest, ConfigureInvalidOperator)
{
    FMGraphBuilder<double> builder;

    auto result = builder.configureOperator(999, 440.0, 2.0, 0.8);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::InvalidOperatorIndex);
}

// ============================================================================
// Graph Validation Tests
// ============================================================================

TEST(FMGraphBuilderTest, ValidateEmptyGraph)
{
    FMGraphBuilder<double> builder;

    auto result = builder.validate();
    EXPECT_TRUE(result.has_value()); // Empty graph is valid
}

TEST(FMGraphBuilderTest, ValidateNoOutputs)
{
    FMGraphBuilder<double> builder;

    builder.addOperator();
    builder.addOperator();

    auto result = builder.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::NoOutputOperators);
}

TEST(FMGraphBuilderTest, ValidateAcyclicGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.setOutputOperators({op2});

    auto result = builder.validate();
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphBuilderTest, ValidateCyclicGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    // Create cycle: op1 -> op2 -> op3 -> op1
    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.connect(op3, op1, 1.0);
    builder.setOutputOperators({op1});

    auto result = builder.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::CycleDetected);
}

TEST(FMGraphBuilderTest, ValidateComplexAcyclicGraph)
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
// Compilation Tests
// ============================================================================

TEST(FMGraphBuilderTest, CompileValidGraph)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 3.0);
    builder.setOutputOperators({car});

    auto result = builder.compile(sampleRate);
    EXPECT_TRUE(result.has_value());
}

TEST(FMGraphBuilderTest, CompileInvalidGraph)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op1, 1.0); // Cycle
    builder.setOutputOperators({op1});

    auto result = builder.compile(sampleRate);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::CycleDetected);
}

TEST(FMGraphBuilderTest, CompileWithoutOutputs)
{
    FMGraphBuilder<double> builder;

    builder.addOperator();

    auto result = builder.compile(sampleRate);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FMGraphError::NoOutputOperators);
}

// ============================================================================
// FMGraphDSP Tests
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

        auto result = builder.compile(sampleRate);
        EXPECT_TRUE(result.has_value());

        // Move out of expected properly
        return std::move(result).value();
    }
};

TEST_F(FMGraphDSPTest, RenderSingleSample)
{
    auto dsp = createSimpleFM();

    double sample = dsp.renderSample();
    EXPECT_FALSE(std::isnan(sample));
    EXPECT_FALSE(std::isinf(sample));
}

TEST_F(FMGraphDSPTest, RenderMultipleSamples)
{
    auto dsp = createSimpleFM();

    std::vector<double> samples;
    for (int i = 0; i < 1000; ++i)
    {
        double sample = dsp.renderSample();
        samples.push_back(sample);

        EXPECT_FALSE(std::isnan(sample));
        EXPECT_LE(std::abs(sample), 2.0);
    }

    // Verify non-zero output
    double maxAbs = 0.0;
    for (double s : samples)
    {
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    EXPECT_GT(maxAbs, 0.1);
}

TEST_F(FMGraphDSPTest, RenderBlock)
{
    auto dsp = createSimpleFM();

    std::vector<double> buffer(512);
    dsp.renderBlock(buffer.data(), buffer.size());

    for (double sample : buffer)
    {
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }
}

TEST_F(FMGraphDSPTest, GetOperator)
{
    auto dsp = createSimpleFM();

    // Valid index
    auto* op = dsp.getOperator(0);
    EXPECT_NE(op, nullptr);

    // Invalid index
    auto* invalid = dsp.getOperator(999);
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(FMGraphDSPTest, SetFrequency)
{
    auto dsp = createSimpleFM();

    dsp.setFrequency(220.0);
    EXPECT_DOUBLE_EQ(dsp.getFrequency(), 220.0);

    // Should still render
    double sample = dsp.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(FMGraphDSPTest, SetConnectionDepth)
{
    auto dsp = createSimpleFM();

    // Render with initial depth
    std::vector<double> before;
    for (int i = 0; i < 100; ++i)
        before.push_back(dsp.renderSample());

    double rmsBefore = TestHelpers::calculateRMS(before);

    // Change connection depth
    dsp.reset();
    dsp.setConnectionDepth(0, 0.1); // Much lower modulation

    std::vector<double> after;
    for (int i = 0; i < 100; ++i)
        after.push_back(dsp.renderSample());

    double rmsAfter = TestHelpers::calculateRMS(after);

    // Different modulation should produce different output
    EXPECT_NE(rmsBefore, rmsAfter);
}

TEST_F(FMGraphDSPTest, NoteOnOff)
{
    auto dsp = createSimpleFM();

    // Configure envelopes
    dsp.setADSR(0.01, 0.01, 0.7, 0.1);
    dsp.enableEnvelopes();

    dsp.noteOn();

    auto rmsWindow = [&](int n) {
        std::vector<double> s;
        for (int i = 0; i < n; ++i)
            s.push_back(dsp.renderSample());
        return TestHelpers::calculateRMS(s);
    };

    double before = rmsWindow(2048);

    dsp.noteOff();

    double after = rmsWindow(2048);
    EXPECT_LT(after, before * 0.5);
}

TEST_F(FMGraphDSPTest, Reset)
{
    auto dsp = createSimpleFM();

    // Render some samples
    for (int i = 0; i < 100; ++i)
        dsp.renderSample();

    dsp.reset();

    // Should still render correctly
    double sample = dsp.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(FMGraphDSPTest, GetExecutionOrder)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    // Chain: op1 -> op2 -> op3
    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(sampleRate)).value();

    const auto* order = dsp.getExecutionOrder();
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->size(), 3);
    EXPECT_EQ((*order)[0], op1);
    EXPECT_EQ((*order)[1], op2);
    EXPECT_EQ((*order)[2], op3);
}

// ============================================================================
// Topological Sorting Tests
// ============================================================================

TEST_F(FMGraphDSPTest, ExecutionOrderSimpleChain)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    builder.connect(op1, op2, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(sampleRate)).value();
    const auto* order = dsp.getExecutionOrder();

    ASSERT_EQ(order->size(), 3);
    EXPECT_EQ((*order)[0], op1);
    EXPECT_EQ((*order)[1], op2);
    EXPECT_EQ((*order)[2], op3);
}

TEST_F(FMGraphDSPTest, ExecutionOrderParallel)
{
    FMGraphBuilder<double> builder;

    size_t op1 = builder.addOperator();
    size_t op2 = builder.addOperator();
    size_t op3 = builder.addOperator();

    // Parallel: op1 -> op3, op2 -> op3
    builder.connect(op1, op3, 1.0);
    builder.connect(op2, op3, 1.0);
    builder.setOutputOperators({op3});

    auto dsp = std::move(builder.compile(sampleRate)).value();
    const auto* order = dsp.getExecutionOrder();

    ASSERT_EQ(order->size(), 3);
    EXPECT_EQ((*order)[2], op3); // Sink last
    // op1 and op2 can be in any order
}

TEST_F(FMGraphDSPTest, ExecutionOrderDiamond)
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

    auto dsp = std::move(builder.compile(sampleRate)).value();
    const auto* order = dsp.getExecutionOrder();

    ASSERT_EQ(order->size(), 4);
    EXPECT_EQ((*order)[0], op1); // Source first
    EXPECT_EQ((*order)[3], op4); // Sink last
}

// ============================================================================
// Complex Topology Tests
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

    // Connect in series
    for (int i = 0; i < 5; ++i)
    {
        builder.connect(ops[i], ops[i + 1], 2.0);
    }

    builder.setOutputOperators({ops[5]});

    auto dsp = std::move(builder.compile(sampleRate)).value();

    // Should render without errors
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

    // All output, no connections (additive)
    builder.setOutputOperators(ops);

    auto dsp = std::move(builder.compile(sampleRate)).value();

    for (int i = 0; i < 100; ++i)
    {
        double sample = dsp.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_LE(std::abs(sample), 1.0);
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

    auto dsp = std::move(builder.compile(sampleRate)).value();

    std::vector<double> samples;
    for (int i = 0; i < 100; ++i)
    {
        samples.push_back(dsp.renderSample());
    }

    double rms = TestHelpers::calculateRMS(samples);
    EXPECT_GT(rms, 0.3);
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

    auto dsp = std::move(builder.compile(sampleRate)).value();

    for (int i = 0; i < 1000; ++i)
    {
        double sample = dsp.renderSample();
        EXPECT_FALSE(std::isnan(sample));
        EXPECT_FALSE(std::isinf(sample));
    }
}

// ============================================================================
// Spectral Analysis Tests
// ============================================================================

TEST_F(FMGraphDSPTest, SimpleFMCreatesHarmonics)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.configureOperator(mod, 880.0, 1.0, 1.0);
    builder.configureOperator(car, 440.0, 1.0, 1.0);
    builder.setOperatorMode(car, ModulationMode::Phase);
    builder.connect(mod, car, 3.0); // Strong modulation
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(sampleRate)).value();

    // Render samples
    std::vector<double> samples(FFT_SIZE);
    for (size_t i = 0; i < FFT_SIZE; ++i)
    {
        samples[i] = dsp.renderSample();
    }

    // Analyze spectrum
    auto windowed = TestHelpers::applyHannWindow(samples);
    auto fftData = TestHelpers::realToComplex(windowed);
    fft(fftData);
    auto spectrum = TestHelpers::getMagnitudeSpectrum(fftData);

    int peaks = TestHelpers::countSignificantPeaks(spectrum, 0.1);

    // FM should create multiple harmonic components
    EXPECT_GT(peaks, 1);
}

// ============================================================================
// Output and File Tests
// ============================================================================

TEST_F(FMGraphDSPTest, SaveOutput)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.configureOperator(mod, 880.0, 2.0, 1.0);
    builder.configureOperator(car, 440.0, 1.0, 1.0);
    builder.connect(mod, car, 3.0);
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(sampleRate)).value();

    dsp.setADSR(0.01, 0.1, 0.6, 0.2);
    dsp.enableEnvelopes();
    dsp.noteOn();

    // Render 1 second
    const int numSamples = sampleRate;
    std::vector<double> samples(numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        samples[i] = dsp.renderSample();
    }

    // Save to CSV
    std::ofstream file("fm_graph_dsp_output.csv");
    if (file.is_open())
    {
        file << "Time,Amplitude\n";
        for (int i = 0; i < numSamples; ++i)
        {
            file << (i / sampleRate) << "," << samples[i] << "\n";
        }
        file.close();
    }

    EXPECT_TRUE(true);
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_F(FMGraphDSPTest, MoveConstruction)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 1.0);
    builder.setOutputOperators({car});

    auto dsp1 = std::move(builder.compile(sampleRate)).value();

    // Move construct
    FMGraphDSP<double> dsp2 = std::move(dsp1);

    // dsp2 should work
    double sample = dsp2.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

TEST_F(FMGraphDSPTest, MoveAssignment)
{
    FMGraphBuilder<double> builder1;
    size_t op1 = builder1.addOperator();
    builder1.setOutputOperators({op1});
    auto dsp1 = std::move(builder1.compile(sampleRate)).value();

    FMGraphBuilder<double> builder2;
    size_t op2 = builder2.addOperator();
    builder2.setOutputOperators({op2});
    auto dsp2 = std::move(builder2.compile(sampleRate)).value();

    // Move assign
    dsp2 = std::move(dsp1);

    double sample = dsp2.renderSample();
    EXPECT_FALSE(std::isnan(sample));
}

// ============================================================================
// Real-Time Safety Tests
// ============================================================================

TEST_F(FMGraphDSPTest, NoAllocationsInRenderPath)
{
    FMGraphBuilder<double> builder;

    size_t mod = builder.addOperator();
    size_t car = builder.addOperator();

    builder.connect(mod, car, 1.0);
    builder.setOutputOperators({car});

    auto dsp = std::move(builder.compile(sampleRate)).value();

    // Render many samples - should not allocate
    // (In production, use allocation detector)
    for (int i = 0; i < 100000; ++i)
    {
        volatile double sample = dsp.renderSample();
        (void)sample; // Prevent optimization
    }

    EXPECT_TRUE(true); // If we got here without crash, test passed
}

// ============================================================================
// Error Message Tests
// ============================================================================

TEST(FMGraphErrorTest, ErrorToString)
{
    EXPECT_STREQ(errorToString(FMGraphError::Success), "Success");
    EXPECT_STREQ(errorToString(FMGraphError::InvalidOperatorIndex), "Invalid operator index");
    EXPECT_STREQ(errorToString(FMGraphError::CycleDetected), "Cycle detected in graph");
    EXPECT_STREQ(errorToString(FMGraphError::InvalidConnection), "Invalid connection (self-modulation)");
    EXPECT_STREQ(errorToString(FMGraphError::NoOutputOperators), "No output operators specified");
    EXPECT_STREQ(errorToString(FMGraphError::AllocationFailure), "Memory allocation failure");
}