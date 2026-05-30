#ifndef CASPI_ENGINE_H
#define CASPI_ENGINE_H

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
 * @file   caspi_Engine.h
 * @author CS Islay
 * @brief  MIDI-driven synthesis engine with optional MPE support.
 *
 * @details
 * ### Overview
 *
 * Engine<F, MaxVoices, Config> is the top-level orchestration layer of the
 * CASPI synthesis stack. It owns a VoiceManager, a lock-free MIDI event
 * queue, per-channel controller state, and an output mix buffer.
 *
 * Signal-processing topology is entirely the caller's responsibility via
 * the voice factory. Engine imposes no constraints on voice architecture.
 *
 * ### Layer responsibilities
 *
 *   Engine             — MIDI drain, per-block dispatch, output accumulation,
 *                        global/per-note controller state (bend, CC, pressure).
 *   VoiceManager       — noteOn / noteOff, voice allocation, voice stealing.
 *   AudioGraph (voice) — signal-processing topology; caller-defined.
 *
 * ### MIDI ingestion
 *
 * pushMidi() and its convenience overloads enqueue from any thread into a
 * moodycamel::ConcurrentQueue. drainMidi() is called at the top of process().
 *
 * When Config::SampleAccurate is false (default), all drained events are
 * dispatched before rendering the full block — one VoiceManager pass.
 *
 * When Config::SampleAccurate is true, events are insertion-sorted by
 * sampleOffset and the block is split at each event boundary. Each sub-block
 * is rendered into a scratch buffer and accumulated into outputBuffer.
 *
 * ### Note-on frequency dispatch
 *
 * Engine does not know which node in a voice graph is the oscillator.
 * Provide onNoteOn to configure per-note state:
 *
 * @code
 *   engine.onNoteOn = [&](uint8_t note, uint8_t vel, uint8_t ch, std::size_t vi) {
 *       auto* osc = engine.getVoiceManager().getVoiceGraph(vi)
 *                       ->template getNodeAs<MyOscNode>(oscNodeId);
 *       if (osc) osc->setFrequency(CASPI::Midi::noteToFrequency<float>(note));
 *   };
 * @endcode
 *
 * ### MPE (MIDI Polyphonic Expression — MMA/AMEI RP-053)
 *
 * Enable at compile time via Config::MpeEnabled = true. No runtime overhead
 * when disabled; no wrapper class required.
 *
 * When MpeEnabled is true, Engine activates MpeZoneManager which tracks:
 *   - Lower zone: ch 1 (global master), ch 2..(1+LowerZoneSize) (members).
 *   - Upper zone: ch 16 (global master), ch (16-UpperZoneSize)..15 (members).
 *   - channelToVoice[]: maps each active member channel to a voice index.
 *   - Per-note bend/pressure/CC read from channelState[memberChannel].
 *   - Global bend/pressure/CC read from channelState[masterChannel].
 *
 * Zone sizes are configured at compile time via Config::LowerZoneSize and
 * Config::UpperZoneSize. Runtime reconfiguration via CC 6 on a master channel
 * (the MIDI CI zone configuration message) is handled by reconfigureZone().
 *
 * MPE callers should read per-note data by channel from the onNoteOn callback:
 *
 * @code
 *   engine.onNoteOn = [&](uint8_t note, uint8_t vel, uint8_t ch, std::size_t vi) {
 *       // ch is the MPE member channel (2..N), not the global channel 0.
 *       // Store ch->vi mapping so subsequent bend/pressure events on ch
 *       // can be routed to the correct voice.
 *   };
 *   engine.onPitchBend = [&](float bend, uint8_t ch) {
 *       // Retrieve voice index for ch from the caller's map and apply.
 *   };
 * @endcode
 *
 * ### Thread safety
 *
 *   pushMidi() / push*()  — any thread, lock-free.
 *   prepare()             — setup thread only, may allocate.
 *   process()             — audio thread only, noexcept, no allocation.
 *   get*() / channel*()   — audio thread only.
 *
 ************************************************************************/

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "core/caspi_AudioBuffer.h"
#include "external/caspi_External.h"
#include "midi/caspi_Midi.h"
#include "synthesizers/caspi_Voice.h"

namespace CASPI
{

    /*======================================================================
     * DefaultSynthConfig
     *====================================================================*/

    /**
     * @brief Compile-time policy knobs for Engine.
     *
     * Inherit and override to customise:
     * @code
     *   struct MySynthConfig : CASPI::DefaultSynthConfig {
     *       static constexpr bool        SampleAccurate  = true;
     *       static constexpr float       MasterGain      = 0.5f;
     *       static constexpr bool        HardClip        = true;
     *       static constexpr bool        MpeEnabled      = true;
     *       static constexpr std::size_t LowerZoneSize   = 7u;
     *       static constexpr std::size_t UpperZoneSize   = 0u;
     *   };
     * @endcode
     */
    struct DefaultSynthConfig
    {
            /** Split block at MIDI event boundaries for sample-accurate delivery. */
            static constexpr bool SampleAccurate = false;

            /** Lock-free queue capacity in events. */
            static constexpr std::size_t MidiQueueCapacity = 512u;

            /** Master gain applied to the accumulated output buffer. */
            static constexpr float MasterGain = 1.0f;

            /** Hard-clip output to [-1, +1] after master gain. */
            static constexpr bool HardClip = false;

            /**
             * Per-channel CC / bend / pressure state table width.
             * Standard MIDI 1.0: 16. MPE: also 16.
             */
            static constexpr std::size_t NumMidiChannels = 16u;

            /**
             * Enable MPE (MIDI Polyphonic Expression) dispatch.
             * When false (default), all MPE fields below are ignored and
             * MPE-specific code is not compiled in.
             */
            static constexpr bool MpeEnabled = false;

            /**
             * Number of member channels in the lower MPE zone (ch 2 .. 1+LowerZoneSize).
             * Ignored when MpeEnabled == false.
             * Set to 0 to disable the lower zone.
             */
            static constexpr std::size_t LowerZoneSize = 0u;

            /**
             * Number of member channels in the upper MPE zone (ch 16-UpperZoneSize .. 15).
             * Ignored when MpeEnabled == false.
             * Set to 0 to disable the upper zone.
             */
            static constexpr std::size_t UpperZoneSize = 0u;
    };

    /*======================================================================
     * Detail: MpeZoneManager
     *
     * Tracks the zone configuration and provides the channel-to-voice map.
     * Only instantiated when Config::MpeEnabled == true.
     *====================================================================*/

    namespace detail
    {
        static constexpr std::size_t INVALID_VOICE_SLOT = std::numeric_limits<std::size_t>::max();
        static constexpr uint8_t LOWER_MASTER_CH        = 0u; // MIDI ch 1, 0-indexed
        static constexpr uint8_t UPPER_MASTER_CH        = 15u; // MIDI ch 16, 0-indexed

        /**
         * @brief Internal MPE zone manager.
         *
         * Maintains:
         *   - Lower zone master channel and member channel range.
         *   - Upper zone master channel and member channel range.
         *   - channelToVoice[16]: member channel index -> voice slot, or INVALID.
         *
         * All operations are O(1) or O(NumMidiChannels) at worst.
         * No heap allocation. Designed for audio-thread use.
         */
        template <typename Config>
        struct MpeZoneManager
        {
                // Zone boundaries (0-indexed channels).
                uint8_t lowerMasterCh    = LOWER_MASTER_CH;
                uint8_t lowerFirstMember = 1u; // ch 2 (0-indexed ch 1)
                uint8_t lowerLastMember  = 0u; // exclusive upper bound, computed from size

                uint8_t upperMasterCh    = UPPER_MASTER_CH;
                uint8_t upperFirstMember = 15u; // ch 16 (0-indexed 15), computed
                uint8_t upperLastMember  = 14u; // exclusive lower bound, computed

                // Maps 0-indexed MIDI channel -> voice index, INVALID_VOICE_SLOT if none.
                std::array<std::size_t, 16u> channelToVoice {};

                MpeZoneManager() noexcept
                {
                    channelToVoice.fill (INVALID_VOICE_SLOT);
                    reconfigure (Config::LowerZoneSize, Config::UpperZoneSize);
                }

                /**
                 * @brief Reconfigure zone sizes at runtime.
                 *
                 * Corresponds to the MIDI CI MCM (Zone Configuration) message
                 * arriving as CC 6 on a master channel. Call from dispatchEvent
                 * when CC 6 is received on ch 1 (lower) or ch 16 (upper).
                 *
                 * @param lowerSize  Number of lower zone member channels (0 = zone off).
                 * @param upperSize  Number of upper zone member channels (0 = zone off).
                 */
                void reconfigure (std::size_t lowerSize, std::size_t upperSize) noexcept CASPI_NON_BLOCKING
                {
                    lowerFirstMember = 1u;
                    lowerLastMember  = static_cast<uint8_t> (1u + lowerSize); // exclusive

                    // Upper zone members count down from ch 15 (0-indexed 14).
                    upperLastMember  = static_cast<uint8_t> (15u - upperSize); // exclusive lower
                    upperFirstMember = 14u; // highest member channel (0-indexed)
                }

                /**
                 * @brief Handle CC 6 on a master channel (zone size configuration).
                 *
                 * Called from dispatchEvent. Avoids referencing Config::LowerZoneSize
                 * or Config::UpperZoneSize from outside this struct, which would cause
                 * MSVC to fail on non-MPE Config instantiations even inside a dead
                 * if constexpr branch.
                 */
                void handleZoneConfig (uint8_t ch, std::size_t ccValue) noexcept CASPI_NON_BLOCKING
                {
                    if (ch == LOWER_MASTER_CH)
                    {
                        reconfigure (ccValue, static_cast<std::size_t> (15u - upperLastMember));
                    }
                    else if (ch == UPPER_MASTER_CH)
                    {
                        reconfigure (static_cast<std::size_t> (lowerLastMember - 1u), ccValue);
                    }
                }

                /** @brief True if the given 0-indexed channel is a lower zone member. */
                bool isLowerMember (uint8_t ch) const noexcept CASPI_NON_BLOCKING
                {
                    return (ch >= lowerFirstMember) && (ch < lowerLastMember);
                }

                /** @brief True if the given 0-indexed channel is an upper zone member. */
                bool isUpperMember (uint8_t ch) const noexcept CASPI_NON_BLOCKING
                {
                    return (Config::UpperZoneSize > 0u) && (ch <= upperFirstMember) && (ch >= upperLastMember);
                }

                /** @brief True if the channel carries per-note data (member of any zone). */
                bool isMemberChannel (uint8_t ch) const noexcept CASPI_NON_BLOCKING
                {
                    return isLowerMember (ch) || isUpperMember (ch);
                }

                /** @brief True if the channel is a global master for either zone. */
                bool isMasterChannel (uint8_t ch) const noexcept CASPI_NON_BLOCKING
                {
                    if (Config::LowerZoneSize > 0u && ch == lowerMasterCh) return true;
                    if (Config::UpperZoneSize > 0u && ch == upperMasterCh) return true;
                    return false;
                }

                /** @brief Record a voice assignment for a member channel. */
                void assignVoice (uint8_t ch, std::size_t voiceIdx) noexcept CASPI_NON_BLOCKING
                {
                    if (ch < 16u)
                    {
                        channelToVoice[ch] = voiceIdx;
                    }
                }

                /** @brief Release a channel's voice assignment. */
                void releaseVoice (uint8_t ch) noexcept CASPI_NON_BLOCKING
                {
                    if (ch < 16u)
                    {
                        channelToVoice[ch] = INVALID_VOICE_SLOT;
                    }
                }

                /** @brief Return the voice index for a member channel, or INVALID_VOICE_SLOT. */
                CASPI_NO_DISCARD std::size_t voiceForChannel (uint8_t ch) const noexcept CASPI_NON_BLOCKING
                {
                    if (ch < 16u)
                    {
                        return channelToVoice[ch];
                    }
                    return INVALID_VOICE_SLOT;
                }
        };

    } // namespace detail

    /*======================================================================
     * Engine<FloatType, MaxVoices, Config>
     *====================================================================*/

    /**
     * @brief MIDI-driven synthesis engine with optional MPE support.
     *
     * @tparam FloatType   Floating-point sample type (float or double).
     * @tparam MaxVoices   Maximum polyphony. Forwarded to VoiceManager.
     * @tparam Config      Compile-time policy struct. Default: DefaultSynthConfig.
     */
    template <typename FloatType, std::size_t MaxVoices = 16u, typename Config = DefaultSynthConfig>
    class Engine
    {
        public:
            using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;
            using FactoryFn  = typename VoiceManager<FloatType, MaxVoices>::FactoryFn;

            /*------------------------------------------------------------------
             * Event callbacks — set before prepare(), called on audio thread.
             *
             * In MPE mode:
             *   onNoteOn   — ch is the member channel (e.g. 2), not 0.
             *   onPitchBend / onChannelPressure — ch is the member channel
             *                for per-note data, or the master channel for
             *                global data.
             *   Use the channel argument to look up per-note voice state.
             *-----------------------------------------------------------------*/

            /** @brief Note-on: note, velocity, MIDI channel (0-15), voice index. */
            std::function<void (uint8_t note, uint8_t vel, uint8_t channel, std::size_t voiceIdx)> onNoteOn;

            /** @brief Note-off: note, MIDI channel. */
            std::function<void (uint8_t note, uint8_t channel)> onNoteOff;

            /** @brief CC: controller number, value, MIDI channel. */
            std::function<void (uint8_t ccNum, uint8_t ccVal, uint8_t channel)> onControlChange;

            /** @brief Pitch bend: normalised [-1,+1], MIDI channel. */
            std::function<void (FloatType normBend, uint8_t channel)> onPitchBend;

            /** @brief Channel pressure: normalised [0,1], MIDI channel. */
            std::function<void (FloatType normPressure, uint8_t channel)> onChannelPressure;

            /** @brief Poly aftertouch: note, normalised pressure, MIDI channel. */
            std::function<void (uint8_t note, FloatType normPressure, uint8_t channel)> onPolyAftertouch;

            /** @brief Program change: program number (0-127), MIDI channel. */
            std::function<void (uint8_t program, uint8_t channel)> onProgramChange;

            /*------------------------------------------------------------------
             * Construction
             *-----------------------------------------------------------------*/

            /**
             * @brief Construct the engine.
             *
             * @param numVoices    Active polyphony (<= MaxVoices).
             * @param factory      Voice factory lambda returning VoiceConfig<FloatType>.
             *                     Called numVoices times during construction.
             * @param stealPolicy  Voice stealing policy.
             */
            Engine (std::size_t numVoices, FactoryFn factory, StealPolicy stealPolicy = StealPolicy::Oldest)
                : voiceManager (numVoices, std::move (factory), stealPolicy)
                , midiQueue (Config::MidiQueueCapacity)
                , producerToken (midiQueue)
            {
                resetControllerState();
            }

            Engine (const Engine&)            = delete;
            Engine& operator= (const Engine&) = delete;
            Engine (Engine&&)                 = default;
            Engine& operator= (Engine&&)      = default;

            /*------------------------------------------------------------------
             * Setup — setup thread only
             *-----------------------------------------------------------------*/

            /**
             * @brief Prepare for audio processing.
             *
             * Resizes internal buffers and calls VoiceManager::prepare().
             * Must be called before the first process() and after any topology change.
             *
             * @param numChannelsIn  Audio channel count.
             * @param numFramesIn    Block size in frames.
             * @param sampleRateIn   Sample rate in Hz.
             */
            void prepare (std::size_t numChannelsIn, std::size_t numFramesIn, double sampleRateIn) CASPI_ALLOCATING
            {
                numChannels = numChannelsIn;
                numFrames   = numFramesIn;
                sampleRate  = sampleRateIn;

                voiceManager.prepare (numChannelsIn, numFramesIn, sampleRateIn);
                outputBuffer.resize (numChannelsIn, numFramesIn);
                outputBuffer.clear();

                if (Config::SampleAccurate)
                {
                    subBlockBuffer.resize (numChannelsIn, numFramesIn);
                    subBlockBuffer.clear();
                }
            }

            /*------------------------------------------------------------------
             * MIDI ingestion — any thread, lock-free
             *-----------------------------------------------------------------*/

            /**
             * @brief Enqueue a MidiMessage for delivery on the next process() call.
             *
             * Lock-free; safe to call from any thread. Returns false if the queue
             * is at capacity (Config::MidiQueueCapacity).
             */
            bool pushMidi (const Midi::MidiMessage& msg) noexcept CASPI_NON_BLOCKING
            {
                return midiQueue.enqueue (producerToken, msg);
            }

            /** @brief Enqueue a Note On. */
            bool pushNoteOn (uint8_t channel, uint8_t note, uint8_t velocity, int32_t offset = 0) noexcept
                CASPI_NON_BLOCKING
            {
                return pushMidi (Midi::MidiMessage::makeNoteOn (channel, note, velocity, offset));
            }

            /** @brief Enqueue a Note Off. */
            bool pushNoteOff (uint8_t channel, uint8_t note, uint8_t velocity = 0u, int32_t offset = 0) noexcept
                CASPI_NON_BLOCKING
            {
                return pushMidi (Midi::MidiMessage::makeNoteOff (channel, note, velocity, offset));
            }

            /** @brief Enqueue a Pitch Bend. */
            bool pushPitchBend (uint8_t channel, int16_t value, int32_t offset = 0) noexcept CASPI_NON_BLOCKING
            {
                return pushMidi (Midi::MidiMessage::makePitchBend (channel, value, offset));
            }

            /** @brief Enqueue a Control Change. */
            bool pushCC (uint8_t channel, uint8_t cc, uint8_t value, int32_t offset = 0) noexcept CASPI_NON_BLOCKING
            {
                return pushMidi (Midi::MidiMessage::makeControlChange (channel, cc, value, offset));
            }

            /*------------------------------------------------------------------
             * Audio processing — audio thread only, noexcept
             *-----------------------------------------------------------------*/

            /**
             * @brief Process one block.
             *
             * Drains the MIDI queue, dispatches events, renders all active voices,
             * and writes the mixed result into the internal output buffer.
             * Audio thread only. noexcept.
             */
            void process() noexcept CASPI_NON_BLOCKING
            {
                outputBuffer.clear();
                drainMidi();

                if (Config::SampleAccurate)
                {
                    processBlockSampleAccurate();
                }
                else
                {
                    processBlockSimple();
                }

                applyMasterGain();
            }

            /**
             * @brief Process one block and accumulate into an external buffer.
             *
             * Calls process() then adds the internal output into dst.
             * dst must match the geometry passed to prepare().
             */
            void process (BufferType& dst) noexcept CASPI_NON_BLOCKING
            {
                process();
                const std::size_t C = dst.numChannels();
                const std::size_t F = dst.numFrames();
                for (std::size_t ch = 0; ch < C; ++ch)
                {
                    for (std::size_t fr = 0; fr < F; ++fr)
                    {
                        dst.sample (ch, fr) += outputBuffer.sample (ch, fr);
                    }
                }
            }

            /*------------------------------------------------------------------
             * Controller state accessors — audio thread only
             *-----------------------------------------------------------------*/

            /**
             * @brief Normalised pitch bend in [-1, +1] for the given MIDI channel.
             *
             * In MPE mode, member channels carry per-note bend; master channels
             * carry global bend. Query the appropriate channel.
             */
            CASPI_NO_DISCARD FloatType getPitchBend (uint8_t channel = 0u) const noexcept CASPI_NON_BLOCKING
            {
                if (channel >= Config::NumMidiChannels)
                {
                    return FloatType (0);
                }
                return channelState[channel].pitchBend;
            }

            /** @brief Normalised channel pressure in [0, 1] for the given MIDI channel. */
            CASPI_NO_DISCARD FloatType getChannelPressure (uint8_t channel = 0u) const noexcept CASPI_NON_BLOCKING
            {
                if (channel >= Config::NumMidiChannels)
                {
                    return FloatType (0);
                }
                return channelState[channel].channelPressure;
            }

            /** @brief Raw CC value 0-127 for a given controller on a given channel. */
            CASPI_NO_DISCARD uint8_t getCCValue (uint8_t cc, uint8_t channel = 0u) const noexcept CASPI_NON_BLOCKING
            {
                if (channel >= Config::NumMidiChannels)
                {
                    return 0u;
                }
                return channelState[channel].cc[cc & 0x7Fu];
            }

            /** @brief Normalised CC value in [0, 1]. */
            CASPI_NO_DISCARD FloatType getCCNormalised (uint8_t cc, uint8_t channel = 0u) const noexcept
                CASPI_NON_BLOCKING
            {
                return Midi::ccToNormalised<FloatType> (getCCValue (cc, channel));
            }

            /*------------------------------------------------------------------
             * MPE zone accessors — only meaningful when Config::MpeEnabled
             *-----------------------------------------------------------------*/

            /**
             * @brief Return the voice index currently assigned to a member channel.
             *
             * Only valid when Config::MpeEnabled == true.
             * Returns detail::INVALID_VOICE_SLOT if the channel has no active note.
             *
             * @param channel  0-indexed MIDI channel.
             */
            CASPI_NO_DISCARD std::size_t mpeVoiceForChannel (uint8_t channel) const noexcept CASPI_NON_BLOCKING
            {
                CASPI_CPP17_IF_CONSTEXPR (Config::MpeEnabled)
                {
                    return mpeZones.voiceForChannel (channel);
                }
                return detail::INVALID_VOICE_SLOT;
            }

            /*------------------------------------------------------------------
             * Output
             *-----------------------------------------------------------------*/

            /** @brief Internal output buffer populated by process(). */
            CASPI_NO_DISCARD const BufferType& getOutputBuffer() const noexcept CASPI_NON_BLOCKING
            {
                return outputBuffer;
            }

            /*------------------------------------------------------------------
             * VoiceManager access
             *-----------------------------------------------------------------*/

            CASPI_NO_DISCARD VoiceManager<FloatType, MaxVoices>& getVoiceManager() noexcept
            {
                return voiceManager;
            }

            CASPI_NO_DISCARD const VoiceManager<FloatType, MaxVoices>& getVoiceManager() const noexcept
            {
                return voiceManager;
            }

            /*------------------------------------------------------------------
             * Observers
             *-----------------------------------------------------------------*/

            CASPI_NO_DISCARD std::size_t getNumActiveVoices() const noexcept CASPI_NON_BLOCKING
            {
                return voiceManager.getNumActiveVoices();
            }

            CASPI_NO_DISCARD std::size_t getNumVoices() const noexcept CASPI_NON_BLOCKING
            {
                return voiceManager.getNumVoices();
            }

            CASPI_NO_DISCARD double getSampleRate() const noexcept
            {
                return sampleRate;
            }
            CASPI_NO_DISCARD std::size_t getNumFrames() const noexcept
            {
                return numFrames;
            }
            CASPI_NO_DISCARD std::size_t getNumChannels() const noexcept
            {
                return numChannels;
            }

            /*------------------------------------------------------------------
             * Utilities — audio thread
             *-----------------------------------------------------------------*/

            /** @brief Immediately silence all active voices. */
            void allNotesOff() noexcept CASPI_NON_BLOCKING
            {
                voiceManager.allNotesOff();

                CASPI_CPP17_IF_CONSTEXPR (Config::MpeEnabled)
                {
                    mpeZones.channelToVoice.fill (detail::INVALID_VOICE_SLOT);
                }
            }

            /** @brief Reset all cached CC, pitch bend, and pressure to defaults. */
            void resetControllerState() noexcept CASPI_NON_BLOCKING
            {
                for (std::size_t ch = 0; ch < Config::NumMidiChannels; ++ch)
                {
                    channelState[ch].pitchBend       = FloatType (0);
                    channelState[ch].channelPressure = FloatType (0);
                    for (std::size_t i = 0; i < 128u; ++i)
                    {
                        channelState[ch].cc[i] = 0u;
                    }
                }
            }

        private:
            /*------------------------------------------------------------------
             * Per-channel state
             *-----------------------------------------------------------------*/

            struct ChannelState
            {
                    FloatType pitchBend       = FloatType (0);
                    FloatType channelPressure = FloatType (0);
                    uint8_t cc[128u]          = {};
            };

            /*------------------------------------------------------------------
             * MIDI drain
             *-----------------------------------------------------------------*/

            void drainMidi() noexcept CASPI_NON_BLOCKING
            {
                pendingCount = 0u;
                Midi::MidiMessage msg;
                while (pendingCount < Config::MidiQueueCapacity && midiQueue.try_dequeue (msg))
                {
                    pending[pendingCount++] = msg;
                }
            }

            /*------------------------------------------------------------------
             * Event dispatch
             *
             * In MIDI 1.0 mode: all events routed to global voiceManager.
             * In MPE mode: note events routed to member channels' voice slots;
             *   CC/bend/pressure stored per-channel as usual (callers read
             *   the correct channel from the onNoteOn callback).
             *-----------------------------------------------------------------*/

            void dispatchEvent (const Midi::MidiMessage& msg) noexcept CASPI_NON_BLOCKING
            {
                const uint8_t ch = msg.getChannel();

                if (msg.isNoteOn())
                {
                    const std::size_t vi = voiceManager.findNextVoiceForNote (msg.getNoteNumber());

                    CASPI_CPP17_IF_CONSTEXPR (Config::MpeEnabled)
                    {
                        mpeZones.assignVoice (ch, vi);
                    }

                    if (onNoteOn)
                    {
                        onNoteOn (msg.getNoteNumber(), msg.getVelocity(), ch, vi);
                    }

                    voiceManager.noteOn (msg.getNoteNumber(), msg.getVelocity());
                }
                else if (msg.isNoteOff())
                {
                    voiceManager.noteOff (msg.getNoteNumber());

                    CASPI_CPP17_IF_CONSTEXPR (Config::MpeEnabled)
                    {
                        mpeZones.releaseVoice (ch);
                    }

                    if (onNoteOff)
                    {
                        onNoteOff (msg.getNoteNumber(), ch);
                    }
                }
                else if (msg.isControlChange())
                {
                    if (ch < Config::NumMidiChannels)
                    {
                        channelState[ch].cc[msg.getCCNumber()] = msg.getCCValue();
                    }

                    // MPE zone configuration: CC 6 on a master channel sets zone size.
                    // handleZoneConfig is a member of both MpeZoneManager and NullMpeZones,
                    // so this compiles for all Config types without touching Config fields.
                    CASPI_CPP17_IF_CONSTEXPR (Config::MpeEnabled)
                    {
                        if (msg.getCCNumber() == 6u)
                        {
                            mpeZones.handleZoneConfig (ch, msg.getCCValue());
                        }
                    }

                    if (msg.getCCNumber() == static_cast<uint8_t> (Midi::ControllerNumber::AllNotesOff)
                        || msg.getCCNumber() == static_cast<uint8_t> (Midi::ControllerNumber::AllSoundOff))
                        allNotesOff();

                    if (onControlChange)
                    {
                        onControlChange (msg.getCCNumber(), msg.getCCValue(), ch);
                    }
                }
                else if (msg.isPitchBend())
                {
                    const FloatType norm = Midi::pitchBendToNormalised<FloatType> (msg.getPitchBendValue());
                    if (ch < Config::NumMidiChannels)
                    {
                        channelState[ch].pitchBend = norm;
                    }
                    if (onPitchBend)
                    {
                        onPitchBend (norm, ch);
                    }
                }
                else if (msg.isChannelPressure())
                {
                    const FloatType norm = Midi::velocityToNormalised<FloatType> (msg.getPressure());
                    if (ch < Config::NumMidiChannels)
                    {
                        channelState[ch].channelPressure = norm;
                    }
                    if (onChannelPressure)
                    {
                        onChannelPressure (norm, ch);
                    }
                }
                else if (msg.isPolyAftertouch())
                {
                    if (onPolyAftertouch)
                    {
                        const FloatType norm = Midi::velocityToNormalised<FloatType> (msg.getPolyPressure());
                        onPolyAftertouch (msg.getNoteNumber(), norm, ch);
                    }
                }
                else if (msg.isProgramChange())
                {
                    if (onProgramChange)
                    {
                        onProgramChange (msg.getProgramNumber(), ch);
                    }
                }
            }

            /*------------------------------------------------------------------
             * Block rendering — simple (default)
             *-----------------------------------------------------------------*/

            void processBlockSimple() noexcept CASPI_NON_BLOCKING
            {
                for (std::size_t i = 0; i < pendingCount; ++i)
                {
                    dispatchEvent (pending[i]);
                }
                voiceManager.process (outputBuffer);
            }

            /*------------------------------------------------------------------
             * Block rendering — sample-accurate
             *-----------------------------------------------------------------*/

            void processBlockSampleAccurate() noexcept CASPI_NON_BLOCKING
            {
                // Insertion sort — O(N) for near-sorted MIDI streams.
                for (std::size_t i = 1u; i < pendingCount; ++i)
                {
                    const Midi::MidiMessage key = pending[i];
                    std::size_t j               = i;
                    while (j > 0u && pending[j - 1u].sampleOffset > key.sampleOffset)
                    {
                        pending[j] = pending[j - 1u];
                        --j;
                    }
                    pending[j] = key;
                }

                int32_t cursor = 0;

                for (std::size_t i = 0u; i < pendingCount; ++i)
                {
                    int32_t evOffset = pending[i].sampleOffset;
                    if (evOffset < 0)
                    {
                        evOffset = 0;
                    }
                    if (evOffset > static_cast<int32_t> (numFrames))
                    {
                        evOffset = static_cast<int32_t> (numFrames);
                    }

                    const int32_t subLen = evOffset - cursor;
                    if (subLen > 0)
                    {
                        renderSubBlock (static_cast<std::size_t> (cursor), static_cast<std::size_t> (subLen));
                    }

                    dispatchEvent (pending[i]);
                    cursor = evOffset;
                }

                const int32_t tail = static_cast<int32_t> (numFrames) - cursor;
                if (tail > 0)
                {
                    renderSubBlock (static_cast<std::size_t> (cursor), static_cast<std::size_t> (tail));
                }
            }

            void renderSubBlock (std::size_t startFrame, std::size_t len) noexcept CASPI_NON_BLOCKING
            {
                subBlockBuffer.clear();
                voiceManager.process (subBlockBuffer);

                for (std::size_t ch = 0u; ch < numChannels; ++ch)
                {
                    for (std::size_t fr = 0u; fr < len; ++fr)
                    {
                        outputBuffer.sample (ch, startFrame + fr) += subBlockBuffer.sample (ch, fr);
                    }
                }
            }

            /*------------------------------------------------------------------
             * Post-processing
             *-----------------------------------------------------------------*/

            void applyMasterGain() noexcept CASPI_NON_BLOCKING
            {
                const FloatType gain = static_cast<FloatType> (Config::MasterGain);

                for (std::size_t ch = 0u; ch < numChannels; ++ch)
                {
                    for (std::size_t fr = 0u; fr < numFrames; ++fr)
                    {
                        FloatType s = outputBuffer.sample (ch, fr) * gain;
                        if (Config::HardClip)
                        {
                            if (s > FloatType (1))
                            {
                                s = FloatType (1);
                            }
                            if (s < -FloatType (1))
                            {
                                s = -FloatType (1);
                            }
                        }
                        outputBuffer.sample (ch, fr) = s;
                    }
                }
            }

            /*------------------------------------------------------------------
             * Data members
             *-----------------------------------------------------------------*/

            VoiceManager<FloatType, MaxVoices> voiceManager;

            external::ConcurrentQueue<Midi::MidiMessage> midiQueue;
            external::ProducerToken producerToken;

            std::array<Midi::MidiMessage, Config::MidiQueueCapacity> pending {};
            std::size_t pendingCount = 0u;

            std::array<ChannelState, Config::NumMidiChannels> channelState {};

            // MPE zone manager — zero-cost when Config::MpeEnabled == false
            // (empty base optimisation applies; struct has no data members in that path).
            // Use a conditional type to avoid instantiating MpeZoneManager internals.
            struct NullMpeZones
            {
                    // Stub methods — present so dispatchEvent compiles uniformly for
                    // any Config regardless of MpeEnabled. All calls are no-ops.
                    void assignVoice (uint8_t, std::size_t) noexcept
                    {
                    }
                    void releaseVoice (uint8_t) noexcept
                    {
                    }
                    std::size_t voiceForChannel (uint8_t) const noexcept
                    {
                        return detail::INVALID_VOICE_SLOT;
                    }
                    void reconfigure (std::size_t, std::size_t) noexcept
                    {
                    }
                    void handleZoneConfig (uint8_t, std::size_t) noexcept
                    {
                    }
                    std::array<std::size_t, 16u> channelToVoice {};
            };

            // Select zone manager type at compile time.
            // Both types present the same interface; CASPI_CPP17_IF_CONSTEXPR
            // guards the branches so only the live path is evaluated.
            using ZoneManager =
                typename std::conditional<Config::MpeEnabled, detail::MpeZoneManager<Config>, NullMpeZones>::type;
            ZoneManager mpeZones {};

            BufferType outputBuffer;
            BufferType subBlockBuffer;

            std::size_t numChannels = 0u;
            std::size_t numFrames   = 0u;
            double sampleRate       = 0.0;
    };

} // namespace CASPI

#endif // CASPI_ENGINE_H