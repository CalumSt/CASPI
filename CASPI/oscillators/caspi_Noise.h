/*****************************************************************************
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

 * @file   caspi_Noise.h
 * @author CS Islay
 * @brief  Noise oscillator with pluggable algorithm policy.
 *
 * @details
 * NoiseOscillator<FloatType, Algo> produces noise at audio rate via a
 * compile-time algorithm policy. Adding a new noise colour requires only a
 * new engine struct and a new AlgorithmTraits specialisation; the oscillator
 * class itself is unchanged.
 *
 * ### Algorithms
 * | NoiseAlgorithm | Engine           | PSD              | Cost (x86-64)   |
 * |----------------|------------------|------------------|-----------------|
 * | White          | xoshiro128+      | Flat             | ~2 ns/sample    |
 * | Pink           | Voss-McCartney   | -3 dB/octave     | ~5 ns/sample    |
 *
 * ### Architecture
 * Three layers:
 *
 * **detail::Xoshiro128Plus** — 32-bit PRNG, period 2^128-1. int32 → FloatType
 * via multiply by 2^-31 (no division, no branch). SplitMix64 warm-up in
 * seed() prevents all-zero state.
 *
 * **detail::WhiteNoiseEngine / PinkNoiseEngine** — thin wrappers around the
 * PRNG. PinkNoiseEngine uses an 8-stage first-order IIR approximation of
 * -3 dB/octave PSD. IIR feedback prevents SIMD of the PinkNoiseEngine inner
 * loop. WhiteNoiseEngine has no feedback and is a SIMD upgrade candidate.
 *
 * **detail::AlgorithmTraits<FloatType, Algo>** — maps a NoiseAlgorithm enum
 * value to an engine type at compile time. Add new noise colours here.
 *
 * ### Inheritance
 * - Core::Producer<FloatType, Traversal::PerSample>
 *   Provides render(AudioBuffer&) via the PerSample traversal policy.
 *   prepareBlock() and renderSample() are overridden.
 * - Core::SampleRateAware<FloatType>
 *   Provides getSampleRate() / setSampleRate(). Noise generation is not
 *   sample-rate dependent, but the parameter is retained for API consistency
 *   with other CASPI generators.
 *
 * ### Modulatable parameters
 * | Parameter | Range  | Scale  |
 * |-----------|--------|--------|
 * | amplitude | [0, 1] | Linear |
 *
 * ### Thread safety
 * - setAmplitude(), seed(), reset(), setSampleRate() — call from the audio
 *   thread or before streaming starts.
 * - amplitude parameter writes (setBaseNormalised, addModulation) — the
 *   underlying atomic base value is thread-safe; smoother state is not.
 * - prepareBlock() / renderSample() / renderBlock() — audio thread only.
 *
 * ### Typical usage
 * @code
 *   // White noise, 1 second
 *   CASPI::Oscillators::NoiseOscillator<float, CASPI::Oscillators::NoiseAlgorithm::White> osc(44100.f);
 *   float buffer[44100];
 *   osc.renderBlock(buffer, 44100);
 *
 *   // Convenience aliases
 *   CASPI::Oscillators::WhiteNoiseOscillator<float> white(44100.f);
 *   CASPI::Oscillators::PinkNoiseOscillator<float>  pink(44100.f);
 *
 *   // Reproducible test vector
 *   white.seed(42);
 *   float s = white.renderSample();
 * @endcode
 *****************************************************************************/

#ifndef CASPI_NOISE_H
#define CASPI_NOISE_H

#include "base/caspi_Assert.h"
#include "base/caspi_Constants.h"
#include "core/caspi_Core.h"
#include "core/caspi_Parameter.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace CASPI
{
namespace Oscillators
{

/*******************************************************************************
 * NoiseAlgorithm
 ******************************************************************************/

/**
 * @brief Selects the noise generation algorithm at compile time.
 *
 * @details
 * Passed as a template parameter to NoiseOscillator. Adding a new algorithm
 * requires a new engine struct in detail:: and a new AlgorithmTraits
 * specialisation; no changes to NoiseOscillator are needed.
 */
enum class NoiseAlgorithm
{
    White, ///< Flat PSD. xoshiro128+, ~2 ns/sample (x86-64).
    Pink   ///< -3 dB/octave PSD. 8-stage IIR, ~5 ns/sample (x86-64).
};

/*******************************************************************************
 * detail — engines and traits; not public API
 ******************************************************************************/

namespace detail
{

/*******************************************************************************
 * Xoshiro128Plus
 ******************************************************************************/

/**
 * @brief xoshiro128+ 32-bit PRNG. Period 2^128-1.
 *
 * @details
 * Produces uniformly distributed uint32_t values. Converted to FloatType
 * by the engine layer via integer cast and multiply by 2^-31 (no division,
 * no branch).
 *
 * seed() uses a SplitMix64 warm-up to prevent the all-zero state that would
 * cause the generator to produce only zeros indefinitely.
 *
 * @note This is an internal type. Use WhiteNoiseEngine or PinkNoiseEngine
 *       via NoiseOscillator.
 */
struct Xoshiro128Plus
{
    std::array<uint32_t, 4> s { 0x12345678u, 0x9ABCDEF0u, 0xFEDCBA98u, 0x76543210u };

    /**
     * @brief Produce the next uint32_t output and advance state.
     *
     * @return Next pseudo-random uint32_t.
     */
    CASPI_ALWAYS_INLINE uint32_t next() noexcept
    {
        const uint32_t result = s[0] + s[3];
        const uint32_t t      = s[1] << 9;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3]  = (s[3] << 11) | (s[3] >> 21);
        return result;
    }

    /**
     * @brief Re-seed the generator from a 64-bit value.
     *
     * @details
     * Uses two SplitMix64 steps to populate all four 32-bit state words,
     * guaranteeing a non-zero initial state regardless of the seed value.
     *
     * @param v  Seed value. 0 is valid.
     */
    void seed (uint64_t v) noexcept
    {
        auto sm64 = [](uint64_t& x) -> uint64_t {
            x += 0x9e3779b97f4a7c15ULL;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        };
        uint64_t x = v;
        uint64_t a = sm64 (x);
        uint64_t b = sm64 (x);
        std::memcpy (&s[0], &a, 8);
        std::memcpy (&s[2], &b, 8);
    }
};

/*******************************************************************************
 * WhiteNoiseEngine
 ******************************************************************************/

/**
 * @brief White noise engine. Flat power spectral density.
 *
 * @details
 * Converts uint32_t PRNG output to FloatType by reinterpreting as int32_t
 * and multiplying by 2^-31. No division, no branch, no data dependency
 * between consecutive samples (SIMD upgrade candidate).
 *
 * @tparam FloatType  float or double.
 */
template <typename FloatType>
struct WhiteNoiseEngine
{
    static constexpr FloatType kScale = FloatType (1) / FloatType (2147483648.0);

    Xoshiro128Plus rng {};

    /**
     * @brief Generate the next white noise sample in approximately [-1, 1].
     *
     * @return Noise sample.
     */
    CASPI_NO_DISCARD CASPI_ALWAYS_INLINE
    FloatType next() noexcept
    {
        return static_cast<FloatType> (static_cast<int32_t> (rng.next())) * kScale;
    }

    /**
     * @brief Re-seed the PRNG.
     *
     * @param s  Seed value.
     */
    void seed  (uint64_t s) noexcept { rng.seed (s); }

    /**
     * @brief Reset the PRNG to its default initial state.
     */
    void reset () noexcept { rng = Xoshiro128Plus {}; }
};

/*******************************************************************************
 * PinkNoiseEngine
 ******************************************************************************/

/**
 * @brief Pink noise engine. Approximates -3 dB/octave PSD.
 *
 * @details
 * Implements an 8-stage first-order IIR filter bank driven by white noise.
 * Each stage acts as a low-pass filter at a different corner frequency.
 * Summing the outputs approximates a -3 dB/octave (1/f) power spectrum
 * over the audible range.
 *
 * Per-sample cost: 8 FMAs + one white noise draw (~5 ns x86-64).
 * IIR feedback prevents SIMD of the inner loop.
 *
 * kOutputScale (0.11) is empirical. Verify the peak level against a
 * spectrum analyser for your specific amplitude requirements before
 * relying on it for metering or normalisation.
 *
 * @tparam FloatType  float or double.
 */
template <typename FloatType>
struct PinkNoiseEngine
{
    WhiteNoiseEngine<FloatType> white {};

    // IIR pole coefficients
    static constexpr FloatType b0c = FloatType ( 0.99886);
    static constexpr FloatType b1c = FloatType ( 0.99332);
    static constexpr FloatType b2c = FloatType ( 0.96900);
    static constexpr FloatType b3c = FloatType ( 0.86650);
    static constexpr FloatType b4c = FloatType ( 0.55000);
    static constexpr FloatType b5c = FloatType (-0.7616);

    // Feed-forward weights (applied to white input per stage)
    static constexpr FloatType w0 = FloatType ( 0.0555179);
    static constexpr FloatType w1 = FloatType ( 0.0750759);
    static constexpr FloatType w2 = FloatType ( 0.1538520);
    static constexpr FloatType w3 = FloatType ( 0.3104856);
    static constexpr FloatType w4 = FloatType ( 0.5329522);
    static constexpr FloatType w5 = FloatType (-0.0168980);

    /// @brief Empirical output normalisation scale. Verify against a spectrum
    ///        analyser for your amplitude range requirements.
    static constexpr FloatType kOutputScale = FloatType (0.11);

    FloatType b0 {}, b1 {}, b2 {}, b3 {}, b4 {}, b5 {}, b6 {};

    /**
     * @brief Generate the next pink noise sample in approximately [-1, 1].
     *
     * @return Noise sample.
     */
    CASPI_NO_DISCARD CASPI_ALWAYS_INLINE
    FloatType next() noexcept
    {
        const FloatType w = white.next();
        b0 = b0c * b0 + w * w0;
        b1 = b1c * b1 + w * w1;
        b2 = b2c * b2 + w * w2;
        b3 = b3c * b3 + w * w3;
        b4 = b4c * b4 + w * w4;
        b5 = b5c * b5 + w * w5;
        b6 = w * FloatType (0.115926);
        return (b0 + b1 + b2 + b3 + b4 + b5 + b6) * kOutputScale;
    }

    /**
     * @brief Re-seed the PRNG. IIR filter state is not reset.
     *
     * @param s  Seed value. Call reset() if filter state must also be cleared.
     */
    void seed (uint64_t s) noexcept { white.seed (s); }

    /**
     * @brief Reset the PRNG and zero all IIR filter state.
     */
    void reset() noexcept
    {
        white.reset();
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = FloatType (0);
    }
};

/*******************************************************************************
 * AlgorithmTraits
 ******************************************************************************/

/**
 * @brief Maps a NoiseAlgorithm enum value to an engine type at compile time.
 *
 * @details
 * To add a new noise colour (e.g. Brown, Blue):
 * 1. Implement a new engine struct with next(), seed(), and reset().
 * 2. Add a new NoiseAlgorithm enumerator.
 * 3. Add a specialisation of AlgorithmTraits here.
 * NoiseOscillator does not need to change.
 *
 * @tparam FloatType  float or double.
 * @tparam Algo       NoiseAlgorithm enum value.
 */
template <typename FloatType, NoiseAlgorithm Algo>
struct AlgorithmTraits;

template <typename FloatType>
struct AlgorithmTraits<FloatType, NoiseAlgorithm::White>
{
    using Engine = WhiteNoiseEngine<FloatType>;
};

template <typename FloatType>
struct AlgorithmTraits<FloatType, NoiseAlgorithm::Pink>
{
    using Engine = PinkNoiseEngine<FloatType>;
};

} // namespace detail


/*******************************************************************************
 * NoiseOscillator
 ******************************************************************************/

/**
 * @brief Noise oscillator with pluggable algorithm policy.
 *
 * @details
 * The algorithm is selected at compile time via the @p Algo template
 * parameter. Runtime algorithm switching is not supported; construct a
 * new oscillator of the desired type.
 *
 * Inherits Core::Producer<FloatType, Traversal::PerSample>. prepareBlock()
 * steps the amplitude smoother once per block; renderSample() generates one
 * noise sample scaled by amplitude.
 *
 * renderBlock() is an additional convenience method for Python bindings and
 * raw-buffer callers. It is not an override of a Producer virtual.
 *
 * @tparam FloatType  float or double.
 * @tparam Algo       NoiseAlgorithm::White (default) or NoiseAlgorithm::Pink.
 *
 * @code
 *   // Pink noise oscillator
 *   CASPI::Oscillators::NoiseOscillator<float, CASPI::Oscillators::NoiseAlgorithm::Pink> osc(44100.f);
 *   osc.seed(42);
 *
 *   float buf[4096];
 *   osc.renderBlock(buf, 4096);
 * @endcode
 */
template <typename FloatType, NoiseAlgorithm Algo = NoiseAlgorithm::White>
class NoiseOscillator
    : public Core::Producer<FloatType, Core::Traversal::PerSample>
    , public Core::SampleRateAware<FloatType>
{
    static_assert (std::is_floating_point<FloatType>::value,
                   "NoiseOscillator requires a floating-point type");

    using Engine = typename detail::AlgorithmTraits<FloatType, Algo>::Engine;

public:

    /*************************************************************************
     * Construction
     *************************************************************************/

    /**
     * @brief Default constructor.
     *
     * @details
     * Initialises the amplitude parameter to 1.0. No sample rate is set;
     * noise generation is not sample-rate dependent, but setSampleRate()
     * is available for API consistency.
     */
    NoiseOscillator() noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
    }

    /**
     * @brief Construct with a sample rate.
     *
     * @details
     * Sample rate is stored but does not affect noise generation. Provided
     * for API consistency with other CASPI generators.
     *
     * @param sampleRate  Audio sample rate in Hz.
     */
    explicit NoiseOscillator (FloatType sampleRate) noexcept CASPI_NON_ALLOCATING
    {
        initParameters();
        this->setSampleRate (sampleRate);
    }

    /*************************************************************************
     * Configuration
     *************************************************************************/

    /**
     * @brief Set amplitude in [0, 1], bypassing parameter smoothing.
     *
     * @details
     * Snaps the amplitude smoother immediately. For smoothed real-time
     * changes, write to the amplitude parameter directly.
     *
     * @param amp  Amplitude in [0, 1]. Asserts in debug builds if out of range.
     *
     * @code
     *   osc.setAmplitude(0.5f);
     * @endcode
     */
    void setAmplitude (FloatType amp) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (amp >= FloatType (0) && amp <= FloatType (1),
                      "Amplitude must be in [0, 1]");
        amplitude.setBaseNormalised (amp);
        amplitude.skip (1000);
    }

    /**
     * @brief Re-seed the PRNG.
     *
     * @details
     * For PinkNoiseEngine, the IIR filter state is not reset; call reset()
     * if a completely clean state is required. For reproducible test vectors,
     * seed() followed by renderBlock() will produce identical output.
     *
     * @param s  Seed value. 0 is valid.
     *
     * @code
     *   osc.seed(42);
     *   float buf[512];
     *   osc.renderBlock(buf, 512);  // always the same 512 samples
     * @endcode
     */
    void seed (uint64_t s) noexcept CASPI_NON_BLOCKING { engine.seed (s); }

    /**
     * @brief Reset the PRNG and (for Pink) all IIR filter state.
     *
     * @details
     * Returns the engine to its default initial state. Subsequent output
     * is identical to a freshly constructed oscillator.
     */
    void reset() noexcept CASPI_NON_BLOCKING { engine.reset(); }

    /*************************************************************************
     * Producer overrides
     *************************************************************************/

    /**
     * @brief Step the amplitude smoother once per block.
     *
     * @details
     * Called by Producer::render() before the per-sample loop. Keeps
     * amplitude stepping at block rate rather than sample rate, matching
     * the renderBlock() behaviour.
     *
     * @param nFrames    Number of frames in the upcoming block (unused).
     * @param nChannels  Number of channels (unused).
     */
    void prepareBlock (std::size_t /*nFrames*/,
                       std::size_t /*nChannels*/) override CASPI_NON_BLOCKING
    {
        amplitude.process();
    }

    /**
     * @brief Render one noise sample.
     *
     * @details
     * Called per-sample by Producer::render() via the PerSample traversal
     * policy. amplitude.value() is valid after prepareBlock() has run.
     * When called directly (outside Producer::render()), amplitude.value()
     * reflects the last prepareBlock() call — call renderBlock() or
     * renderSample() consistently, not a mix of both.
     *
     * @return Noise sample scaled by amplitude.value().
     */
    CASPI_NO_DISCARD FloatType renderSample() noexcept override CASPI_NON_BLOCKING
    {
        return engine.next() * amplitude.value();
    }

    /*************************************************************************
     * Raw-buffer path
     *************************************************************************/

    /**
     * @brief Render @p numSamples into a raw output buffer.
     *
     * @details
     * Steps the amplitude smoother once per block (not per sample), then
     * fills the buffer. CASPI_RESTRICT on @p output allows the compiler to
     * emit SIMD stores for the amplitude multiply on the WhiteNoiseEngine
     * path (no loop-carried dependency).
     *
     * @note This method is not an override of a Producer virtual. It is an
     *       additional method for Python bindings and raw-buffer callers.
     *       Do not mix calls to renderBlock() with calls to
     *       Producer::render(AudioBuffer&) within the same block, as both
     *       call amplitude.process() independently.
     *
     * @param output      Pointer to a buffer of at least @p numSamples
     *                    elements. Must not be null.
     * @param numSamples  Number of samples to generate. Must be > 0.
     *
     * @code
     *   float buffer[4096];
     *   osc.renderBlock(buffer, 4096);
     *
     *   // Amplitude modulation
     *   osc.amplitude.addModulation(envOut - 1.f);
     *   osc.renderBlock(buffer, 4096);
     *   osc.amplitude.clearModulation();
     * @endcode
     */
    void renderBlock (FloatType* CASPI_RESTRICT output,
                      int                       numSamples) noexcept CASPI_NON_BLOCKING
    {
        CASPI_ASSERT (output     != nullptr, "Output buffer must not be null");
        CASPI_ASSERT (numSamples >  0,       "numSamples must be positive");

        amplitude.process();
        const FloatType amp = amplitude.value();

        for (int i = 0; i < numSamples; ++i)
        {
            output[i] = engine.next() * amp;
        }
    }

    /*************************************************************************
     * Public modulatable parameter
     *************************************************************************/

    /**
     * @brief Output amplitude in [0, 1]. Linear scale.
     *
     * @details
     * Stepped once per prepareBlock() or renderBlock() call.
     *
     * @code
     *   osc.amplitude.addModulation(envOut - 1.f);
     *   osc.renderBlock(buffer, 512);
     *   osc.amplitude.clearModulation();
     * @endcode
     */
    Core::ModulatableParameter<FloatType> amplitude;

private:

    /**
     * @brief Initialise the amplitude parameter and snap the smoother.
     */
    void initParameters() noexcept CASPI_NON_BLOCKING
    {
        amplitude.setRange (FloatType (0), FloatType (1));
        amplitude.setBaseNormalised (FloatType (1));
        amplitude.skip (1000);
    }

    Engine engine {};
};

/*******************************************************************************
 * Convenience aliases
 ******************************************************************************/

/**
 * @brief Alias for NoiseOscillator<FloatType, NoiseAlgorithm::White>.
 *
 * @tparam FloatType  float (default) or double.
 */
template <typename FloatType = float>
using WhiteNoiseOscillator = NoiseOscillator<FloatType, NoiseAlgorithm::White>;

/**
 * @brief Alias for NoiseOscillator<FloatType, NoiseAlgorithm::Pink>.
 *
 * @tparam FloatType  float (default) or double.
 */
template <typename FloatType = float>
using PinkNoiseOscillator = NoiseOscillator<FloatType, NoiseAlgorithm::Pink>;

} // namespace Oscillators
} // namespace CASPI

#endif // CASPI_NOISE_H