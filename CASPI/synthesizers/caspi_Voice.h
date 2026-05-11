#ifndef CASPI_VOICE_MANAGER_H
#define CASPI_VOICE_MANAGER_H

/*
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
 * @file   caspi_VoiceManager.h
 * @author CS Islay
 * @brief  Polyphonic voice manager owning N independent AudioGraph instances.
 *
 * ARCHITECTURE
 *
 * VoiceManager<F, MaxVoices> owns N AudioGraph instances (voices). Each voice
 * is an independent graph built by a user-supplied factory lambda. VoiceManager
 * dispatches noteOn / noteOff to voices and accumulates their outputs into a
 * shared buffer each block.
 *
 * VoiceManager is NOT itself a graph node. It sits above the graph layer and
 * is driven by Engine or directly by the host.
 *
 * FACTORY
 *
 * The caller provides a lambda returning VoiceConfig<F>, which carries the
 * constructed AudioGraph and the NodeIds of the output and envelope nodes.
 * The factory is called once per voice during construction.
 *
 * Using emplace<T>() inside the factory is the preferred pattern:
 *
 * @code
 *   VoiceManager<float, 8> vm (8, [] ()
 *   {
 *       AudioGraph<float> g;
 *       auto [oscId, osc] = g.emplace<BlepOscillator<float>>();
 *       auto [envId, env] = g.emplace<ADSR<float>>();
 *       osc.setFrequency (440.f);
 *       g.connect (envId, oscId);
 *       return VoiceConfig<float> { std::move (g), oscId, envId };
 *   });
 * @endcode
 *
 * VOICE STEALING
 *
 *   StealPolicy::Oldest    Steal the voice activated earliest.
 *   StealPolicy::Quietest  Steal the voice with the lowest envelope level.
 *   StealPolicy::None      Drop the note if all voices are active.
 *
 * THREAD SAFETY
 *
 *   prepare()                          setup thread only, may allocate
 *   noteOn / noteOff / process         audio thread only, noexcept
 *   allNotesOff / findNextVoiceForNote audio thread only, noexcept
 */

#include "controls/caspi_Envelope.h"
#include "core/caspi_AudioBuffer.h"
#include "core/caspi_Graph.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace CASPI
{

    /*======================================================================
     * VoiceConfig<FloatType>
     *====================================================================*/

    /**
     * @brief Return type of the voice factory lambda.
     *
     * Carries the constructed AudioGraph and the NodeIds of the output and
     * (optionally) envelope nodes. Construct via the three-argument constructor
     * or by aggregate initialisation.
     *
     * The envelopeNodeId is used by VoiceManager to detect when a voice has
     * finished its release phase and can be deactivated. Set it to
     * Graph::INVALID_NODE_ID if the voice has no envelope — VoiceManager will
     * deactivate the voice immediately on noteOff.
     *
     * @tparam FloatType  float or double.
     */
    template <typename FloatType>
    struct VoiceConfig
    {
        VoiceConfig() = default;

        VoiceConfig (Graph::AudioGraph<FloatType>&& g,
                     Graph::NodeId outId,
                     Graph::NodeId envId = Graph::INVALID_NODE_ID)
            : graph (std::move (g))
            , outputNodeId (outId)
            , envelopeNodeId (envId)
        {
        }

        /** @brief The voice graph. Moved into the VoiceManager on construction. */
        Graph::AudioGraph<FloatType> graph;

        /** @brief NodeId of the final output node (port 0 = audio output). */
        Graph::NodeId outputNodeId = Graph::INVALID_NODE_ID;

        /**
         * @brief NodeId of the Envelope node (ADSR or similar).
         *
         * VoiceManager monitors this node via getNodeAs<Envelope::Envelope<F>>().
         * Set to INVALID_NODE_ID if the voice has no envelope.
         */
        Graph::NodeId envelopeNodeId = Graph::INVALID_NODE_ID;
    };

    /*======================================================================
     * StealPolicy
     *====================================================================*/

    /**
     * @brief Determines which active voice is sacrificed when all voices are busy.
     */
    enum class StealPolicy
    {
        Oldest,   ///< Steal the voice that received noteOn earliest.
        Quietest, ///< Steal the voice whose envelope level is lowest.
        None      ///< Drop the note silently if all voices are active.
    };

    /*======================================================================
     * VoiceManager<FloatType, MAX_VOICES>
     *====================================================================*/

    /**
     * @brief Polyphonic voice manager owning N independent AudioGraph instances.
     *
     * @tparam FloatType   float or double.
     * @tparam MAX_VOICES  Compile-time maximum polyphony. Default: 16.
     */
    template <typename FloatType, std::size_t MAX_VOICES = 16>
    class VoiceManager
    {
        public:
            using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;
            using FactoryFn  = std::function<VoiceConfig<FloatType>()>;

            /*------------------------------------------------------------------
             * Construction
             *-----------------------------------------------------------------*/

            /**
             * @brief Construct the voice manager and build all voice graphs.
             *
             * Calls factory() exactly numVoices times. numVoices is clamped to
             * MAX_VOICES.
             *
             * @param numVoices    Active polyphony (<= MAX_VOICES).
             * @param factory      Lambda returning VoiceConfig<FloatType>.
             * @param policy       Voice stealing policy.
             */
            VoiceManager (std::size_t numVoices,
                          FactoryFn   factory,
                          StealPolicy policy = StealPolicy::Oldest)
                : numVoices (numVoices < MAX_VOICES ? numVoices : MAX_VOICES)
                , stealPolicy (policy)
            {
                for (std::size_t i = 0; i < this->numVoices; ++i)
                {
                    auto config        = factory();
                    voices[i].graph      = std::move (config.graph);
                    voices[i].outputNode = config.outputNodeId;
                    voices[i].envNode    = config.envelopeNodeId;
                }
            }

            /*------------------------------------------------------------------
             * Setup — setup thread only
             *-----------------------------------------------------------------*/

            /**
             * @brief Prepare all voice graphs for the given block geometry.
             *
             * Calls AudioGraph::prepare() on each voice graph. Must be called
             * before the first process() call and after any factory-time topology
             * changes.
             *
             * @param numChannels  Audio channel count.
             * @param numFrames    Block size in frames.
             * @param sampleRate   Sample rate in Hz.
             */
            void prepare (std::size_t numChannels, std::size_t numFrames, double sampleRate)
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    voices[i].graph.prepare (numChannels, numFrames, sampleRate);
                }
            }

            /*------------------------------------------------------------------
             * Note events — audio thread only
             *-----------------------------------------------------------------*/

            /**
             * @brief Activate a voice for the given MIDI note.
             *
             * Finds a free voice (or steals one per policy), marks it active,
             * resets and triggers its envelope if present.
             *
             * @param midiNote  MIDI note number (0-127).
             * @param velocity  Note velocity (0-127). Passed for completeness;
             *                  amplitude scaling is the caller's responsibility
             *                  via the onNoteOn callback in Engine.
             */
            void noteOn (int midiNote, int velocity) noexcept CASPI_NON_BLOCKING
            {
                (void) velocity;

                std::size_t idx = findFreeVoice();
                if (idx == INVALID_VOICE)
                {
                    idx = stealVoice();
                    if (idx == INVALID_VOICE)
                    {
                        return;
                    }
                }

                Voice& v   = voices[idx];
                v.midiNote = midiNote;
                v.active   = true;
                v.age      = nextAge++;

                if (v.envNode != Graph::INVALID_NODE_ID)
                {
                    auto* env = v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                    if (env != nullptr)
                    {
                        env->reset();
                        env->noteOn();
                    }
                }
            }

            /**
             * @brief Begin release phase for the first active voice matching midiNote.
             *
             * If the voice has an envelope, triggers its release. If there is no
             * envelope, deactivates the voice immediately.
             *
             * Only the first matching voice is affected (one note-on per note).
             *
             * @param midiNote  MIDI note number to release.
             */
            void noteOff (int midiNote) noexcept CASPI_NON_BLOCKING
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    Voice& v = voices[i];
                    if (! v.active || v.midiNote != midiNote)
                    {
                        continue;
                    }

                    if (v.envNode != Graph::INVALID_NODE_ID)
                    {
                        auto* env = v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                        if (env != nullptr)
                        {
                            env->noteOff();
                        }
                    }
                    else
                    {
                        v.active = false;
                    }
                    return;
                }
            }

            /**
             * @brief Release all active voices.
             *
             * Calls noteOff() for each active voice. Voices with envelopes will
             * complete their release phase before deactivating.
             */
            void allNotesOff() noexcept CASPI_NON_BLOCKING
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    if (voices[i].active)
                    {
                        noteOff (voices[i].midiNote);
                    }
                }
            }

            /*------------------------------------------------------------------
             * Process — audio thread only
             *-----------------------------------------------------------------*/

            /**
             * @brief Process all active voices and accumulate into outputBuffer.
             *
             * For each active voice:
             *   1. Calls graph.process().
             *   2. Checks envelope idle state; deactivates if idle.
             *   3. Accumulates the output node's buffer into outputBuffer.
             *
             * outputBuffer is cleared at entry. noexcept.
             *
             * @param outputBuffer  Destination mix buffer (cleared then accumulated).
             */
            void process (BufferType& outputBuffer) noexcept CASPI_NON_BLOCKING
            {
                outputBuffer.clear();

                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    Voice& v = voices[i];
                    if (! v.active)
                    {
                        continue;
                    }

                    v.graph.process();

                    if (v.envNode != Graph::INVALID_NODE_ID)
                    {
                        const auto* env =
                            v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                        if (env != nullptr && env->isIdle())
                        {
                            v.active = false;
                            continue;
                        }
                    }

                    if (v.outputNode == Graph::INVALID_NODE_ID)
                    {
                        continue;
                    }

                    const auto* node = v.graph.getNode (v.outputNode);
                    if (node == nullptr)
                    {
                        continue;
                    }

                    const auto* src = node->getOutputBuffer (0);
                    if (src == nullptr)
                    {
                        continue;
                    }

                    const std::size_t C = outputBuffer.numChannels();
                    const std::size_t F = outputBuffer.numFrames();

                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        for (std::size_t fr = 0; fr < F; ++fr)
                        {
                            outputBuffer.sample (ch, fr) += src->sample (ch, fr);
                        }
                    }
                }
            }

            /*------------------------------------------------------------------
             * Observers
             *-----------------------------------------------------------------*/

            /** @brief Number of voices this manager was constructed with. */
            CASPI_NO_DISCARD std::size_t getNumVoices() const noexcept
            {
                return numVoices;
            }

            /** @brief Number of currently active voices. */
            CASPI_NO_DISCARD std::size_t getNumActiveVoices() const noexcept CASPI_NON_BLOCKING
            {
                std::size_t n = 0;
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    if (voices[i].active)
                    {
                        ++n;
                    }
                }
                return n;
            }

            /**
             * @brief Return a non-owning pointer to the graph for the given voice index.
             *
             * Returns nullptr if index >= numVoices. Not for audio-thread use in
             * combination with topology changes.
             *
             * @param index  Zero-based voice index.
             */
            CASPI_NO_DISCARD Graph::AudioGraph<FloatType>* getVoiceGraph (std::size_t index) noexcept
            {
                return (index < numVoices) ? &voices[index].graph : nullptr;
            }

            /**
             * @brief Return the voice index that would be activated by the next noteOn.
             *
             * Read-only preview of findFreeVoice() / steal logic. Used by Engine to
             * configure the voice (e.g. set oscillator frequency) before noteOn()
             * activates the envelope.
             *
             * Returns INVALID_VOICE (std::size_t max) if no voice is available and
             * policy is StealPolicy::None.
             *
             * @param midiNote  Unused; reserved for future per-note routing.
             */
            CASPI_NO_DISCARD std::size_t findNextVoiceForNote (int /*midiNote*/) const noexcept
                CASPI_NON_BLOCKING
            {
                const std::size_t free = findFreeVoice();
                if (free != INVALID_VOICE)
                {
                    return free;
                }

                /* Mirror steal logic read-only: return oldest without side effects. */
                std::size_t oldest   = INVALID_VOICE;
                uint64_t    oldestAge = std::numeric_limits<uint64_t>::max();

                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    if (voices[i].active && voices[i].age < oldestAge)
                    {
                        oldestAge = voices[i].age;
                        oldest    = i;
                    }
                }
                return oldest;
            }

            /** @brief Sentinel returned by findNextVoiceForNote() when no voice is available. */
            static constexpr std::size_t INVALID_VOICE = std::numeric_limits<std::size_t>::max();

        private:
            /*------------------------------------------------------------------
             * Voice state
             *-----------------------------------------------------------------*/

            struct Voice
            {
                Graph::AudioGraph<FloatType> graph;
                Graph::NodeId outputNode = Graph::INVALID_NODE_ID;
                Graph::NodeId envNode    = Graph::INVALID_NODE_ID;
                int      midiNote        = -1;
                bool     active          = false;
                uint64_t age             = 0;
            };

            /*------------------------------------------------------------------
             * Internal allocation
             *-----------------------------------------------------------------*/

            std::size_t findFreeVoice() const noexcept CASPI_NON_BLOCKING
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    if (! voices[i].active)
                    {
                        return i;
                    }
                }
                return INVALID_VOICE;
            }

            std::size_t stealVoice() noexcept CASPI_NON_BLOCKING
            {
                switch (stealPolicy)
                {
                    case StealPolicy::None:
                    {
                        return INVALID_VOICE;
                    }

                    case StealPolicy::Oldest:
                    {
                        std::size_t oldest   = INVALID_VOICE;
                        uint64_t    oldestAge = std::numeric_limits<uint64_t>::max();

                        for (std::size_t i = 0; i < numVoices; ++i)
                        {
                            if (voices[i].active && voices[i].age < oldestAge)
                            {
                                oldestAge = voices[i].age;
                                oldest    = i;
                            }
                        }

                        if (oldest != INVALID_VOICE)
                        {
                            forceStop (oldest);
                        }
                        return oldest;
                    }

                    case StealPolicy::Quietest:
                    {
                        std::size_t quietest      = INVALID_VOICE;
                        FloatType   quietestLevel  = std::numeric_limits<FloatType>::max();

                        for (std::size_t i = 0; i < numVoices; ++i)
                        {
                            if (! voices[i].active)
                            {
                                continue;
                            }
                            const FloatType lv = voiceLevel (i);
                            if (lv < quietestLevel)
                            {
                                quietestLevel = lv;
                                quietest      = i;
                            }
                        }

                        if (quietest != INVALID_VOICE)
                        {
                            forceStop (quietest);
                        }
                        return quietest;
                    }

                    default:
                        return INVALID_VOICE;
                }
            }

            void forceStop (std::size_t i) noexcept CASPI_NON_BLOCKING
            {
                Voice& v = voices[i];
                if (v.envNode != Graph::INVALID_NODE_ID)
                {
                    auto* env =
                        v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                    if (env != nullptr)
                    {
                        env->reset();
                    }
                }
                v.active = false;
            }

            FloatType voiceLevel (std::size_t i) const noexcept CASPI_NON_BLOCKING
            {
                const Voice& v = voices[i];
                if (v.envNode == Graph::INVALID_NODE_ID)
                {
                    return FloatType (1);
                }
                const auto* env =
                    v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                return (env != nullptr) ? env->getLevel() : FloatType (1);
            }

            /*------------------------------------------------------------------
             * Data members
             *-----------------------------------------------------------------*/

            std::size_t numVoices   = 0;
            StealPolicy stealPolicy = StealPolicy::Oldest;
            uint64_t    nextAge     = 0;

            std::array<Voice, MAX_VOICES> voices;
    };

} // namespace CASPI

#endif // CASPI_VOICE_MANAGER_H