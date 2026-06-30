/*************************************************************************
 * @file caspi_Midi_test.cpp
 *
 * Unit tests for:
 *   CASPI::Midi::MidiMessage
 *   CASPI::Midi::MidiStream<Capacity>
 *   CASPI::Midi utility functions (noteToFrequency, velocityToNormalised,
 *       ccToNormalised, pitchBendToNormalised, pitchBendToSemitones,
 *       pitchBendToCents)
 *
 * TEST PLAN SUMMARY
 * =================
 *
 * -----------------------------------------------------------------------
 * Section 1: MidiMessage — factory methods and field accessors
 * -----------------------------------------------------------------------
 * 1.1  NoteOnFactoryFields
 *      makeNoteOn(ch, note, vel, offset) sets status, data1, data2, offset.
 *      Channel is in the lower nibble; type is in the upper nibble.
 *
 * 1.2  NoteOffFactoryFields
 *      makeNoteOff encodes NoteOff type, note, release velocity.
 *
 * 1.3  NoteOffDefaultVelocityIsZero
 *      makeNoteOff with no velocity argument stores 0.
 *
 * 1.4  ControlChangeFactoryFields
 *      makeControlChange encodes type, controller number, value.
 *
 * 1.5  PitchBendCenter
 *      makePitchBend(ch, 0) -> getPitchBendValue() == 0.
 *
 * 1.6  PitchBendMax
 *      makePitchBend(ch, +8191) -> getPitchBendValue() == +8191.
 *
 * 1.7  PitchBendMin
 *      makePitchBend(ch, -8192) -> getPitchBendValue() == -8192.
 *
 * 1.8  PitchBendRoundTrip
 *      A sweep of 200 representative values encodes and decodes exactly.
 *
 * 1.9  ProgramChangeFactoryFields
 *      makeProgramChange encodes type and program number.
 *
 * 1.10 ChannelPressureFactoryFields
 *      makeChannelPressure encodes type and pressure.
 *
 * 1.11 PolyAftertouchFactoryFields
 *      makePolyAftertouch encodes type, note, and pressure.
 *
 * 1.12 AllNotesOffIsCC123
 *      makeAllNotesOff produces CC 123, value 0.
 *
 * 1.13 AllSoundOffIsCC120
 *      makeAllSoundOff produces CC 120, value 0.
 *
 * 1.14 SampleOffsetPreserved
 *      Arbitrary offsets survive the factory round-trip.
 *
 * 1.15 ChannelMasking
 *      Channels 0-15 are stored in the lower nibble only.
 *      A channel value of 16+ is masked to 4 bits.
 *
 * 1.16 DataBytesMaskedTo7Bits
 *      Values > 127 passed to factory methods are masked to 0x7F.
 *
 * -----------------------------------------------------------------------
 * Section 2: MidiMessage — type predicates
 * -----------------------------------------------------------------------
 * 2.1  IsNoteOnTrueForPositiveVelocity
 * 2.2  IsNoteOnFalseWhenVelocityZero
 *      A Note On with velocity 0 is semantically Note Off.
 * 2.3  IsNoteOffTrueForNoteOffStatus
 * 2.4  IsNoteOffTrueForNoteOnVelocityZero
 * 2.5  IsNoteOnFalseForNoteOff
 * 2.6  IsControlChange
 * 2.7  IsPitchBend
 * 2.8  IsProgramChange
 * 2.9  IsChannelPressure
 * 2.10 IsPolyAftertouch
 * 2.11 IsValidFalseForDefaultConstructed
 * 2.12 IsValidTrueForNoteOn
 * 2.13 MutualExclusionOfPredicates
 *      A NoteOn message returns false for all non-NoteOn predicates.
 *
 * -----------------------------------------------------------------------
 * Section 3: MidiStream — push / clear / iterate / overflow
 * -----------------------------------------------------------------------
 * 3.1  DefaultStreamIsEmpty
 * 3.2  PushIncreasesSize
 * 3.3  ClearResetsSize
 * 3.4  ClearAllowsRePush
 * 3.5  IterationMatchesPushOrder
 * 3.6  IndexedAccessMatchesPushOrder
 * 3.7  FullReturnsFalse
 * 3.8  OverflowDiscards
 *      push() on a full stream returns false and does not increase size.
 * 3.9  CapacityIsTemplateArg
 *      MidiStream<64>::capacity() == 64.
 * 3.10 CustomCapacity
 *      A MidiStream<4> holds exactly 4 messages before reporting full.
 *
 * -----------------------------------------------------------------------
 * Section 4: Utility functions
 * -----------------------------------------------------------------------
 * 4.1  NoteToFrequencyA4Is440
 * 4.2  NoteToFrequencyA3Is220
 * 4.3  NoteToFrequencyC4IsApprox261
 * 4.4  NoteToFrequencyC5IsApprox523
 * 4.5  NoteToFrequencyNoteZeroIsPositive
 *      MIDI note 0 should produce a positive (audible) frequency.
 * 4.6  VelocityToNormalisedZeroIsZero
 * 4.7  VelocityToNormalisedMaxIsOne
 * 4.8  VelocityToNormalisedMidIsApproxHalf
 * 4.9  CcToNormalisedZeroIsZero
 * 4.10 CcToNormalisedMaxIsOne
 * 4.11 PitchBendToNormalisedCenterIsZero
 * 4.12 PitchBendToNormalisedMaxIsOne
 * 4.13 PitchBendToNormalisedMinIsMinusOne
 * 4.14 PitchBendToSemitonesDefaultRange
 *      Full positive deflection with range=2 gives +2 semitones.
 * 4.15 PitchBendToSemitonesCustomRange
 *      Full negative deflection with range=12 gives -12 semitones.
 * 4.16 PitchBendToCents
 *      Full deflection, range=2 -> ±200 cents.
 *
 ************************************************************************/

#include "midi/caspi_Midi.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using namespace CASPI::Midi;

/*======================================================================
 * Section 1: MidiMessage — factory methods and field accessors
 *====================================================================*/

TEST (MidiMessage, NoteOnFactoryFields)
{
    const auto msg = MidiMessage::makeNoteOn (3u, 60u, 100u, 12);

    EXPECT_EQ (msg.getType(),       MidiStatus::NoteOn);
    EXPECT_EQ (msg.getChannel(),    3u);
    EXPECT_EQ (msg.getNoteNumber(), 60u);
    EXPECT_EQ (msg.getVelocity(),   100u);
    EXPECT_EQ (msg.sampleOffset,    12);
    EXPECT_EQ (msg.reserved,        0u);
}

TEST (MidiMessage, NoteOffFactoryFields)
{
    const auto msg = MidiMessage::makeNoteOff (7u, 45u, 64u, 0);

    EXPECT_EQ (msg.getType(),       MidiStatus::NoteOff);
    EXPECT_EQ (msg.getChannel(),    7u);
    EXPECT_EQ (msg.getNoteNumber(), 45u);
    EXPECT_EQ (msg.getVelocity(),   64u);
}

TEST (MidiMessage, NoteOffDefaultVelocityIsZero)
{
    const auto msg = MidiMessage::makeNoteOff (0u, 60u);
    EXPECT_EQ (msg.getVelocity(), 0u);
}

TEST (MidiMessage, ControlChangeFactoryFields)
{
    const auto msg = MidiMessage::makeControlChange (1u, 7u, 127u);

    EXPECT_EQ (msg.getType(),      MidiStatus::ControlChange);
    EXPECT_EQ (msg.getChannel(),   1u);
    EXPECT_EQ (msg.getCCNumber(),  7u);
    EXPECT_EQ (msg.getCCValue(),   127u);
}

TEST (MidiMessage, PitchBendCenter)
{
    const auto msg = MidiMessage::makePitchBend (0u, 0);
    EXPECT_EQ (msg.getType(),           MidiStatus::PitchBend);
    EXPECT_EQ (msg.getPitchBendValue(), static_cast<int16_t> (0));
}

TEST (MidiMessage, PitchBendMax)
{
    const auto msg = MidiMessage::makePitchBend (0u, static_cast<int16_t> (8191));
    EXPECT_EQ (msg.getPitchBendValue(), static_cast<int16_t> (8191));
}

TEST (MidiMessage, PitchBendMin)
{
    const auto msg = MidiMessage::makePitchBend (0u, static_cast<int16_t> (-8192));
    EXPECT_EQ (msg.getPitchBendValue(), static_cast<int16_t> (-8192));
}

TEST (MidiMessage, PitchBendRoundTrip)
{
    // Sweep 200 evenly-spaced values across [-8192, +8191].
    for (int i = 0; i <= 200; ++i)
    {
        const int raw   = -8192 + (16383 * i / 200);
        const auto val  = static_cast<int16_t> (raw);
        const auto msg  = MidiMessage::makePitchBend (0u, val);
        EXPECT_EQ (msg.getPitchBendValue(), val) << "round-trip failed for value " << val;
    }
}

TEST (MidiMessage, ProgramChangeFactoryFields)
{
    const auto msg = MidiMessage::makeProgramChange (5u, 42u, 8);

    EXPECT_EQ (msg.getType(),          MidiStatus::ProgramChange);
    EXPECT_EQ (msg.getChannel(),       5u);
    EXPECT_EQ (msg.getProgramNumber(), 42u);
    EXPECT_EQ (msg.sampleOffset,       8);
}

TEST (MidiMessage, ChannelPressureFactoryFields)
{
    const auto msg = MidiMessage::makeChannelPressure (2u, 99u);

    EXPECT_EQ (msg.getType(),     MidiStatus::ChannelPressure);
    EXPECT_EQ (msg.getChannel(),  2u);
    EXPECT_EQ (msg.getPressure(), 99u);
}

TEST (MidiMessage, PolyAftertouchFactoryFields)
{
    const auto msg = MidiMessage::makePolyAftertouch (4u, 69u, 80u);

    EXPECT_EQ (msg.getType(),         MidiStatus::PolyAftertouch);
    EXPECT_EQ (msg.getChannel(),      4u);
    EXPECT_EQ (msg.getNoteNumber(),   69u);
    EXPECT_EQ (msg.getPolyPressure(), 80u);
}

TEST (MidiMessage, AllNotesOffIsCC123)
{
    const auto msg = MidiMessage::makeAllNotesOff (0u);

    EXPECT_EQ (msg.getType(),     MidiStatus::ControlChange);
    EXPECT_EQ (msg.getCCNumber(), static_cast<uint8_t> (ControllerNumber::AllNotesOff));
    EXPECT_EQ (msg.getCCValue(),  0u);
}

TEST (MidiMessage, AllSoundOffIsCC120)
{
    const auto msg = MidiMessage::makeAllSoundOff (0u);

    EXPECT_EQ (msg.getType(),     MidiStatus::ControlChange);
    EXPECT_EQ (msg.getCCNumber(), static_cast<uint8_t> (ControllerNumber::AllSoundOff));
    EXPECT_EQ (msg.getCCValue(),  0u);
}

TEST (MidiMessage, SampleOffsetPreserved)
{
    const int32_t kOffset = 511;
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 100u, kOffset);
    EXPECT_EQ (msg.sampleOffset, kOffset);
}

TEST (MidiMessage, ChannelMasking)
{
    // Channels 0-15 should be stored verbatim.
    for (uint8_t ch = 0u; ch < 16u; ++ch)
    {
        const auto msg = MidiMessage::makeNoteOn (ch, 60u, 80u);
        EXPECT_EQ (msg.getChannel(), ch) << "channel " << static_cast<int> (ch);
    }

    // Values >= 16 should be masked to the lower 4 bits.
    const auto msg = MidiMessage::makeNoteOn (0x1Fu, 60u, 80u); // 0x1F & 0x0F == 0x0F = 15
    EXPECT_EQ (msg.getChannel(), 15u);
}

TEST (MidiMessage, DataBytesMaskedTo7Bits)
{
    // Value 0xFF (255) masked to 0x7F (127).
    const auto cc = MidiMessage::makeControlChange (0u, 0xFFu, 0xFFu);
    EXPECT_EQ (cc.getCCNumber(), 0x7Fu);
    EXPECT_EQ (cc.getCCValue(),  0x7Fu);

    const auto note = MidiMessage::makeNoteOn (0u, 0xFFu, 0xFFu);
    EXPECT_EQ (note.getNoteNumber(), 0x7Fu);
    EXPECT_EQ (note.getVelocity(),   0x7Fu);
}

/*======================================================================
 * Section 2: MidiMessage — type predicates
 *====================================================================*/

TEST (MidiMessage, IsNoteOnTrueForPositiveVelocity)
{
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 100u);
    EXPECT_TRUE (msg.isNoteOn());
    EXPECT_FALSE (msg.isNoteOff());
}

TEST (MidiMessage, IsNoteOnFalseWhenVelocityZero)
{
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 0u);
    EXPECT_FALSE (msg.isNoteOn())
        << "Note On with velocity 0 must NOT be treated as Note On";
}

TEST (MidiMessage, IsNoteOffTrueForNoteOffStatus)
{
    const auto msg = MidiMessage::makeNoteOff (0u, 60u, 0u);
    EXPECT_TRUE (msg.isNoteOff());
    EXPECT_FALSE (msg.isNoteOn());
}

TEST (MidiMessage, IsNoteOffTrueForNoteOnVelocityZero)
{
    // MIDI 1.0 Running Status: NoteOn with velocity 0 == NoteOff.
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 0u);
    EXPECT_TRUE (msg.isNoteOff())
        << "Note On with velocity 0 must be treated as Note Off";
}

TEST (MidiMessage, IsNoteOnFalseForNoteOff)
{
    const auto msg = MidiMessage::makeNoteOff (0u, 60u, 64u);
    EXPECT_FALSE (msg.isNoteOn());
}

TEST (MidiMessage, IsControlChange)
{
    const auto msg = MidiMessage::makeControlChange (0u, 7u, 100u);
    EXPECT_TRUE  (msg.isControlChange());
    EXPECT_FALSE (msg.isNoteOn());
    EXPECT_FALSE (msg.isPitchBend());
}

TEST (MidiMessage, IsPitchBend)
{
    const auto msg = MidiMessage::makePitchBend (0u, 0);
    EXPECT_TRUE  (msg.isPitchBend());
    EXPECT_FALSE (msg.isNoteOn());
    EXPECT_FALSE (msg.isControlChange());
}

TEST (MidiMessage, IsProgramChange)
{
    const auto msg = MidiMessage::makeProgramChange (0u, 10u);
    EXPECT_TRUE  (msg.isProgramChange());
    EXPECT_FALSE (msg.isNoteOn());
}

TEST (MidiMessage, IsChannelPressure)
{
    const auto msg = MidiMessage::makeChannelPressure (0u, 64u);
    EXPECT_TRUE  (msg.isChannelPressure());
    EXPECT_FALSE (msg.isNoteOn());
}

TEST (MidiMessage, IsPolyAftertouch)
{
    const auto msg = MidiMessage::makePolyAftertouch (0u, 60u, 80u);
    EXPECT_TRUE  (msg.isPolyAftertouch());
    EXPECT_FALSE (msg.isNoteOn());
}

TEST (MidiMessage, IsValidFalseForDefaultConstructed)
{
    MidiMessage msg; // default constructor -> status = 0
    EXPECT_FALSE (msg.isValid());
}

TEST (MidiMessage, IsValidTrueForNoteOn)
{
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 100u);
    EXPECT_TRUE (msg.isValid());
}

TEST (MidiMessage, MutualExclusionOfPredicates)
{
    const auto msg = MidiMessage::makeNoteOn (0u, 60u, 80u);

    EXPECT_TRUE  (msg.isNoteOn());
    EXPECT_FALSE (msg.isNoteOff());
    EXPECT_FALSE (msg.isControlChange());
    EXPECT_FALSE (msg.isPitchBend());
    EXPECT_FALSE (msg.isProgramChange());
    EXPECT_FALSE (msg.isChannelPressure());
    EXPECT_FALSE (msg.isPolyAftertouch());
}

/*======================================================================
 * Section 3: MidiStream
 *====================================================================*/

TEST (MidiStream, DefaultStreamIsEmpty)
{
    MidiStream<> stream;
    EXPECT_TRUE  (stream.empty());
    EXPECT_EQ    (stream.size(), 0u);
    EXPECT_FALSE (stream.full());
}

TEST (MidiStream, PushIncreasesSize)
{
    MidiStream<> stream;
    stream.push (MidiMessage::makeNoteOn (0u, 60u, 100u));
    EXPECT_EQ (stream.size(), 1u);
    EXPECT_FALSE (stream.empty());

    stream.push (MidiMessage::makeNoteOff (0u, 60u));
    EXPECT_EQ (stream.size(), 2u);
}

TEST (MidiStream, ClearResetsSize)
{
    MidiStream<> stream;
    stream.push (MidiMessage::makeNoteOn (0u, 60u, 100u));
    ASSERT_EQ (stream.size(), 1u);

    stream.clear();
    EXPECT_EQ (stream.size(), 0u);
    EXPECT_TRUE (stream.empty());
}

TEST (MidiStream, ClearAllowsRePush)
{
    MidiStream<4> stream;
    for (std::size_t i = 0; i < 4; ++i)
        stream.push (MidiMessage::makeNoteOn (0u, static_cast<uint8_t> (i), 100u));

    ASSERT_TRUE (stream.full());
    stream.clear();
    EXPECT_FALSE (stream.full());

    EXPECT_TRUE (stream.push (MidiMessage::makeNoteOn (0u, 60u, 100u)));
    EXPECT_EQ (stream.size(), 1u);
}

TEST (MidiStream, IterationMatchesPushOrder)
{
    MidiStream<8> stream;

    const uint8_t notes[] = { 60u, 62u, 64u };
    for (uint8_t n : notes)
        stream.push (MidiMessage::makeNoteOn (0u, n, 100u));

    std::size_t idx = 0u;
    for (const auto& msg : stream)
    {
        EXPECT_EQ (msg.getNoteNumber(), notes[idx]) << "index " << idx;
        ++idx;
    }
    EXPECT_EQ (idx, 3u);
}

TEST (MidiStream, IndexedAccessMatchesPushOrder)
{
    MidiStream<8> stream;

    stream.push (MidiMessage::makeControlChange (0u, 7u, 64u));
    stream.push (MidiMessage::makeControlChange (0u, 10u, 96u));

    EXPECT_EQ (stream[0u].getCCNumber(), 7u);
    EXPECT_EQ (stream[1u].getCCNumber(), 10u);
}

TEST (MidiStream, FullReturnsFalse)
{
    MidiStream<2> stream;
    EXPECT_TRUE (stream.push (MidiMessage::makeNoteOn (0u, 60u, 100u)));
    EXPECT_TRUE (stream.push (MidiMessage::makeNoteOn (0u, 62u, 100u)));
    EXPECT_TRUE (stream.full());
}

TEST (MidiStream, OverflowDiscards)
{
    MidiStream<2> stream;
    stream.push (MidiMessage::makeNoteOn (0u, 60u, 100u));
    stream.push (MidiMessage::makeNoteOn (0u, 62u, 100u));
    ASSERT_TRUE (stream.full());

    // Third push must fail and not modify the stream.
    const bool ok = stream.push (MidiMessage::makeNoteOn (0u, 64u, 100u));
    EXPECT_FALSE (ok);
    EXPECT_EQ (stream.size(), 2u);
}

TEST (MidiStream, CapacityIsTemplateArg)
{
    EXPECT_EQ (MidiStream<64>().capacity(), 64u);
    EXPECT_EQ (MidiStream<1>().capacity(),  1u);
}

TEST (MidiStream, CustomCapacity)
{
    MidiStream<4> stream;

    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE (stream.push (MidiMessage::makeNoteOn (0u, static_cast<uint8_t> (60 + i), 100u)))
            << "push " << i << " should succeed";
    }

    EXPECT_TRUE (stream.full());
    EXPECT_EQ   (stream.size(), 4u);
}

/*======================================================================
 * Section 4: Utility functions
 *====================================================================*/

TEST (MidiUtils, NoteToFrequencyA4Is440)
{
    EXPECT_NEAR (noteToFrequency<float> (69u), 440.0f, 0.001f);
    EXPECT_NEAR (noteToFrequency<double> (69u), 440.0, 0.001);
}

TEST (MidiUtils, NoteToFrequencyA3Is220)
{
    EXPECT_NEAR (noteToFrequency<double> (57u), 220.0, 0.01);
}

TEST (MidiUtils, NoteToFrequencyC4IsApprox261)
{
    EXPECT_NEAR (noteToFrequency<float> (60u), 261.63f, 0.05f);
}

TEST (MidiUtils, NoteToFrequencyC5IsApprox523)
{
    EXPECT_NEAR (noteToFrequency<float> (72u), 523.25f, 0.05f);
}

TEST (MidiUtils, NoteToFrequencyNoteZeroIsPositive)
{
    EXPECT_GT (noteToFrequency<float> (0u), 0.0f);
}

TEST (MidiUtils, NoteToFrequencyOctavesDouble)
{
    const double a3 = noteToFrequency<double> (57u);
    const double a4 = noteToFrequency<double> (69u);
    const double a5 = noteToFrequency<double> (81u);
    EXPECT_NEAR (a4, a3 * 2.0, 0.01);
    EXPECT_NEAR (a5, a4 * 2.0, 0.01);
}

TEST (MidiUtils, VelocityToNormalisedZeroIsZero)
{
    EXPECT_FLOAT_EQ (velocityToNormalised<float> (0u), 0.0f);
}

TEST (MidiUtils, VelocityToNormalisedMaxIsOne)
{
    EXPECT_FLOAT_EQ (velocityToNormalised<float> (127u), 1.0f);
}

TEST (MidiUtils, VelocityToNormalisedMidIsApproxHalf)
{
    // 64 / 127 ≈ 0.504
    EXPECT_NEAR (velocityToNormalised<float> (64u), 0.504f, 0.002f);
}

TEST (MidiUtils, CcToNormalisedZeroIsZero)
{
    EXPECT_FLOAT_EQ (ccToNormalised<float> (0u), 0.0f);
}

TEST (MidiUtils, CcToNormalisedMaxIsOne)
{
    EXPECT_FLOAT_EQ (ccToNormalised<float> (127u), 1.0f);
}

TEST (MidiUtils, PitchBendToNormalisedCenterIsZero)
{
    EXPECT_FLOAT_EQ (pitchBendToNormalised<float> (static_cast<int16_t> (0)), 0.0f);
}

TEST (MidiUtils, PitchBendToNormalisedMaxIsOne)
{
    EXPECT_FLOAT_EQ (pitchBendToNormalised<float> (static_cast<int16_t> (8191)), 1.0f);
}

TEST (MidiUtils, PitchBendToNormalisedMinIsMinusOne)
{
    EXPECT_FLOAT_EQ (pitchBendToNormalised<float> (static_cast<int16_t> (-8192)), -1.0f);
}

TEST (MidiUtils, PitchBendToNormalisedPositiveIsInRange)
{
    // Intermediate positive values should be in (0, 1).
    const float v = pitchBendToNormalised<float> (static_cast<int16_t> (4096));
    EXPECT_GT (v, 0.0f);
    EXPECT_LT (v, 1.0f);
}

TEST (MidiUtils, PitchBendToNormalisedNegativeIsInRange)
{
    const float v = pitchBendToNormalised<float> (static_cast<int16_t> (-4096));
    EXPECT_LT (v, 0.0f);
    EXPECT_GT (v, -1.0f);
}

TEST (MidiUtils, PitchBendToSemitonesDefaultRange)
{
    // Full positive deflection with range=2 -> +2.0 semitones.
    const float st = pitchBendToSemitones<float> (static_cast<int16_t> (8191));
    EXPECT_FLOAT_EQ (st, 2.0f);
}

TEST (MidiUtils, PitchBendToSemitonesNegativeDefaultRange)
{
    const float st = pitchBendToSemitones<float> (static_cast<int16_t> (-8192));
    EXPECT_FLOAT_EQ (st, -2.0f);
}

TEST (MidiUtils, PitchBendToSemitonesCustomRange)
{
    // Full negative deflection with range=12 -> -12.0 semitones.
    const double st = pitchBendToSemitones<double> (static_cast<int16_t> (-8192), 12);
    EXPECT_DOUBLE_EQ (st, -12.0);
}

TEST (MidiUtils, PitchBendToSemitonesCenterIsZero)
{
    EXPECT_FLOAT_EQ (pitchBendToSemitones<float> (static_cast<int16_t> (0)), 0.0f);
}

TEST (MidiUtils, PitchBendToCentsFullDeflection)
{
    // range=2, full positive -> +200 cents.
    const float cents = pitchBendToCents<float> (static_cast<int16_t> (8191));
    EXPECT_FLOAT_EQ (cents, 200.0f);

    // Full negative -> -200 cents.
    const float centsNeg = pitchBendToCents<float> (static_cast<int16_t> (-8192));
    EXPECT_FLOAT_EQ (centsNeg, -200.0f);
}

TEST (MidiUtils, PitchBendToCentsCenterIsZero)
{
    EXPECT_FLOAT_EQ (pitchBendToCents<float> (static_cast<int16_t> (0)), 0.0f);
}