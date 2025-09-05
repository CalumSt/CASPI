// test_caspi_Phase.cpp
#include "core/caspi_Phase.h"
#include <gtest/gtest.h>
#include <cmath>

using CASPI::Phase;
using CASPI::SyncMode;

constexpr double tolerance = 1e-8;
constexpr double kWrapLimit = 1.0;

TEST(PhaseTest, PhaseIncrementsAndWraps) {
    Phase<double> p;
    p.setFrequency(0.25, 1.0); // increment = 0.25
    EXPECT_NEAR(p.advanceAndWrap(kWrapLimit), 0.0, tolerance);
    EXPECT_NEAR(p.phase, 0.25, tolerance);
    p.advanceAndWrap(kWrapLimit); // phase = 0.5
    p.advanceAndWrap(kWrapLimit); // phase = 0.75
    p.advanceAndWrap(kWrapLimit); // phase = 1.0 -> wraps to 0
    EXPECT_NEAR(p.phase, 0.0, tolerance);
}

TEST(PhaseTest, ResetPhase) {
    Phase<double> p;
    p.setFrequency(0.5, 1.0);
    p.advanceAndWrap(kWrapLimit);
    p.resetPhase();
    EXPECT_NEAR(p.phase, 0.0, tolerance);
}

TEST(PhaseTest, HardSyncResetsPhase) {
    Phase<double> p;
    p.setFrequency(0.1, 1.0);
    p.setHardSyncFrequency(0.5); // hardSyncIncrement = 0.5
    p.syncMode = SyncMode::Hard;

    // After two advances, hardSyncPhase wraps and phase should reset
    p.advanceAndWrap(kWrapLimit); // phase = 0.1, hardSyncPhase = 0.5
    p.advanceAndWrap(kWrapLimit); // phase = 0.2, hardSyncPhase = 0.0, phase reset to 0
    EXPECT_NEAR(p.phase, 0.0, tolerance);
}

TEST(PhaseTest, SoftSyncReflectsPhase) {
    Phase<double> p;
    p.setFrequency(0.1, 1.0);
    p.setHardSyncFrequency(0.5);
    p.syncMode = SyncMode::Soft;

    p.advanceAndWrap(kWrapLimit); // phase = 0.1, hardSyncPhase = 0.5
    p.advanceAndWrap(kWrapLimit); // phase = 0.2, hardSyncPhase = 0.0, phase reflected
    EXPECT_NEAR(p.phase, kWrapLimit - 0.2, tolerance);
}

TEST(PhaseTest, NoSyncDoesNotAffectPhase) {
    Phase<double> p;
    p.setFrequency(0.1, 1.0);
    p.setHardSyncFrequency(0.5);
    p.syncMode = SyncMode::None;

    p.advanceAndWrap(kWrapLimit); // phase = 0.1
    p.advanceAndWrap(kWrapLimit); // phase = 0.2
    EXPECT_NEAR(p.phase, 0.2, tolerance);
}