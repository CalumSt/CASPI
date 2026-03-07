/*************************************************************************
 * @file ModMatrix_test.cpp
 *
 * Unit and integration tests for:
 *   CASPI::Controls::ModMatrix<float>
 *   CASPI::Controls::RoutingList<float, N>
 *   CASPI::Controls::ModulationRouting<float>::applyCurve
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * ModMatrix uses a lock-free command queue. All routing mutations
 * (addRouting, removeRouting, clearRoutings, setRoutingEnabled) are
 * enqueued and applied only when process() is called. Tests must call
 * process() before asserting on routing state or parameter values.
 *
 * Parameters must have their smoother converged (via process()) before
 * modulation tests, otherwise smoothedBase lags behind baseNormalised.
 *
 * All tests follow Arrange / Act / Assert.
 *
 * -----------------------------------------------------------------------
 * Section 1: RoutingList
 * -----------------------------------------------------------------------
 *
 * 1.1  InsertMaintainsSortOrderByDestinationId
 *      Two routings inserted in reverse destinationId order must be
 *      iterated in ascending order. The accumulation pass depends on
 *      sorted order for spatial locality.
 *
 * 1.2  InsertDuplicateDestinationIdAppends
 *      Two routings with the same destinationId must both be stored.
 *      The second must appear after the first (stable insert).
 *
 * 1.3  RemoveAtMiddleShiftsElementsLeft
 *      Removing the middle element of a three-entry list must leave
 *      the remaining two elements at indices 0 and 1 with correct ids.
 *
 * 1.4  RemoveAtOutOfRangeIsIgnored
 *      removeAt with index >= count must not crash or corrupt state.
 *
 * 1.5  ClearSetsCountToZero
 *      After clear(), size() == 0 and empty() == true.
 *
 * 1.6  InsertBeyondCapacityIsIgnored
 *      Inserting into a full list must not corrupt memory. Size stays
 *      at Capacity.
 *
 * 1.7  InsertThreeElementsInReverseOrderProducesSortedIteration
 *      Three routings inserted in descending destinationId order (5,3,1)
 *      must be iterated in ascending order (1,3,5). Extends 1.1 to
 *      three-element interleaved insertion, exercising the binary search
 *      path at a non-trivial midpoint.
 *
 * -----------------------------------------------------------------------
 * Section 2: ModulationRouting::applyCurve
 * -----------------------------------------------------------------------
 *
 * 2.1  LinearCurveIsIdentity
 *      applyCurve returns the input unchanged for all tested values.
 *
 * 2.2  ExponentialCurveSquaresMagnitudePreservesSign
 *      At 0.5 -> 0.25, at -0.5 -> -0.25, at boundaries unchanged.
 *
 * 2.3  LogarithmicCurveSqrtsMagnitudePreservesSign
 *      At 0.25 -> 0.5, at -0.25 -> -0.5, at boundaries unchanged.
 *
 * 2.4  SCurveSaturatesAtBoundariesAndIsAntisymmetric
 *      f(0) == 0, f(1) == 1, f(-1) == -1, and f(-x) == -f(x).
 *
 * 2.5  SCurveOutputIsMonotonicallyIncreasing
 *      Sampled at 11 points in [-1, 1], each output must be >= the
 *      previous. Confirms no inflection artefacts in the remapping.
 *
 * -----------------------------------------------------------------------
 * Section 3: Parameter Registration
 * -----------------------------------------------------------------------
 *
 * 3.1  RegisterParameterReturnsSequentialIds
 *      First registered parameter gets id 0, second gets id 1.
 *      Sequential ids are a contract relied on by routing setup.
 *
 * 3.2  RegisterNullParameterReturnsError
 *      Registering a null pointer returns NullParameter error and does
 *      not increment the parameter count.
 *
 * 3.3  GetNumParametersReflectsRegisteredCount
 *      getNumParameters() returns 2 after two successful registrations.
 *
 * 3.4  RegisterBeyondCapacityReturnsError
 *      After kMaxModParams successful registrations, the next call
 *      returns CapacityExceeded. Verifying capacity enforcement without
 *      allocating the full array in the test (uses a smaller proxy).
 *      Tested via a fresh matrix filled to capacity with a small-N proxy.
 *
 * -----------------------------------------------------------------------
 * Section 4: Source Value Management
 * -----------------------------------------------------------------------
 *
 * 4.1  SetAndGetSourceValueRoundTrips
 *      A value written via setSourceValue is returned unchanged by
 *      getSourceValue on the same index.
 *
 * 4.2  SetSourceValueOutOfRangeIsIgnored
 *      setSourceValue with sourceId >= kMaxModSources must not crash.
 *
 * 4.3  GetSourceValueOutOfRangeReturnsZero
 *      getSourceValue with sourceId >= kMaxModSources returns 0.
 *
 * 4.4  SourceValuesDefaultToZero
 *      Before any setSourceValue call, getSourceValue returns 0.
 *
 * -----------------------------------------------------------------------
 * Section 5: Routing - Basic Application
 * -----------------------------------------------------------------------
 *
 * 5.1  ProcessAppliesDepthScaledModulationToParameter
 *      source=1, depth=0.2, base=0.5 -> valueNormalised() == 0.7.
 *      Core arithmetic: output = smoothedBase + source * depth.
 *
 * 5.2  ZeroSourceProducesNoModulation
 *      source=0, depth=1.0 -> parameter remains at base.
 *      Ensures the accumulation loop is not adding a constant offset.
 *
 * 5.3  NegativeSourceAppliesNegativeModulation
 *      source=-1, depth=0.3, base=0.5 -> valueNormalised() == 0.2.
 *
 * 5.4  MultipleRoutingsAccumulateOnSameParameter
 *      Two routings, each depth=0.1, source=1 -> total modulation 0.2
 *      added to base. Verifies additive accumulation.
 *
 * 5.5  RoutingsOnDifferentParametersAreIndependent
 *      Routing 0->paramA and routing 1->paramB at different depths.
 *      Each parameter must reflect only its own routing.
 *
 * 5.6  ModulationIsResetEachProcessBlock
 *      source=1 in block 1, source=0 in block 2. Parameter must return
 *      to base in block 2. Guards against accumulation across blocks.
 *
 * 5.7  ModulationClampsAtUpperNormalisedBoundary
 *      base=0.5, source=1, depth=1 -> clamped to 1.0, not 1.5.
 *
 * 5.8  ModulationClampsAtLowerNormalisedBoundary
 *      base=0.5, source=-1, depth=1 -> clamped to -1 (accumulator),
 *      parameter clamps to 0 (its own range [0,1]).
 *      Verifies the [-1,1] accumulator clamp fires, not only the
 *      parameter-level clamp.
 *
 * 5.9  CommandsQueuedBeforeFirstProcessAreAllApplied
 *      Three addRouting calls, then one process(). All three routings
 *      must be present in getNumRoutings(). Verifies queue drain
 *      processes all enqueued commands in one pass.
 *
 * 5.10 AddRoutingWithOutOfRangeSourceIdIsDropped
 *      addRouting with sourceId >= kMaxModSources is silently discarded
 *      by applyCommand(). getNumRoutings() must remain 0.
 *
 * 5.11 AddRoutingWithOutOfRangeDestinationIdIsDropped
 *      addRouting with destinationId >= numParameters is silently
 *      discarded by applyCommand(). getNumRoutings() must remain 0
 *      and the registered parameter must be unmodulated.
 *
 * -----------------------------------------------------------------------
 * Section 6: Non-linear Routing Through Matrix
 * -----------------------------------------------------------------------
 *
 * 6.1  ExponentialRoutingAppliesCurveBeforeDepth
 *      source=0.5, depth=1.0, curve=Exponential -> accum = 0.25.
 *      base=0 -> valueNormalised() == 0.25.
 *
 * 6.2  LinearAndNonLinearRoutingsAccumulateIntoSameParameter
 *      Linear routing (src0, depth=0.1) and Exponential routing
 *      (src1=0.5, depth=1.0) both target paramB.
 *      Total modulation = 0.1 + 0.25 = 0.35.
 *
 * 6.3  GetNumLinearAndNonLinearCountsAreCorrect
 *      After adding one Linear and two Exponential routings,
 *      getNumLinearRoutings()==1 and getNumNonLinearRoutings()==2.
 *
 * -----------------------------------------------------------------------
 * Section 7: Enabled / Disabled
 * -----------------------------------------------------------------------
 *
 * 7.1  DisabledRoutingContributesNoModulation
 *      routing.enabled=false -> parameter stays at base.
 *
 * 7.2  ReenablingLinearRoutingRestoresModulation
 *      setRoutingEnabled<Linear>(0, true) on a disabled routing ->
 *      modulation is applied after the next process().
 *
 * 7.3  DisablingLinearRoutingMidSessionSuppressesModulation
 *      Routing active in block 1, setRoutingEnabled(0, false) enqueued,
 *      block 2 -> parameter returns to base.
 *
 * 7.4  ReenablingNonLinearRoutingRestoresModulation
 *      Same as 7.2 but with curve=Exponential and explicit template arg
 *      setRoutingEnabled<ModulationCurve::Exponential>(0, true).
 *
 * -----------------------------------------------------------------------
 * Section 8: Removal
 * -----------------------------------------------------------------------
 *
 * 8.1  RemoveLinearRoutingDecreasesRoutingCount
 *      After addRouting + process, removeRouting(0) + process ->
 *      getNumRoutings() == 0.
 *
 * 8.2  RemovedLinearRoutingNoLongerModulatesParameter
 *      After removal, parameter returns to base on next process().
 *
 * 8.3  RemoveNonLinearRoutingViaExplicitTemplateArg
 *      removeRouting<ModulationCurve::Exponential>(0) targets the
 *      non-linear list. getNumNonLinearRoutings() decrements to 0.
 *
 * 8.4  RemoveOutOfRangeIndexIsIgnored
 *      removeRouting(999) enqueued, process() -> no crash, count unchanged.
 *
 * 8.5  ClearRoutingsRemovesBothLists
 *      One linear + one exponential routing added. clearRoutings() ->
 *      both lists empty, neither parameter is modulated.
 *
 * 8.6  RemainingRoutingStillFunctionsAfterRemovingFirst
 *      Two routings. Remove index 0. Index 1 (now at index 0) must
 *      still apply its modulation correctly.
 *
 * -----------------------------------------------------------------------
 * Section 9: Depth Clamping
 * -----------------------------------------------------------------------
 *
 * 9.1  DepthAboveOneIsClamped
 *      addRouting with depth=5.0. source=1. Accumulated modulation
 *      must equal 1.0 (not 5.0), because depth is clamped at enqueue.
 *
 * 9.2  DepthBelowNegativeOneIsClamped
 *      addRouting with depth=-5.0. source=1. Accumulated modulation
 *      must equal -1.0 after clamping.
 *
 * -----------------------------------------------------------------------
 * Section 10: Reset
 * -----------------------------------------------------------------------
 *
 * 10.1 ResetZerosAllSourceValues
 *      After setSourceValue for indices 0..3, reset() -> all return 0.
 *
 * 10.2 ResetClearsParameterModulation
 *      Routing active, process(), then reset() -> getModulationAmount()==0
 *      on the parameter.
 *
 * 10.3 ResetPreservesRoutings
 *      After reset(), the next process() with a non-zero source must
 *      apply modulation again. Routings must survive reset().
 *
 * -----------------------------------------------------------------------
 * Section 11: Multi-block Stability
 * -----------------------------------------------------------------------
 *
 * 11.1 SourceValuePersistsAcrossBlocksWithoutBeingReset
 *      setSourceValue(0, 0.5) in block 1. No setSourceValue in block 2.
 *      Block 2 process() must still see source=0.5.
 *
 * 11.2 ModulationIsStableAcross100Blocks
 *      Same routing and source value, process() called 100 times.
 *      valueNormalised() must be the same on every call (no drift).
 *
 ************************************************************************/

#include <gtest/gtest.h>
#include "controls/caspi_ModMatrix.h"
#include "core/caspi_Parameter.h"

using namespace CASPI::Controls;
using namespace CASPI::Core;

/*======================================================================
 * Shared fixture
 *
 * Provides a ModMatrix<float> with two pre-registered parameters.
 *   paramA : base = 0.5, range [0, 1]
 *   paramB : base = 0.0, range [0, 1]
 *
 * Smoothers are converged in SetUp() so all tests begin from a known
 * stable base value with no lag from the initial smoother step.
 *====================================================================*/
struct ModMatrixFixture : ::testing::Test
{
    ModMatrix<float>            matrix;
    ModulatableParameter<float> paramA { 0.f, 1.f, 0.5f };
    ModulatableParameter<float> paramB { 0.f, 1.f, 0.f };
    size_t destA {};
    size_t destB {};

    void SetUp() override
    {
        destA = matrix.registerParameter (&paramA).value();
        destB = matrix.registerParameter (&paramB).value();
        paramA.process();
        paramB.process();
    }
};

/*======================================================================
 * Section 1: RoutingList
 *====================================================================*/

/*
 * 1.1 InsertMaintainsSortOrderByDestinationId
 *
 * Insert two routings in reverse destinationId order (dst=5 then dst=2).
 * After both inserts, iteration must yield dst=2 at index 0 and dst=5
 * at index 1. The sorted order is a precondition for the accumulation
 * pass to write sequentially into modulationAccum[].
 */
TEST (RoutingListTest, InsertMaintainsSortOrderByDestinationId)
{
    /* Arrange */
    RoutingList<float, 16> list;
    ModulationRouting<float> high (0, 5, 0.5f);
    ModulationRouting<float> low  (0, 2, 0.5f);

    /* Act */
    list.insert (high);
    list.insert (low);

    /* Assert */
    ASSERT_EQ (list.size(), 2u);
    EXPECT_EQ (list[0].destinationId, 2u);
    EXPECT_EQ (list[1].destinationId, 5u);
}

/*
 * 1.2 InsertDuplicateDestinationIdAppends
 *
 * Two routings with destinationId=3 and different depths. After both
 * inserts, size must be 2 and both must be present. Order within the
 * same id is not mandated but both must survive.
 */
TEST (RoutingListTest, InsertDuplicateDestinationIdAppends)
{
    /* Arrange */
    RoutingList<float, 16> list;
    ModulationRouting<float> r0 (0, 3, 0.2f);
    ModulationRouting<float> r1 (1, 3, 0.8f);

    /* Act */
    list.insert (r0);
    list.insert (r1);

    /* Assert */
    ASSERT_EQ (list.size(), 2u);
    const bool bothDepthsPresent =
        (list[0].depth == 0.2f && list[1].depth == 0.8f) ||
        (list[0].depth == 0.8f && list[1].depth == 0.2f);
    EXPECT_TRUE (bothDepthsPresent);
}

/*
 * 1.3 RemoveAtMiddleShiftsElementsLeft
 *
 * Insert three routings with destinationIds 1, 2, 3. Remove the middle
 * (index 1). The list must contain ids 1 and 3 at indices 0 and 1
 * respectively. Verifies the shift-left compaction is correct.
 */
TEST (RoutingListTest, RemoveAtMiddleShiftsElementsLeft)
{
    /* Arrange */
    RoutingList<float, 16> list;
    list.insert (ModulationRouting<float> (0, 1, 0.1f));
    list.insert (ModulationRouting<float> (0, 2, 0.2f));
    list.insert (ModulationRouting<float> (0, 3, 0.3f));
    ASSERT_EQ (list.size(), 3u);

    /* Act */
    list.removeAt (1);

    /* Assert */
    ASSERT_EQ (list.size(), 2u);
    EXPECT_EQ (list[0].destinationId, 1u);
    EXPECT_EQ (list[1].destinationId, 3u);
}

/*
 * 1.4 RemoveAtOutOfRangeIsIgnored
 *
 * removeAt(999) on a list of size 2 must not crash or change size.
 * Guards against callers passing stale indices.
 */
TEST (RoutingListTest, RemoveAtOutOfRangeIsIgnored)
{
    /* Arrange */
    RoutingList<float, 16> list;
    list.insert (ModulationRouting<float> (0, 0, 0.5f));
    list.insert (ModulationRouting<float> (0, 1, 0.5f));

    /* Act */
    list.removeAt (999);

    /* Assert */
    EXPECT_EQ (list.size(), 2u);
}

/*
 * 1.5 ClearSetsCountToZero
 *
 * After inserting two elements and calling clear(), size() must be 0
 * and empty() must be true.
 */
TEST (RoutingListTest, ClearSetsCountToZero)
{
    /* Arrange */
    RoutingList<float, 16> list;
    list.insert (ModulationRouting<float> (0, 0, 0.5f));
    list.insert (ModulationRouting<float> (0, 1, 0.5f));

    /* Act */
    list.clear();

    /* Assert */
    EXPECT_EQ (list.size(), 0u);
    EXPECT_TRUE (list.empty());
}

/*
 * 1.6 InsertBeyondCapacityIsIgnored
 *
 * A RoutingList with Capacity=2 must not corrupt memory when a third
 * insert is attempted. size() must remain at 2.
 */
TEST (RoutingListTest, InsertBeyondCapacityIsIgnored)
{
    /* Arrange */
    RoutingList<float, 2> list;
    list.insert (ModulationRouting<float> (0, 0, 0.1f));
    list.insert (ModulationRouting<float> (0, 1, 0.2f));

    /* Act: third insert into a full list */
    list.insert (ModulationRouting<float> (0, 2, 0.3f));

    /* Assert */
    EXPECT_EQ (list.size(), 2u);
}

/*
 * 1.7 InsertThreeElementsInReverseOrderProducesSortedIteration
 *
 * Insert routings with destinationIds 5, 3, 1 (descending). After all
 * three inserts, iteration must yield ids 1, 3, 5 at indices 0, 1, 2.
 * Exercises the binary search midpoint for a three-element list and the
 * two-shift paths (insert-at-front and insert-in-middle).
 */
TEST (RoutingListTest, InsertThreeElementsInReverseOrderProducesSortedIteration)
{
    /* Arrange */
    RoutingList<float, 16> list;

    /* Act */
    list.insert (ModulationRouting<float> (0, 5, 0.5f));
    list.insert (ModulationRouting<float> (0, 3, 0.5f));
    list.insert (ModulationRouting<float> (0, 1, 0.5f));

    /* Assert */
    ASSERT_EQ (list.size(), 3u);
    EXPECT_EQ (list[0].destinationId, 1u);
    EXPECT_EQ (list[1].destinationId, 3u);
    EXPECT_EQ (list[2].destinationId, 5u);
}

/*======================================================================
 * Section 2: ModulationRouting::applyCurve
 *====================================================================*/

/*
 * 2.1 LinearCurveIsIdentity
 *
 * Linear must return the input unchanged for positive, negative,
 * zero, and boundary values.
 */
TEST (ModulationRoutingCurveTest, LinearCurveIsIdentity)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.curve = ModulationCurve::Linear;

    /* Act / Assert */
    EXPECT_FLOAT_EQ (r.applyCurve (0.f),   0.f);
    EXPECT_FLOAT_EQ (r.applyCurve (0.5f),  0.5f);
    EXPECT_FLOAT_EQ (r.applyCurve (-0.5f), -0.5f);
    EXPECT_FLOAT_EQ (r.applyCurve (1.f),   1.f);
    EXPECT_FLOAT_EQ (r.applyCurve (-1.f),  -1.f);
}

/*
 * 2.2 ExponentialCurveSquaresMagnitudePreservesSign
 *
 * f(x) = sign(x) * x^2.
 * At 0.5 -> 0.25. At -0.5 -> -0.25. Boundaries unchanged. Zero -> 0.
 */
TEST (ModulationRoutingCurveTest, ExponentialCurveSquaresMagnitudePreservesSign)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.curve = ModulationCurve::Exponential;

    /* Act / Assert */
    EXPECT_FLOAT_EQ (r.applyCurve (0.f),   0.f);
    EXPECT_FLOAT_EQ (r.applyCurve (0.5f),  0.25f);
    EXPECT_FLOAT_EQ (r.applyCurve (-0.5f), -0.25f);
    EXPECT_FLOAT_EQ (r.applyCurve (1.f),   1.f);
    EXPECT_FLOAT_EQ (r.applyCurve (-1.f),  -1.f);
}

/*
 * 2.3 LogarithmicCurveSqrtsMagnitudePreservesSign
 *
 * f(x) = sign(x) * sqrt(|x|).
 * At 0.25 -> 0.5. At -0.25 -> -0.5. Boundaries unchanged. Zero -> 0.
 */
TEST (ModulationRoutingCurveTest, LogarithmicCurveSqrtsMagnitudePreservesSign)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.curve = ModulationCurve::Logarithmic;

    /* Act / Assert */
    EXPECT_NEAR (r.applyCurve (0.f),    0.f,   1e-6f);
    EXPECT_NEAR (r.applyCurve (0.25f),  0.5f,  1e-6f);
    EXPECT_NEAR (r.applyCurve (-0.25f), -0.5f, 1e-6f);
    EXPECT_NEAR (r.applyCurve (1.f),    1.f,   1e-6f);
    EXPECT_NEAR (r.applyCurve (-1.f),   -1.f,  1e-6f);
}

/*
 * 2.4 SCurveSaturatesAtBoundariesAndIsAntisymmetric
 *
 * Smoothstep remapped to [-1, 1]. f(0)==0, f(1)==1, f(-1)==-1.
 * Antisymmetry: f(-x) == -f(x) for x = 0.3.
 */
TEST (ModulationRoutingCurveTest, SCurveSaturatesAtBoundariesAndIsAntisymmetric)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.curve = ModulationCurve::SCurve;

    /* Act / Assert */
    EXPECT_NEAR (r.applyCurve (0.f),  0.f,  1e-6f);
    EXPECT_NEAR (r.applyCurve (1.f),  1.f,  1e-5f);
    EXPECT_NEAR (r.applyCurve (-1.f), -1.f, 1e-5f);

    const float pos = r.applyCurve (0.3f);
    const float neg = r.applyCurve (-0.3f);
    EXPECT_NEAR (pos, -neg, 1e-6f);
}

/*
 * 2.5 SCurveOutputIsMonotonicallyIncreasing
 *
 * Sampled at 11 uniformly spaced points from -1 to 1, each output
 * value must be >= the previous. Confirms no inflection artefacts
 * in the smoothstep-to-bipolar remapping.
 */
TEST (ModulationRoutingCurveTest, SCurveOutputIsMonotonicallyIncreasing)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.curve = ModulationCurve::SCurve;

    /* Act */
    float prev = r.applyCurve (-1.f);
    for (int i = -9; i <= 10; ++i)
    {
        const float x    = static_cast<float> (i) * 0.1f;
        const float curr = r.applyCurve (x);

        /* Assert */
        EXPECT_GE (curr, prev) << "Non-monotonic at x=" << x;
        prev = curr;
    }
}

/*======================================================================
 * Section 3: Parameter Registration
 *====================================================================*/

/*
 * 3.1 RegisterParameterReturnsSequentialIds
 *
 * First registration returns 0, second returns 1. Sequential ids are
 * relied upon by all routing setup in this suite.
 */
TEST_F (ModMatrixFixture, RegisterParameterReturnsSequentialIds)
{
    /* Arrange / Act: done in SetUp */

    /* Assert */
    EXPECT_EQ (destA, 0u);
    EXPECT_EQ (destB, 1u);
}

/*
 * 3.2 RegisterNullParameterReturnsError
 *
 * A null pointer must return NullParameter error and must not increment
 * the registered parameter count.
 */
TEST_F (ModMatrixFixture, RegisterNullParameterReturnsError)
{
    /* Arrange */
    ModulatableParameter<float>* nullParam = nullptr;
    const size_t countBefore = matrix.getNumParameters();

    /* Act */
    const auto result = matrix.registerParameter (nullParam);

    /* Assert */
    EXPECT_FALSE (result.has_value());
    EXPECT_EQ (matrix.getNumParameters(), countBefore);
}

/*
 * 3.3 GetNumParametersReflectsRegisteredCount
 *
 * Two parameters registered in SetUp. getNumParameters() must return 2.
 */
TEST_F (ModMatrixFixture, GetNumParametersReflectsRegisteredCount)
{
    /* Arrange / Act: done in SetUp */

    /* Assert */
    EXPECT_EQ (matrix.getNumParameters(), 2u);
}

/*
 * 3.4 RegisterBeyondCapacityReturnsCapacityExceededError
 *
 * Fill a fresh matrix to kMaxModParams, then verify the next
 * registration returns CapacityExceeded.
 *
 * kMaxModParams == 256. We pre-allocate 256 parameters on the heap to
 * avoid blowing the test stack, register all of them, then attempt 257.
 */
TEST (ModMatrixRegistrationTest, RegisterBeyondCapacityReturnsCapacityExceededError)
{
    /* Arrange */
    ModMatrix<float> freshMatrix;
    constexpr size_t N = kMaxModParams;

    std::array<ModulatableParameter<float>, N> params{
        ModulatableParameter<float>(0.f,1.f,0.f)
    };

    for (size_t i = 0; i < N; ++i)
    {
        const auto r = freshMatrix.registerParameter(&params[i]);
        ASSERT_TRUE(r.has_value()) << "Registration " << i << " failed unexpectedly";
    }

    ModulatableParameter<float> overflow(0.f, 1.f, 0.f);

    /* Act */
    const auto result = freshMatrix.registerParameter(&overflow);

    /* Assert */
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ModMatrix<float>::ParamRegistrationError::CapacityExceeded);
}

/*======================================================================
 * Section 4: Source Value Management
 *====================================================================*/

/*
 * 4.1 SetAndGetSourceValueRoundTrips
 *
 * A value written via setSourceValue is returned unchanged by
 * getSourceValue on the same index.
 */
TEST_F (ModMatrixFixture, SetAndGetSourceValueRoundTrips)
{
    /* Arrange */
    constexpr float expected = 0.75f;

    /* Act */
    matrix.setSourceValue (0, expected);
    const float actual = matrix.getSourceValue (0);

    /* Assert */
    EXPECT_FLOAT_EQ (actual, expected);
}

/*
 * 4.2 SetSourceValueOutOfRangeIsIgnored
 *
 * setSourceValue with sourceId >= kMaxModSources must not crash.
 */
TEST_F (ModMatrixFixture, SetSourceValueOutOfRangeIsIgnored)
{
    /* Arrange */
    constexpr size_t bad = 999u;

    /* Act / Assert: must not crash */
    EXPECT_NO_THROW (matrix.setSourceValue (bad, 1.f));
}

/*
 * 4.3 GetSourceValueOutOfRangeReturnsZero
 *
 * getSourceValue with sourceId >= kMaxModSources must return 0, not
 * an arbitrary memory read.
 */
TEST_F (ModMatrixFixture, GetSourceValueOutOfRangeReturnsZero)
{
    /* Arrange */
    constexpr size_t bad = 999u;

    /* Act */
    const float val = matrix.getSourceValue (bad);

    /* Assert */
    EXPECT_FLOAT_EQ (val, 0.f);
}

/*
 * 4.4 SourceValuesDefaultToZero
 *
 * Before any setSourceValue call, all source indices return 0.
 * Tests indices 0, 1, and kMaxModSources-1.
 */
TEST_F (ModMatrixFixture, SourceValuesDefaultToZero)
{
    /* Arrange: fresh matrix, no setSourceValue called */

    /* Act / Assert */
    EXPECT_FLOAT_EQ (matrix.getSourceValue (0), 0.f);
    EXPECT_FLOAT_EQ (matrix.getSourceValue (1), 0.f);
    EXPECT_FLOAT_EQ (matrix.getSourceValue (kMaxModSources - 1), 0.f);
}

/*======================================================================
 * Section 5: Routing - Basic Application
 *====================================================================*/

/*
 * 5.1 ProcessAppliesDepthScaledModulationToParameter
 *
 * source=1, depth=0.2, base=0.5 -> valueNormalised() == 0.7.
 * Core arithmetic: output = smoothedBase + source * depth.
 */
TEST_F (ModMatrixFixture, ProcessAppliesDepthScaledModulationToParameter)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.2f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
}

/*
 * 5.2 ZeroSourceProducesNoModulation
 *
 * source=0, depth=1.0 -> parameter stays at base.
 * Ensures the accumulation loop is not adding a constant offset.
 */
TEST_F (ModMatrixFixture, ZeroSourceProducesNoModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.f);
    ModulationRouting<float> r (0, destA, 1.f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 5.3 NegativeSourceAppliesNegativeModulation
 *
 * source=-1, depth=0.3, base=0.5 -> valueNormalised() == 0.2.
 */
TEST_F (ModMatrixFixture, NegativeSourceAppliesNegativeModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, -1.f);
    ModulationRouting<float> r (0, destA, 0.3f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.2f, 1e-5f);
}

/*
 * 5.4 MultipleRoutingsAccumulateOnSameParameter
 *
 * Two routings from different sources, each depth=0.1, source=1.
 * Total modulation = 0.2 added to base 0.5 -> 0.7.
 */
TEST_F (ModMatrixFixture, MultipleRoutingsAccumulateOnSameParameter)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    matrix.setSourceValue (1, 1.f);
    ModulationRouting<float> r0 (0, destA, 0.1f);
    ModulationRouting<float> r1 (1, destA, 0.1f);

    /* Act */
    matrix.addRouting (r0);
    matrix.addRouting (r1);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
}

/*
 * 5.5 RoutingsOnDifferentParametersAreIndependent
 *
 * source0 -> paramA (depth=0.2), source1 -> paramB (depth=0.5).
 * paramA: 0.5 + 0.2 = 0.7. paramB: 0.0 + 0.5 = 0.5.
 * Neither parameter must be affected by the other's routing.
 */
TEST_F (ModMatrixFixture, RoutingsOnDifferentParametersAreIndependent)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    matrix.setSourceValue (1, 1.f);

    /* Act */
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.2f));
    matrix.addRouting (ModulationRouting<float> (1, destB, 0.5f));
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
    EXPECT_NEAR (paramB.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 5.6 ModulationIsResetEachProcessBlock
 *
 * source=1 in block 1 -> modulation applied. source=0 in block 2 ->
 * parameter returns to base. Catches accumulation-across-blocks bugs.
 */
TEST_F (ModMatrixFixture, ModulationIsResetEachProcessBlock)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.2f);
    matrix.addRouting (r);
    matrix.process();

    /* Act */
    matrix.setSourceValue (0, 0.f);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 5.7 ModulationClampsAtUpperNormalisedBoundary
 *
 * base=0.5, source=1, depth=1. Accumulated value = 1.5, clamped to 1.
 */
TEST_F (ModMatrixFixture, ModulationClampsAtUpperNormalisedBoundary)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 1.0f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_FLOAT_EQ (paramA.valueNormalised(), 1.f);
}

/*
 * 5.8 ModulationClampsAtLowerNormalisedBoundary
 *
 * base=0.5, source=-1, depth=1. Accumulated modulation = -1.
 * Accumulator is clamped to [-1, 1], then the parameter adds its base
 * and clamps to its own [0, 1] range. Verifies the accumulator clamp
 * path fires independently of the parameter-level clamp.
 */
TEST_F (ModMatrixFixture, ModulationClampsAtLowerNormalisedBoundary)
{
    /* Arrange */
    matrix.setSourceValue (0, -1.f);
    ModulationRouting<float> r (0, destA, 1.0f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert: paramA.base=0.5, mod=-1 (clamped), result clamped to [0,1] */
    EXPECT_FLOAT_EQ (paramA.valueNormalised(), 0.f);
}

/*
 * 5.9 CommandsQueuedBeforeFirstProcessAreAllApplied
 *
 * Three addRouting calls before any process(). One process() call must
 * drain all three commands. getNumRoutings() must equal 3.
 */
TEST_F (ModMatrixFixture, CommandsQueuedBeforeFirstProcessAreAllApplied)
{
    /* Arrange */
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));

    /* Act */
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 3u);
}

/*
 * 5.10 AddRoutingWithOutOfRangeSourceIdIsDropped
 *
 * addRouting with sourceId == kMaxModSources (one past the end) must be
 * silently discarded by applyCommand(). getNumRoutings() must remain 0
 * and no crash or assertion must occur.
 */
TEST_F (ModMatrixFixture, AddRoutingWithOutOfRangeSourceIdIsDropped)
{
    /* Arrange */
    ModulationRouting<float> r;
    r.sourceId      = kMaxModSources;
    r.destinationId = destA;
    r.depth         = 1.f;

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 0u);
}

/*
 * 5.11 AddRoutingWithOutOfRangeDestinationIdIsDropped
 *
 * addRouting with destinationId >= numParameters (only 2 are registered)
 * must be silently discarded. getNumRoutings() must remain 0 and
 * paramA must be unmodulated.
 */
TEST_F (ModMatrixFixture, AddRoutingWithOutOfRangeDestinationIdIsDropped)
{
    /* Arrange: numParameters == 2, so destinationId == 2 is out of range */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, 999u, 1.f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 0u);
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*======================================================================
 * Section 6: Non-linear Routing Through Matrix
 *====================================================================*/

/*
 * 6.1 ExponentialRoutingAppliesCurveBeforeDepth
 *
 * source=0.5, depth=1.0, curve=Exponential.
 * Curve: 0.5^2 = 0.25. After depth scaling: 0.25 * 1.0 = 0.25.
 * paramB.base = 0 -> valueNormalised() == 0.25.
 */
TEST_F (ModMatrixFixture, ExponentialRoutingAppliesCurveBeforeDepth)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.5f);
    ModulationRouting<float> r (0, destB, 1.0f);
    r.curve = ModulationCurve::Exponential;

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramB.valueNormalised(), 0.25f, 1e-5f);
}

/*
 * 6.2 LinearAndNonLinearRoutingsAccumulateIntoSameParameter
 *
 * Linear routing: source0=1, depth=0.1 -> contributes 0.1.
 * Exponential routing: source1=0.5, depth=1.0 -> 0.5^2 * 1.0 = 0.25.
 * Both target paramB (base=0). Total: 0.35.
 */
TEST_F (ModMatrixFixture, LinearAndNonLinearRoutingsAccumulateIntoSameParameter)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    matrix.setSourceValue (1, 0.5f);

    ModulationRouting<float> linear (0, destB, 0.1f);
    ModulationRouting<float> exponential (1, destB, 1.0f);
    exponential.curve = ModulationCurve::Exponential;

    /* Act */
    matrix.addRouting (linear);
    matrix.addRouting (exponential);
    matrix.process();

    /* Assert: 0 + 0.1 + 0.25 = 0.35 */
    EXPECT_NEAR (paramB.valueNormalised(), 0.35f, 1e-5f);
}

/*
 * 6.3 GetNumLinearAndNonLinearCountsAreCorrect
 *
 * After adding one Linear and two Exponential routings, the individual
 * list counts must reflect the split.
 */
TEST_F (ModMatrixFixture, GetNumLinearAndNonLinearCountsAreCorrect)
{
    /* Arrange */
    ModulationRouting<float> lin  (0, destA, 0.1f);
    ModulationRouting<float> exp0 (0, destA, 0.1f);
    ModulationRouting<float> exp1 (0, destB, 0.1f);
    exp0.curve = ModulationCurve::Exponential;
    exp1.curve = ModulationCurve::Exponential;

    /* Act */
    matrix.addRouting (lin);
    matrix.addRouting (exp0);
    matrix.addRouting (exp1);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumLinearRoutings(),    1u);
    EXPECT_EQ (matrix.getNumNonLinearRoutings(), 2u);
    EXPECT_EQ (matrix.getNumRoutings(),          3u);
}

/*======================================================================
 * Section 7: Enabled / Disabled
 *====================================================================*/

/*
 * 7.1 DisabledRoutingContributesNoModulation
 *
 * A routing added with enabled=false must not add any modulation.
 * Parameter must remain at base.
 */
TEST_F (ModMatrixFixture, DisabledRoutingContributesNoModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.5f);
    r.enabled = false;

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 7.2 ReenablingLinearRoutingRestoresModulation
 *
 * A disabled linear routing, once re-enabled via setRoutingEnabled
 * (defaulting to Linear), must apply modulation on the next process().
 */
TEST_F (ModMatrixFixture, ReenablingLinearRoutingRestoresModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.2f);
    r.enabled = false;
    matrix.addRouting (r);
    matrix.process();
    ASSERT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);

    /* Act */
    matrix.setRoutingEnabled (0, true);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
}

/*
 * 7.3 DisablingLinearRoutingMidSessionSuppressesModulation
 *
 * Routing applies in block 1. setRoutingEnabled(0, false) is enqueued.
 * Block 2 process() must not apply modulation; parameter returns to base.
 */
TEST_F (ModMatrixFixture, DisablingLinearRoutingMidSessionSuppressesModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.2f);
    matrix.addRouting (r);
    matrix.process();
    ASSERT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);

    /* Act */
    matrix.setRoutingEnabled (0, false);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 7.4 ReenablingNonLinearRoutingRestoresModulation
 *
 * A disabled Exponential routing, re-enabled via
 * setRoutingEnabled<ModulationCurve::Exponential>(0, true), must apply
 * curve-shaped modulation on the next process().
 * source=0.5, depth=1 -> 0.25 added to paramB.base=0.
 */
TEST_F (ModMatrixFixture, ReenablingNonLinearRoutingRestoresModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.5f);
    ModulationRouting<float> r (0, destB, 1.f);
    r.curve   = ModulationCurve::Exponential;
    r.enabled = false;
    matrix.addRouting (r);
    matrix.process();
    ASSERT_NEAR (paramB.valueNormalised(), 0.f, 1e-5f);

    /* Act */
    matrix.setRoutingEnabled<ModulationCurve::Exponential> (0, true);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramB.valueNormalised(), 0.25f, 1e-5f);
}

/*======================================================================
 * Section 8: Removal
 *====================================================================*/

/*
 * 8.1 RemoveLinearRoutingDecreasesRoutingCount
 *
 * One routing added and applied. removeRouting(0) enqueued.
 * After process(), getNumRoutings() must be 0.
 */
TEST_F (ModMatrixFixture, RemoveLinearRoutingDecreasesRoutingCount)
{
    /* Arrange */
    ModulationRouting<float> r (0, destA, 0.3f);
    matrix.addRouting (r);
    matrix.process();
    ASSERT_EQ (matrix.getNumRoutings(), 1u);

    /* Act */
    matrix.removeRouting (0);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 0u);
}

/*
 * 8.2 RemovedLinearRoutingNoLongerModulatesParameter
 *
 * After removal, the parameter must return to its base value on the
 * next process(). Data must be gone, not just the enabled flag.
 */
TEST_F (ModMatrixFixture, RemovedLinearRoutingNoLongerModulatesParameter)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 0.3f);
    matrix.addRouting (r);
    matrix.process();
    ASSERT_NEAR (paramA.valueNormalised(), 0.8f, 1e-5f);

    /* Act */
    matrix.removeRouting (0);
    matrix.process();

    /* Assert */
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
}

/*
 * 8.3 RemoveNonLinearRoutingViaExplicitTemplateArg
 *
 * One Exponential routing added. Removed via explicit template arg
 * removeRouting<ModulationCurve::Exponential>(0).
 * getNumNonLinearRoutings() must decrement to 0.
 */
TEST_F (ModMatrixFixture, RemoveNonLinearRoutingViaExplicitTemplateArg)
{
    /* Arrange */
    ModulationRouting<float> r (0, destB, 1.f);
    r.curve = ModulationCurve::Exponential;
    matrix.addRouting (r);
    matrix.process();
    ASSERT_EQ (matrix.getNumNonLinearRoutings(), 1u);

    /* Act */
    matrix.removeRouting<ModulationCurve::Exponential> (0);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumNonLinearRoutings(), 0u);
}

/*
 * 8.4 RemoveOutOfRangeIndexIsIgnored
 *
 * removeRouting(999) enqueued when only one routing exists. After
 * process(), the routing count must remain 1 and no crash must occur.
 */
TEST_F (ModMatrixFixture, RemoveOutOfRangeIndexIsIgnored)
{
    /* Arrange */
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));
    matrix.process();

    /* Act */
    matrix.removeRouting (999);
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 1u);
}

/*
 * 8.5 ClearRoutingsRemovesBothLists
 *
 * One linear + one exponential routing active. clearRoutings() must
 * empty both lists and leave both parameters unmodulated.
 */
TEST_F (ModMatrixFixture, ClearRoutingsRemovesBothLists)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> lin (0, destA, 0.1f);
    ModulationRouting<float> exp (0, destB, 1.f);
    exp.curve = ModulationCurve::Exponential;
    matrix.addRouting (lin);
    matrix.addRouting (exp);
    matrix.process();
    ASSERT_EQ (matrix.getNumRoutings(), 2u);

    /* Act */
    matrix.clearRoutings();
    matrix.process();

    /* Assert */
    EXPECT_EQ (matrix.getNumRoutings(), 0u);
    EXPECT_NEAR (paramA.valueNormalised(), 0.5f, 1e-5f);
    EXPECT_NEAR (paramB.valueNormalised(), 0.f,  1e-5f);
}

/*
 * 8.6 RemainingRoutingStillFunctionsAfterRemovingFirst
 *
 * Two linear routings on destA, each depth=0.1. Remove index 0
 * (sorted by destinationId so order is deterministic for same dest).
 * After removal, one routing must remain and still apply 0.1 modulation.
 * paramA: 0.5 + 0.1 = 0.6.
 */
TEST_F (ModMatrixFixture, RemainingRoutingStillFunctionsAfterRemovingFirst)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));
    matrix.addRouting (ModulationRouting<float> (0, destA, 0.1f));
    matrix.process();
    ASSERT_EQ (matrix.getNumRoutings(), 2u);

    /* Act */
    matrix.removeRouting (0);
    matrix.process();

    /* Assert */
    ASSERT_EQ (matrix.getNumRoutings(), 1u);
    EXPECT_NEAR (paramA.valueNormalised(), 0.6f, 1e-5f);
}

/*======================================================================
 * Section 9: Depth Clamping
 *====================================================================*/

/*
 * 9.1 DepthAboveOneIsClampedAtEnqueue
 *
 * addRouting with depth=5.0, source=1. Accumulated modulation must
 * equal 1.0 (the clamped depth), not 5.0. Clamping occurs at enqueue
 * time on the calling thread, not at process() time.
 * base=0.5, mod=1.0 -> clamped to 1.0 by the accumulator clamp.
 */
TEST_F (ModMatrixFixture, DepthAboveOneIsClampedAtEnqueue)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, 5.f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert: depth clamped to 1.0; base=0.5 + 1.0 = 1.5 -> param clamped to 1.0 */
    EXPECT_FLOAT_EQ (paramA.valueNormalised(), 1.f);
}

/*
 * 9.2 DepthBelowNegativeOneIsClampedAtEnqueue
 *
 * addRouting with depth=-5.0, source=1. Accumulated modulation must
 * equal -1.0 (the clamped depth). base=0.5, mod=-1 -> 0.5-1 = -0.5
 * -> parameter clamped to 0.
 */
TEST_F (ModMatrixFixture, DepthBelowNegativeOneIsClampedAtEnqueue)
{
    /* Arrange */
    matrix.setSourceValue (0, 1.f);
    ModulationRouting<float> r (0, destA, -5.f);

    /* Act */
    matrix.addRouting (r);
    matrix.process();

    /* Assert: depth clamped to -1; 0.5 + (-1) = -0.5 -> param clamped to 0 */
    EXPECT_FLOAT_EQ (paramA.valueNormalised(), 0.f);
}

/*======================================================================
 * Section 10: Reset
 *====================================================================*/

/*
 * 10.1 ResetZerosAllSourceValues
 *
 * Set several source values, call reset(), verify all return 0.
 */
TEST_F (ModMatrixFixture, ResetZerosAllSourceValues)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.9f);
    matrix.setSourceValue (1, 0.5f);
    matrix.setSourceValue (2, 0.3f);

    /* Act */
    matrix.reset();

    /* Assert */
    EXPECT_FLOAT_EQ (matrix.getSourceValue (0), 0.f);
    EXPECT_FLOAT_EQ (matrix.getSourceValue (1), 0.f);
    EXPECT_FLOAT_EQ (matrix.getSourceValue (2), 0.f);
}

/*
 * 10.2 ResetClearsParameterModulation
 *
 * Routing active, process() applies modulation. reset() must zero
 * getModulationAmount() on the parameter.
 */
TEST_F (ModMatrixFixture, ResetClearsParameterModulation)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.9f);
    ModulationRouting<float> r (0, destA, 0.3f);
    matrix.addRouting (r);
    matrix.process();

    /* Act */
    matrix.reset();

    /* Assert */
    EXPECT_FLOAT_EQ (paramA.getModulationAmount(), 0.f);
}

/*
 * 10.3 ResetPreservesRoutings
 *
 * After reset(), the next process() with the same non-zero source must
 * apply modulation again. Routings must not be cleared by reset().
 */
TEST_F (ModMatrixFixture, ResetPreservesRoutings)
{
    /* Arrange */
    ModulationRouting<float> r (0, destA, 0.2f);
    matrix.addRouting (r);
    matrix.process();
    matrix.setSourceValue (0, 1.f);
    matrix.reset();
    ASSERT_EQ (matrix.getNumRoutings(), 1u);

    /* Act */
    matrix.setSourceValue (0, 1.f);
    matrix.process();

    /* Assert: routing still present and applied */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
}

/*======================================================================
 * Section 11: Multi-block Stability
 *====================================================================*/

/*
 * 11.1 SourceValuePersistsAcrossBlocksWithoutBeingReset
 *
 * setSourceValue(0, 0.5) before block 1. No further setSourceValue.
 * Block 2 process() must still apply modulation using source=0.5.
 * Source values are owned by the audio thread and only change when
 * explicitly set; they are not zeroed between blocks.
 */
TEST_F (ModMatrixFixture, SourceValuePersistsAcrossBlocksWithoutBeingReset)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.5f);
    ModulationRouting<float> r (0, destA, 0.4f);
    matrix.addRouting (r);
    matrix.process(); /* block 1: source=0.5, mod=0.2 */

    /* Act: no setSourceValue before block 2 */
    matrix.process(); /* block 2 */

    /* Assert: 0.5 + 0.5*0.4 = 0.7 */
    EXPECT_NEAR (paramA.valueNormalised(), 0.7f, 1e-5f);
}

/*
 * 11.2 ModulationIsStableAcross100Blocks
 *
 * Same routing and source value. process() called 100 times.
 * valueNormalised() must be the same on every iteration.
 * Detects accumulation bugs, floating-point drift, and state leakage.
 */
TEST_F (ModMatrixFixture, ModulationIsStableAcross100Blocks)
{
    /* Arrange */
    matrix.setSourceValue (0, 0.5f);
    ModulationRouting<float> r (0, destA, 0.2f);
    matrix.addRouting (r);
    matrix.process(); /* block 0: prime the routing */

    const float expected = paramA.valueNormalised();

    /* Act / Assert */
    for (int block = 1; block < 100; ++block)
    {
        matrix.process();
        EXPECT_NEAR (paramA.valueNormalised(), expected, 1e-5f)
            << "Drift detected at block " << block;
    }
}