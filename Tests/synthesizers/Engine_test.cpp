/*************************************************************************
 * @file caspi_Engine_test.cpp
 *
 * Unit tests for:
 *   CASPI::Engine<FloatType, MaxVoices, Config>
 *   CASPI::DefaultSynthConfig / custom Config structs
 *   MPE dispatch logic (MpeConfig)
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * Helper infrastructure:
 *   DummyConfig         — minimal Config struct with small queue for speed.
 *   SaConfig            — DummyConfig with SampleAccurate = true.
 *   MpeConfig           — DummyConfig with MpeEnabled = true.
 *   makeEngine()        — builds an Engine<float,4,DummyConfig> with a
 *                         trivial factory: ConstantNode voice, no envelope.
 *   NoteLog             — records (note, vel, ch, voiceIdx) from onNoteOn.
 *   CcLog               — records (ccNum, ccVal, ch) from onControlChange.
 *
 * -----------------------------------------------------------------------
 * Section 1: Construction and prepare()
 * -----------------------------------------------------------------------
 * 1.1  ConstructsWithoutError
 * 1.2  NumVoicesReflectsArgument
 * 1.3  GetSampleRateBeforePrepareIsZero
 * 1.4  GetSampleRateAfterPrepare
 * 1.5  GetNumFramesAfterPrepare
 * 1.6  GetNumChannelsAfterPrepare
 * 1.7  NoActiveVoicesInitially
 * 1.8  OutputBufferGeometryAfterPrepare
 *
 * -----------------------------------------------------------------------
 * Section 2: MIDI ingestion — pushMidi / pushNoteOn / pushCC / pushPitchBend
 * -----------------------------------------------------------------------
 * 2.1  PushNoteOnEnqueues
 *      After pushNoteOn + process(), onNoteOn fires once.
 * 2.2  PushNoteOffEnqueues
 *      After pushNoteOn + process() + pushNoteOff + process(), active voices = 0.
 * 2.3  PushCCEnqueues
 *      getCCValue(7) is 0 before push, reflects value after process().
 * 2.4  PushPitchBend
 *      getPitchBend() is 0 before push, non-zero after process().
 * 2.5  PushMidiDirectly
 *      pushMidi() with a raw MidiMessage works identically to helpers.
 * 2.6  QueueBelowCapacitySucceeds
 *      Able to push to queue while it is below capacity
 * 2.7  QueueAtCapacityReturnsFalse
 *      Filling the queue to Config::MidiQueueCapacity causes the next
 *      push to return false.
 *
 * -----------------------------------------------------------------------
 * Section 3: Event dispatch — callbacks
 * -----------------------------------------------------------------------
 * 3.1  OnNoteOnFiredOnce
 * 3.2  OnNoteOnVoiceIdxIsValid
 *      voiceIdx < engine.getNumVoices().
 * 3.3  OnNoteOnNoteAndVelocityMatch
 * 3.4  OnNoteOnChannelMatches
 * 3.5  OnNoteOffFiredAfterNoteOff
 * 3.6  OnNoteOffNoteMatches
 * 3.7  OnControlChangeFired
 * 3.8  OnControlChangeCCNumberAndValue
 * 3.9  OnPitchBendFiredAndNormalised
 *      Full positive deflection fires onPitchBend with value near +1.
 * 3.10 OnChannelPressureFired
 * 3.11 OnPolyAftertouchFired
 * 3.12 OnProgramChangeFired
 * 3.13 AllNotesOffCC123TriggersAllNotesOff
 *      CC 123 dispatched -> allNotesOff() -> active voices == 0.
 * 3.14 AllSoundOffCC120TriggersAllNotesOff
 *
 * -----------------------------------------------------------------------
 * Section 4: Controller state cache
 * -----------------------------------------------------------------------
 * 4.1  CCCacheInitiallyZero
 * 4.2  CCCacheUpdatedAfterProcess
 * 4.3  CCCachePerChannel
 *      CC 7 set on ch 0, ch 1 still returns 0.
 * 4.4  PitchBendCacheInitiallyZero
 * 4.5  PitchBendCacheUpdatedAfterProcess
 * 4.6  PitchBendCachePerChannel
 * 4.7  ChannelPressureCacheUpdatedAfterProcess
 * 4.8  CCNormalisedMaxIsOne
 * 4.9  CCNormalisedZeroIsZero
 * 4.10 ResetControllerStateClearsCache
 *      After setCCValue via event + process(), resetControllerState()
 *      returns getCCValue to 0 and getPitchBend to 0.
 * 4.11 OutOfRangeChannelReturnsSafeDefault
 *      getCCValue / getPitchBend / getChannelPressure with ch >= 16 return 0.
 *
 * -----------------------------------------------------------------------
 * Section 5: Voice activation and counting
 * -----------------------------------------------------------------------
 * 5.1  NoteOnActivatesVoice
 * 5.2  NoteOffDeactivatesVoiceWithNoEnvelope
 *      Voice with no envelope deactivates on noteOff (VoiceManager behaviour).
 * 5.3  MultipleNoteOnActivatesMultipleVoices
 * 5.4  AllNotesOffDeactivatesAllVoices
 *
 * -----------------------------------------------------------------------
 * Section 6: Output buffer
 * -----------------------------------------------------------------------
 * 6.1  OutputBufferClearedEachBlock
 *      With no voices active, all output samples are 0.
 * 6.2  ActiveVoiceContributesToOutput
 *      A voice whose ConstantNode fills with 0.5f -> output samples > 0.
 * 6.3  ProcessAccumulatesIntoExternalBuffer
 *      process(dst) adds engine output to pre-filled dst.
 * 6.4  MasterGainApplied
 *      A Config with MasterGain = 0.5f halves the output.
 * 6.5  HardClipClampsOutput
 *      A Config with HardClip = true and MasterGain = 4.0f clamps to [-1,+1].
 *
 * -----------------------------------------------------------------------
 * Section 7: Sample-accurate mode
 * -----------------------------------------------------------------------
 * 7.1  SampleAccurateEventBeforeRenderFiresBeforeBlock
 *      An event at offset 0 fires before VoiceManager processes.
 * 7.2  SampleAccurateMultipleOffsets
 *      Two note-ons at different offsets both fire onNoteOn.
 * 7.3  NegativeOffsetClampedToZero
 *      sampleOffset < 0 does not crash and fires the event.
 * 7.4  OffsetBeyondBlockClampedToBlockEnd
 *      sampleOffset > numFrames does not write out of bounds.
 *
 * -----------------------------------------------------------------------
 * Section 8: MPE — zone configuration and per-note routing
 * -----------------------------------------------------------------------
 * 8.1  MpeDisabledByDefault
 *      DefaultSynthConfig::MpeEnabled does not exist; Engine compiles
 *      without it (tested by checking normal dispatch still works).
 * 8.2  MpeEnabledEngineCompiles
 *      Engine<float, 8, MpeConfig> constructs without error.
 * 8.3  MpeNoteOnOnMemberChannelFiresCallback
 *      Note-on on ch 2 (lower zone member) fires onNoteOn.
 * 8.4  MpeNoteOnChannelMatchesMidiChannel
 *      onNoteOn receives the correct MIDI channel (2, not 0).
 * 8.5  MpePerNotePitchBendUpdatesChannelCache
 *      Pitch bend on ch 2 updates channelState[2], not channelState[0].
 * 8.6  MpePerNoteChannelPressureUpdatesChannelCache
 *      Channel pressure on ch 2 updates channelState[2].
 * 8.7  MpeMasterChannelPitchBendUpdatesChannel1
 *      Pitch bend on ch 1 (lower master) updates channelState[1].
 * 8.8  MpeNoteOffOnSameChannelDeactivates
 *      Note-off on the same channel as the note-on deactivates the voice.
 * 8.9  MpeCc74SlideUpdatesCache
 *      CC 74 (slide) on a member channel is stored in channelState[ch].cc[74].
 * 8.10 MpeAllNotesOffSilencesAllVoices
 *      CC 123 on the master channel fires allNotesOff.
 *
 ************************************************************************/

#include "base/caspi_RealtimeContext.h"
#include "synthesizers/caspi_Engine.h"
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

using namespace CASPI;
using namespace CASPI::Midi;

/*======================================================================
 * Test configurations
 *====================================================================*/

/** Small queue so overflow tests don't need 512 pushes. */
struct DummyConfig : CASPI::DefaultSynthConfig
{
    static constexpr bool        SampleAccurate    = false;
    static constexpr std::size_t MidiQueueCapacity = 16u;
    static constexpr float       MasterGain        = 1.0f;
    static constexpr bool        HardClip          = false;
    static constexpr std::size_t NumMidiChannels   = 16u;
    static constexpr bool        MpeEnabled        = false;
};

struct SaConfig : DummyConfig
{
    static constexpr bool SampleAccurate = true;
};

struct HalfGainConfig : DummyConfig
{
    static constexpr float MasterGain = 0.5f;
};

struct ClipConfig : DummyConfig
{
    static constexpr float MasterGain = 4.0f;
    static constexpr bool  HardClip   = true;
};

struct MpeConfig : DummyConfig
{
    static constexpr bool        MpeEnabled      = true;
    static constexpr std::size_t LowerZoneSize   = 4u; // ch 2-5
    static constexpr std::size_t UpperZoneSize   = 0u; // unused in these tests
    static constexpr std::size_t MidiQueueCapacity = 32u;
};

/*======================================================================
 * Minimal node stubs (no AudioGraph compile dependency)
 *====================================================================*/

/**
 * Trivial voice factory: returns a VoiceConfig with an AudioGraph containing
 * a single ConstantNode that fills its output with `fillValue`. No envelope.
 *
 * Because these tests focus on Engine dispatch, not AudioGraph signal flow,
 * the factory just needs to produce valid VoiceConfig objects.
 */
static VoiceConfig<float> makeConstantVoiceConfig (float fillValue = 0.25f)
{
    // Build the simplest possible graph: one ConstantNode output.
    // (ConstantNode is defined in Graph_test.cpp in the real test suite;
    //  here we use the same pattern inline.)
    using FloatType = float;

    Graph::AudioGraph<FloatType> g;

    // A minimal AudioNode CRTP stub — fills its output buffer with fillValue.
    struct FillNode : public Graph::AudioNode<FillNode, FloatType>
    {
        float value = 0.0f;
        explicit FillNode (float v)
            : Graph::AudioNode<FillNode, FloatType> (0, 1)
            , value (v)
        {
        }
        void onPrepare (std::size_t, std::size_t, double) noexcept {}
        void processImpl (Graph::AudioContext<FloatType>&) noexcept
        {
            this->outputBuffer.fill (value);
        }
    };

    auto nodeResult = g.addNode (std::make_unique<FillNode> (fillValue));
    EXPECT_TRUE (nodeResult.has_value());
    const Graph::NodeId outId = nodeResult.value();

    return VoiceConfig<FloatType> { std::move (g), outId, Graph::INVALID_NODE_ID };
}

/*======================================================================
 * Engine fixture helpers
 *====================================================================*/

static constexpr std::size_t kNumVoices  = 4u;
static constexpr std::size_t kChannels   = 2u;
static constexpr std::size_t kFrames     = 64u;
static constexpr double      kSampleRate = 44100.0;

template <typename Cfg = DummyConfig>
Engine<float, kNumVoices, Cfg> makeEngine (float voiceFill = 0.25f)
{
    Engine<float, kNumVoices, Cfg> eng (
        kNumVoices,
        [voiceFill]() { return makeConstantVoiceConfig (voiceFill); });
    return eng;
}

template <typename Cfg = DummyConfig>
Engine<float, kNumVoices, Cfg> makePreparedEngine (float voiceFill = 0.25f)
{
    auto eng = makeEngine<Cfg> (voiceFill);
    eng.prepare (kChannels, kFrames, kSampleRate);
    return eng;
}

/*======================================================================
 * Logging helpers
 *====================================================================*/

struct NoteOnRecord
{
    uint8_t     note;
    uint8_t     vel;
    uint8_t     channel;
    std::size_t voiceIdx;
};

struct NoteOffRecord
{
    uint8_t note;
    uint8_t channel;
};

struct CcRecord
{
    uint8_t ccNum;
    uint8_t ccVal;
    uint8_t channel;
};

/*======================================================================
 * Section 1: Construction and prepare()
 *====================================================================*/

TEST (EngineConstruct, ConstructsWithoutError)
{
    EXPECT_NO_FATAL_FAILURE ({ auto eng = makeEngine(); (void)eng; });
}

TEST (EngineConstruct, NumVoicesReflectsArgument)
{
    auto eng = makeEngine();
    EXPECT_EQ (eng.getNumVoices(), kNumVoices);
}

TEST (EngineConstruct, GetSampleRateBeforePrepareIsZero)
{
    auto eng = makeEngine();
    EXPECT_DOUBLE_EQ (eng.getSampleRate(), 0.0);
}

TEST (EngineConstruct, GetSampleRateAfterPrepare)
{
    auto eng = makePreparedEngine();
    EXPECT_DOUBLE_EQ (eng.getSampleRate(), kSampleRate);
}

TEST (EngineConstruct, GetNumFramesAfterPrepare)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ (eng.getNumFrames(), kFrames);
}

TEST (EngineConstruct, GetNumChannelsAfterPrepare)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ (eng.getNumChannels(), kChannels);
}

TEST (EngineConstruct, NoActiveVoicesInitially)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

TEST (EngineConstruct, OutputBufferGeometryAfterPrepare)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ (eng.getOutputBuffer().numChannels(), kChannels);
    EXPECT_EQ (eng.getOutputBuffer().numFrames(),   kFrames);
}

/*======================================================================
 * Section 2: MIDI ingestion
 *====================================================================*/

TEST (EngineMidiIngestion, PushNoteOnEnqueues)
{
    auto eng = makePreparedEngine();

    int noteOnCount = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++noteOnCount; };

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();

    EXPECT_EQ (noteOnCount, 1);
}

TEST (EngineMidiIngestion, PushNoteOffEnqueues)
{
    auto eng = makePreparedEngine();

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    ASSERT_GT (eng.getNumActiveVoices(), 0u);

    eng.pushNoteOff (0u, 60u);
    eng.process();

    // VoiceManager with no envelope deactivates immediately on noteOff.
    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

TEST (EngineMidiIngestion, PushCCEnqueues)
{
    auto eng = makePreparedEngine();
    ASSERT_EQ (eng.getCCValue (7u, 0u), 0u);

    eng.pushCC (0u, 7u, 96u);
    eng.process();

    EXPECT_EQ (eng.getCCValue (7u, 0u), 96u);
}

TEST (EngineMidiIngestion, PushPitchBend)
{
    auto eng = makePreparedEngine();
    ASSERT_FLOAT_EQ (eng.getPitchBend (0u), 0.0f);

    eng.pushPitchBend (0u, static_cast<int16_t> (8191));
    eng.process();

    EXPECT_NEAR (eng.getPitchBend (0u), 1.0f, 1e-4f);
}

TEST (EngineMidiIngestion, PushMidiDirectly)
{
    auto eng = makePreparedEngine();

    int noteOnCount = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++noteOnCount; };

    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 100u);
    eng.pushMidi (msg);
    eng.process();

    EXPECT_EQ (noteOnCount, 1);
}

TEST (EngineMidiIngestion, QueueBelowCapacitySucceeds)
{
    auto eng = makePreparedEngine();
    for (std::size_t i = 0; i < DummyConfig::MidiQueueCapacity - 1; ++i)
    {
        EXPECT_TRUE (eng.pushNoteOn (0u, static_cast<uint8_t> (i % 127u), 100u));
    }
}

TEST (EngineMidiIngestion, QueueAtCapacityReturnsFalse)
{
    auto eng = makePreparedEngine();

    // Fill the queue (DummyConfig::MidiQueueCapacity == 16).
    bool allSucceeded = true;
    for (std::size_t i = 0; i < DummyConfig::MidiQueueCapacity; ++i)
        allSucceeded &= eng.pushNoteOn (0u, static_cast<uint8_t> (i % 127u), 100u);

    EXPECT_TRUE (allSucceeded) << "First 16 pushes should succeed";

    const bool overflow = eng.pushNoteOn (0u, 60u, 100u);
    EXPECT_FALSE (overflow) << "17th push must fail when queue is full";
}

/*======================================================================
 * Section 3: Event dispatch callbacks
 *====================================================================*/

TEST (EngineCallbacks, OnNoteOnFiredOnce)
{
    auto eng = makePreparedEngine();
    int count = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++count; };

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();

    EXPECT_EQ (count, 1);
}

TEST (EngineCallbacks, OnNoteOnVoiceIdxIsValid)
{
    auto eng = makePreparedEngine();
    std::size_t capturedIdx = 999u;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t vi) { capturedIdx = vi; };

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();

    EXPECT_LT (capturedIdx, eng.getNumVoices());
}

TEST (EngineCallbacks, OnNoteOnNoteAndVelocityMatch)
{
    auto eng = makePreparedEngine();
    NoteOnRecord rec {};
    eng.onNoteOn = [&](uint8_t n, uint8_t v, uint8_t ch, std::size_t vi)
    {
        rec = { n, v, ch, vi };
    };

    eng.pushNoteOn (0u, 69u, 127u);
    eng.process();

    EXPECT_EQ (rec.note, 69u);
    EXPECT_EQ (rec.vel,  127u);
}

TEST (EngineCallbacks, OnNoteOnChannelMatches)
{
    auto eng = makePreparedEngine();
    uint8_t capturedCh = 99u;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t ch, std::size_t) { capturedCh = ch; };

    eng.pushNoteOn (5u, 60u, 100u);
    eng.process();

    EXPECT_EQ (capturedCh, 5u);
}

TEST (EngineCallbacks, OnNoteOffFiredAfterNoteOff)
{
    auto eng = makePreparedEngine();
    int count = 0;
    eng.onNoteOff = [&](uint8_t, uint8_t) { ++count; };

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    eng.pushNoteOff (0u, 60u);
    eng.process();

    EXPECT_EQ (count, 1);
}

TEST (EngineCallbacks, OnNoteOffNoteMatches)
{
    auto eng = makePreparedEngine();
    NoteOffRecord rec {};
    eng.onNoteOff = [&](uint8_t n, uint8_t ch) { rec = { n, ch }; };

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    eng.pushNoteOff (3u, 60u);
    eng.process();

    EXPECT_EQ (rec.note,    60u);
    EXPECT_EQ (rec.channel,  3u);
}

TEST (EngineCallbacks, OnControlChangeFired)
{
    auto eng = makePreparedEngine();
    int count = 0;
    eng.onControlChange = [&](uint8_t, uint8_t, uint8_t) { ++count; };

    eng.pushCC (0u, 1u, 64u);
    eng.process();

    EXPECT_EQ (count, 1);
}

TEST (EngineCallbacks, OnControlChangeCCNumberAndValue)
{
    auto eng = makePreparedEngine();
    CcRecord rec {};
    eng.onControlChange = [&](uint8_t num, uint8_t val, uint8_t ch)
    {
        rec = { num, val, ch };
    };

    eng.pushCC (2u, 74u, 90u);
    eng.process();

    EXPECT_EQ (rec.ccNum,   74u);
    EXPECT_EQ (rec.ccVal,   90u);
    EXPECT_EQ (rec.channel,  2u);
}

TEST (EngineCallbacks, OnPitchBendFiredAndNormalised)
{
    auto eng = makePreparedEngine();
    float capturedBend = -99.0f;
    eng.onPitchBend = [&](float v, uint8_t) { capturedBend = v; };

    eng.pushPitchBend (0u, static_cast<int16_t> (8191));
    eng.process();

    EXPECT_NEAR (capturedBend, 1.0f, 1e-4f);
}

TEST (EngineCallbacks, OnChannelPressureFired)
{
    auto eng = makePreparedEngine();
    float captured = -1.0f;
    eng.onChannelPressure = [&](float v, uint8_t) { captured = v; };

    eng.pushMidi (MidiMessage::makeChannelPressure (0u, 127u));
    eng.process();

    EXPECT_NEAR (captured, 1.0f, 1e-3f);
}

TEST (EngineCallbacks, OnPolyAftertouchFired)
{
    auto eng = makePreparedEngine();
    uint8_t capturedNote = 0u;
    float   capturedPressure = 0.0f;
    eng.onPolyAftertouch = [&](uint8_t n, float p, uint8_t) { capturedNote = n; capturedPressure = p; };

    eng.pushMidi (MidiMessage::makePolyAftertouch (0u, 60u, 64u));
    eng.process();

    EXPECT_EQ (capturedNote, 60u);
    EXPECT_NEAR (capturedPressure, 64.0f / 127.0f, 1e-3f);
}

TEST (EngineCallbacks, OnProgramChangeFired)
{
    auto eng = makePreparedEngine();
    uint8_t capturedProg = 255u;
    eng.onProgramChange = [&](uint8_t p, uint8_t) { capturedProg = p; };

    eng.pushMidi (MidiMessage::makeProgramChange (0u, 42u));
    eng.process();

    EXPECT_EQ (capturedProg, 42u);
}

TEST (EngineCallbacks, AllNotesOffCC123TriggersAllNotesOff)
{
    auto eng = makePreparedEngine();

    eng.pushNoteOn (0u, 60u, 100u);
    eng.pushNoteOn (0u, 62u, 100u);
    eng.process();
    ASSERT_GT (eng.getNumActiveVoices(), 0u);

    eng.pushMidi (MidiMessage::makeAllNotesOff (0u));
    eng.process();

    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

TEST (EngineCallbacks, AllSoundOffCC120TriggersAllNotesOff)
{
    auto eng = makePreparedEngine();

    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    ASSERT_GT (eng.getNumActiveVoices(), 0u);

    eng.pushMidi (MidiMessage::makeAllSoundOff (0u));
    eng.process();

    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

/*======================================================================
 * Section 4: Controller state cache
 *====================================================================*/

TEST (EngineControllerCache, CCCacheInitiallyZero)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ (eng.getCCValue (7u, 0u), 0u);
    EXPECT_EQ (eng.getCCValue (74u, 3u), 0u);
}

TEST (EngineControllerCache, CCCacheUpdatedAfterProcess)
{
    auto eng = makePreparedEngine();
    eng.pushCC (0u, 7u, 120u);
    eng.process();
    EXPECT_EQ (eng.getCCValue (7u, 0u), 120u);
}

TEST (EngineControllerCache, CCCachePerChannel)
{
    auto eng = makePreparedEngine();
    eng.pushCC (1u, 7u, 64u); // ch 1
    eng.process();

    EXPECT_EQ (eng.getCCValue (7u, 1u), 64u);
    EXPECT_EQ (eng.getCCValue (7u, 0u),  0u) << "ch 0 should be unaffected";
}

TEST (EngineControllerCache, PitchBendCacheInitiallyZero)
{
    auto eng = makePreparedEngine();
    EXPECT_FLOAT_EQ (eng.getPitchBend (0u), 0.0f);
}

TEST (EngineControllerCache, PitchBendCacheUpdatedAfterProcess)
{
    auto eng = makePreparedEngine();
    eng.pushPitchBend (0u, static_cast<int16_t> (-8192));
    eng.process();
    EXPECT_FLOAT_EQ (eng.getPitchBend (0u), -1.0f);
}

TEST (EngineControllerCache, PitchBendCachePerChannel)
{
    auto eng = makePreparedEngine();
    eng.pushPitchBend (2u, static_cast<int16_t> (4096));
    eng.process();

    EXPECT_GT  (eng.getPitchBend (2u), 0.0f);
    EXPECT_FLOAT_EQ (eng.getPitchBend (0u), 0.0f) << "ch 0 should be unaffected";
}

TEST (EngineControllerCache, ChannelPressureCacheUpdatedAfterProcess)
{
    auto eng = makePreparedEngine();
    eng.pushMidi (MidiMessage::makeChannelPressure (0u, 64u));
    eng.process();
    EXPECT_GT (eng.getChannelPressure (0u), 0.0f);
}

TEST (EngineControllerCache, CCNormalisedMaxIsOne)
{
    auto eng = makePreparedEngine();
    eng.pushCC (0u, 1u, 127u);
    eng.process();
    EXPECT_FLOAT_EQ (eng.getCCNormalised (1u, 0u), 1.0f);
}

TEST (EngineControllerCache, CCNormalisedZeroIsZero)
{
    auto eng = makePreparedEngine();
    EXPECT_FLOAT_EQ (eng.getCCNormalised (1u, 0u), 0.0f);
}

TEST (EngineControllerCache, ResetControllerStateClearsCache)
{
    auto eng = makePreparedEngine();
    eng.pushCC (0u, 7u, 100u);
    eng.pushPitchBend (0u, static_cast<int16_t> (4000));
    eng.process();

    ASSERT_NE (eng.getCCValue (7u, 0u), 0u);
    ASSERT_NE (eng.getPitchBend (0u), 0.0f);

    eng.resetControllerState();

    EXPECT_EQ (eng.getCCValue (7u, 0u), 0u);
    EXPECT_FLOAT_EQ (eng.getPitchBend (0u), 0.0f);
}

TEST (EngineControllerCache, OutOfRangeChannelReturnsSafeDefault)
{
    auto eng = makePreparedEngine();
    EXPECT_EQ   (eng.getCCValue (7u, 20u),          0u);
    EXPECT_FLOAT_EQ (eng.getPitchBend (20u),         0.0f);
    EXPECT_FLOAT_EQ (eng.getChannelPressure (200u),  0.0f);
}

/*======================================================================
 * Section 5: Voice activation and counting
 *====================================================================*/

TEST (EngineVoices, NoteOnActivatesVoice)
{
    auto eng = makePreparedEngine();
    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    EXPECT_GT (eng.getNumActiveVoices(), 0u);
}

TEST (EngineVoices, NoteOffDeactivatesVoiceWithNoEnvelope)
{
    auto eng = makePreparedEngine();
    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();
    ASSERT_GT (eng.getNumActiveVoices(), 0u);

    eng.pushNoteOff (0u, 60u);
    eng.process();

    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

TEST (EngineVoices, MultipleNoteOnActivatesMultipleVoices)
{
    auto eng = makePreparedEngine();
    eng.pushNoteOn (0u, 60u, 100u);
    eng.pushNoteOn (0u, 62u, 100u);
    eng.pushNoteOn (0u, 64u, 100u);
    eng.process();

    EXPECT_EQ (eng.getNumActiveVoices(), 3u);
}

TEST (EngineVoices, AllNotesOffDeactivatesAllVoices)
{
    auto eng = makePreparedEngine();
    eng.pushNoteOn (0u, 60u, 100u);
    eng.pushNoteOn (0u, 62u, 100u);
    eng.process();
    ASSERT_EQ (eng.getNumActiveVoices(), 2u);

    eng.allNotesOff();

    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

/*======================================================================
 * Section 6: Output buffer
 *====================================================================*/

TEST (EngineOutput, OutputBufferClearedEachBlock)
{
    auto eng = makePreparedEngine (1.0f);
    eng.process(); // no active voices
    const auto& buf = eng.getOutputBuffer();
    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t fr = 0; fr < kFrames; ++fr)
            EXPECT_FLOAT_EQ (buf.sample (ch, fr), 0.0f)
                << "ch=" << ch << " fr=" << fr;
}

TEST (EngineOutput, ActiveVoiceContributesToOutput)
{
    auto eng = makePreparedEngine (0.5f);
    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();

    const auto& buf = eng.getOutputBuffer();
    bool hasNonZero = false;
    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t fr = 0; fr < kFrames; ++fr)
            if (buf.sample (ch, fr) != 0.0f) hasNonZero = true;

    EXPECT_TRUE (hasNonZero) << "Active voice with fillValue=0.5 must produce non-zero output";
}

TEST (EngineOutput, ProcessAccumulatesIntoExternalBuffer)
{
    auto eng = makePreparedEngine (0.25f);
    eng.pushNoteOn (0u, 60u, 100u);

    AudioBuffer<float, ChannelMajorLayout> dst;
    dst.resize (kChannels, kFrames);
    dst.fill (0.5f); // pre-fill

    eng.process (dst);

    // Engine output was accumulated on top of 0.5f.
    bool anyAboveHalf = false;
    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t fr = 0; fr < kFrames; ++fr)
            if (dst.sample (ch, fr) > 0.5f) anyAboveHalf = true;

    EXPECT_TRUE (anyAboveHalf);
}

TEST (EngineOutput, MasterGainApplied)
{
    auto engFull = makePreparedEngine<DummyConfig>  (1.0f); // gain = 1
    auto engHalf = makePreparedEngine<HalfGainConfig> (1.0f); // gain = 0.5

    engFull.pushNoteOn (0u, 60u, 100u);
    engHalf.pushNoteOn (0u, 60u, 100u);

    engFull.process();
    engHalf.process();

    const float full = engFull.getOutputBuffer().sample (0, 0);
    const float half = engHalf.getOutputBuffer().sample (0, 0);

    ASSERT_NE (full, 0.0f) << "Voice must produce non-zero output for this test";
    EXPECT_NEAR (half, full * 0.5f, 1e-5f);
}

TEST (EngineOutput, HardClipClampsOutput)
{
    // ClipConfig: MasterGain=4, HardClip=true; fillValue=1.0 -> raw=4.0 -> clipped=1.0.
    auto eng = makePreparedEngine<ClipConfig> (1.0f);
    eng.pushNoteOn (0u, 60u, 100u);
    eng.process();

    const auto& buf = eng.getOutputBuffer();
    for (std::size_t ch = 0; ch < kChannels; ++ch)
        for (std::size_t fr = 0; fr < kFrames; ++fr)
            EXPECT_LE (buf.sample (ch, fr), 1.0f)
                << "Hard clip must prevent output > 1.0";
}

/*======================================================================
 * Section 7: Sample-accurate mode
 *====================================================================*/

TEST (EngineSampleAccurate, SampleAccurateEventBeforeRenderFiresBeforeBlock)
{
    auto eng = makePreparedEngine<SaConfig>();
    int noteOnCount = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++noteOnCount; };

    // sampleOffset = 0 must still fire.
    eng.pushMidi (MidiMessage::makeNoteOn (0u, 60u, 100u, 0));
    eng.process();

    EXPECT_EQ (noteOnCount, 1);
}

TEST (EngineSampleAccurate, SampleAccurateMultipleOffsets)
{
    auto eng = makePreparedEngine<SaConfig>();
    std::vector<int32_t> offsets;
    offsets.reserve (2);  // Pre-allocate for expected events

    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t)
    {
        offsets.push_back (0);
    };
    

    eng.pushMidi (MidiMessage::makeNoteOn (0u, 60u, 100u, 0));
    eng.pushMidi (MidiMessage::makeNoteOn (0u, 62u, 100u, static_cast<int32_t> (kFrames / 2)));
    eng.process();

    EXPECT_EQ (offsets.size(), 2u) << "Both note-ons must fire";
}

TEST (EngineSampleAccurate, NegativeOffsetClampedToZero)
{
    auto eng = makePreparedEngine<SaConfig>();
    int count = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++count; };

    // Negative sampleOffset must not crash and must still dispatch the event.
    eng.pushMidi (MidiMessage::makeNoteOn (0u, 60u, 100u, -10));
    EXPECT_NO_FATAL_FAILURE (eng.process());
    EXPECT_EQ (count, 1);
}

TEST (EngineSampleAccurate, OffsetBeyondBlockClampedToBlockEnd)
{
    auto eng = makePreparedEngine<SaConfig>();
    int count = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++count; };

    // sampleOffset larger than numFrames must not write out of bounds.
    const int32_t bigOffset = static_cast<int32_t> (kFrames) + 100;
    eng.pushMidi (MidiMessage::makeNoteOn (0u, 60u, 100u, bigOffset));
    EXPECT_NO_FATAL_FAILURE (eng.process());
    EXPECT_EQ (count, 1);
}

/*======================================================================
 * Section 8: MPE — zone routing and per-note controller state
 *
 * These tests verify that Engine<float, 8, MpeConfig> correctly routes
 * per-note MIDI data to per-channel state slots and fires the expected
 * callbacks, without requiring changes to VoiceManager or AudioGraph.
 *
 * MPE routing rules under test:
 *   ch 1      = lower zone global master (bend/pressure applied globally)
 *   ch 2..5   = lower zone member channels (per-note bend, pressure, CC 74)
 *   ch 16     = upper zone global master (not used in these tests; UpperZoneSize=0)
 *
 * The Engine is responsible for:
 *   - Storing per-channel pitch bend in channelState[ch].
 *   - Storing per-channel pressure in channelState[ch].
 *   - Storing per-channel CC in channelState[ch].cc[ccNum].
 *   - Firing onNoteOn / onNoteOff with the correct MIDI channel.
 *   - The onNoteOn callback receiving the member channel (e.g. 2), not 0.
 *
 * The existing dispatchEvent() already does all of this correctly for any
 * channel. These tests verify the behaviour is channel-specific.
 *====================================================================*/

using MpeEngine = Engine<float, 8u, MpeConfig>;

static MpeEngine makeMpeEngine()
{
    MpeEngine eng (8u, []() { return makeConstantVoiceConfig (0.1f); });
    eng.prepare (kChannels, kFrames, kSampleRate);
    return eng;
}

TEST (EngineMpe, MpeEnabledEngineCompiles)
{
    // Construction itself is the test.
    EXPECT_NO_FATAL_FAILURE ({ auto eng = makeMpeEngine(); (void)eng; });
}

TEST (EngineMpe, MpeNoteOnOnMemberChannelFiresCallback)
{
    auto eng = makeMpeEngine();
    int count = 0;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t, std::size_t) { ++count; };

    // ch 2 = lower zone member channel.
    eng.pushNoteOn (2u, 60u, 100u);
    eng.process();

    EXPECT_EQ (count, 1);
}

TEST (EngineMpe, MpeNoteOnChannelMatchesMidiChannel)
{
    auto eng = makeMpeEngine();
    uint8_t capturedCh = 0xFFu;
    eng.onNoteOn = [&](uint8_t, uint8_t, uint8_t ch, std::size_t) { capturedCh = ch; };

    eng.pushNoteOn (2u, 60u, 100u);
    eng.process();

    EXPECT_EQ (capturedCh, 2u)
        << "MPE note-on on ch 2 must report channel 2, not the global channel 0";
}

TEST (EngineMpe, MpePerNotePitchBendUpdatesChannelCache)
{
    auto eng = makeMpeEngine();

    // Per-note bend on ch 2.
    eng.pushPitchBend (2u, static_cast<int16_t> (8191));
    eng.process();

    EXPECT_NEAR (eng.getPitchBend (2u), 1.0f, 1e-4f)
        << "Per-note pitch bend must be stored in channelState[2]";
    EXPECT_FLOAT_EQ (eng.getPitchBend (0u), 0.0f)
        << "ch 0 must be unaffected by per-note bend on ch 2";
}

TEST (EngineMpe, MpePerNoteChannelPressureUpdatesChannelCache)
{
    auto eng = makeMpeEngine();

    // Per-note pressure on ch 3.
    eng.pushMidi (MidiMessage::makeChannelPressure (3u, 127u));
    eng.process();

    EXPECT_NEAR (eng.getChannelPressure (3u), 1.0f, 1e-3f);
    EXPECT_FLOAT_EQ (eng.getChannelPressure (1u), 0.0f)
        << "Master channel must be unaffected by per-note pressure on ch 3";
}

TEST (EngineMpe, MpeMasterChannelPitchBendUpdatesChannel1)
{
    auto eng = makeMpeEngine();

    // Global bend on lower master (ch 1).
    eng.pushPitchBend (1u, static_cast<int16_t> (-8192));
    eng.process();

    EXPECT_FLOAT_EQ (eng.getPitchBend (1u), -1.0f);
    EXPECT_FLOAT_EQ (eng.getPitchBend (2u),  0.0f)
        << "Member channel must be unaffected by master bend";
}

TEST (EngineMpe, MpeNoteOffOnSameChannelDeactivates)
{
    auto eng = makeMpeEngine();

    // In MPE, note-on and note-off for the same note share the same channel.
    eng.pushNoteOn (2u, 60u, 100u);
    eng.process();

    const std::size_t activeAfterOn = eng.getNumActiveVoices();
    ASSERT_GT (activeAfterOn, 0u);

    eng.pushNoteOff (2u, 60u);
    eng.process();

    // With no envelope, voice deactivates immediately.
    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}

TEST (EngineMpe, MpeCc74SlideUpdatesCache)
{
    auto eng = makeMpeEngine();

    // CC 74 = "slide" (timbre) in MPE on a member channel.
    eng.pushCC (2u, 74u, 80u);
    eng.process();

    EXPECT_EQ (eng.getCCValue (74u, 2u), 80u);
    EXPECT_EQ (eng.getCCValue (74u, 0u),  0u)
        << "ch 0 must be unaffected by slide on ch 2";
}

TEST (EngineMpe, MpeAllNotesOffSilencesAllVoices)
{
    auto eng = makeMpeEngine();

    // Activate three voices on different member channels.
    eng.pushNoteOn (2u, 60u, 100u);
    eng.pushNoteOn (3u, 64u, 100u);
    eng.pushNoteOn (4u, 67u, 100u);
    eng.process();
    ASSERT_GT (eng.getNumActiveVoices(), 0u);

    // CC 123 on master channel 1 -> allNotesOff.
    eng.pushMidi (MidiMessage::makeAllNotesOff (1u));
    eng.process();

    EXPECT_EQ (eng.getNumActiveVoices(), 0u);
}