#ifndef CASPI_MIXER_H
#define CASPI_MIXER_H

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
 * @file   caspi_Mixer.h
 * @author CS Islay
 * @brief  Generic N-input audio summing node for Graph::AudioGraph.
 * @ingroup core
 *
 * @details
 * Mixer<FloatType> sums all connected audio input ports into a single output
 * port. It exists to let graph-native voices (e.g. several Operator carriers
 * summed to a voice output) combine signals without a bespoke engine — the
 * same mixing FMGraphDSP does internally for its designated output operators,
 * expressed as a plain graph node instead.
 *
 * Unconnected ports contribute silence (not an error) and are excluded from
 * the auto-scale divisor's port count only in the sense that the divisor is
 * the node's *declared* port count, matching FMGraphDSP::updateEffectiveGain()
 * (which divides by the declared number of output operators, not how many
 * happen to be producing non-zero signal on a given block).
 *
 * ### Typical usage
 * @code
 *   auto [mixId, mix] = graph.emplace<Mixer<float>>(2); // 2 carriers to sum
 *   graph.connect(car1Id, Port(mixId, 0));
 *   graph.connect(car2Id, Port(mixId, 1));
 *   mix.setAutoScale(true); // divide by 2 to keep headroom, like FMGraphDSP
 * @endcode
 ************************************************************************/

#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"

namespace CASPI
{
    /**
     * @class Mixer
     * @brief Sums N audio input ports into one output port.
     *
     * @tparam FloatType  float or double.
     */
    template <CASPI_FLOAT_TYPE FloatType>
    class Mixer final : public Graph::AudioNode<Mixer<FloatType>, FloatType>
    {
            using Base = Graph::AudioNode<Mixer<FloatType>, FloatType>;

        public:
            /**
             * @brief Construct a mixer with a fixed number of input ports.
             * @param numInputs  Number of audio input ports to sum. Must be > 0.
             */
            explicit Mixer (std::size_t numInputs) : Base (numInputs, 1)
            {
            }

            /**
             * @brief Enable/disable dividing the summed output by the declared
             *        input port count (headroom management for N summed signals).
             *
             * Matches FMGraphDSP::setAutoScaleOutputs(). Default: true.
             */
            void setAutoScale (bool enable) CASPI_NON_BLOCKING
            {
                autoScale = enable;
            }

            CASPI_NO_DISCARD bool getAutoScale() const CASPI_NON_BLOCKING
            {
                return autoScale;
            }

            /**
             * @brief Set a linear output gain applied after summing (and after
             *        auto-scaling, if enabled).
             */
            void setOutputGain (FloatType gain) CASPI_NON_BLOCKING
            {
                outputGain = gain;
            }

            CASPI_NO_DISCARD FloatType getOutputGain() const CASPI_NON_BLOCKING
            {
                return outputGain;
            }

            /** @brief No additional setup needed at prepare time. */
            void onPrepare (std::size_t, std::size_t, double) noexcept
            {
            }

            /**
             * @brief Sums all connected input ports into outputBuffer, then
             *        applies auto-scale (if enabled) and output gain.
             */
            void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
            {
                auto& buf = this->outputBuffer;
                buf.clear();

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
                            buf.sample (ch, fr) += in->sample (ch, fr);
                        }
                    }
                }

                FloatType scale = outputGain;
                if (autoScale && numPorts > 1)
                {
                    scale /= static_cast<FloatType> (numPorts);
                }

                if (scale != FloatType (1))
                {
                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        for (std::size_t fr = 0; fr < Fm; ++fr)
                        {
                            buf.sample (ch, fr) *= scale;
                        }
                    }
                }
            }

        private:
            bool autoScale         = true;
            FloatType outputGain   = FloatType (1);
    };
} // namespace CASPI

#endif // CASPI_MIXER_H
