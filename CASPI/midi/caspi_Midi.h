#ifndef CASPI_MIDI_H
#define CASPI_MIDI_H

/*************************************************************************
 *  .d8888b.                             d8b
 * d88P  Y88b                            Y8P
 * 888    888
 * 888         8888b.  .d8888b  88888b.  888
 * 888            "88b 88K      888 "88b 888
 * 888    888 .d888888 "Y8888b. 888  888 888
 * Y88b  d88P 888  888      X88 888 d88P 888
 *  "Y8888P"  "Y888888  88888P' 88888P"  888
 *                              888
 *                              888
 *                              888
 *
 * @file   caspi_Midi.h
 * @author CS Islay
 * @brief  MIDI 1.0 message representation, fixed-capacity stream buffer,
 *         and audio-thread-safe utility functions.
 *
 * @details
 * ### Overview
 *
 * This header provides:
 *   MidiStatus        - Status byte constants (upper nibble).
 *   ControllerNumber  - Common CC numbers.
 *   MidiMessage       - 8-byte POD value type for a single MIDI event.
 *   MidiStream<Cap>   - Fixed-capacity block buffer; no heap allocation.
 *   Utility functions - noteToFrequency, velocityToNormalised, etc.
 *
 * ### Memory layout
 *
 *   MidiMessage is exactly 8 bytes:
 *     uint8_t  status       - type (upper 4 bits) | channel (lower 4 bits)
 *     uint8_t  data1        - note number, CC number, program, or pitch bend LSB
 *     uint8_t  data2        - velocity, CC value, pressure, or pitch bend MSB
 *     uint8_t  reserved     - explicit padding; always 0
 *     int32_t  sampleOffset - sample-accurate position within the current block
 *
 * A static_assert enforces this size.
 *
 * ### Pitch bend encoding
 *
 *   The 14-bit raw value spans [0, 16383] with 8192 as the neutral centre.
 *   CASPI normalises to a signed range [-8192, +8191] (center = 0):
 *     raw  = value + 8192
 *     data1 = raw & 0x7F        (LSB, 7 bits)
 *     data2 = (raw >> 7) & 0x7F (MSB, 7 bits)
 *   getPitchBendValue() reverses this.
 *
 * ### Note On with velocity 0
 *
 *   Per MIDI 1.0 specification, a Note On with velocity 0 is equivalent to
 *   Note Off (Running Status optimisation). isNoteOn() returns false and
 *   isNoteOff() returns true in this case.
 *
 * ### Thread safety
 *
 *   MidiMessage        - value type; trivially copyable, no synchronisation needed.
 *   Factory methods    - CASPI_NON_BLOCKING; safe to call from any thread.
 *   MidiStream::push() - CASPI_NON_BLOCKING; audio thread only.
 *   MidiStream::clear()- CASPI_NON_BLOCKING; audio thread only.
 *   Utility functions  - CASPI_NON_BLOCKING; safe to call from any thread.
 *
 * ### Capacity
 *
 *   MidiStream defaults to MIDI_EVENT_QUEUE_ESTIMATED_PER_BLOCK (32).
 *   Override via the template argument if your host delivers denser event
 *   bursts. push() discards and debug-asserts on overflow.
 *
 ************************************************************************/

//------------------------------------------------------------------------------
// Includes - System
//------------------------------------------------------------------------------
#include <array>
#include <cmath>
#include <cstdint>

//------------------------------------------------------------------------------
// Includes - Project
//------------------------------------------------------------------------------
#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"

namespace CASPI
{
    namespace Midi
    {

        /*======================================================================
         * MidiStatus
         *====================================================================*/

        /**
         * @brief Raw MIDI status byte values (upper nibble only).
         *
         * The lower nibble carries the MIDI channel (0-15) and is ORed in
         * by each factory method. Use getType() to extract the upper nibble
         * from a MidiMessage::status byte.
         */
        enum class MidiStatus : uint8_t
        {
            Invalid         = 0x00u, ///< Default / uninitialised message.
            NoteOff         = 0x80u, ///< Note Off         (2 data bytes).
            NoteOn          = 0x90u, ///< Note On          (2 data bytes; vel=0 == NoteOff).
            PolyAftertouch  = 0xA0u, ///< Polyphonic Aftertouch (2 data bytes).
            ControlChange   = 0xB0u, ///< Control Change   (2 data bytes).
            ProgramChange   = 0xC0u, ///< Program Change   (1 data byte).
            ChannelPressure = 0xD0u, ///< Channel Pressure (1 data byte).
            PitchBend       = 0xE0u, ///< Pitch Bend       (2 data bytes, 14-bit value).
            SysEx           = 0xF0u, ///< System Exclusive (variable length; not buffered here).
        };

        /*======================================================================
         * ControllerNumber
         *====================================================================*/

        /**
         * @brief Common MIDI Continuous Controller (CC) numbers.
         *
         * Pass as the controller argument to MidiMessage::makeControlChange().
         * cast to uint8_t: static_cast<uint8_t>(ControllerNumber::ModWheel).
         */
        enum class ControllerNumber : uint8_t
        {
            BankSelectMSB       = 0u,
            ModWheel            = 1u,
            BreathController    = 2u,
            FootController      = 4u,
            PortamentoTime      = 5u,
            Volume              = 7u,
            Balance             = 8u,
            Pan                 = 10u,
            Expression          = 11u,
            SustainPedal        = 64u,
            Portamento          = 65u,
            Sostenuto           = 66u,
            SoftPedal           = 67u,
            LegatoFootswitch    = 68u,
            AllSoundOff         = 120u,
            ResetAllControllers = 121u,
            AllNotesOff         = 123u,
        };

        /*======================================================================
         * MidiMessage
         *====================================================================*/

        /**
         * @brief POD value type representing a single MIDI 1.0 event.
         *
         * Exactly 8 bytes (verified by static_assert). Factory static methods
         * are the intended construction path. All methods are noexcept and
         * CASPI_NON_BLOCKING.
         *
         * The sampleOffset field holds the sample-accurate position within the
         * current audio block (0 = first sample). The host or plugin wrapper is
         * responsible for assigning correct offsets before pushing to MidiStream.
         */
        struct MidiMessage
        {
                uint8_t  status       = 0u; ///< Status byte: type (upper 4) | channel (lower 4).
                uint8_t  data1        = 0u; ///< Note, CC number, program, or pitch bend LSB.
                uint8_t  data2        = 0u; ///< Velocity, CC value, pressure, or pitch bend MSB.
                uint8_t  reserved     = 0u; ///< Explicit padding. Always 0.
                int32_t  sampleOffset = 0;  ///< Sample index within the current audio block.

                /*------------------------------------------------------------------
                 * Factory methods — any thread, CASPI_NON_BLOCKING, no allocation
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Create a Note On message.
                 *
                 * A Note On with velocity 0 is semantically a Note Off per the
                 * MIDI 1.0 specification. isNoteOn() reflects this.
                 *
                 * @param channel   MIDI channel 0-15.
                 * @param note      Note number 0-127.
                 * @param velocity  Velocity 0-127.
                 * @param offset    Sample offset within the current block.
                 */
                static MidiMessage makeNoteOn (uint8_t channel,
                                               uint8_t note,
                                               uint8_t velocity,
                                               int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::NoteOn) | (channel & 0x0Fu);
                    msg.data1        = note     & 0x7Fu;
                    msg.data2        = velocity & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Note Off message.
                 * @param channel   MIDI channel 0-15.
                 * @param note      Note number 0-127.
                 * @param velocity  Release velocity 0-127 (commonly 0 or 64).
                 * @param offset    Sample offset within the current block.
                 */
                static MidiMessage makeNoteOff (uint8_t channel,
                                                uint8_t note,
                                                uint8_t velocity = 0u,
                                                int32_t offset   = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::NoteOff) | (channel & 0x0Fu);
                    msg.data1        = note     & 0x7Fu;
                    msg.data2        = velocity & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Control Change message.
                 * @param channel     MIDI channel 0-15.
                 * @param controller  CC number 0-127. See ControllerNumber enum.
                 * @param value       CC value 0-127.
                 * @param offset      Sample offset within the current block.
                 */
                static MidiMessage makeControlChange (uint8_t channel,
                                                      uint8_t controller,
                                                      uint8_t value,
                                                      int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::ControlChange) | (channel & 0x0Fu);
                    msg.data1        = controller & 0x7Fu;
                    msg.data2        = value      & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Pitch Bend message.
                 *
                 * Encodes the signed 14-bit value into the standard two-byte format:
                 *   raw   = value + 8192   (unsigned 14-bit, center = 0x2000)
                 *   data1 = raw & 0x7F     (LSB, 7 bits)
                 *   data2 = (raw>>7) & 0x7F (MSB, 7 bits)
                 *
                 * @param channel  MIDI channel 0-15.
                 * @param value    Signed pitch bend [-8192, +8191]. Center = 0.
                 * @param offset   Sample offset within the current block.
                 */
                static MidiMessage makePitchBend (uint8_t channel,
                                                  int16_t value,
                                                  int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    const uint16_t raw = static_cast<uint16_t> (
                        static_cast<int> (value) + static_cast<int> (Constants::MIDI_PITCH_BEND_HALF_RANGE));
                    msg.status       = static_cast<uint8_t> (MidiStatus::PitchBend) | (channel & 0x0Fu);
                    msg.data1        = static_cast<uint8_t> (raw         & 0x7Fu); // LSB
                    msg.data2        = static_cast<uint8_t> ((raw >> 7u) & 0x7Fu); // MSB
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Program Change message.
                 * @param channel  MIDI channel 0-15.
                 * @param program  Program number 0-127.
                 * @param offset   Sample offset within the current block.
                 */
                static MidiMessage makeProgramChange (uint8_t channel,
                                                      uint8_t program,
                                                      int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::ProgramChange) | (channel & 0x0Fu);
                    msg.data1        = program & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Channel Pressure (mono aftertouch) message.
                 * @param channel   MIDI channel 0-15.
                 * @param pressure  Pressure value 0-127.
                 * @param offset    Sample offset within the current block.
                 */
                static MidiMessage makeChannelPressure (uint8_t channel,
                                                        uint8_t pressure,
                                                        int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::ChannelPressure) | (channel & 0x0Fu);
                    msg.data1        = pressure & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create a Polyphonic Aftertouch message.
                 * @param channel   MIDI channel 0-15.
                 * @param note      Note number 0-127.
                 * @param pressure  Pressure value 0-127.
                 * @param offset    Sample offset within the current block.
                 */
                static MidiMessage makePolyAftertouch (uint8_t channel,
                                                       uint8_t note,
                                                       uint8_t pressure,
                                                       int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    MidiMessage msg;
                    msg.status       = static_cast<uint8_t> (MidiStatus::PolyAftertouch) | (channel & 0x0Fu);
                    msg.data1        = note     & 0x7Fu;
                    msg.data2        = pressure & 0x7Fu;
                    msg.sampleOffset = offset;
                    return msg;
                }

                /**
                 * @brief Create an All Notes Off message (CC 123, value 0).
                 * @param channel  MIDI channel 0-15.
                 * @param offset   Sample offset within the current block.
                 */
                static MidiMessage makeAllNotesOff (uint8_t channel,
                                                    int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    return makeControlChange (channel,
                                              static_cast<uint8_t> (ControllerNumber::AllNotesOff),
                                              0u,
                                              offset);
                }

                /**
                 * @brief Create an All Sound Off message (CC 120, value 0).
                 * @param channel  MIDI channel 0-15.
                 * @param offset   Sample offset within the current block.
                 */
                static MidiMessage makeAllSoundOff (uint8_t channel,
                                                    int32_t offset = 0) noexcept CASPI_NON_BLOCKING
                {
                    return makeControlChange (channel,
                                              static_cast<uint8_t> (ControllerNumber::AllSoundOff),
                                              0u,
                                              offset);
                }

                /*------------------------------------------------------------------
                 * Type predicates
                 *-----------------------------------------------------------------*/

                /**
                 * @brief True if this is a Note On with velocity > 0.
                 *
                 * A Note On with velocity 0 is treated as Note Off per the
                 * MIDI 1.0 Running Status specification.
                 */
                CASPI_NO_DISCARD bool isNoteOn() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::NoteOn) && data2 > 0u;
                }

                /**
                 * @brief True if this is a Note Off, or a Note On with velocity 0.
                 */
                CASPI_NO_DISCARD bool isNoteOff() const noexcept CASPI_NON_BLOCKING
                {
                    const uint8_t type = status & 0xF0u;
                    return type == static_cast<uint8_t> (MidiStatus::NoteOff)
                           || (type == static_cast<uint8_t> (MidiStatus::NoteOn) && data2 == 0u);
                }

                CASPI_NO_DISCARD bool isControlChange() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::ControlChange);
                }

                CASPI_NO_DISCARD bool isPitchBend() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::PitchBend);
                }

                CASPI_NO_DISCARD bool isProgramChange() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::ProgramChange);
                }

                CASPI_NO_DISCARD bool isChannelPressure() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::ChannelPressure);
                }

                CASPI_NO_DISCARD bool isPolyAftertouch() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) == static_cast<uint8_t> (MidiStatus::PolyAftertouch);
                }

                /** @brief True if the status byte is non-zero (message is not default-constructed). */
                CASPI_NO_DISCARD bool isValid() const noexcept CASPI_NON_BLOCKING
                {
                    return (status & 0xF0u) != 0u;
                }

                /*------------------------------------------------------------------
                 * Field accessors
                 *-----------------------------------------------------------------*/

                /** @brief MIDI channel 0-15, extracted from the lower nibble of status. */
                CASPI_NO_DISCARD uint8_t getChannel() const noexcept CASPI_NON_BLOCKING
                {
                    return status & 0x0Fu;
                }

                /** @brief Status type (upper nibble) as a MidiStatus enum value. */
                CASPI_NO_DISCARD MidiStatus getType() const noexcept CASPI_NON_BLOCKING
                {
                    return static_cast<MidiStatus> (status & 0xF0u);
                }

                /** @brief Note number 0-127. Valid for NoteOn, NoteOff, PolyAftertouch. */
                CASPI_NO_DISCARD uint8_t getNoteNumber() const noexcept CASPI_NON_BLOCKING { return data1; }

                /** @brief Velocity 0-127. Valid for NoteOn and NoteOff. */
                CASPI_NO_DISCARD uint8_t getVelocity() const noexcept CASPI_NON_BLOCKING { return data2; }

                /** @brief Controller number 0-127. Valid for ControlChange. */
                CASPI_NO_DISCARD uint8_t getCCNumber() const noexcept CASPI_NON_BLOCKING { return data1; }

                /** @brief Controller value 0-127. Valid for ControlChange. */
                CASPI_NO_DISCARD uint8_t getCCValue() const noexcept CASPI_NON_BLOCKING { return data2; }

                /** @brief Program number 0-127. Valid for ProgramChange. */
                CASPI_NO_DISCARD uint8_t getProgramNumber() const noexcept CASPI_NON_BLOCKING { return data1; }

                /** @brief Channel pressure value 0-127. Valid for ChannelPressure. */
                CASPI_NO_DISCARD uint8_t getPressure() const noexcept CASPI_NON_BLOCKING { return data1; }

                /** @brief Per-note pressure 0-127. Valid for PolyAftertouch. */
                CASPI_NO_DISCARD uint8_t getPolyPressure() const noexcept CASPI_NON_BLOCKING { return data2; }

                /**
                 * @brief Decode the pitch bend value from its two 7-bit data bytes.
                 *
                 * Reconstructs the unsigned 14-bit value (data1 | data2<<7) and
                 * subtracts the center offset (8192) to produce the signed result
                 * in [-8192, +8191].
                 *
                 * @return Signed pitch bend value. Center = 0.
                 */
                CASPI_NO_DISCARD int16_t getPitchBendValue() const noexcept CASPI_NON_BLOCKING
                {
                    const int raw = static_cast<int> (data1)
                                  | (static_cast<int> (data2) << 7);
                    return static_cast<int16_t> (raw - static_cast<int> (Constants::MIDI_PITCH_BEND_HALF_RANGE));
                }
        };

        // Verify layout assumption at compile time.
        static_assert (sizeof (MidiMessage) == 8u, "MidiMessage must be exactly 8 bytes");

        /*======================================================================
         * MidiStream<Capacity>
         *====================================================================*/

        /**
         * @brief Fixed-capacity buffer of MidiMessages for one audio block.
         *
         * No heap allocation; all storage is in-place inside the object.
         * Intended usage: clear() at the start of each block, push() as events
         * arrive, then iterate with begin()/end() or operator[] during process().
         *
         * push() returns false and fires CASPI_RT_ASSERT on overflow. Tune the
         * Capacity template argument if your host delivers denser event bursts
         * than MIDI_EVENT_QUEUE_ESTIMATED_PER_BLOCK.
         *
         * All methods are CASPI_NON_BLOCKING and must be called from the audio
         * thread only (or a thread that serialises with it).
         *
         * @tparam Capacity  Maximum MidiMessages per block.
         *                   Defaults to MIDI_EVENT_QUEUE_ESTIMATED_PER_BLOCK (32).
         */
        template <std::size_t Capacity = Constants::MIDI_EVENT_QUEUE_ESTIMATED_PER_BLOCK>
        class MidiStream
        {
            public:
                /*------------------------------------------------------------------
                 * Mutation (audio thread only)
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Clear all messages, setting size to 0.
                 *
                 * Call at the start of each audio block before pushing new events.
                 * Existing message data is not zeroed; it will be overwritten on
                 * subsequent push() calls.
                 */
                void clear() noexcept CASPI_NON_BLOCKING
                {
                    count_ = 0;
                }

                /**
                 * @brief Append a message to the stream.
                 *
                 * @param msg  Message to append.
                 * @return     true on success; false if the stream is full.
                 *             On overflow, the message is discarded and
                 *             CASPI_RT_ASSERT fires in debug builds.
                 */
                bool push (const MidiMessage& msg) noexcept CASPI_NON_BLOCKING
                {
                    if (count_ >= Capacity)
                    {
                        CASPI_RT_ASSERT (false); // overflow: increase Capacity or rate-limit input
                        return false;
                    }
                    messages_[count_++] = msg;
                    return true;
                }

                /*------------------------------------------------------------------
                 * Iteration (audio thread only)
                 *-----------------------------------------------------------------*/

                /** @brief Pointer to the first message. */
                CASPI_NO_DISCARD const MidiMessage* begin() const noexcept CASPI_NON_BLOCKING
                {
                    return messages_.data();
                }

                /** @brief One-past-end pointer. */
                CASPI_NO_DISCARD const MidiMessage* end() const noexcept CASPI_NON_BLOCKING
                {
                    return messages_.data() + count_;
                }

                /** @brief Mutable pointer to the first message. */
                CASPI_NO_DISCARD MidiMessage* begin() noexcept CASPI_NON_BLOCKING
                {
                    return messages_.data();
                }

                /** @brief Mutable one-past-end pointer. */
                CASPI_NO_DISCARD MidiMessage* end() noexcept CASPI_NON_BLOCKING
                {
                    return messages_.data() + count_;
                }

                /*------------------------------------------------------------------
                 * Element access (audio thread only)
                 *-----------------------------------------------------------------*/

                /** @brief Const indexed access. No bounds checking in release. */
                CASPI_NO_DISCARD const MidiMessage& operator[] (std::size_t i) const noexcept CASPI_NON_BLOCKING
                {
                    CASPI_RT_ASSERT (i < count_);
                    return messages_[i];
                }

                /** @brief Mutable indexed access. No bounds checking in release. */
                CASPI_NO_DISCARD MidiMessage& operator[] (std::size_t i) noexcept CASPI_NON_BLOCKING
                {
                    CASPI_RT_ASSERT (i < count_);
                    return messages_[i];
                }

                /*------------------------------------------------------------------
                 * Observers (audio thread only)
                 *-----------------------------------------------------------------*/

                /** @brief Number of messages currently in the stream. */
                CASPI_NO_DISCARD std::size_t size() const noexcept CASPI_NON_BLOCKING { return count_; }

                /** @brief True when no messages are present. */
                CASPI_NO_DISCARD bool empty() const noexcept CASPI_NON_BLOCKING { return count_ == 0u; }

                /** @brief True when push() would discard the next message. */
                CASPI_NO_DISCARD bool full() const noexcept CASPI_NON_BLOCKING { return count_ >= Capacity; }

                /** @brief Maximum number of messages this stream can hold. */
                CASPI_NO_DISCARD std::size_t capacity() const noexcept CASPI_NON_BLOCKING { return Capacity; }

            private:
                std::array<MidiMessage, Capacity> messages_ {};
                std::size_t count_ = 0u;
        };

        /*======================================================================
         * Utility functions
         *====================================================================*/

        /**
         * @brief Convert a MIDI note number to frequency in Hz.
         *
         * Formula: f = 440 * 2^((note - 69) / 12)
         *
         * @tparam FloatType  float or double.
         * @param  note       MIDI note number 0-127. 69 = A4 = 440 Hz.
         * @return            Frequency in Hz.
         */
        template <typename FloatType>
        inline FloatType noteToFrequency (uint8_t note) noexcept CASPI_NON_BLOCKING
        {
            return Constants::A4_FREQUENCY<FloatType>
                   * std::pow (FloatType (2),
                                (static_cast<FloatType> (note) - FloatType (69)) / FloatType (12));
        }

        /**
         * @brief Convert a MIDI velocity (0-127) to a normalised linear value [0, 1].
         *
         * @tparam FloatType  float or double.
         * @param  velocity   MIDI velocity 0-127.
         * @return            Normalised value in [0.0, 1.0].
         */
        template <typename FloatType>
        inline FloatType velocityToNormalised (uint8_t velocity) noexcept CASPI_NON_BLOCKING
        {
            return static_cast<FloatType> (velocity) / FloatType (127);
        }

        /**
         * @brief Convert a MIDI CC value (0-127) to a normalised value [0, 1].
         *
         * @tparam FloatType  float or double.
         * @param  value      CC value 0-127.
         * @return            Normalised value in [0.0, 1.0].
         */
        template <typename FloatType>
        inline FloatType ccToNormalised (uint8_t value) noexcept CASPI_NON_BLOCKING
        {
            return static_cast<FloatType> (value) / FloatType (127);
        }

        /**
         * @brief Convert a pitch bend value to a normalised value in [-1.0, +1.0].
         *
         * Positive values are divided by 8191 (MIDI_PITCH_BEND_MAX).
         * Negative values are divided by 8192 (-MIDI_PITCH_BEND_MIN).
         * This correctly maps both extremes to ±1.0 despite the MIDI
         * specification's inherent one-step asymmetry.
         * Center (0) maps exactly to 0.0.
         *
         * @tparam FloatType  float or double.
         * @param  value      Pitch bend in [-8192, +8191].
         * @return            Normalised value in [-1.0, +1.0].
         */
        template <typename FloatType>
        inline FloatType pitchBendToNormalised (int16_t value) noexcept CASPI_NON_BLOCKING
        {
            if (value >= 0)
            {
                return static_cast<FloatType> (value) / FloatType (Constants::MIDI_PITCH_BEND_MAX);
            }

            // Divide by 8192 (= -MIDI_PITCH_BEND_MIN) so that -8192 -> -1.0.
            return static_cast<FloatType> (value)
                   / FloatType (-static_cast<int> (Constants::MIDI_PITCH_BEND_MIN));
        }

        /**
         * @brief Convert a pitch bend value to semitones.
         *
         * @tparam FloatType  float or double.
         * @param  value      Pitch bend in [-8192, +8191].
         * @param  range      Semitones for full deflection (default: 2).
         * @return            Pitch offset in semitones.
         */
        template <typename FloatType>
        inline FloatType pitchBendToSemitones (int16_t value,
                                               int range = Constants::MIDI_PITCH_BEND_DEFAULT_RANGE) noexcept
            CASPI_NON_BLOCKING
        {
            return pitchBendToNormalised<FloatType> (value) * static_cast<FloatType> (range);
        }

        /**
         * @brief Convert a pitch bend value to cents.
         *
         * @tparam FloatType  float or double.
         * @param  value      Pitch bend in [-8192, +8191].
         * @param  range      Semitones for full deflection (default: 2).
         * @return            Pitch offset in cents (100 cents = 1 semitone).
         */
        template <typename FloatType>
        inline FloatType pitchBendToCents (int16_t value,
                                           int range = Constants::MIDI_PITCH_BEND_DEFAULT_RANGE) noexcept
            CASPI_NON_BLOCKING
        {
            return pitchBendToSemitones<FloatType> (value, range) * FloatType (100);
        }

    } // namespace Midi
} // namespace CASPI

#endif // CASPI_MIDI_H