#ifndef CASPI_MULTIPLY_H
#define CASPI_MULTIPLY_H

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
 * @file   caspi_Multiply.h
 * @author CS Islay
 * @brief  Generic N-input audio multiplying node for Graph::AudioGraph.
 * @ingroup core
 *
 * @details
 * Multiply<FloatType> multiplies all connected audio input ports together
 * into a single output port. Its primary use is as a VCA: multiplying an
 * oscillator/FM voice's audio output by an envelope node's audio output to
 * shape amplitude and note lifecycle from a plain graph connection, rather
 * than requiring the source node itself to understand envelopes.
 *
 * Unconnected ports contribute the multiplicative identity (1), not 0 — an
 * unconnected port means "no scaling from this input", not silence. A
 * Multiply node with nothing connected at all outputs a constant 1.
 *
 * ### Typical usage (envelope-shaped voice output)
 * @code
 *   auto [carId, car]   = graph.emplace<Operator<float>>();
 *   auto [envId, env]   = graph.emplace<Envelope::ADSR<float>>();
 *   auto [vcaId, vca]   = graph.emplace<Multiply<float>>(2);
 *   graph.connect(Port(carId), Port(vcaId, 0));
 *   graph.connect(Port(envId), Port(vcaId, 1));
 *   // vcaId is the voice's output node; envId is its lifecycle envelope.
 * @endcode
 ************************************************************************/

#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"

namespace CASPI
{
    /**
     * @class Multiply
     * @brief Multiplies N audio input ports into one output port.
     *
     * @tparam FloatType  float or double.
     */
    template <CASPI_FLOAT_TYPE FloatType>
    class Multiply final : public Graph::AudioNode<Multiply<FloatType>, FloatType>
    {
            using Base = Graph::AudioNode<Multiply<FloatType>, FloatType>;

        public:
            /**
             * @brief Construct a multiplier with a fixed number of input ports.
             * @param numInputs  Number of audio input ports to multiply. Must be > 0.
             */
            explicit Multiply (std::size_t numInputs) : Base (numInputs, 1)
            {
            }

            /** @brief No additional setup needed at prepare time. */
            void onPrepare (std::size_t, std::size_t, double) noexcept
            {
            }

            /**
             * @brief Multiplies all connected input ports into outputBuffer.
             *
             * Unconnected ports contribute a factor of 1 (identity), so a
             * partially-wired Multiply still passes through whatever is
             * connected rather than going silent.
             */
            void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
            {
                auto& buf = this->outputBuffer;
                buf.fill (FloatType (1));

                const std::size_t numPorts = this->getNumInputPorts();
                const std::size_t C        = buf.numChannels();
                const std::size_t Fm       = buf.numFrames();

                for (std::size_t port = 0; port < numPorts; ++port)
                {
                    const auto* in = ctx.getAudioInput (this->getId(), port);
                    if (in == nullptr)
                    {
                        continue;
                    }

                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        for (std::size_t fr = 0; fr < Fm; ++fr)
                        {
                            buf.sample (ch, fr) *= in->sample (ch, fr);
                        }
                    }
                }
            }
    };
} // namespace CASPI

#endif // CASPI_MULTIPLY_H
