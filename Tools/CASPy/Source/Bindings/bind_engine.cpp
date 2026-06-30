/*
 * bind_engine.cpp
 *
 * Binds Engine<float, 16, DefaultSynthConfig> and MidiMessage.
 *
 * VOICE FACTORY
 *
 * Engine's C++ voice factory must return VoiceConfig<float> by value.
 * From Python, a callable returns a tuple (AudioGraph, output_node_id, envelope_node_id).
 * Use -1 for envelope_node_id when there is no envelope.
 *
 * AudioGraph is non-copyable so the tuple must transfer graph ownership.
 * The Python factory should NOT keep a reference to the graph after returning it.
 *
 * Example factory:
 *
 *   def make_voice():
 *       g = caspy.AudioGraph()
 *       osc_id = g.add_node(caspy.BlepOscillator())
 *       return (g, osc_id, -1)   # -1 = no envelope
 *
 *   engine = caspy.Engine(8, make_voice)
 *
 * NOTE ON set_voice_frequency:
 *
 * Engine does not know which nodes in a voice graph are oscillators.
 * The recommended pattern is to set frequency inside on_note_on:
 *
 *   def on_note_on(note, vel, ch, voice_idx):
 *       engine.set_voice_frequency(voice_idx, note)
 *
 *   engine.on_note_on = on_note_on
 *
 * set_voice_frequency walks the voice graph and sets frequency on every
 * BlepOscillator node found. For other oscillator types, use a custom
 * on_note_on that calls get_voice_graph() and addresses nodes by id.
 */

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "synthesizers/caspi_Voice.h"
#include "midi/caspi_Midi.h"
#include "oscillators/caspi_BlepOscillator.h"
#include "synthesizers/caspi_Engine.h"

namespace py = pybind11;

using namespace CASPI;
using namespace CASPI::Midi;

using F        = float;
using Engine_t = Engine<F, 16u>;
using Graph_t  = Graph::AudioGraph<F>;

static py::array_t<F> buffer_to_numpy (const AudioBuffer<F, ChannelMajorLayout>& buf)
{
    const std::size_t C = buf.numChannels();
    const std::size_t N = buf.numFrames();

    py::array_t<F> out ({
        static_cast<py::ssize_t> (C),
        static_cast<py::ssize_t> (N)
    });
    auto r = out.mutable_unchecked<2>();

    for (std::size_t ch = 0; ch < C; ++ch)
    {
        for (std::size_t fr = 0; fr < N; ++fr)
        {
            r (static_cast<py::ssize_t> (ch), static_cast<py::ssize_t> (fr)) = buf.sample (ch, fr);
        }
    }
    return out;
}

void bind_engine (py::module_& m)
{
    /* StealPolicy */
    py::enum_<StealPolicy> (m, "StealPolicy")
        .value ("Oldest",   StealPolicy::Oldest)
        .value ("Quietest", StealPolicy::Quietest)
        .value ("None_",    StealPolicy::None)
        .export_values();

    /* MidiMessage */
    py::class_<MidiMessage> (m, "MidiMessage")
        .def_static ("note_on",
            [] (uint8_t ch, uint8_t note, uint8_t vel, int32_t off)
            { return MidiMessage::makeNoteOn (ch, note, vel, off); },
            py::arg ("channel"), py::arg ("note"), py::arg ("velocity"), py::arg ("offset") = 0)
        .def_static ("note_off",
            [] (uint8_t ch, uint8_t note, uint8_t vel, int32_t off)
            { return MidiMessage::makeNoteOff (ch, note, vel, off); },
            py::arg ("channel"), py::arg ("note"),
            py::arg ("velocity") = 0u, py::arg ("offset") = 0)
        .def_static ("control_change",
            [] (uint8_t ch, uint8_t cc, uint8_t val, int32_t off)
            { return MidiMessage::makeControlChange (ch, cc, val, off); },
            py::arg ("channel"), py::arg ("cc"), py::arg ("value"), py::arg ("offset") = 0)
        .def_static ("pitch_bend",
            [] (uint8_t ch, int16_t val, int32_t off)
            { return MidiMessage::makePitchBend (ch, val, off); },
            py::arg ("channel"), py::arg ("value"), py::arg ("offset") = 0)
        .def ("is_note_on",      &MidiMessage::isNoteOn)
        .def ("is_note_off",     &MidiMessage::isNoteOff)
        .def ("get_channel",     &MidiMessage::getChannel)
        .def ("get_note",        &MidiMessage::getNoteNumber)
        .def ("get_velocity",    &MidiMessage::getVelocity)
        .def ("get_cc_number",   &MidiMessage::getCCNumber)
        .def ("get_cc_value",    &MidiMessage::getCCValue)
        .def ("get_pitch_bend",  &MidiMessage::getPitchBendValue)
        .def_readwrite ("sample_offset", &MidiMessage::sampleOffset);

    m.def ("note_to_frequency",
        [] (uint8_t note) { return noteToFrequency<F> (note); },
        py::arg ("note"),
        "Convert MIDI note number to frequency in Hz.");

    /* Engine */
    py::class_<Engine_t> (m, "Engine",
        R"pbdoc(
        MIDI-driven polyphonic synthesis engine (up to 16 voices, float precision).

        Voice factory:
            Must be a Python callable returning (AudioGraph, output_id, envelope_id).
            Use -1 for envelope_id if the voice has no envelope node.

            def make_voice():
                g = caspy.AudioGraph()
                osc_id = g.add_node(caspy.BlepOscillator())
                return (g, osc_id, -1)

            engine = caspy.Engine(8, make_voice)
            engine.prepare(num_channels=1, num_frames=512, sample_rate=44100.0)

        Note-on frequency routing:
            engine.on_note_on = lambda note, vel, ch, vi: engine.set_voice_frequency(vi, note)

        Rendering:
            engine.push_note_on(0, 60, 100)
            engine.process()
            audio = engine.get_output()  # numpy [channels, frames]
        )pbdoc")

        .def (py::init (
            [] (std::size_t numVoices, py::object factory, StealPolicy policy)
            {
                auto cppFactory = [factory] () -> VoiceConfig<F>
                {
                    py::tuple result = factory();
                    if (result.size() != 3)
                    {
                        throw py::value_error (
                            "Voice factory must return (AudioGraph, output_id, envelope_id)");
                    }

                    Graph_t& g = result[0].cast<Graph_t&>();

                    const int outInt = result[1].cast<int>();
                    const int envInt = result[2].cast<int>();

                    const Graph::NodeId outId =
                        outInt < 0 ? Graph::INVALID_NODE_ID
                                   : static_cast<Graph::NodeId> (outInt);
                    const Graph::NodeId envId =
                        envInt < 0 ? Graph::INVALID_NODE_ID
                                   : static_cast<Graph::NodeId> (envInt);

                    return VoiceConfig<F> (std::move (g), outId, envId);
                };

                return std::make_unique<Engine_t> (numVoices, cppFactory, policy);
            }),
            py::arg ("num_voices"),
            py::arg ("factory"),
            py::arg ("steal_policy") = StealPolicy::Oldest)

        .def ("prepare",
            [] (Engine_t& self, std::size_t ch, std::size_t frames, double sr)
            { self.prepare (ch, frames, sr); },
            py::arg ("num_channels"), py::arg ("num_frames"), py::arg ("sample_rate"))

        .def ("process",     [] (Engine_t& self) { self.process(); })
        .def ("get_output",  [] (const Engine_t& self) { return buffer_to_numpy (self.getOutputBuffer()); },
              "Return last block as numpy [channels, frames].")

        .def ("push_midi",      [] (Engine_t& s, const MidiMessage& m) { return s.pushMidi (m); }, py::arg ("msg"))
        .def ("push_note_on",   [] (Engine_t& s, uint8_t ch, uint8_t n, uint8_t v, int32_t off) { return s.pushNoteOn (ch, n, v, off); },
              py::arg ("channel"), py::arg ("note"), py::arg ("velocity"), py::arg ("offset") = 0)
        .def ("push_note_off",  [] (Engine_t& s, uint8_t ch, uint8_t n, uint8_t v, int32_t off) { return s.pushNoteOff (ch, n, v, off); },
              py::arg ("channel"), py::arg ("note"), py::arg ("velocity") = 0u, py::arg ("offset") = 0)
        .def ("push_pitch_bend",[] (Engine_t& s, uint8_t ch, int16_t v, int32_t off) { return s.pushPitchBend (ch, v, off); },
              py::arg ("channel"), py::arg ("value"), py::arg ("offset") = 0)
        .def ("push_cc",        [] (Engine_t& s, uint8_t ch, uint8_t cc, uint8_t v, int32_t off) { return s.pushCC (ch, cc, v, off); },
              py::arg ("channel"), py::arg ("cc"), py::arg ("value"), py::arg ("offset") = 0)
        .def ("all_notes_off",  [] (Engine_t& s) { s.allNotesOff(); })

        .def ("get_num_active_voices", &Engine_t::getNumActiveVoices)
        .def ("get_num_voices",        &Engine_t::getNumVoices)
        .def ("get_pitch_bend",        &Engine_t::getPitchBend,        py::arg ("channel") = 0u)
        .def ("get_channel_pressure",  &Engine_t::getChannelPressure,  py::arg ("channel") = 0u)
        .def ("get_cc_value",          &Engine_t::getCCValue,          py::arg ("cc"), py::arg ("channel") = 0u)
        .def ("get_cc_normalised",     &Engine_t::getCCNormalised,     py::arg ("cc"), py::arg ("channel") = 0u)
        .def ("reset_controller_state", [] (Engine_t& s) { s.resetControllerState(); })

        /* Callbacks */
        .def_property ("on_note_on",
            [] (const Engine_t&) -> py::object { return py::none(); },
            [] (Engine_t& self, py::object cb)
            {
                if (cb.is_none())
                {
                    self.onNoteOn = nullptr;
                }
                else
                {
                    self.onNoteOn = [cb] (uint8_t note, uint8_t vel, uint8_t ch, std::size_t vi)
                    { cb (note, vel, ch, vi); };
                }
            },
            "Callback(note, velocity, channel, voice_index). Set before process().")

        .def_property ("on_note_off",
            [] (const Engine_t&) -> py::object { return py::none(); },
            [] (Engine_t& self, py::object cb)
            {
                if (cb.is_none())
                {
                    self.onNoteOff = nullptr;
                }
                else
                {
                    self.onNoteOff = [cb] (uint8_t note, uint8_t ch) { cb (note, ch); };
                }
            },
            "Callback(note, channel).")

        .def_property ("on_pitch_bend",
            [] (const Engine_t&) -> py::object { return py::none(); },
            [] (Engine_t& self, py::object cb)
            {
                if (cb.is_none())
                {
                    self.onPitchBend = nullptr;
                }
                else
                {
                    self.onPitchBend = [cb] (F bend, uint8_t ch) { cb (bend, ch); };
                }
            },
            "Callback(normalised_bend [-1,1], channel).")

        .def_property ("on_control_change",
            [] (const Engine_t&) -> py::object { return py::none(); },
            [] (Engine_t& self, py::object cb)
            {
                if (cb.is_none())
                {
                    self.onControlChange = nullptr;
                }
                else
                {
                    self.onControlChange = [cb] (uint8_t cc, uint8_t val, uint8_t ch)
                    { cb (cc, val, ch); };
                }
            },
            "Callback(cc_number, cc_value, channel).")

        /*
         * set_voice_frequency
         *
         * Walks all nodes in the voice graph and calls setFrequency()
         * on every BlepOscillator found. Call from inside on_note_on.
         */
        .def ("set_voice_frequency",
            [] (Engine_t& self, std::size_t voiceIdx, uint8_t midiNote)
            {
                auto* g = self.getVoiceManager().getVoiceGraph (voiceIdx);
                if (g == nullptr)
                {
                    return;
                }
                const F freq = noteToFrequency<F> (midiNote);
                for (Graph::NodeId id : g->getSortedOrder())
                {
                    auto* osc = g->getNodeAs<Oscillators::BlepOscillator<F>> (id);
                    if (osc != nullptr)
                    {
                        osc->setFrequency (freq);
                    }
                }
            },
            py::arg ("voice_index"),
            py::arg ("midi_note"),
            "Set BlepOscillator frequency in a voice from a MIDI note number.");
}