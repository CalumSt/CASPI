#pragma once

/**
 * @file juce_caspi_Buffer.h
 * @brief Thin adapter copying a CASPI audio buffer into a juce::AudioBuffer<float>.
 * @ingroup juce_caspi
 */

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>

namespace CASPI::JuceAdapters
{
    /**
     * @brief Copy a rendered CASPI buffer (e.g. CASPI::Engine::getOutputBuffer())
     *        into a juce::AudioBuffer<float>, sample-for-sample.
     *
     * @tparam CaspiBufferType Any type exposing sample(channel, frame).
     *
     * Copies min(src, dst) geometry; caller is responsible for matching
     * channel/frame counts (e.g. via engine.prepare()).
     */
    template <typename CaspiBufferType>
    void copyToJuceBuffer (const CaspiBufferType& src, juce::AudioBuffer<float>& dst) noexcept
    {
        const auto numChannels = static_cast<std::size_t> (dst.getNumChannels());
        const auto numFrames   = static_cast<std::size_t> (dst.getNumSamples());

        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* out = dst.getWritePointer (static_cast<int> (ch));
            for (std::size_t f = 0; f < numFrames; ++f)
            {
                out[f] = src.sample (ch, f);
            }
        }
    }
} // namespace CASPI::JuceAdapters
