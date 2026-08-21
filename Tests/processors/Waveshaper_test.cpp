/*
 * @file Waveshaper_test.cpp
 *
 * Unit tests for:
 *   CASPI::Distortion::Waveshaper<FloatType>
 *
 * Section 1: Built-in curves — via a live graph (evaluate()/applyBlock()
 *            are private; the node is graph-only, unlike Gain<F>).
 * Section 2: Parameter API get/set round trips.
 * Section 3: Custom curve registration.
 * Section 4: Asymmetric shaping.
 * Section 5: Graph integration (full block, non-SIMD-width-multiple length).
 */

#include "core/caspi_Graph.h"
#include "gain/caspi_Waveshaper.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace CASPI;
using namespace CASPI::Graph;
using CASPI::Distortion::Waveshaper;
using CASPI::Distortion::WaveshapeType;

namespace
{
    template <typename FloatType>
    class ConstantNode : public AudioNode<ConstantNode<FloatType>, FloatType>
    {
        public:
            FloatType fillValue = FloatType (0);

            explicit ConstantNode (FloatType value = FloatType (0))
                : AudioNode<ConstantNode<FloatType>, FloatType> (0, 1)
                , fillValue (value)
            {
            }

            void processImpl (AudioContext<FloatType>&) noexcept
            {
                this->outputBuffer.fill (fillValue);
            }
    };

    // Render a single value through a Waveshaper via a live graph.
    // `configure` is called on the freshly-emplaced node before prepare().
    template <typename FloatType, typename Configurator>
    FloatType renderOne (Configurator&& configure, FloatType input)
    {
        AudioGraph<FloatType> graph;
        auto srcId = graph.template emplace<ConstantNode<FloatType>> (input).id;
        auto shaperId = graph.template emplace<Waveshaper<FloatType>>().id;

        configure (*graph.template getNodeAs<Waveshaper<FloatType>> (shaperId));

        EXPECT_TRUE (graph.connect (Port (srcId, 0), Port (shaperId, 0)).has_value());
        EXPECT_TRUE (graph.prepare (1, 1, 48000.0).has_value());
        graph.process();

        const auto* out = graph.getNode (shaperId)->getOutputBuffer (0);
        return out->sample (0, 0);
    }
}

// =================================================================================================
// Section 1: Built-in curves
// =================================================================================================

TEST (WaveshaperCurves, LinearIsPassthrough)
{
    auto cfg = [] (Waveshaper<float>& w) { w.setWaveshape (WaveshapeType::Linear); };
    EXPECT_NEAR (renderOne<float> (cfg, 0.3f), 0.3f, 1e-6f);
    EXPECT_NEAR (renderOne<float> (cfg, -0.7f), -0.7f, 1e-6f);
}

TEST (WaveshaperCurves, HardClipClampsToLimit)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::HardClip);
        w.setClipLimit (0.5f);
    };
    EXPECT_NEAR (renderOne<float> (cfg, 0.9f), 0.5f, 1e-6f);
    EXPECT_NEAR (renderOne<float> (cfg, -0.9f), -0.5f, 1e-6f);
    EXPECT_NEAR (renderOne<float> (cfg, 0.2f), 0.2f, 1e-6f);
}

TEST (WaveshaperCurves, SoftClipIsDistinctFromHardClip)
{
    auto hardCfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::HardClip);
        w.setClipLimit (0.5f);
    };
    auto softCfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::SoftClip);
        w.setClipLimit (0.5f);
    };

    // Below the limit, SoftClip already compresses (unlike HardClip's flat pass-through),
    // so the two curves diverge well before the hard boundary.
    const float hardOut = renderOne<float> (hardCfg, 0.4f);
    const float softOut = renderOne<float> (softCfg, 0.4f);
    EXPECT_NEAR (hardOut, 0.4f, 1e-6f);
    EXPECT_LT (softOut, hardOut);
}

TEST (WaveshaperCurves, SoftClipMatchesTanhFormula)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::SoftClip);
        w.setClipLimit (0.8f);
    };

    const float x = 0.6f;
    const float expected = 0.8f * std::tanh (x / 0.8f);
    EXPECT_NEAR (renderOne<float> (cfg, x), expected, 1e-5f);
}

TEST (WaveshaperCurves, SoftClipStaysBoundedForLargeInput)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::SoftClip);
        w.setClipLimit (1.0f);
    };

    // Far outside a Taylor approximation's valid domain -- exercises exactly
    // the regime std::tanh was chosen for over the SIMD Taylor tanh_block.
    const float out = renderOne<float> (cfg, 50.0f);
    EXPECT_NEAR (out, 1.0f, 1e-3f);
}

TEST (WaveshaperCurves, CubicMatchesFormulaWithinDomain)
{
    auto cfg = [] (Waveshaper<float>& w) { w.setWaveshape (WaveshapeType::Cubic); };
    EXPECT_NEAR (renderOne<float> (cfg, 0.5f), 0.125f, 1e-5f);
    EXPECT_NEAR (renderOne<float> (cfg, -0.5f), -0.125f, 1e-5f);
}

TEST (WaveshaperCurves, CubicClampsInputDomainFirst)
{
    auto cfg = [] (Waveshaper<float>& w) { w.setWaveshape (WaveshapeType::Cubic); };
    // Input is clamped to [-1, 1] before cubing, so 2.0 behaves like 1.0.
    EXPECT_NEAR (renderOne<float> (cfg, 2.0f), 1.0f, 1e-5f);
}

TEST (WaveshaperCurves, ArayaMatchesClosedForm)
{
    auto cfg = [] (Waveshaper<float>& w) { w.setWaveshape (WaveshapeType::Araya); };
    const float x = 0.6f;
    const float expected = 1.5f * x * (1.0f - x * x / 3.0f);
    EXPECT_NEAR (renderOne<float> (cfg, x), expected, 1e-5f);
}

TEST (WaveshaperCurves, SineMatchesStdSine)
{
    auto cfg = [] (Waveshaper<float>& w) { w.setWaveshape (WaveshapeType::Sine); };
    EXPECT_NEAR (renderOne<float> (cfg, 0.4f), std::sin (0.4f), 1e-5f);
}

TEST (WaveshaperCurves, SigmoidIsBoundedAndOddSymmetric)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::Sigmoid);
        w.setDrive (2.0f);
    };

    const float pos = renderOne<float> (cfg, 0.5f);
    const float neg = renderOne<float> (cfg, -0.5f);
    EXPECT_NEAR (pos, -neg, 1e-5f);
    EXPECT_GT (pos, 0.0f);
    EXPECT_LT (pos, 1.0f);
}

TEST (WaveshaperCurves, TanhDriveIsUnityGainAtOne)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::TanhDrive);
        w.setDrive (3.0f);
    };
    EXPECT_NEAR (renderOne<float> (cfg, 1.0f), 1.0f, 1e-4f);
}

TEST (WaveshaperCurves, TanhDriveHandlesZeroInputWithoutNaN)
{
    // The pre-fix formula (tanh(drive*x)/tanh(x)) was 0/0 at x=0.
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::TanhDrive);
        w.setDrive (2.0f);
    };
    const float out = renderOne<float> (cfg, 0.0f);
    EXPECT_FALSE (std::isnan (out));
    EXPECT_NEAR (out, 0.0f, 1e-6f);
}

TEST (WaveshaperCurves, ArctanDriveIsUnityGainAtOne)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::ArctanDrive);
        w.setDrive (3.0f);
    };
    EXPECT_NEAR (renderOne<float> (cfg, 1.0f), 1.0f, 1e-4f);
}

TEST (WaveshaperCurves, ArctanDriveHandlesZeroInputWithoutNaN)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::ArctanDrive);
        w.setDrive (2.0f);
    };
    const float out = renderOne<float> (cfg, 0.0f);
    EXPECT_FALSE (std::isnan (out));
    EXPECT_NEAR (out, 0.0f, 1e-6f);
}

TEST (WaveshaperCurves, AnalogKneeIsIdentityAtAmountOne)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::AnalogKnee);
        w.setAnalogKnee (1.0f);
    };
    EXPECT_NEAR (renderOne<float> (cfg, 0.4f), 0.4f, 1e-5f);
    EXPECT_NEAR (renderOne<float> (cfg, -0.4f), -0.4f, 1e-5f);
}

TEST (WaveshaperCurves, AnalogKneeSoftensTowardZero)
{
    // Raising a value < 1 to a fractional power > 1 pulls its magnitude UP
    // toward 1 (softening the knee near zero), while preserving sign.
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::AnalogKnee);
        w.setAnalogKnee (2.0f);
    };
    const float out = renderOne<float> (cfg, 0.25f);
    EXPECT_GT (out, 0.25f);
    EXPECT_LT (out, 1.0f);
}

// =================================================================================================
// Section 2: Parameter API
// =================================================================================================

TEST (WaveshaperParameters, SettersUpdateGetters)
{
    Waveshaper<float> w;
    w.setWaveshape (WaveshapeType::SoftClip);
    w.setClipLimit (0.75f);
    w.setDrive (4.0f);
    w.setAnalogKnee (2.5f);
    w.setNegativeWaveshape (WaveshapeType::HardClip);
    w.setAsymmetry (true, -0.1f);

    EXPECT_EQ (w.getWaveshape(), WaveshapeType::SoftClip);
    EXPECT_FLOAT_EQ (w.getClipLimit(), 0.75f);
    EXPECT_FLOAT_EQ (w.getDrive(), 4.0f);
    EXPECT_FLOAT_EQ (w.getAnalogKnee(), 2.5f);
    EXPECT_EQ (w.getNegativeWaveshape(), WaveshapeType::HardClip);
    EXPECT_TRUE (w.getIsAsymmetric());
}

TEST (WaveshaperParameters, SetDriveDbConvertsToLinear)
{
    Waveshaper<float> w;
    w.setDriveDb (0.0f);
    EXPECT_NEAR (w.getDrive(), 1.0f, 1e-5f);
}

// =================================================================================================
// Section 3: Custom curve registration
// =================================================================================================

TEST (WaveshaperCustom, RegisterAndSelectSucceeds)
{
    Waveshaper<float> w;
    w.registerCustomWaveshape ("Square", [] (float x) { return x * x; });
    EXPECT_TRUE (w.setCustomWaveshape ("Square"));
    EXPECT_EQ (w.getWaveshape(), WaveshapeType::Custom);
}

TEST (WaveshaperCustom, RegisteredCurveIsAppliedThroughGraph)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.registerCustomWaveshape ("Square", [] (float x) { return x * x; });
        EXPECT_TRUE (w.setCustomWaveshape ("Square"));
    };
    EXPECT_NEAR (renderOne<float> (cfg, 0.5f), 0.25f, 1e-5f);
}

TEST (WaveshaperCustom, SelectUnknownNameLeavesShapeUnchanged)
{
    Waveshaper<float> w;
    w.setWaveshape (WaveshapeType::Sine);
    EXPECT_FALSE (w.setCustomWaveshape ("DoesNotExist"));
    EXPECT_EQ (w.getWaveshape(), WaveshapeType::Sine);
}

// =================================================================================================
// Section 4: Asymmetric shaping
// =================================================================================================

TEST (WaveshaperAsymmetric, UsesNegativeCurveBelowAsymmetryPoint)
{
    auto cfg = [] (Waveshaper<float>& w)
    {
        w.setWaveshape (WaveshapeType::Linear);
        w.setNegativeWaveshape (WaveshapeType::HardClip);
        w.setClipLimit (0.3f);
        w.setAsymmetry (true, 0.0f);
    };

    // Above the asymmetry point: Linear passthrough.
    EXPECT_NEAR (renderOne<float> (cfg, 0.5f), 0.5f, 1e-5f);
    // Below the asymmetry point: HardClip at 0.3.
    EXPECT_NEAR (renderOne<float> (cfg, -0.5f), -0.3f, 1e-5f);
}

// =================================================================================================
// Section 5: Graph integration -- full block through a live AudioGraph
// =================================================================================================

TEST (WaveshaperGraph, ProcessesFullBlockMatchingPerSampleReference)
{
    AudioGraph<float> graph;
    constexpr std::size_t frames = 37; // deliberately not a SIMD-width multiple
    auto srcId = graph.emplace<ConstantNode<float>> (0.9f).id;
    auto shaperId = graph.emplace<Waveshaper<float>>().id;

    auto& shaper = *graph.getNodeAs<Waveshaper<float>> (shaperId);
    shaper.setWaveshape (WaveshapeType::Araya);

    ASSERT_TRUE (graph.connect (Port (srcId, 0), Port (shaperId, 0)).has_value());
    ASSERT_TRUE (graph.prepare (1, frames, 48000.0).has_value());
    graph.process();

    const auto* out = graph.getNode (shaperId)->getOutputBuffer (0);
    const float expected = 1.5f * 0.9f * (1.0f - 0.9f * 0.9f / 3.0f);
    for (std::size_t f = 0; f < frames; ++f)
    {
        EXPECT_NEAR (out->sample (0, f), expected, 1e-5f);
    }
}

TEST (WaveshaperGraph, SilentInputGivesSilentOutput)
{
    AudioGraph<float> graph;
    auto shaperId = graph.emplace<Waveshaper<float>>().id;
    // No source connected to port 0.
    ASSERT_TRUE (graph.prepare (1, 16, 48000.0).has_value());
    graph.process();

    const auto* out = graph.getNode (shaperId)->getOutputBuffer (0);
    for (std::size_t f = 0; f < 16; ++f)
    {
        EXPECT_FLOAT_EQ (out->sample (0, f), 0.0f);
    }
}

// =================================================================================================
// Section 6: Standalone processSample() (no graph)
// =================================================================================================

TEST (WaveshaperStandalone, MatchesRenderOneForSymmetricCurve)
{
    Waveshaper<float> w;
    w.setWaveshape (WaveshapeType::Araya);

    auto cfg = [] (Waveshaper<float>& shaper) { shaper.setWaveshape (WaveshapeType::Araya); };
    EXPECT_NEAR (w.processSample (0.6f), renderOne<float> (cfg, 0.6f), 1e-6f);
}

TEST (WaveshaperStandalone, RespectsAsymmetricSelection)
{
    Waveshaper<float> w;
    w.setWaveshape (WaveshapeType::Linear);
    w.setNegativeWaveshape (WaveshapeType::HardClip);
    w.setClipLimit (0.3f);
    w.setAsymmetry (true, 0.0f);

    EXPECT_NEAR (w.processSample (0.5f), 0.5f, 1e-5f);
    EXPECT_NEAR (w.processSample (-0.5f), -0.3f, 1e-5f);
}

TEST (WaveshaperStandalone, ClampsOutputToUnitRange)
{
    Waveshaper<float> w;
    w.setWaveshape (WaveshapeType::Sine);
    // sin(10) is well outside [-1, 1] before the safety clamp.
    const float out = w.processSample (10.0f);
    EXPECT_LE (out, 1.0f);
    EXPECT_GE (out, -1.0f);
}
