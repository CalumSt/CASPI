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
#include "caspi_Core.h"
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

                Parameter() noexcept
                    : baseNormalized (FloatType (0))
                    , smoothedBase (FloatType (0))
                    , smoothingCoeff (FloatType (1))
                    , minValue (FloatType (0))
                    , maxValue (FloatType (1))
                    , scale (ParameterScale::Linear)
                {
                    // In Parameter constructor or as static_assert at class scope:
                    CASPI_STATIC_ASSERT(std::atomic<FloatType>::is_always_lock_free,
                        "FloatType atomic must be lock-free for RT safety");
                }

                explicit Parameter (FloatType initialNormalized) noexcept
                    : baseNormalized (Maths::clamp (initialNormalized, FloatType (0), FloatType (1)))
                    , smoothedBase (initialNormalized)
                    , smoothingCoeff (FloatType (1))
                    , minValue (FloatType (0))
                    , maxValue (FloatType (1))
                    , scale (ParameterScale::Linear)
                {
                }

                Parameter (FloatType min,
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
                void setBaseNormalized (FloatType input) noexcept CASPI_NON_BLOCKING
                {
                    const FloatType clamped = Maths::clamp (input, FloatType (0), FloatType (1));
                    baseNormalized.store (clamped, std::memory_order_relaxed);
                }

                /**
                 * @brief Get base normalized value (any thread)
                 * @return Current base normalized value [0, 1]
                 */
                CASPI_NO_DISCARD FloatType getBaseNormalized() const noexcept CASPI_NON_BLOCKING
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
                void setSmoothingTime (FloatType timeSeconds, FloatType sampleRate) noexcept CASPI_NON_BLOCKING
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
                void setRange (FloatType min,
                                                  FloatType max,
                                                  const ParameterScale scalingMode = ParameterScale::Linear) noexcept CASPI_NON_BLOCKING
                {
                    CASPI_EXPECT(max > min, "Max must be greater than min");
                    if (scalingMode == ParameterScale::Logarithmic) {
                        CASPI_EXPECT(min > FloatType(0), "Log scale requires min > 0");
                        CASPI_EXPECT(max > FloatType(0), "Log scale requires max > 0");
                    }
                    minValue = min;
                    maxValue = max;
                    scale    = scalingMode;
                }

                /**
                 * @brief Get minimum value
                 */
                CASPI_NO_DISCARD FloatType getMinValue() const noexcept CASPI_NON_BLOCKING
                {
                    return minValue;
                }

                /**
                 * @brief Get maximum value
                 */
                CASPI_NO_DISCARD FloatType getMaxValue() const noexcept CASPI_NON_BLOCKING
                {
                    return maxValue;
                }

                /**
                 * @brief Get current scaling mode
                 */
                CASPI_NO_DISCARD ParameterScale getScale() const noexcept CASPI_NON_BLOCKING
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
                void process() noexcept CASPI_NON_BLOCKING
                {
                    ScopedFlushDenormals flush; // Avoid denormals in smoothing calculations

                    const FloatType target = baseNormalized.load (std::memory_order_relaxed);

                    smoothedBase += (target - smoothedBase) * smoothingCoeff;
                }

                // Advance N samples without reading value — skips hot loop cost
                void skip(int numSamples) noexcept CASPI_NON_BLOCKING
                {
                    const FloatType target = baseNormalized.load(std::memory_order_relaxed);
                    // Coefficient for N steps: coeff_N = 1 - (1 - coeff)^N
                    const FloatType remaining = std::pow(FloatType(1) - smoothingCoeff,
                                                          FloatType(numSamples));
                    smoothedBase = target + (smoothedBase - target) * remaining;
                    if (std::abs(target - smoothedBase) < FloatType(1e-15))
                        smoothedBase = target;
                }

                /**
                 * @brief Get smoothed normalized value [0, 1]
                 * @return Smoothed normalized value (audio thread only)
                 */
                CASPI_NO_DISCARD FloatType valueNormalized() const noexcept CASPI_NON_BLOCKING
                {
                    return Maths::clamp (smoothedBase, FloatType (0), FloatType (1));
                }

                /**
                 * @brief Get scaled/mapped value
                 * @return Value mapped according to range and scale mode
                 */
                CASPI_NO_DISCARD FloatType value() const noexcept CASPI_NON_BLOCKING
                {
                    return mapNormalizedToScaled (valueNormalized());
                }

                /**
                 * @brief Convenience conversion operator
                 */
                CASPI_NO_DISCARD explicit operator FloatType() const noexcept CASPI_NON_BLOCKING
                {
                    return value();
                }

            protected:
                /**
                 * @brief Map normalized [0, 1] to scaled value
                 */
                CASPI_NO_DISCARD FloatType
                mapNormalizedToScaled (FloatType normalized) const noexcept CASPI_NON_BLOCKING
                {
                    switch (scale)
                    {
                        case ParameterScale::Linear:
                            return Maths::linearInterpolation_bl (minValue, maxValue, normalized);

                        case ParameterScale::Logarithmic:
                        {

                            const FloatType logMin   = std::log (minValue);
                            const FloatType logMax   = std::log (maxValue);
                            const FloatType logValue = Maths::linearInterpolation_bl (logMin, logMax, normalized);
                            return std::exp (logValue);
                        }

                        case ParameterScale::Bipolar:
                        {
                            // Map [0, 1] to [-range, +range]
                            const FloatType centre  = (minValue + maxValue) / FloatType(2);
                            const FloatType range   = (maxValue - minValue) / FloatType(2);
                            const FloatType bipolar = normalized * FloatType(2) - FloatType(1);
                            return centre + bipolar * range;
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

                ModulatableParameter() noexcept
                    : Parameter<FloatType>()
                    , modulationAccum (FloatType (0))
                {
                }

                explicit ModulatableParameter (FloatType initialNormalized) noexcept
                    : Parameter<FloatType> (initialNormalized)
                    , modulationAccum (FloatType (0))
                {
                }

                ModulatableParameter (FloatType min,
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
                void clearModulation() noexcept CASPI_NON_BLOCKING
                {
                    modulationAccum = FloatType (0);
                }

                /**
                 * @brief Add modulation amount (normalized)
                 * @param mod Modulation to add (can be negative)
                 */
                void addModulation (FloatType mod) noexcept CASPI_NON_BLOCKING
                {
                    modulationAccum += mod;
                }

                /**
                 * @brief Get current modulation amount
                 * @return Accumulated modulation value
                 */
                CASPI_NO_DISCARD FloatType getModulationAmount() const noexcept CASPI_NON_BLOCKING
                {
                    return modulationAccum;
                }

                /**
                 * @brief Get smoothed + modulated normalized value [0, 1]
                 * @return Final normalized value with modulation applied
                 */
                CASPI_NO_DISCARD FloatType valueNormalized() const noexcept CASPI_NON_BLOCKING
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
