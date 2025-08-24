/************************************************************************
 .d8888b.                             d8b
d88P  Y88b                            Y8P
888    888
888         8888b.  .d8888b  88888b.  888
888            "88b 88K      888 "88b 888
888    888 .d888888 "Y8888b. 888  888 888
Y88b  d88P 888  888      X88 888 d88P 888
 "Y8888P"  "Y888888  88888P' 88888P"  888
                             888
                             888
                             888


* @file caspi_BlepOscillator.h
* @author CS Islay
* @class BlepOscillator
* @brief A class implementing a BLEP oscillator.
*        I wanted to make a super performant oscillator with a BLEP
*        implementation, so have made it templated.
*
*        Should provide a decent implementation for 'trivial' waveforms.
*        The core 'BLEP' function is based on Martin Finke's implementation.
*
* @see https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/
*
* TODO: Generate to buffer
* TODO: Better docstring
*
************************************************************************/

#ifndef CASPI_BLEPOSCILLATOR_H
#define CASPI_BLEPOSCILLATOR_H

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Phase.h"
#include <cmath>

namespace CASPI::BlepOscillator
{

/// This is the core blep function
template <typename FloatType>
static FloatType blep (FloatType phase, FloatType increment)
{
    if (phase < increment)
    {
        /// internal phase variable
        auto phaseInternal = phase / increment;
        return (2 - phaseInternal) * phaseInternal - 1;
    }

    if (phase > 1 - increment)
    {
        auto phaseInternal = (phase - 1) / increment;
        return (phaseInternal + 2) * phaseInternal + 1;
    }

    return CASPI::Constants::zero<FloatType>;
}

/// Sine oscillator
template <typename FloatType>
struct Sine final : public Core::Producer<FloatType, Core::Traversal::PerFrame>
{
    void resetPhase() { phase.resetPhase(); }

    void setFrequency (FloatType frequency, FloatType sampleRate) { phase.setFrequency (CASPI::Constants::TWO_PI<float> * frequency, sampleRate); }

    FloatType renderSample() override { return std::sin (phase.advanceAndWrap (CASPI::Constants::TWO_PI<float>)); }

    Phase<FloatType> phase;
};

template <typename FloatType>
struct Saw final : public Core::Producer<FloatType, Core::Traversal::PerFrame>
{
    void resetPhase() { phase.resetPhase(); }
    void setFrequency (FloatType frequency, FloatType sampleRate) { phase.setFrequency (frequency, sampleRate); }
    FloatType renderSample() override
    {
        auto phaseInternal = phase.advanceAndWrap (1);
        return 2 * phaseInternal - 1 - blep<FloatType> (phaseInternal, phase.increment);
    }
    Phase<FloatType> phase;
};

/// Square oscillator
template <typename FloatType>
struct Square final : public Core::Producer<FloatType, Core::Traversal::PerFrame>
{
    void resetPhase() { phase.resetPhase(); }
    void setFrequency (FloatType frequency, FloatType sampleRate) { phase.setFrequency (frequency, sampleRate); }

    FloatType renderSample() override
    {
        auto phaseInternal = phase.advanceAndWrap (1);
        auto half          = static_cast<FloatType> (0.5);
        auto one           = static_cast<FloatType> (1);
        /// These static casts are ugly
        return ((phaseInternal < half) ? -one : one)
               - blep<FloatType> (phaseInternal, phase.increment)
               + blep<FloatType> (std::fmod (phaseInternal + half, one), phase.increment);
    }

    Phase<FloatType> phase;
};

/// Triangle Oscillator
template <typename FloatType>
struct Triangle final : public Core::Producer<FloatType, Core::Traversal::PerFrame>
{
    void resetPhase()
    {
        square.resetPhase();
        sum = 1;
    }
    void setFrequency (FloatType frequency, FloatType sampleRate)
    {
        square.setFrequency (frequency, sampleRate);
    }

    FloatType renderSample() override
    {
        sum += 4 * square.phase.increment * square.renderSample();
        constexpr auto offset = static_cast<FloatType>(0.05);
        return sum - offset;
    }

private:
    Square<FloatType> square;
    FloatType sum = 1;
};

template <typename OscillatorType, typename FloatType>
std::vector<FloatType> renderBlock (FloatType frequency, FloatType sampleRate, const int numberOfSamples = 1)
{
    OscillatorType osc;
    auto output = std::vector<FloatType> (numberOfSamples);
    osc.setFrequency (frequency, sampleRate);
    for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++)
    {
        // get sample from (mono) oscillator
        auto sample             = osc.renderSample();
        output.at (sampleIndex) = sample;
    }
    return output;
}

template <typename OscillatorType, typename BufferType=std::vector<float>, typename FloatType=float>
void renderBlock (BufferType &buffer, FloatType frequency, FloatType sampleRate, const int numberOfSamples = 1)
{
    OscillatorType osc;
    buffer.clear();
    osc.setFrequency (frequency, sampleRate);
    for (int sampleIndex = 0; sampleIndex < numberOfSamples; sampleIndex++)
    {
        // get sample from (mono) oscillator
        auto sample             = osc.renderSample();
       buffer[sampleIndex] = sample;
    }
}

}; // namespace CASPI::BlepOscillator

#endif //CASPI_BLEPOSCILLATOR_H
