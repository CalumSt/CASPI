#ifndef CASPI_WAVESHAPER_H
#define CASPI_WAVESHAPER_H

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
 * @file   gain/caspi_Waveshaper.h
 * @author CS Islay
 * @brief  AudioNode waveshaper with a library of built-in transfer curves.
 * @ingroup gain
 *
 * @details
 * ### Overview
 *
 * Waveshaper<FloatType> is a stateless sample-by-sample nonlinearity —
 * there is no history between samples, so a whole block can be shaped in
 * one pass. Built-in curves that reduce to a polynomial or a clamp
 * (Linear, HardClip, Cubic, Araya) are dispatched through CASPI's existing
 * SIMD block kernels (SIMD::kernels::ClampKernel, SIMD::kernels::PolyKernel)
 * via SIMD::block_op_unary(); curves that need a transcendental function
 * (SoftClip, Sine, Tan, Arctan, Sigmoid, TanhDrive, ArctanDrive, AnalogKnee,
 * and any user-registered Custom curve) run a tight scalar loop instead —
 * still far cheaper than the old design's per-sample std::unordered_map
 * lookup plus std::function indirection.
 *
 * Asymmetric shaping (a different curve above/below asymmetryPoint) always
 * takes the scalar per-sample path, since the curve choice itself varies
 * sample-to-sample.
 *
 * ### Why not SIMD::ops::tanh_block for SoftClip?
 *
 * ops::tanh_block is a degree-3/7 Taylor approximation valid only for
 * |x| <= 0.65 (float) / far tighter for double — see its own doc comment.
 * A driven saturator needs to be accurate exactly where a Taylor series
 * around 0 is worst: far from 0, where the curve is doing its saturating.
 * SoftClip therefore uses std::tanh directly (accurate everywhere) rather
 * than reuse that approximation outside its valid domain.
 *
 ************************************************************************/

#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "base/caspi_Features.h"
#include "base/caspi_SIMD.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include "maths/caspi_Maths.h"

namespace CASPI
{
    namespace Distortion
    {
        /** @brief Built-in waveshaper transfer curves. */
        enum class WaveshapeType
        {
            Linear,      ///< Passthrough.
            HardClip,    ///< clamp(x, -clipLimit, clipLimit).
            SoftClip,    ///< clipLimit * tanh(x / clipLimit) -- continuous saturation.
            Cubic,       ///< clamp(x, -1, 1)^3.
            Araya,       ///< Araya-Suyama cubic softener: 1.5x(1 - x^2/3), |x| <= 1.
            Sine,        ///< sin(x).
            Tan,         ///< tan(x).
            Arctan,      ///< atan(x).
            Sigmoid,     ///< Logistic sigmoid rescaled to [-1, 1], shaped by drive.
            TanhDrive,   ///< tanh(drive * x) / tanh(drive) -- unity gain at x = 1.
            ArctanDrive, ///< atan(drive * x) / atan(drive) -- unity gain at x = 1.
            AnalogKnee,  ///< sign(x) * |x|^(1 / analogKnee) -- power-law knee softening.
            Custom       ///< User-registered curve, selected via setCustomWaveshape().
        };

        /*======================================================================
         * Waveshaper<FloatType>
         *====================================================================*/

        template <CASPI_FLOAT_TYPE FloatType>
        class Waveshaper final : public Graph::AudioNode<Waveshaper<FloatType>, FloatType>
        {
            public:
                using Base = Graph::AudioNode<Waveshaper<FloatType>, FloatType>;

                Waveshaper() noexcept
                    : Base (1, 1)
                {
                }

                /*------------------------------------------------------------------
                 * Curve selection
                 *-----------------------------------------------------------------*/

                void setWaveshape (WaveshapeType type) noexcept
                {
                    shape = type;
                }

                CASPI_NO_DISCARD WaveshapeType getWaveshape() const noexcept
                {
                    return shape;
                }

                /**
                 * @brief Register a custom transfer function by name.
                 *
                 * Not real-time safe (may allocate) -- call from the setup thread
                 * only, before or between blocks, never mid-process().
                 *
                 * @tparam Func   Callable accepting FloatType and returning FloatType.
                 * @param name    Name to select this curve by later.
                 * @param fn      The callable.
                 */
                template <typename Func>
                void registerCustomWaveshape (const std::string& name, Func fn)
                {
                    customFunctions[name] = std::function<FloatType (FloatType)> (fn);
                }

                /**
                 * @brief Select a previously registered custom curve by name.
                 *
                 * On success, sets the active shape to Custom. On failure the
                 * active shape is left unchanged.
                 *
                 * @param name  Name passed to a prior registerCustomWaveshape() call.
                 * @return      True if found and selected, false otherwise.
                 */
                bool setCustomWaveshape (const std::string& name) noexcept
                {
                    const auto it = customFunctions.find (name);
                    if (it == customFunctions.end())
                    {
                        return false;
                    }
                    customFn = it->second;
                    shape = WaveshapeType::Custom;
                    return true;
                }

                /*------------------------------------------------------------------
                 * Asymmetric shaping
                 *-----------------------------------------------------------------*/

                /**
                 * @brief Enable a different curve for samples below asymmetryPoint.
                 * @param enabled          True to enable asymmetric shaping.
                 * @param newAsymmetryPoint Threshold below which negativeWaveshape is used.
                 */
                void setAsymmetry (bool enabled, FloatType newAsymmetryPoint) noexcept
                {
                    isAsymmetric = enabled;
                    asymmetryPoint = newAsymmetryPoint;
                }

                CASPI_NO_DISCARD bool getIsAsymmetric() const noexcept
                {
                    return isAsymmetric;
                }

                void setNegativeWaveshape (WaveshapeType type) noexcept
                {
                    negativeShape = type;
                }

                CASPI_NO_DISCARD WaveshapeType getNegativeWaveshape() const noexcept
                {
                    return negativeShape;
                }

                /*------------------------------------------------------------------
                 * Curve parameters
                 *-----------------------------------------------------------------*/

                /** @brief Set the clip limit used by HardClip/SoftClip. Must be > 0. */
                void setClipLimit (FloatType newClipLimit) noexcept
                {
                    CASPI_ASSERT (newClipLimit > FloatType (0), "Clip limit must be positive");
                    clipLimit = newClipLimit;
                }

                CASPI_NO_DISCARD FloatType getClipLimit() const noexcept
                {
                    return clipLimit;
                }

                /** @brief Set the drive amount used by Sigmoid/TanhDrive/ArctanDrive. Must be > 0. */
                void setDrive (FloatType newDrive) noexcept
                {
                    CASPI_ASSERT (newDrive > FloatType (0), "Drive must be positive");
                    drive = newDrive;
                }

                /** @brief Set the drive amount in dBFS. */
                void setDriveDb (FloatType dB) noexcept
                {
                    setDrive (Maths::dBFSToLinear (dB));
                }

                CASPI_NO_DISCARD FloatType getDrive() const noexcept
                {
                    return drive;
                }

                /** @brief Set the knee exponent used by AnalogKnee. Must be >= 1 (1 == linear). */
                void setAnalogKnee (FloatType amount) noexcept
                {
                    CASPI_ASSERT (amount >= FloatType (1), "Analog knee amount must be >= 1");
                    analogKnee = amount;
                }

                CASPI_NO_DISCARD FloatType getAnalogKnee() const noexcept
                {
                    return analogKnee;
                }

                /*------------------------------------------------------------------
                 * Graph hooks
                 *-----------------------------------------------------------------*/

                void onPrepare (std::size_t, std::size_t, double) noexcept
                {
                }

                /**
                 * @brief Called by AudioNode::process() each block.
                 *
                 * Shapes the main input (port 0) into outputBuffer, then clamps
                 * the result to [-1, 1] as a final safety net (matches the
                 * unconditional restrict() the previous design always applied).
                 */
                void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept CASPI_NON_BLOCKING
                {
                    const auto* in = ctx.getAudioInput (this->getId(), 0);
                    auto& buf = this->outputBuffer;
                    const auto C = buf.numChannels();
                    const auto F = buf.numFrames();

                    if (in == nullptr)
                    {
                        buf.clear();
                        return;
                    }

                    for (std::size_t ch = 0; ch < C; ++ch)
                    {
                        const FloatType* src = in->channelData (ch);
                        FloatType* dst = buf.channelData (ch);
                        processChannel (dst, src, F);
                    }
                }

                /**
                 * @brief Process one sample outside the graph.
                 *
                 * Applies the same curve selection (including asymmetric
                 * switching) as the block path, then clamps to [-1, 1]. Handy
                 * for testing a curve without building an AudioGraph.
                 *
                 * @param x  Input sample.
                 * @return   Shaped, clamped output sample.
                 */
                CASPI_NO_DISCARD FloatType processSample (FloatType x) const noexcept CASPI_NON_BLOCKING
                {
                    const WaveshapeType type = (isAsymmetric && x < asymmetryPoint) ? negativeShape : shape;
                    return Maths::clamp (evaluate (type, x), FloatType (-1), FloatType (1));
                }

            private:
                /*------------------------------------------------------------------
                 * Block dispatch
                 *-----------------------------------------------------------------*/

                void processChannel (FloatType* dst, const FloatType* src, std::size_t count) noexcept
                {
                    if (isAsymmetric)
                    {
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            dst[i] = processSample (src[i]); // already clamped
                        }
                        return;
                    }

                    applyBlock (shape, dst, src, count);
                    SIMD::ops::clamp (dst, FloatType (-1), FloatType (1), count);
                }

                void applyBlock (WaveshapeType type, FloatType* dst, const FloatType* src, std::size_t count) noexcept
                {
                    switch (type)
                    {
                        case WaveshapeType::Linear:
                        {
                            SIMD::ops::copy (dst, src, count);
                            break;
                        }
                        case WaveshapeType::HardClip:
                        {
                            SIMD::block_op_unary (dst, src, count, SIMD::kernels::ClampKernel<FloatType> (-clipLimit, clipLimit));
                            break;
                        }
                        case WaveshapeType::Cubic:
                        {
                            SIMD::block_op_unary (dst, src, count, SIMD::kernels::ClampKernel<FloatType> (FloatType (-1), FloatType (1)));
                            SIMD::block_op_unary (dst, dst, count, SIMD::kernels::PolyKernel<FloatType, 3> ({ FloatType (0), FloatType (0), FloatType (0), FloatType (1) }));
                            break;
                        }
                        case WaveshapeType::Araya:
                        {
                            SIMD::block_op_unary (dst, src, count, SIMD::kernels::ClampKernel<FloatType> (FloatType (-1), FloatType (1)));
                            SIMD::block_op_unary (dst, dst, count, SIMD::kernels::PolyKernel<FloatType, 3> ({ FloatType (0), FloatType (1.5), FloatType (0), FloatType (-0.5) }));
                            break;
                        }
                        default:
                        {
                            for (std::size_t i = 0; i < count; ++i)
                            {
                                dst[i] = evaluate (type, src[i]);
                            }
                            break;
                        }
                    }
                }

                /*------------------------------------------------------------------
                 * Reference scalar evaluation — used for the asymmetric path and
                 * every transcendental curve.
                 *-----------------------------------------------------------------*/

                CASPI_NO_DISCARD FloatType evaluate (WaveshapeType type, FloatType x) const noexcept
                {
                    switch (type)
                    {
                        case WaveshapeType::Linear:
                        {
                            return x;
                        }
                        case WaveshapeType::HardClip:
                        {
                            return Maths::clamp (x, -clipLimit, clipLimit);
                        }
                        case WaveshapeType::SoftClip:
                        {
                            return clipLimit * std::tanh (x / clipLimit);
                        }
                        case WaveshapeType::Cubic:
                        {
                            const FloatType c = Maths::clamp (x, FloatType (-1), FloatType (1));
                            return c * c * c;
                        }
                        case WaveshapeType::Araya:
                        {
                            const FloatType c = Maths::clamp (x, FloatType (-1), FloatType (1));
                            return FloatType (1.5) * c * (FloatType (1) - c * c / FloatType (3));
                        }
                        case WaveshapeType::Sine:
                        {
                            return std::sin (x);
                        }
                        case WaveshapeType::Tan:
                        {
                            return std::tan (x);
                        }
                        case WaveshapeType::Arctan:
                        {
                            return std::atan (x);
                        }
                        case WaveshapeType::Sigmoid:
                        {
                            return (FloatType (2) / (FloatType (1) + std::exp (-drive * x))) - FloatType (1);
                        }
                        case WaveshapeType::TanhDrive:
                        {
                            const FloatType denom = std::tanh (drive);
                            return (denom != FloatType (0)) ? (std::tanh (drive * x) / denom) : x;
                        }
                        case WaveshapeType::ArctanDrive:
                        {
                            const FloatType denom = std::atan (drive);
                            return (denom != FloatType (0)) ? (std::atan (drive * x) / denom) : x;
                        }
                        case WaveshapeType::AnalogKnee:
                        {
                            const FloatType s = (x < FloatType (0)) ? FloatType (-1) : FloatType (1);
                            return s * std::pow (std::fabs (x), FloatType (1) / analogKnee);
                        }
                        case WaveshapeType::Custom:
                        {
                            return customFn ? customFn (x) : x;
                        }
                    }
                    return x;
                }

                /*------------------------------------------------------------------
                 * Parameters
                 *-----------------------------------------------------------------*/

                WaveshapeType shape = WaveshapeType::Linear;
                WaveshapeType negativeShape = WaveshapeType::Linear;

                bool isAsymmetric = false;
                FloatType asymmetryPoint = FloatType (0);

                FloatType clipLimit = FloatType (1);
                FloatType drive = FloatType (1);
                FloatType analogKnee = FloatType (2);

                std::function<FloatType (FloatType)> customFn;
                std::unordered_map<std::string, std::function<FloatType (FloatType)>> customFunctions;
        };

    } // namespace Distortion
} // namespace CASPI

#endif // CASPI_WAVESHAPER_H
