#pragma once
#include "test_helpers.h"

#include "Envelopes/caspi_EnvelopeGenerator.h"
#include <gtest/gtest.h>
#include <string>

CASPI::Envelope::ADSR<float> ADSR; // This needs to be tidied up
const int numberOfSettings = 6;
const float sampleRate = 44100.0f;
const std::vector<float> testTimeList = { 0.05f,0.1f,0.5f,0.75f,1.0f,2.0f };
const std::vector<float> testSustainList = { 0.00001f,0.01f,0.05f,0.1f,0.5f,1.0f };

// =================================================================================================
TEST(AdsrTests,Constructor_test)
{
    const std::string expect = {"idle" };
    ADSR.setSampleRate(44100.0f);
    EXPECT_EQ (ADSR.getState(), expect);
}


TEST(AdsrTests, Setters_test) {
    float test = 0.0f;
    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);

        test = ADSR.getAttack();
        EXPECT_NEAR(test,1.0f,0.1f);
        test = ADSR.getDecay();
        EXPECT_NEAR(test,1.0f,0.1f);
        test = ADSR.getSustainLevel();
        EXPECT_EQ(test,testSustainList[testIndex]);
        test = ADSR.getRelease();
        EXPECT_NEAR(test,1.0f,0.1f);
    }
}

TEST(AdsrTests, noteOn_test) {
    ADSR.noteOn();
    std::string test = ADSR.getState();
    std::string expect = {"attack"};
    EXPECT_EQ(test,expect);
}
// This test exercises the attack stage at various times and sustain levels.
// We expect that the envelope has progressed to decay after the test time is up.
// This is except for the last test, where no decay will occur and will progress straight to sustain.
TEST(AdsrTests, Attack_test) {
    int numberOfSamples;
    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {

        // Arrange
        ADSR.reset();
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);

        // Act
        ADSR.noteOn();
        std::string test = ADSR.getState();
        std::string expect = {"attack"};

        // Assert
        /// Attack test
        EXPECT_EQ(test,expect);
        auto output = ADSR.render();
        EXPECT_NEAR(output,0.0f,0.01f);
        numberOfSamples = static_cast<int>(46000.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
            EXPECT_GT(output,0.0f);
            EXPECT_LE(output,1.0f);
        }
        // Expect to have to switched to decay
        if (testIndex < 5) {
            EXPECT_EQ(ADSR.getState(),"decay");
        } else if (testIndex >= 5) {
            EXPECT_EQ(ADSR.getState(),"sustain");
        }

        ADSR.noteOff();
        EXPECT_EQ(ADSR.getState(),"release");
    }
}


TEST(AdsrTests, Decay_test) {
    int numberOfSamples;
    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {
        // Arrange
        ADSR.reset();
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);

        // Act
        ADSR.noteOn();
        auto output = 0.0f;
        numberOfSamples = static_cast<int>(46000.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
        }
        // Assert
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
            EXPECT_GT(output,0.0f);
            EXPECT_LE(output,1.0f);
        }

        // Expect to have to switched to decay
        EXPECT_EQ(ADSR.getState(),"sustain");

    }
}

/// This test checks that envelope actually sustains without switching itself off
TEST(AdsrTests, Sustain_test) {
    int numberOfSamples;
    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {
        // Arrange
        ADSR.reset();
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);

        // Act
        ADSR.noteOn();
        auto output = 0.0f;
        // Get a lot of samples
        numberOfSamples = static_cast<int>(4.0f * 46000.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
        }

        EXPECT_EQ(ADSR.getState(),"sustain");
        }
    }
/// This test checks that the ADSR releases properly.
TEST(AdsrTests, Release_test) {
    int numberOfSamples;
    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {
        // Arrange
        ADSR.reset();
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);

        // Act
        ADSR.noteOn();
        auto output = 0.0f;
        // Get a lot of samples
        numberOfSamples = static_cast<int>(44100.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
        }

        ADSR.noteOff();
        EXPECT_EQ(ADSR.getState(),"release");
        numberOfSamples = static_cast<int>(2000.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
        }
        if (testIndex > 1) {
            EXPECT_EQ(ADSR.getState(),"release"); // Test to see make sure the envelope doesn't release too quickly.
            EXPECT_GT(ADSR.render(),0.0f);
        }
        numberOfSamples = static_cast<int>(44100.0f * testTimeList[testIndex]);
        for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++) {
            output = ADSR.render();
        }
        EXPECT_EQ(ADSR.getState(),"idle");
        EXPECT_EQ(ADSR.render(),0.0f);
    }
};

/// This test generates the envelope figure to make sure everything is working.
TEST(AdsrTests, render_test)
{

    for (int testIndex = 0; testIndex < numberOfSettings; testIndex++) {
        int numberOfSamples = 0;
        auto totalTime = 4.0f * testTimeList[testIndex];
        auto totalNumberOfSamples = static_cast<int>(totalTime * sampleRate);
        auto output = std::vector<float>(totalNumberOfSamples, 0.0f);
        auto numberOfSamplesPerState = static_cast<int>(sampleRate * testTimeList[testIndex]);

        ADSR.reset();
        ADSR.setAttackTime(testTimeList[testIndex]);
        ADSR.setSustainLevel(testSustainList[testIndex]);
        ADSR.setDecayTime(testTimeList[testIndex]);
        ADSR.setReleaseTime(testTimeList[testIndex]);
        // Attack
        ADSR.noteOn();
        for (int sampleIndex = 0; sampleIndex < numberOfSamplesPerState; sampleIndex++) {
            output.at(numberOfSamples) = ADSR.render();
            numberOfSamples++;
        }
        // Decay
        for (int sampleIndex = 0; sampleIndex < numberOfSamplesPerState; sampleIndex++) {
            output.at(numberOfSamples) = ADSR.render();
            numberOfSamples++;
        }
        // Sustain
        for (int sampleIndex = 0; sampleIndex < numberOfSamplesPerState; sampleIndex++) {
            output.at(numberOfSamples) = ADSR.render();
            numberOfSamples++;
        }
        ADSR.noteOff();
        // Release
        for (int sampleIndex = 0; sampleIndex < numberOfSamplesPerState; sampleIndex++) {
            output.at(numberOfSamples) = ADSR.render();
            numberOfSamples++;
        }
        auto times = range (0.0f, totalTime, 1.0f / sampleRate);
        const std::string filename = "Envelope" + std::to_string(testIndex);
        //createPlot<float> (times, output, "Single Envelope", filename);
        EXPECT_EQ(ADSR.getState(),"idle");
        EXPECT_EQ(ADSR.render(),0.0f);
    }
}

