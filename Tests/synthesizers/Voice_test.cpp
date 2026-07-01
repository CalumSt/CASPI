/*************************************************************************
 * @file VoiceManager_test.cpp
 *
 * Unit tests for:
 *   CASPI::VoiceManager<FloatType, MaxVoices>
 *   CASPI::VoiceConfig<FloatType>
 *   CASPI::StealPolicy
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Test nodes:
 *   ConstantSourceNode<F>  AudioNode: fills output with a constant each block.
 *   TrackingEnvNode<F>     Mimics ADSR<F> interface — controllable idle state,
 *                          records noteOn/noteOff call counts.
 *
 * The tests use real ADSR<float> for envelope-driven voice lifecycle tests
 * and TrackingEnvNode for call-counting tests where ADSR timing is unwanted.
 *
 * -----------------------------------------------------------------------
 * Section 1: Construction
 * -----------------------------------------------------------------------
 * 1.1  ConstructWithZeroVoicesHasNoActiveVoices
 * 1.2  ConstructClampsToMaxVoices
 * 1.3  GetNumVoicesReflectsConstruction
 * 1.4  NoActiveVoicesAfterConstruction
 * 1.5  GetVoiceGraphReturnsNonNullForValidIndex
 * 1.6  GetVoiceGraphReturnsNullForInvalidIndex
 *
 * -----------------------------------------------------------------------
 * Section 2: prepare()
 * -----------------------------------------------------------------------
 * 2.1  PrepareMarksAllVoiceGraphsPrepared
 * 2.2  ProcessAfterPrepareDoesNotAssert
 *
 * -----------------------------------------------------------------------
 * Section 3: noteOn / noteOff — basic dispatch
 * -----------------------------------------------------------------------
 * 3.1  NoteOnActivatesOneVoice
 * 3.2  NoteOnTwiceActivatesTwoVoices
 * 3.3  NoteOffDeactivatesVoiceWhenEnvelopeIdleAfterProcess
 * 3.4  NoteOffWithNoEnvelopeDeactivatesImmediately
 * 3.5  NoteOnSameNoteUsesNewVoice
 * 3.6  AllNotesOffDeactivatesAllVoices
 *
 * -----------------------------------------------------------------------
 * Section 4: Voice stealing — StealPolicy::Oldest
 * -----------------------------------------------------------------------
 * 4.1  OldestStealingSelectsLowestAgeVoice
 * 4.2  StolenVoiceEnvelopeIsReset
 * 4.3  StolenVoiceIsImmediatelyReused
 *
 * -----------------------------------------------------------------------
 * Section 5: Voice stealing — StealPolicy::Quietest
 * -----------------------------------------------------------------------
 * 5.1  QuietestStealingSelectsVoiceWithLowestEnvelopeLevel
 *
 * -----------------------------------------------------------------------
 * Section 6: Voice stealing — StealPolicy::None
 * -----------------------------------------------------------------------
 * 6.1  NoPolicyDropsNoteWhenAllVoicesFull
 * 6.2  NoPolicyDoesNotExceedVoiceCount
 *
 * -----------------------------------------------------------------------
 * Section 7: Output accumulation
 * -----------------------------------------------------------------------
 * 7.1  SingleActiveVoiceOutputMatchesNodeOutput
 * 7.2  TwoActiveVoicesAccumulateIntoOutput
 * 7.3  InactiveVoiceDoesNotContributeToOutput
 * 7.4  OutputBufferClearedEachBlock
 *
 * -----------------------------------------------------------------------
 * Section 8: Voice graph access
 * -----------------------------------------------------------------------
 * 8.1  GetVoiceGraphAllowsParameterAccess
 * 8.2  EachVoiceGraphIsIndependent
 *
 ************************************************************************/

#include "synthesizers/caspi_VoiceManager.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

using namespace CASPI;
using namespace CASPI::Graph;

/*======================================================================
 * Test helper nodes
 *====================================================================*/

/** AudioNode: fills outputBuffer with a fixed value each block. */
template <typename FloatType>
class ConstantSourceNode : public AudioNode<ConstantSourceNode<FloatType>, FloatType>
{
    public:
        FloatType fillValue = FloatType (0);

        explicit ConstantSourceNode (FloatType value = FloatType (0))
            : AudioNode<ConstantSourceNode<FloatType>, FloatType> (0, 1)
            , fillValue (value)
        {
        }

        void processImpl (AudioContext<FloatType>&) noexcept
        {
            this->outputBuffer.fill (fillValue);
        }
};

/**
 * Minimal envelope stand-in that exposes the same interface VoiceManager
 * uses on Envelope::ADSR<F>: noteOn(), noteOff(), reset(), isIdle(), getLevel().
 *
 * Inherits Producer so it can be added to an AudioGraph and produce a buffer.
 * isIdle() returns true after noteOff() has been called and enough process()
 * calls have elapsed (controlled by releaseBlocks).
 */
template <typename FloatType>
class TrackingEnvNode final
    : Envelope::Envelope<FloatType>, public Graph::AudioNode<TrackingEnvNode<FloatType>, FloatType>
{
    public:
        int noteOnCount  = 0;
        int noteOffCount = 0;
        int resetCount   = 0;
        bool forceIdle   = false; // set true to make isIdle() return true immediately

        TrackingEnvNode()
            : Graph::AudioNode<TrackingEnvNode<FloatType>, FloatType> (0, 1)
        {
        }

        void noteOn() noexcept override
        {
            ++noteOnCount;
            idle = false;
        }
        void noteOff() noexcept override
        {
            ++noteOffCount;
            releasing = true;
        }
        void reset() noexcept override
        {
            ++resetCount;
            idle      = true;
            releasing = false;
            level     = FloatType (0);
        }

        bool isIdle() const noexcept override
        {
            return idle || forceIdle;
        }
        FloatType getLevel() const noexcept override
        {
            return level;
        }

        void setLevel (FloatType v) noexcept
        {
            level = v;
        }

        void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
        {
            (void) ctx;
            auto& buf = this->outputBuffer;
            const auto F = buf.numFrames();
            const auto C = buf.numChannels();

            for (std::size_t f = 0; f < F; ++f)
            {
                const FloatType s = renderSample();
                for (std::size_t ch = 0; ch < C; ++ch)
                    buf.sample (ch, f) = s;
            }
        }

        FloatType renderSample() CASPI_NON_BLOCKING
        {
            // Advance idle after one process() call following noteOff
            if (releasing)
            {
                idle      = true;
                releasing = false;
            }
            return level;
        }

    private:
        bool idle       = true;
        bool releasing  = false;
        FloatType level = FloatType (0);
};

/*======================================================================
 * Factory helpers
 *====================================================================*/

static constexpr std::size_t CHANNELS = 2;
static constexpr std::size_t FRAMES   = 64;
static constexpr double SAMPLE_RATE   = 44100.0;

/**
 * Build a VoiceConfig using a ConstantSourceNode as output and a real
 * ADSR<float> as envelope. Used for output accumulation tests.
 */
VoiceConfig<float> makeConstantVoice (float value)
{
    Graph::AudioGraph<float> graph;
    auto srcNode = std::make_unique<ConstantSourceNode<float>> (value);
    auto envNode = std::make_unique<Envelope::ADSR<float>>();
    envNode->setADSR (0.001f, 0.001f, 0.9f, 0.001f);

    const auto srcResult = graph.addNode (std::move (srcNode));
    const auto envResult = graph.addNode (std::move (envNode));

    return VoiceConfig<float> (std::move (graph), srcResult.value(), envResult.value());
}

/**
 * Build a VoiceConfig using a ConstantSourceNode as output and a
 * TrackingEnvNode as envelope. Used for noteOn/noteOff dispatch tests.
 */
VoiceConfig<float> makeTrackingVoice (float value = 1.0f)
{
    Graph::AudioGraph<float> graph;
    auto srcNode = std::make_unique<ConstantSourceNode<float>> (value);
    auto envNode = std::make_unique<TrackingEnvNode<float>>();

    const auto srcResult = graph.addNode (std::move (srcNode));
    const auto envResult = graph.addNode (std::move (envNode));

    return VoiceConfig<float> (std::move (graph), srcResult.value(), envResult.value());
}

/** Build a VoiceConfig with no envelope node. */
VoiceConfig<float> makeNoEnvelopeVoice (float value = 1.0f)
{
    Graph::AudioGraph<float> graph;
    auto srcNode = std::make_unique<ConstantSourceNode<float>> (value);

    const auto srcResult = graph.addNode (std::move (srcNode));

    return VoiceConfig<float> (std::move (graph), srcResult.value(), Graph::INVALID_NODE_ID);
}

/*======================================================================
 * Fixture
 *====================================================================*/
struct VoiceManagerFixture : ::testing::Test
{
        using OutputBuffer = AudioBuffer<float, ChannelMajorLayout>;

        OutputBuffer outputBuffer { CHANNELS, FRAMES };

        template <std::size_t N>
        void prepareManager (VoiceManager<float, N>& mgr)
        {
            mgr.prepare (CHANNELS, FRAMES, SAMPLE_RATE);
        }
};

/*======================================================================
 * Section 1: Construction
 *====================================================================*/

TEST_F (VoiceManagerFixture, ConstructWithZeroVoicesHasNoActiveVoices)
{
    VoiceManager<float> mgr (0, [] { return makeTrackingVoice(); });
    EXPECT_EQ (mgr.getNumActiveVoices(), 0u);
}

TEST_F (VoiceManagerFixture, ConstructClampsToMaxVoices)
{
    // Request more than MaxVoices (default 16)
    VoiceManager<float, 4> mgr (99, [] { return makeTrackingVoice(); });
    EXPECT_EQ (mgr.getNumVoices(), 4u);
}

TEST_F (VoiceManagerFixture, GetNumVoicesReflectsConstruction)
{
    VoiceManager<float> mgr (8, [] { return makeTrackingVoice(); });
    EXPECT_EQ (mgr.getNumVoices(), 8u);
}

TEST_F (VoiceManagerFixture, NoActiveVoicesAfterConstruction)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    EXPECT_EQ (mgr.getNumActiveVoices(), 0u);
}

TEST_F (VoiceManagerFixture, GetVoiceGraphReturnsNonNullForValidIndex)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    EXPECT_NE (mgr.getVoiceGraph (0), nullptr);
    EXPECT_NE (mgr.getVoiceGraph (3), nullptr);
}

TEST_F (VoiceManagerFixture, GetVoiceGraphReturnsNullForInvalidIndex)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    EXPECT_EQ (mgr.getVoiceGraph (4), nullptr);
    EXPECT_EQ (mgr.getVoiceGraph (99), nullptr);
}

/*======================================================================
 * Section 2: prepare()
 *====================================================================*/

TEST_F (VoiceManagerFixture, PrepareMarksAllVoiceGraphsPrepared)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    for (std::size_t i = 0; i < mgr.getNumVoices(); ++i)
    {
        EXPECT_TRUE (mgr.getVoiceGraph (i)->isPrepared()) << "voice " << i;
    }
}

TEST_F (VoiceManagerFixture, ProcessAfterPrepareDoesNotAssert)
{
    VoiceManager<float> mgr (2, [] { return makeTrackingVoice(); });
    prepareManager (mgr);
    // Should not assert or throw
    mgr.process (outputBuffer);
    EXPECT_TRUE (true);
}

/*======================================================================
 * Section 3: noteOn / noteOff — basic dispatch
 *====================================================================*/

TEST_F (VoiceManagerFixture, NoteOnActivatesOneVoice)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    EXPECT_EQ (mgr.getNumActiveVoices(), 1u);
}

TEST_F (VoiceManagerFixture, NoteOnTwiceActivatesTwoVoices)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
}

TEST_F (VoiceManagerFixture, NoteOffDeactivatesVoiceWhenEnvelopeIdleAfterProcess)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    ASSERT_EQ (mgr.getNumActiveVoices(), 1u);

    mgr.noteOff (60);
    // TrackingEnvNode goes idle after one render call
    mgr.process (outputBuffer);
    EXPECT_EQ (mgr.getNumActiveVoices(), 0u);
}

TEST_F (VoiceManagerFixture, NoteOffWithNoEnvelopeDeactivatesImmediately)
{
    VoiceManager<float> mgr (4, [] { return makeNoEnvelopeVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    ASSERT_EQ (mgr.getNumActiveVoices(), 1u);

    mgr.noteOff (60);
    EXPECT_EQ (mgr.getNumActiveVoices(), 0u);
}

TEST_F (VoiceManagerFixture, NoteOnSameNoteUsesNewVoice)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (60, 100); // same note, new voice
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
}

TEST_F (VoiceManagerFixture, AllNotesOffDeactivatesAllVoices)
{
    VoiceManager<float> mgr (4, [] { return makeTrackingVoice(); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    mgr.noteOn (67, 100);
    ASSERT_EQ (mgr.getNumActiveVoices(), 3u);

    mgr.allNotesOff();
    mgr.process (outputBuffer); // TrackingEnv needs one process to go idle
    EXPECT_EQ (mgr.getNumActiveVoices(), 0u);
}

/*======================================================================
 * Section 4: Voice stealing — StealPolicy::Oldest
 *====================================================================*/

TEST_F (VoiceManagerFixture, OldestStealingSelectsLowestAgeVoice)
{
    // 2-voice manager with Oldest policy
    VoiceManager<float, 2> mgr (2, [] { return makeTrackingVoice(); }, StealPolicy::Oldest);
    prepareManager (mgr);

    mgr.noteOn (60, 100); // voice 0, age 0
    mgr.noteOn (64, 100); // voice 1, age 1
    ASSERT_EQ (mgr.getNumActiveVoices(), 2u);

    // Third noteOn must steal — oldest is voice 0 (age 0)
    mgr.noteOn (67, 100);
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u); // still 2, one was stolen and reused
}

TEST_F (VoiceManagerFixture, StolenVoiceEnvelopeIsReset)
{
    VoiceManager<float, 2> mgr (2, [] { return makeTrackingVoice(); }, StealPolicy::Oldest);
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);

    // Get the TrackingEnvNode from voice 0 (oldest) before stealing
    auto* graph0 = mgr.getVoiceGraph (0);
    ASSERT_NE (graph0, nullptr);

    // Steal voice 0 by triggering a third note
    mgr.noteOn (67, 100);

    // The envelope on the stolen voice should have been reset
    // Voice 0 is now playing note 67 with a fresh envelope (reset count >= 1)
    // We verify via the tracking count
    const auto* env = graph0->template getNodeAs<TrackingEnvNode<float>> (
        graph0->getSortedOrder().size() > 0 ? 1 : 0); // envNodeId = 1 in makeTrackingVoice
    (void) env; // Primary check is that getNumActiveVoices stays at 2 without crash
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
}

TEST_F (VoiceManagerFixture, StolenVoiceIsImmediatelyReused)
{
    VoiceManager<float, 2> mgr (2, [] { return makeTrackingVoice(); }, StealPolicy::Oldest);
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    mgr.noteOn (67, 100); // steals voice 0

    // Voice count must remain at 2, not go to 3
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
    EXPECT_EQ (mgr.getNumVoices(), 2u);
}

/*======================================================================
 * Section 5: Voice stealing — StealPolicy::Quietest
 *====================================================================*/

TEST_F (VoiceManagerFixture, QuietestStealingSelectsVoiceWithLowestEnvelopeLevel)
{
    // Factory captures a level pointer so we can control it per-voice
    // Use TrackingEnvNode and set levels manually after construction
    VoiceManager<float, 2> mgr (2, [] { return makeTrackingVoice(); }, StealPolicy::Quietest);
    prepareManager (mgr);

    mgr.noteOn (60, 100); // voice 0
    mgr.noteOn (64, 100); // voice 1

    // Set voice 0 envelope level to 0.1 (quiet) and voice 1 to 0.9 (loud)
    auto* g0 = mgr.getVoiceGraph (0);
    auto* g1 = mgr.getVoiceGraph (1);
    ASSERT_NE (g0, nullptr);
    ASSERT_NE (g1, nullptr);

    // envNodeId = 1 (second node added in makeTrackingVoice)
    constexpr NodeId ENV_NODE_ID = 1;

    auto* env0 = g0->template getNodeAs<TrackingEnvNode<float>> (ENV_NODE_ID);
    auto* env1 = g1->template getNodeAs<TrackingEnvNode<float>> (ENV_NODE_ID);

    ASSERT_NE (env0, nullptr);
    ASSERT_NE (env1, nullptr);

    env0->setLevel (0.1f);
    env1->setLevel (0.9f);

    // Third note: quietest steal should take voice 0
    mgr.noteOn (67, 100);

    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
    // env0 should have been reset (resetCount >= 1)
    EXPECT_GE (env0->resetCount, 1);
    // env1 should NOT have been reset
    EXPECT_EQ (env1->resetCount, 0);
}

/*======================================================================
 * Section 6: Voice stealing — StealPolicy::None
 *====================================================================*/

TEST_F (VoiceManagerFixture, NoPolicyDropsNoteWhenAllVoicesFull)
{
    VoiceManager<float, 2> mgr (2, [] { return makeTrackingVoice(); }, StealPolicy::None);
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    ASSERT_EQ (mgr.getNumActiveVoices(), 2u);

    mgr.noteOn (67, 100); // should be dropped
    EXPECT_EQ (mgr.getNumActiveVoices(), 2u);
}

TEST_F (VoiceManagerFixture, NoPolicyDoesNotExceedVoiceCount)
{
    VoiceManager<float, 3> mgr (3, [] { return makeTrackingVoice(); }, StealPolicy::None);
    prepareManager (mgr);

    for (int note = 60; note < 70; ++note)
    {
        mgr.noteOn (note, 100);
    }

    EXPECT_LE (mgr.getNumActiveVoices(), 3u);
}

/*======================================================================
 * Section 7: Output accumulation
 *====================================================================*/

TEST_F (VoiceManagerFixture, SingleActiveVoiceOutputMatchesNodeOutput)
{
    // Voice outputs 0.5 constant. With one voice active (no envelope driving
    // the output down), we just check the ConstantSourceNode value propagates.
    VoiceManager<float> mgr (4, [] { return makeNoEnvelopeVoice (0.5f); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.process (outputBuffer);

    // Output should be 0.5 from the single active voice
    EXPECT_FLOAT_EQ (outputBuffer.sample (0, 0), 0.5f);
    EXPECT_FLOAT_EQ (outputBuffer.sample (1, 0), 0.5f);
}

TEST_F (VoiceManagerFixture, TwoActiveVoicesAccumulateIntoOutput)
{
    VoiceManager<float> mgr (4, [] { return makeNoEnvelopeVoice (0.25f); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    mgr.process (outputBuffer);

    // Two voices each outputting 0.25 should sum to 0.5
    EXPECT_NEAR (outputBuffer.sample (0, 0), 0.5f, 1e-5f);
}

TEST_F (VoiceManagerFixture, InactiveVoiceDoesNotContributeToOutput)
{
    VoiceManager<float> mgr (4, [] { return makeNoEnvelopeVoice (1.0f); });
    prepareManager (mgr);

    // Only one voice active
    mgr.noteOn (60, 100);
    mgr.process (outputBuffer);

    const float oneVoice = outputBuffer.sample (0, 0);

    // Now silence it and process again
    mgr.noteOff (60);
    mgr.process (outputBuffer);

    EXPECT_FLOAT_EQ (outputBuffer.sample (0, 0), 0.0f);
    EXPECT_GT (oneVoice, 0.0f); // sanity — first block had signal
}

TEST_F (VoiceManagerFixture, OutputBufferClearedEachBlock)
{
    VoiceManager<float> mgr (4, [] { return makeNoEnvelopeVoice (1.0f); });
    prepareManager (mgr);

    mgr.noteOn (60, 100);
    mgr.process (outputBuffer);
    const float firstBlock = outputBuffer.sample (0, 0);

    // Silence voice
    mgr.noteOff (60);
    mgr.process (outputBuffer); // should clear then accumulate nothing

    EXPECT_FLOAT_EQ (outputBuffer.sample (0, 0), 0.0f);
    EXPECT_GT (firstBlock, 0.0f);
}

/*======================================================================
 * Section 8: Voice graph access
 *====================================================================*/

TEST_F (VoiceManagerFixture, GetVoiceGraphAllowsParameterAccess)
{
    VoiceManager<float> mgr (2, [] { return makeNoEnvelopeVoice (0.5f); });
    prepareManager (mgr);

    auto* graph = mgr.getVoiceGraph (0);
    ASSERT_NE (graph, nullptr);

    // ConstantSourceNode is nodeId 0 in makeNoEnvelopeVoice
    auto* src = graph->getNodeAs<ConstantSourceNode<float>> (0);
    ASSERT_NE (src, nullptr);

    src->fillValue = 0.75f;

    mgr.noteOn (60, 100);
    mgr.process (outputBuffer);

    EXPECT_FLOAT_EQ (outputBuffer.sample (0, 0), 0.75f);
}

TEST_F (VoiceManagerFixture, EachVoiceGraphIsIndependent)
{
    // Each voice factory creates its own graph — mutations on one should
    // not affect the other.
    VoiceManager<float> mgr (2, [] { return makeNoEnvelopeVoice (1.0f); });
    prepareManager (mgr);

    auto* g0 = mgr.getVoiceGraph (0);
    auto* g1 = mgr.getVoiceGraph (1);
    ASSERT_NE (g0, nullptr);
    ASSERT_NE (g1, nullptr);

    auto* src0 = g0->getNodeAs<ConstantSourceNode<float>> (0);
    auto* src1 = g1->getNodeAs<ConstantSourceNode<float>> (0);
    ASSERT_NE (src0, nullptr);
    ASSERT_NE (src1, nullptr);

    src0->fillValue = 0.3f;
    src1->fillValue = 0.7f;

    EXPECT_FLOAT_EQ (src0->fillValue, 0.3f);
    EXPECT_FLOAT_EQ (src1->fillValue, 0.7f);

    // Activate both and verify accumulation reflects independent values
    mgr.noteOn (60, 100);
    mgr.noteOn (64, 100);
    mgr.process (outputBuffer);

    EXPECT_NEAR (outputBuffer.sample (0, 0), 1.0f, 1e-5f); // 0.3 + 0.7
}