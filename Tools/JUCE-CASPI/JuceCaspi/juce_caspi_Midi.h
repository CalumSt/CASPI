#pragma once

/**
 * @file juce_caspi_Midi.h
 * @brief Thin adapter draining a juce::MidiBuffer into a CASPI MIDI-driven engine.
 * @ingroup juce_caspi
 */

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace CASPI::JuceAdapters
{
    /**
     * @brief Push every event in a juce::MidiBuffer into a CASPI Engine.
     *
     * @tparam EngineType Any type exposing pushNoteOn/pushNoteOff/pushCC/pushPitchBend
     *                    with CASPI::Engine's (channel, ..., sampleOffset) signatures.
     *                    Not constrained to CASPI::Engine so custom engines work too.
     *
     * JUCE MIDI channels are 1-based; CASPI channels are 0-based.
     */
    template <typename EngineType>
    void pushMidiBuffer (EngineType& engine, const juce::MidiBuffer& midi) noexcept
    {
        for (const auto metadata : midi)
        {
            const auto msg     = metadata.getMessage();
            const auto channel = static_cast<uint8_t> (juce::jlimit (0, 15, msg.getChannel() - 1));
            const auto offset  = metadata.samplePosition;

            if (msg.isNoteOn())
            {
                engine.pushNoteOn (channel, static_cast<uint8_t> (msg.getNoteNumber()),
                                    static_cast<uint8_t> (msg.getVelocity() * 127.0f), offset);
            }
            else if (msg.isNoteOff())
            {
                engine.pushNoteOff (channel, static_cast<uint8_t> (msg.getNoteNumber()),
                                     static_cast<uint8_t> (msg.getVelocity() * 127.0f), offset);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                engine.pushCC (channel, 123, 0, offset);
            }
            else if (msg.isController())
            {
                engine.pushCC (channel, static_cast<uint8_t> (msg.getControllerNumber()),
                                static_cast<uint8_t> (msg.getControllerValue()), offset);
            }
            else if (msg.isPitchWheel())
            {
                engine.pushPitchBend (channel, static_cast<int16_t> (msg.getPitchWheelValue() - 8192), offset);
            }
        }
    }
} // namespace CASPI::JuceAdapters
