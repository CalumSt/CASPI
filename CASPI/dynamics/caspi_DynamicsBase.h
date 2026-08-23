#ifndef CASPI_DYNAMICS_BASE_H
#define CASPI_DYNAMICS_BASE_H

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
 * @file   dynamics/caspi_DynamicsBase.h
 * @author CS Islay
 * @brief  CRTP base class for dynamics processors (compressors, limiters,
 *         gates) integrating with AudioNode.
 * @ingroup dynamics
 *
 * @details
 * ### Overview
 *
 * DynamicsBase<Derived, FloatType> inherits Graph::AudioNode and adds:
 *   - A peak level detector with independent attack/release ballistics,
 *     shaped by the same analog TCO curve ADSR uses (Maths::analogTcoCoefficient).
 *   - Threshold / ratio / knee / makeup-gain parameter API, all in dB.
 *   - An optional second audio input port (1) for an external sidechain —
 *     if unconnected, the detector reads the main input (port 0) instead.
 *   - CRTP hook: computeGainReductionDb(levelDb) — the gain computer.
 *     Derived supplies the curve (VCA/FET/Optical/...); DynamicsBase
 *     supplies a standard soft-knee curve via defaultGainReductionDb()
 *     that Derived can call or replace outright.
 *
 * ### CRTP contract
 *
 * Derived must override:
 *   FloatType computeGainReductionDb(FloatType levelDb) noexcept
 *     — Return gain reduction in dB (>= 0; 0 == no reduction) for a
 *       detected level in dBFS. Typically calls defaultGainReductionDb()
 *       or implements a different topology's curve.
 *
 * ### Detector model
 *
 * Each frame, the detector picks a source sample (sidechain if connected,
 * else the main input), takes the peak absolute value across channels,
 * converts to dBFS, and smooths it with a one-pole follower:
 *
 *   smoothedDb = coeff * smoothedDb + (1 - coeff) * instantDb
 *
 * where coeff is attackCoeff when instantDb is rising (gain reduction
 * increasing) and releaseCoeff when falling — the standard attack/release
 * ballistics used by analog and digital compressors alike. Both
 * coefficients come from Maths::analogTcoCoefficient(tco, lengthInSamples),
 * the same closed form ADSR uses for its segment coefficients, so attack
 * and release times get the same "analog" curve character as an envelope
 * stage rather than a plain RC exponential.
 *
 * ### Output stage
 *
 * outputSample = inputSample * dBFSToLinear(makeupGainDb - gainReductionDb)
 *
 * No separate ramped Gain<FloatType> stage is used — the detector's own
 * attack/release smoothing already provides click-free gain changes.
 *
 ************************************************************************/

#include <algorithm>
#include <array>
#include <cmath>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include "maths/caspi_Maths.h"

namespace CASPI
{
    namespace Dynamics
    {
        /*======================================================================
         * DynamicsBase<Derived, FloatType>
         *====================================================================*/

        /**
         * @brief CRTP base class for dynamics (compressor/limiter/gate) nodes.
         *
         * @tparam Derived    Concrete dynamics class (CRTP).
         * @tparam FloatType  float or double.
         */
        template <typename Derived, typename FloatType>
        class DynamicsBase : public Graph::AudioNode<Derived, FloatType>
        {
            public:
                /*------------------------------------------------------------------
                 * Public parameter API — setup / GUI thread
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Set the threshold above which gain reduction begins.
                 * @param dB  Threshold level in dBFS.
                 */
                void setThreshold (FloatType dB) noexcept
                {
                    thresholdDb = dB;
                }

                /**
                 * @brief Set the compression ratio.
                 *
                 * A ratio of 1 means no compression; larger ratios compress more
                 * strongly. Very large ratios approximate a limiter.
                 *
                 * @param newRatio  Input:output ratio. Must be >= 1.
                 */
                void setRatio (FloatType newRatio) noexcept
                {
                    CASPI_ASSERT (newRatio >= FloatType (1), "Ratio must be >= 1");
                    ratio = newRatio;
                }

                /**
                 * @brief Set the knee width in dB.
                 *
                 * 0 dB is a hard knee (sharp bend at threshold). Larger values
                 * blend gradually into compression around the threshold.
                 *
                 * @param dB  Knee width in dB. Must be >= 0.
                 */
                void setKnee (FloatType dB) noexcept
                {
                    CASPI_ASSERT (dB >= FloatType (0), "Knee width must be non-negative");
                    kneeDb = dB;
                }

                /**
                 * @brief Set the attack time in seconds.
                 * @param seconds  Time for the detector to respond to a level
                 *                 increase. Must be > 0.
                 */
                void setAttackTime (FloatType seconds) noexcept
                {
                    CASPI_ASSERT (seconds > FloatType (0), "Attack time must be positive");
                    attackTime_s = seconds;
                    updateBallistics();
                }

                /**
                 * @brief Set the release time in seconds.
                 * @param seconds  Time for the detector to respond to a level
                 *                 decrease. Must be > 0.
                 */
                void setReleaseTime (FloatType seconds) noexcept
                {
                    CASPI_ASSERT (seconds > FloatType (0), "Release time must be positive");
                    releaseTime_s = seconds;
                    updateBallistics();
                }

                /**
                 * @brief Set the makeup gain applied after gain reduction.
                 * @param dB  Makeup gain in dB.
                 */
                void setMakeupGain (FloatType dB) noexcept
                {
                    makeupGainDb = dB;
                }

                /*------------------------------------------------------------------
                 * Parameter accessors
                 *-----------------------------------------------------------------*/

                CASPI_NO_DISCARD FloatType getThreshold() const noexcept { return thresholdDb; }
                CASPI_NO_DISCARD FloatType getRatio() const noexcept { return ratio; }
                CASPI_NO_DISCARD FloatType getKnee() const noexcept { return kneeDb; }
                CASPI_NO_DISCARD FloatType getAttackTime() const noexcept { return attackTime_s; }
                CASPI_NO_DISCARD FloatType getReleaseTime() const noexcept { return releaseTime_s; }
                CASPI_NO_DISCARD FloatType getMakeupGain() const noexcept { return makeupGainDb; }

                /**
                 * @brief Read the most recently computed gain reduction, in dB.
                 *
                 * Non-negative; 0 means no reduction is being applied. Useful
                 * for gain-reduction metering. Audio thread safe.
                 */
                CASPI_NO_DISCARD FloatType getGainReductionDb() const noexcept CASPI_NON_BLOCKING
                {
                    return lastGainReductionDb;
                }

                /*------------------------------------------------------------------
                 * State management
                 *-----------------------------------------------------------------*/

                /** @brief Reset the detector state. Call on transport stop to avoid clicks. */
                void reset() noexcept CASPI_NON_BLOCKING
                {
                    smoothedLevelDb = Constants::MINUS_INF_DBFS<FloatType>;
                    lastGainReductionDb = FloatType (0);
                }

                /*------------------------------------------------------------------
                 * Sample rate hook — NodeBase/AudioNode calls this
                 *-----------------------------------------------------------------*/

                void onSampleRateChanged (FloatType) noexcept override
                {
                    updateBallistics();
                }

                /*------------------------------------------------------------------
                 * Graph hooks
                 *-----------------------------------------------------------------*/

                void onPrepare (std::size_t, std::size_t, double sampleRateIn) noexcept
                {
                    const FloatType fs = static_cast<FloatType> (sampleRateIn);
                    Graph::NodeBase<FloatType>::setSampleRate (fs);
                }

                /**
                 * @brief Called by AudioNode::process() each block.
                 *
                 * Reads the main input (port 0) and, if connected, the sidechain
                 * input (port 1); runs the detector and gain computer per frame;
                 * writes the gain-reduced, makeup-gained signal to outputBuffer.
                 */
                void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept CASPI_NON_BLOCKING
                {
                    const auto* mainIn = ctx.getAudioInput (this->getId(), 0);
                    const auto* sideIn = ctx.getAudioInput (this->getId(), 1);
                    const auto* detectorSource = (sideIn != nullptr) ? sideIn : mainIn;

                    auto& buf = this->outputBuffer;
                    const auto C = buf.numChannels();
                    const auto F = buf.numFrames();

                    if (mainIn == nullptr)
                    {
                        buf.clear();
                        return;
                    }

                    for (std::size_t f = 0; f < F; ++f)
                    {
                        FloatType peak = FloatType (0);
                        if (detectorSource != nullptr)
                        {
                            for (std::size_t ch = 0; ch < C; ++ch)
                            {
                                peak = std::max (peak, std::abs (detectorSource->sample (ch, f)));
                            }
                        }

                        const FloatType linearGain = computeGainForPeak (peak);

                        for (std::size_t ch = 0; ch < C; ++ch)
                        {
                            buf.sample (ch, f) = mainIn->sample (ch, f) * linearGain;
                        }
                    }
                }

                /**
                 * @brief Process one mono sample outside the graph.
                 *
                 * Runs the same detector/gain-computer/makeup-gain path as
                 * processImpl(), using x itself as the detector source. Handy
                 * for testing a dynamics node without building an AudioGraph.
                 * Advances the detector's internal ballistics state exactly
                 * like a real block would.
                 *
                 * @param x  Input sample.
                 * @return   Gain-reduced, makeup-gained output sample.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) noexcept CASPI_NON_BLOCKING
                {
                    return x * computeGainForPeak (std::abs (x));
                }

                /**
                 * @brief Process one mono sample with an explicit sidechain value.
                 *
                 * As processSample(FloatType), but the detector reads sidechain
                 * instead of x -- mirrors connecting an external sidechain to
                 * port 1 in the graph.
                 *
                 * @param x          Input sample (what gets gain-reduced).
                 * @param sidechain  Sample the detector reads instead of x.
                 * @return           Gain-reduced, makeup-gained output sample.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x, FloatType sidechain) noexcept CASPI_NON_BLOCKING
                {
                    return x * computeGainForPeak (std::abs (sidechain));
                }

            protected:
                /*------------------------------------------------------------------
                 * Construction — protected; only Derived constructs via CRTP
                 *-----------------------------------------------------------------*/

                DynamicsBase()
                    : Graph::AudioNode<Derived, FloatType> (2, 1)
                {
                    updateBallistics();
                    reset();
                }

                /**
                 * @brief Standard soft-knee gain computer, shared by concrete
                 *        dynamics topologies.
                 *
                 * Below (threshold - knee/2): no reduction.
                 * Above (threshold + knee/2): straight-line ratio compression.
                 * Within the knee: a quadratic blend between the two, per
                 * Giannoulis, Massberg & Reiss (2012), eq. 4.
                 *
                 * @param levelDb  Detected (smoothed) level in dBFS.
                 * @return         Gain reduction in dB (>= 0).
                 */
                CASPI_NO_DISCARD FloatType defaultGainReductionDb (FloatType levelDb) const noexcept CASPI_NON_BLOCKING
                {
                    const FloatType overshoot = levelDb - thresholdDb;
                    const FloatType halfKnee = kneeDb * FloatType (0.5);

                    if (overshoot <= -halfKnee)
                    {
                        return FloatType (0);
                    }

                    const FloatType slope = FloatType (1) - (FloatType (1) / ratio);

                    if (overshoot > halfKnee)
                    {
                        return overshoot * slope;
                    }

                    // Quadratic knee blend.
                    const FloatType kneeSpan = (kneeDb > FloatType (0)) ? kneeDb : FloatType (1);
                    const FloatType x = overshoot + halfKnee;
                    return slope * (x * x) / (FloatType (2) * kneeSpan);
                }

            private:
                /*------------------------------------------------------------------
                 * Detector + gain computer for one frame, given its peak level.
                 * Shared by processImpl() (multi-channel, from a graph buffer) and
                 * processSample() (mono, standalone). Advances smoothedLevelDb and
                 * lastGainReductionDb as a side effect.
                 *-----------------------------------------------------------------*/

                FloatType computeGainForPeak (FloatType peak) noexcept
                {
                    const FloatType instantDb = Maths::linearTodBFS (peak);
                    const FloatType coeff = (instantDb > smoothedLevelDb) ? attackCoefficient : releaseCoefficient;
                    smoothedLevelDb = coeff * smoothedLevelDb + (FloatType (1) - coeff) * instantDb;

                    const FloatType reductionDb = static_cast<Derived*> (this)->computeGainReductionDb (smoothedLevelDb);
                    lastGainReductionDb = reductionDb;

                    return Maths::dBFSToLinear (makeupGainDb - reductionDb);
                }

                /*------------------------------------------------------------------
                 * Ballistics
                 *-----------------------------------------------------------------*/

                void updateBallistics() noexcept
                {
                    const FloatType fs = this->getSampleRate();
                    if (fs <= FloatType (0))
                    {
                        return;
                    }

                    attackCoefficient = Maths::analogTcoCoefficient (Constants::ATTACK_TCO<FloatType>, fs * attackTime_s);
                    releaseCoefficient = Maths::analogTcoCoefficient (Constants::DECAY_TCO<FloatType>, fs * releaseTime_s);
                }

                /*------------------------------------------------------------------
                 * Parameters
                 *-----------------------------------------------------------------*/

                FloatType thresholdDb = FloatType (-18);
                FloatType ratio = FloatType (4);
                FloatType kneeDb = FloatType (6);
                FloatType attackTime_s = FloatType (0.01);
                FloatType releaseTime_s = FloatType (0.1);
                FloatType makeupGainDb = FloatType (0);

                /*------------------------------------------------------------------
                 * Ballistics coefficients (recomputed by updateBallistics())
                 *-----------------------------------------------------------------*/

                FloatType attackCoefficient = FloatType (0);
                FloatType releaseCoefficient = FloatType (0);

                /*------------------------------------------------------------------
                 * Detector state
                 *-----------------------------------------------------------------*/

                FloatType smoothedLevelDb = Constants::MINUS_INF_DBFS<FloatType>;
                FloatType lastGainReductionDb = FloatType (0);
        };

    } // namespace Dynamics
} // namespace CASPI

#endif // CASPI_DYNAMICS_BASE_H
