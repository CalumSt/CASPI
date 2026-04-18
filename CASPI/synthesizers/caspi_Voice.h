#ifndef CASPI_VOICE_MANAGER_H
#define CASPI_VOICE_MANAGER_H
/*************************************************************************
 * @file caspi_VoiceManager.h
 * @author CS Islay
 *
 * ### Architecture
 *   VoiceManager<F, MaxVoices> owns N AudioGraph instances (voices).
 *   Each voice is an independent graph built by a user-supplied factory.
 *   VoiceManager dispatches noteOn/noteOff to voices and accumulates
 *   their outputs into a shared buffer each block.
 *
 *   VoiceManager is NOT itself a graph node. It sits above the graph layer.
 *
 * ### Factory
 *   Caller provides a lambda returning VoiceConfig<F> containing the graph
 *   and the NodeIds of the output and envelope nodes.
 *
 * ### Voice stealing
 *   StealPolicy::Oldest   — steal the voice activated earliest
 *   StealPolicy::Quietest — steal the voice with the lowest envelope level
 *   StealPolicy::None     — drop note if all voices active
 *
 * ### Thread safety
 *   noteOn / noteOff / process — audio thread only
 *   prepare                    — setup thread only
 *
 * ### Example
 *   auto manager = VoiceManager<float, 8>(8, [&]() {
 *       AudioGraph<float> g;
 *       auto oscId = g.addNode(std::make_unique<OscillatorNode<float>>()).value();
 *       auto envId = g.addNode(std::make_unique<ADSR<float>>()).value();
 *       g.connect(envId, 0, oscId, 0, ConnectionType::Audio);
 *       return VoiceConfig<float>{ std::move(g), oscId, envId };
 *   });
 *   manager.prepare(2, 512, 48000.0);
 *   manager.noteOn(60, 100);
 *   manager.process(outputBuffer);
 ************************************************************************/

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

            Graph::AudioGraph<FloatType> graph;

            /** NodeId of the final output node (port 0 = audio output). */
            Graph::NodeId outputNodeId = Graph::INVALID_NODE_ID;

            /** NodeId of the ADSR<FloatType> envelope. INVALID if voice has no envelope. */
            Graph::NodeId envelopeNodeId = Graph::INVALID_NODE_ID;
    };

    /*======================================================================
     * StealPolicy
     *====================================================================*/
    enum class StealPolicy
    {
        Oldest, ///< Steal the voice that received noteOn earliest.
        Quietest, ///< Steal the voice whose envelope level is lowest.
        None ///< Drop the note if all voices are active.
    };

    /*======================================================================
     * VoiceManager<FloatType, MAX_VOICES>
     *====================================================================*/
    template <typename FloatType, std::size_t MAX_VOICES = 16>
    class VoiceManager
    {
        public:
            using BufferType = AudioBuffer<FloatType, ChannelMajorLayout>;
            using FactoryFn  = std::function<VoiceConfig<FloatType>()>;

            // ====================================================================
            // Construction
            // ====================================================================

            VoiceManager (std::size_t numVoices, FactoryFn factory, StealPolicy policy = StealPolicy::Oldest)
                : numVoices (numVoices < MAX_VOICES ? numVoices : MAX_VOICES)
                , stealPolicy (policy)
            {
                for (std::size_t i = 0; i < this->numVoices; ++i)
                {
                    auto config          = factory();
                    voices[i].graph      = std::move (config.graph);
                    voices[i].outputNode = config.outputNodeId;
                    voices[i].envNode    = config.envelopeNodeId;
                }
            }

            // ====================================================================
            // Setup
            // ====================================================================

            void prepare (std::size_t numChannels, std::size_t numFrames, double sampleRate)
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    voices[i].graph.prepare (numChannels, numFrames, sampleRate);
                }
            }

            // ====================================================================
            // Note events — audio thread only
            // ====================================================================

            void noteOn (int midiNote, int velocity)
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

            void noteOff (int midiNote)
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    Voice& v = voices[i];
                    if (v.active && v.midiNote == midiNote)
                    {
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
                            // Voice has no envelope; deactivate immediately
                            v.active = false;
                        }
                        return; // Only deactivate the first matching voice
                    }
                }
            }

            void allNotesOff()
            {
                for (std::size_t i = 0; i < numVoices; ++i)
                {
                    if (voices[i].active)
                    {
                        noteOff (voices[i].midiNote);
                    }
                }
            }

            // ====================================================================
            // Process — audio thread only
            // ====================================================================

            void process (BufferType& outputBuffer) noexcept
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
                        const auto* env = v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                        if (env != nullptr && env->isIdle())
                        {
                            v.active = false;
                            continue;
                        }
                    }

                    if (v.outputNode != Graph::INVALID_NODE_ID)
                    {
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
            }

            // ====================================================================
            // Observers
            // ====================================================================

            CASPI_NO_DISCARD std::size_t getNumVoices() const noexcept
            {
                return numVoices;
            }

            CASPI_NO_DISCARD std::size_t getNumActiveVoices() const noexcept
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

            CASPI_NO_DISCARD Graph::AudioGraph<FloatType>* getVoiceGraph (std::size_t index) noexcept
            {
                return (index < numVoices) ? &voices[index].graph : nullptr;
            }

            CASPI_NO_DISCARD std::size_t findNextVoiceForNote (int) const noexcept
            {
                std::size_t idx = findFreeVoice();
                if (idx == INVALID_VOICE)
                {
                    // Mirror stealVoice() without side effects — return candidate only.
                    // Simplest: return oldest active voice index without stopping it.
                    std::size_t oldest = INVALID_VOICE;
                    uint64_t age       = std::numeric_limits<uint64_t>::max();
                    for (std::size_t i = 0; i < numVoices; ++i)
                    {
                        if (voices[i].active && voices[i].age < age)
                        {
                            age    = voices[i].age;
                            oldest = i;
                        }
                    }
                    return oldest;
                }
                return idx;
            }

        private:
            // ====================================================================
            // Voice state
            // ====================================================================

            struct Voice
            {
                    Graph::AudioGraph<FloatType> graph;
                    Graph::NodeId outputNode = Graph::INVALID_NODE_ID;
                    Graph::NodeId envNode    = Graph::INVALID_NODE_ID;
                    int midiNote             = -1;
                    bool active              = false;
                    uint64_t age             = 0;
            };

            // ====================================================================
            // Allocation
            // ====================================================================

            static constexpr std::size_t INVALID_VOICE = std::numeric_limits<std::size_t>::max();

            std::size_t findFreeVoice() const noexcept
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

            std::size_t stealVoice() noexcept
            {
                switch (stealPolicy)
                {
                    case StealPolicy::None:
                    {
                        return INVALID_VOICE;
                    }
                    case StealPolicy::Oldest:
                    {
                        std::size_t oldest = INVALID_VOICE;
                        uint64_t oldestAge = std::numeric_limits<uint64_t>::max();

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
                        std::size_t quietest    = INVALID_VOICE;
                        FloatType quietestLevel = std::numeric_limits<FloatType>::max();

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
                    {
                        return INVALID_VOICE;
                    }
                }
            }

            void forceStop (std::size_t i) noexcept
            {
                Voice& v = voices[i];
                if (v.envNode != Graph::INVALID_NODE_ID)
                {
                    auto* env = v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                    if (env != nullptr)
                    {
                        env->reset();
                    }
                }
                v.active = false;
            }

            FloatType voiceLevel (std::size_t i) const noexcept
            {
                const Voice& v = voices[i];
                if (v.envNode == Graph::INVALID_NODE_ID)
                {
                    return FloatType (1);
                }
                const auto* env = v.graph.template getNodeAs<Envelope::Envelope<FloatType>> (v.envNode);
                return (env != nullptr) ? env->getLevel() : FloatType (1);
            }

            // ====================================================================
            // Data
            // ====================================================================

            std::size_t numVoices   = 0;
            StealPolicy stealPolicy = StealPolicy::Oldest;
            uint64_t nextAge        = 0;
            std::array<Voice, MAX_VOICES> voices;
    };

} // namespace CASPI

#endif // CASPI_VOICE_MANAGER_H