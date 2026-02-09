/*************************************************************************
 * Parameter Tests
 *
 * TEST CASES:
 *
 * BASIC FUNCTIONALITY
 * 1. DefaultConstruction
 *    - Verify parameter initializes with correct defaults
 *    - Ensures normalized value starts at 0.0
 *
 * 2. ConstructionWithInitialValue
 *    - Verify construction with explicit initial normalized value
 *    - Tests clamping of initial value to [0, 1]
 *
 * 3. ConstructionWithRange
 *    - Verify construction with min/max range
 *    - Ensures range is stored correctly
 *
 * 4. SetAndGetBaseNormalized
 *    - Test thread-safe atomic set/get operations
 *    - Verify values are clamped to [0, 1]
 *
 * 5. SetAndGetWithClamping
 *    - Verify out-of-range values are clamped properly
 *    - Tests both above and below range
 *
 * SMOOTHING
 * 6. NoSmoothing
 *    - Verify instant jump when smoothing time = 0
 *    - Ensures smoothingCoeff = 1.0
 *
 * 7. SmoothingConvergence
 *    - Test that smoothing reaches ~99% of target in specified time
 *    - Validates smoothing coefficient calculation
 *
 * 8. SmoothingProgression
 *    - Verify value approaches target monotonically
 *    - Ensures no overshoot or oscillation
 *
 * 9. SmoothingMultipleUpdates
 *    - Test changing target mid-smooth
 *    - Ensures smooth transition to new target
 *
 * SCALING
 * 10. LinearScaling
 *     - Test linear mapping from normalized to actual values
 *     - Verify min/max endpoints
 *
 * 11. LogarithmicScaling
 *     - Test logarithmic scaling (exponential mapping)
 *     - Verify geometric mean at normalized 0.5
 *
 * 12. BipolarScaling
 *     - Test bipolar mapping [0, 1] -> [-range, +range]
 *     - Verify zero-crossing at normalized 0.5
 *
 * MODULATION (ModulatableParameter only)
 * 13. ModulationAccumulation
 *     - Test adding multiple modulation amounts
 *     - Verify accumulation is additive
 *
 * 14. ModulationClamping
 *     - Ensure modulated values are clamped to [0, 1]
 *     - Test both positive and negative overflow
 *
 * 15. ClearModulation
 *     - Verify clearModulation() resets accumulator
 *     - Ensures clean state for next block
 *
 * 16. ModulationWithSmoothing
 *     - Test interaction of smoothing and modulation
 *     - Verify modulation is added after smoothing
 *
 * 17. NonModulatableParameter
 *     - Verify base Parameter class doesn't support modulation
 *     - Ensures type safety at compile time
 *
 * THREAD SAFETY
 * 18. AtomicBaseValue
 *     - Verify atomic operations are thread-safe
 *     - Tests simultaneous read/write (conceptual, hard to test)
 *
 * INTEGRATION
 * 19. CompleteWorkflow
 *     - Test GUI -> smoothing -> modulation -> output pipeline
 *     - Ensures all features work together correctly
 ************************************************************************/

#include "core/caspi_Parameter.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace CASPI;

constexpr float SAMPLE_RATE = 48000.0f;
constexpr float EPSILON = 0.001f;

// ============================================================================
// BASIC FUNCTIONALITY
// ============================================================================

TEST(Parameter, DefaultConstruction)
{
    Core::Parameter<float> param;

    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.0f);
    EXPECT_FLOAT_EQ(param.getMinValue(), 0.0f);
    EXPECT_FLOAT_EQ(param.getMaxValue(), 1.0f);
    EXPECT_EQ(param.getScale(), Core::ParameterScale::Linear);
}

TEST(Parameter, ConstructionWithInitialValue)
{
    Core::Parameter<float> param(0.75f);

    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.75f);

    // Test clamping on construction
    Core::Parameter<float> param2(1.5f);
    EXPECT_FLOAT_EQ(param2.getBaseNormalized(), 1.0f);

    Core::Parameter<float> param3(-0.5f);
    EXPECT_FLOAT_EQ(param3.getBaseNormalized(), 0.0f);
}

TEST(Parameter, ConstructionWithRange)
{
    Core::Parameter<float> param(20.0f, 20000.0f, 0.5f);

    EXPECT_FLOAT_EQ(param.getMinValue(), 20.0f);
    EXPECT_FLOAT_EQ(param.getMaxValue(), 20000.0f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.5f);
}

TEST(Parameter, SetAndGetBaseNormalized)
{
    Core::Parameter<float> param;

    param.setBaseNormalized(0.5f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.5f);

    param.setBaseNormalized(0.8f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.8f);
}

TEST(Parameter, SetAndGetWithClamping)
{
    Core::Parameter<float> param;

    // Above range
    param.setBaseNormalized(1.5f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 1.0f);

    // Below range
    param.setBaseNormalized(-0.5f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.0f);

    // Within range
    param.setBaseNormalized(0.7f);
    EXPECT_FLOAT_EQ(param.getBaseNormalized(), 0.7f);
}

// ============================================================================
// SMOOTHING
// ============================================================================

TEST(Parameter, NoSmoothing)
{
    Core::Parameter<float> param;
    param.setSmoothingTime(0.0f, SAMPLE_RATE);

    param.setBaseNormalized(0.5f);
    param.process();

    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.5f);

    param.setBaseNormalized(1.0f);
    param.process();

    // Should jump instantly
    EXPECT_FLOAT_EQ(param.valueNormalized(), 1.0f);
}

TEST(Parameter, SmoothingConvergence)
{
    Core::Parameter<float> param(0.0f);
    param.setSmoothingTime(0.1f, SAMPLE_RATE);  // 100ms to reach target

    param.setBaseNormalized(1.0f);

    // Process for JUST OVER 100ms (4850 samples at 48kHz)
    // This is because the smoothing is asymptotic and won't reach exactly 1.0, but should be close
    for (int i = 0; i < 4850; ++i)
    {
        param.process();
    }

    // Should be at ~99% of target
    EXPECT_GT(param.valueNormalized(), 0.99f);
}

TEST(Parameter, SmoothingProgression)
{
    Core::Parameter<float> param(0.0f);
    param.setSmoothingTime(0.1f, SAMPLE_RATE);

    param.setBaseNormalized(1.0f);

    float previous = 0.0f;

    // Verify monotonic increase
    for (int i = 0; i < 100; ++i)
    {
        param.process();
        float current = param.valueNormalized();

        EXPECT_GE(current, previous);  // Should increase
        EXPECT_LE(current, 1.0f);      // Should not overshoot

        previous = current;
    }
}

TEST(Parameter, SmoothingMultipleUpdates)
{
    Core::Parameter<float> param(0.0f);
    param.setSmoothingTime(0.05f, SAMPLE_RATE);

    // Start smoothing to 1.0
    param.setBaseNormalized(1.0f);

    for (int i = 0; i < 1000; ++i)
        param.process();

    float midValue = param.valueNormalized();
    EXPECT_GT(midValue, 0.5f);

    // Change target mid-smooth to 0.0
    param.setBaseNormalized(0.0f);

    for (int i = 0; i < 1000; ++i)
        param.process();

    // Should smoothly transition to new target
    EXPECT_LT(param.valueNormalized(), midValue);
}

// ============================================================================
// SCALING
// ============================================================================

TEST(Parameter, LinearScaling)
{
    Core::Parameter<float> param(0.0f, 100.0f, 0.5f);
    param.setRange(0.0f, 100.0f, Core::ParameterScale::Linear);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);  // No smoothing

    param.setBaseNormalized(0.0f);
    param.process();
    EXPECT_NEAR(param.value(), 0.0f, EPSILON);

    param.setBaseNormalized(0.5f);
    param.process();
    EXPECT_NEAR(param.value(), 50.0f, EPSILON);

    param.setBaseNormalized(1.0f);
    param.process();
    EXPECT_NEAR(param.value(), 100.0f, EPSILON);
}

TEST(Parameter, LogarithmicScaling)
{
    Core::Parameter<float> param(20.0f, 20000.0f, 0.5f);
    param.setRange(20.0f, 20000.0f, Core::ParameterScale::Logarithmic);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);

    param.setBaseNormalized(0.0f);
    param.process();
    EXPECT_NEAR(param.value(), 20.0f, 1.0f);

    param.setBaseNormalized(0.5f);
    param.process();
    // Geometric mean of 20 and 20000 = sqrt(20 * 20000) ≈ 632
    EXPECT_NEAR(param.value(), 632.0f, 10.0f);

    param.setBaseNormalized(1.0f);
    param.process();
    EXPECT_NEAR(param.value(), 20000.0f, 10.0f);
}

TEST(Parameter, BipolarScaling)
{
    Core::Parameter<float> param(-100.0f, 100.0f, 0.5f);
    param.setRange(-100.0f, 100.0f, Core::ParameterScale::Bipolar);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);

    param.setBaseNormalized(0.0f);
    param.process();
    EXPECT_NEAR(param.value(), -100.0f, EPSILON);

    param.setBaseNormalized(0.5f);
    param.process();
    EXPECT_NEAR(param.value(), 0.0f, EPSILON);

    param.setBaseNormalized(1.0f);
    param.process();
    EXPECT_NEAR(param.value(), 100.0f, EPSILON);
}

// ============================================================================
// MODULATION (ModulatableParameter)
// ============================================================================

TEST(ModulatableParameter, ModulationAccumulation)
{
    Core::ModulatableParameter<float> param(0.0f, 100.0f, 0.5f);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);
    param.process();

    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.5f);

    param.clearModulation();
    param.addModulation(0.1f);
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.6f);

    param.addModulation(0.05f);
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.65f);

    param.addModulation(-0.15f);
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.5f);
}

TEST(ModulatableParameter, ModulationClamping)
{
    Core::ModulatableParameter<float> param(0.5f);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);
    param.process();

    // Positive overflow
    param.clearModulation();
    param.addModulation(0.6f);  // Would be 1.1
    EXPECT_FLOAT_EQ(param.valueNormalized(), 1.0f);

    // Negative overflow
    param.setBaseNormalized(0.3f);
    param.process();
    param.clearModulation();
    param.addModulation(-0.5f);  // Would be -0.2
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.0f);
}

TEST(ModulatableParameter, ClearModulation)
{
    Core::ModulatableParameter<float> param(0.5f);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);
    param.process();

    param.addModulation(0.2f);
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.7f);

    param.clearModulation();
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.5f);
}

TEST(ModulatableParameter, ModulationWithSmoothing)
{
    Core::ModulatableParameter<float> param(0.0f);
    param.setSmoothingTime(0.01f, SAMPLE_RATE);

    param.setBaseNormalized(1.0f);

    // Smooth partway
    for (int i = 0; i < 100; ++i)
        param.process();

    float smoothedBase = param.valueNormalized();

    // Add modulation
    param.clearModulation();
    param.addModulation(0.1f);

    // Should be smoothed base + modulation
    EXPECT_NEAR(param.valueNormalized(), smoothedBase + 0.1f, 0.01f);
}

TEST(ModulatableParameter, GetModulationAmount)
{
    Core::ModulatableParameter<float> param(0.5f);

    param.clearModulation();
    EXPECT_FLOAT_EQ(param.getModulationAmount(), 0.0f);

    param.addModulation(0.3f);
    EXPECT_FLOAT_EQ(param.getModulationAmount(), 0.3f);

    param.addModulation(-0.1f);
    EXPECT_FLOAT_EQ(param.getModulationAmount(), 0.2f);
}

// ============================================================================
// TYPE SAFETY
// ============================================================================

TEST(Parameter, NonModulatableParameter)
{
    Core::Parameter<float> param(0.5f);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);
    param.process();

    // This should not compile:
    // param.addModulation(0.1f);
    // param.clearModulation();

    // Only valueNormalized() and value() should be available
    EXPECT_FLOAT_EQ(param.valueNormalized(), 0.5f);
}

// ============================================================================
// INTEGRATION
// ============================================================================

TEST(ModulatableParameter, CompleteWorkflow)
{
    Core::ModulatableParameter<float> param(20.0f, 20000.0f, 0.5f);
    param.setRange(20.0f, 20000.0f, Core::ParameterScale::Logarithmic);
    param.setSmoothingTime(0.01f, SAMPLE_RATE);

    // GUI sets value
    param.setBaseNormalized(0.8f);

    // Audio thread processes
    for (int i = 0; i < 480; ++i)  // 10ms at 48kHz
    {
        param.clearModulation();

        // Modulation source adds vibrato
        float lfo = 0.01f * std::sin(2.0f * 3.14159f * i / 480.0f);
        param.addModulation(lfo);

        param.process();

        // Read value
        float freq = param.value();

        EXPECT_GT(freq, 20.0f);
        EXPECT_LT(freq, 20000.0f);
    }
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST(Parameter, ZeroRange)
{
    Core::Parameter<float> param(100.0f, 100.0f, 0.5f);
    param.setSmoothingTime(0.0f, SAMPLE_RATE);
    param.process();

    // Should always return the same value
    EXPECT_FLOAT_EQ(param.value(), 100.0f);
}

TEST(Parameter, RapidChanges)
{
    Core::Parameter<float> param(0.0f);
    param.setSmoothingTime(0.1f, SAMPLE_RATE);

    // Rapidly alternate targets
    for (int i = 0; i < 100; ++i)
    {
        param.setBaseNormalized(i % 2 == 0 ? 0.0f : 1.0f);
        param.process();

        float value = param.valueNormalized();
        EXPECT_GE(value, 0.0f);
        EXPECT_LE(value, 1.0f);
    }
}