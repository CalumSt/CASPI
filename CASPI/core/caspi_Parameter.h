#ifndef CASPI_PARAMETER_H
#define CASPI_PARAMETER_H

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
 * @file caspi_Parameter.h A template for CASPI header files.
 * @author CS Islay
 * @brief Use this file to write new files.
 ************************************************************************/

//------------------------------------------------------------------------------
// Includes - System
//------------------------------------------------------------------------------
#include <atomic>

//------------------------------------------------------------------------------
// Includes - Project
//------------------------------------------------------------------------------
#include "base/caspi_Assert.h"
#include "maths/caspi_Maths.h"

//------------------------------------------------------------------------------
// MACROs, constants, enums, typedefs
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Declarations
//------------------------------------------------------------------------------
namespace CASPI
{
    namespace Core
    {
        /**
         * @brief Scaling mode for parameter value mapping
         */
        enum class ParameterScale
        {
            Linear, // Linear mapping: normalized -> [min, max]
            Logarithmic, // Logarithmic mapping: normalized -> [min, max]
                         // (exponential)
            Bipolar // Bipolar mapping: normalized [0, 1] -> [-range, +range]
        };

        /**
         * @brief Non-modulatable parameter (base class)
         *
         * Thread-safe parameter with atomic base value and smoothing.
         * Cannot be modulated - use ModulatableParameter for that.
         *
         * @tparam FloatType Floating point type (float or double)
         */
        template <typename FloatType>
        class Parameter
        {
            public:
                // ====================================================================
                // Construction
                // ====================================================================

                CASPI_NON_BLOCKING Parameter() noexcept
                    : baseNormalized (FloatType (0))
                    , smoothedBase (FloatType (0))
                    , smoothingCoeff (FloatType (1))
                    , minValue (FloatType (0))
                    , maxValue (FloatType (1))
                    , scale (ParameterScale::Linear)
                {
                }

                CASPI_NON_BLOCKING explicit Parameter (FloatType initialNormalized) noexcept
                    : baseNormalized (Maths::clamp (initialNormalized, FloatType (0), FloatType (1)))
                    , smoothedBase (initialNormalized)
                    , smoothingCoeff (FloatType (1))
                    , minValue (FloatType (0))
                    , maxValue (FloatType (1))
                    , scale (ParameterScale::Linear)
                {
                }

                CASPI_NON_BLOCKING Parameter (FloatType min,
                                              FloatType max,
                                              FloatType defaultNormalized = FloatType (0)) noexcept
                    : baseNormalized (Maths::clamp (defaultNormalized, FloatType (0), FloatType (1)))
                    , smoothedBase (defaultNormalized)
                    , smoothingCoeff (FloatType (1))
                    , minValue (min)
                    , maxValue (max)
                    , scale (ParameterScale::Linear)
                {
                }

                // ====================================================================
                // GUI / Control Thread (Thread-Safe)
                // ====================================================================

                /**
                 * @brief Set base normalized value (GUI/control thread)
                 * @param input Normalized value [0, 1]
                 */
                CASPI_NON_BLOCKING void setBaseNormalized (FloatType input) noexcept
                {
                    const FloatType clamped = Maths::clamp (input, FloatType (0), FloatType (1));
                    baseNormalized.store (clamped, std::memory_order_relaxed);
                }

                /**
                 * @brief Get base normalized value (any thread)
                 * @return Current base normalized value [0, 1]
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType getBaseNormalized() const noexcept
                {
                    return baseNormalized.load (std::memory_order_relaxed);
                }

                /**
                 * @brief Set smoothing time to reach target
                 * @param timeSeconds Time to fully reach target value (in
                 * seconds)
                 * @param sampleRate Sample rate in Hz
                 *
                 * Uses a one-pole lowpass filter where the coefficient is
                 * calculated to reach ~99% of the target value in the specified
                 * time.
                 */
                CASPI_NON_BLOCKING void setSmoothingTime (FloatType timeSeconds, FloatType sampleRate) noexcept
                {
                    if (timeSeconds <= FloatType (0))
                    {
                        smoothingCoeff = FloatType (1); // Instant jump, no smoothing
                    }
                    else
                    {
                        // Calculate coefficient for 99% convergence in
                        // timeSeconds Using tau = timeSeconds / 4.6 (since
                        // e^-4.6 ≈ 0.01, so 99% reached)
                        const FloatType tau = timeSeconds / FloatType (4.6);

                        smoothingCoeff = FloatType (1) - std::exp (FloatType (-1) / (tau * sampleRate));
                    }
                }

                /**
                 * @brief Set value range and scaling mode
                 * @param min Minimum value
                 * @param max Maximum value
                 * @param scalingMode How to map normalized to actual values
                 */
                CASPI_NON_BLOCKING void setRange (FloatType min,
                                                  FloatType max,
                                                  const ParameterScale scalingMode = ParameterScale::Linear) noexcept
                {
                    CASPI_EXPECT (max > min, "Max must be greater than min");

                    minValue = min;
                    maxValue = max;
                    scale    = scalingMode;
                }

                /**
                 * @brief Get minimum value
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType getMinValue() const noexcept
                {
                    return minValue;
                }

                /**
                 * @brief Get maximum value
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType getMaxValue() const noexcept
                {
                    return maxValue;
                }

                /**
                 * @brief Get current scaling mode
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD ParameterScale getScale() const noexcept
                {
                    return scale;
                }

                // ====================================================================
                // Audio Thread (NOT Thread-Safe - Audio Thread Only)
                // ====================================================================

                /**
                 * @brief Process smoothing (call once per sample in audio
                 * thread)
                 */
                CASPI_NON_BLOCKING void process() noexcept
                {
                    const FloatType target = baseNormalized.load (std::memory_order_relaxed);

                    smoothedBase += (target - smoothedBase) * smoothingCoeff;
                }

                /**
                 * @brief Get smoothed normalized value [0, 1]
                 * @return Smoothed normalized value (audio thread only)
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType valueNormalized() const noexcept
                {
                    return Maths::clamp (smoothedBase, FloatType (0), FloatType (1));
                }

                /**
                 * @brief Get scaled/mapped value
                 * @return Value mapped according to range and scale mode
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType value() const noexcept
                {
                    return mapNormalizedToScaled (valueNormalized());
                }

                /**
                 * @brief Convenience conversion operator
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD explicit operator FloatType() const noexcept
                {
                    return value();
                }

            protected:
                /**
                 * @brief Map normalized [0, 1] to scaled value
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType
                mapNormalizedToScaled (FloatType normalized) const noexcept
                {
                    switch (scale)
                    {
                        case ParameterScale::Linear:
                            return Maths::linearInterpolation_bl (minValue, maxValue, normalized);

                        case ParameterScale::Logarithmic:
                        {
                            CASPI_RT_ASSERT (minValue > FloatType (0));
                            CASPI_RT_ASSERT (maxValue > FloatType (0));

                            const FloatType logMin   = std::log (minValue);
                            const FloatType logMax   = std::log (maxValue);
                            const FloatType logValue = Maths::linearInterpolation_bl (logMin, logMax, normalized);
                            return std::exp (logValue);
                        }

                        case ParameterScale::Bipolar:
                        {
                            // Map [0, 1] to [-range, +range]
                            const FloatType range   = (maxValue - minValue) / FloatType (2);
                            const FloatType bipolar = normalized * FloatType (2) - FloatType (1);
                            return bipolar * range;
                        }

                        default:
                            return minValue;
                    }
                }

            protected:
                // Thread-safe base value (GUI -> DSP)
                std::atomic<FloatType> baseNormalized;

                // Audio thread only
                FloatType smoothedBase;
                FloatType smoothingCoeff;

                // Range/scaling
                FloatType minValue;
                FloatType maxValue;
                ParameterScale scale;
        };

        /**
         * @brief Modulatable parameter (compile-time distinction)
         *
         * Extends Parameter with modulation accumulation.
         * Use this for parameters that need modulation support.
         *
         * @tparam FloatType Floating point type (float or double)
         */
        template <typename FloatType>
        class ModulatableParameter : public Parameter<FloatType>
        {
            public:
                using Parameter<FloatType>::Parameter;

                CASPI_NON_BLOCKING ModulatableParameter() noexcept
                    : Parameter<FloatType>()
                    , modulationAccum (FloatType (0))
                {
                }

                CASPI_NON_BLOCKING explicit ModulatableParameter (FloatType initialNormalized) noexcept
                    : Parameter<FloatType> (initialNormalized)
                    , modulationAccum (FloatType (0))
                {
                }

                CASPI_NON_BLOCKING ModulatableParameter (FloatType min,
                                                         FloatType max,
                                                         FloatType defaultNormalized = FloatType (0)) noexcept
                    : Parameter<FloatType> (min, max, defaultNormalized)
                    , modulationAccum (FloatType (0))
                {
                }

                // ====================================================================
                // Modulation (Audio Thread Only)
                // ====================================================================

                /**
                 * @brief Clear modulation accumulation (call at start of audio
                 * block)
                 */
                CASPI_NON_BLOCKING void clearModulation() noexcept
                {
                    modulationAccum = FloatType (0);
                }

                /**
                 * @brief Add modulation amount (normalized)
                 * @param mod Modulation to add (can be negative)
                 */
                CASPI_NON_BLOCKING void addModulation (FloatType mod) noexcept
                {
                    modulationAccum += mod;
                }

                /**
                 * @brief Get current modulation amount
                 * @return Accumulated modulation value
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType getModulationAmount() const noexcept
                {
                    return modulationAccum;
                }

                /**
                 * @brief Get smoothed + modulated normalized value [0, 1]
                 * @return Final normalized value with modulation applied
                 */
                CASPI_NON_BLOCKING CASPI_NO_DISCARD FloatType valueNormalized() const noexcept
                {
                    const FloatType modulated = this->smoothedBase + modulationAccum;
                    return Maths::clamp (modulated, FloatType (0), FloatType (1));
                }

            private:
                FloatType modulationAccum;
        };

    } // namespace Core
} // namespace CASPI

#endif // CASPI_PARAMETER_H
