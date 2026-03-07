// caspi_Parameter_test.cpp
#include <gtest/gtest.h>
#include "core/caspi_Parameter.h"

using namespace CASPI::Core;

// ---- Parameter ----

TEST(ParameterTest, DefaultNormalisedIsZero) {
    Parameter<float> p;
    EXPECT_FLOAT_EQ(p.getBaseNormalised(), 0.f);
}

TEST(ParameterTest, SetBaseNormalisedClampsToUnitRange) {
    Parameter<float> p;
    p.setBaseNormalised(1.5f);
    EXPECT_FLOAT_EQ(p.getBaseNormalised(), 1.f);
    p.setBaseNormalised(-0.5f);
    EXPECT_FLOAT_EQ(p.getBaseNormalised(), 0.f);
}

TEST(ParameterTest, LinearScaleMapsBoundaries) {
    Parameter<float> p(0.f, 100.f, 0.f);
    p.setBaseNormalised(0.f);
    p.process(); // converge smoother
    EXPECT_FLOAT_EQ(p.value(), 0.f);
    p.setBaseNormalised(1.f);
    // Run enough iterations for smoother to converge (coeff = 1 by default)
    p.process();
    EXPECT_FLOAT_EQ(p.value(), 100.f);
}

TEST(ParameterTest, LogarithmicScaleMidpointIsGeometricMean) {
    Parameter<float> p(1.f, 100.f, 0.f);
    p.setRange(1.f, 100.f, ParameterScale::Logarithmic);
    p.setBaseNormalised(0.5f);
    p.process();
    // Geometric mean of 1 and 100 is 10
    EXPECT_NEAR(p.value(), 10.f, 0.01f);
}

TEST(ParameterTest, SmoothingConvergesWithinExpectedTime) {
    Parameter<float> p;
    constexpr float sampleRate = 48000.f;
    constexpr float smoothTime = 0.01f; // 10ms
    p.setSmoothingTime(smoothTime, sampleRate);
    p.setBaseNormalised(1.f);
    // Run 99% convergence: timeSeconds samples
    int samples = static_cast<int>(smoothTime * sampleRate);
    for (int i = 0; i < samples; ++i) p.process();
    EXPECT_GT(p.valueNormalised(), 0.989f);
}

TEST(ParameterTest, NoSmoothingWithZeroTime) {
    Parameter<float> p;
    p.setSmoothingTime(0.f, 48000.f);
    p.setBaseNormalised(1.f);
    p.process();
    EXPECT_FLOAT_EQ(p.valueNormalised(), 1.f);
}

// ---- ModulatableParameter ----

TEST(ModulatableParameterTest, ClearModulationResetsAccum) {
    ModulatableParameter<float> p(0.f, 1.f, 0.5f);
    p.addModulation(0.2f);
    p.clearModulation();
    EXPECT_FLOAT_EQ(p.getModulationAmount(), 0.f);
}

TEST(ModulatableParameterTest, ValueNormalisedClampsWithModulation) {
    ModulatableParameter<float> p;
    p.setBaseNormalised(0.9f);
    // Bypass smoother
    p.process();
    p.addModulation(0.5f); // Would push to 1.4
    EXPECT_FLOAT_EQ(p.valueNormalised(), 1.f);
}

TEST(ModulatableParameterTest, NegativeModulationClampsToZero) {
    ModulatableParameter<float> p;
    p.setBaseNormalised(0.1f);
    p.process();
    p.addModulation(-0.5f);
    EXPECT_FLOAT_EQ(p.valueNormalised(), 0.f);
}