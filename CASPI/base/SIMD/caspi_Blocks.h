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

* @file caspi_Blocks.h
* @author CS Islay
* @brief Block-based SIMD operations with kernel abstraction.
*
*
* OVERVIEW
*
* Provides high-level block operations that process arrays with automatic
* handling of:
*   - Prologue: Scalar processing until alignment boundary
*   - SIMD main loop: Optimized aligned/unaligned paths
*   - Epilogue: Scalar processing for remainder
*
* The kernel abstraction allows each operation to have both SIMD and scalar
* implementations, automatically dispatching to the appropriate code path.
*
*
* USAGE EXAMPLES
*
* @code
* // Basic operations
* float output[1024], input[1024];
* SIMD::ops::add(output, input, 1024);      // output[i] += input[i]
* SIMD::ops::scale(output, 1024, 0.5f);      // output[i] *= 0.5
* SIMD::ops::clamp(output, -1.0f, 1.0f, 1024);
*
* // Reductions
* float peak = SIMD::ops::find_max(audio, 512);
* float sum = SIMD::ops::sum(samples, count);
* float energy = SIMD::ops::dot_product(samples, samples, count);
*
* // Advanced
* SIMD::ops::mac(dst, src1, src2, count);    // dst[i] += src1[i] * src2[i]
* SIMD::ops::lerp(dst, a, b, 0.5f, count);  // dst[i] = a[i] + t * (b[i] - a[i])
* @endcode
*
*
* KERNEL SYSTEM
*
* Each kernel provides:
*   - SIMD operator() for vector processing
*   - Scalar operator() for scalar processing
*
* Available kernels in CASPI::SIMD::kernels:
*   - AddKernel      : dst[i] = dst[i] + src[i]
*   - SubKernel      : dst[i] = dst[i] - src[i]
*   - MulKernel      : dst[i] = dst[i] * src[i]
*   - ScaleKernel    : data[i] = data[i] * factor
*   - FillKernel     : data[i] = value
*   - MACKernel      : dst[i] += src1[i] * src2[i]
*   - LerpKernel     : dst[i] = a[i] + t * (b[i] - a[i])
*   - ClampKernel    : data[i] = clamp(data[i], min, max)
*   - AbsKernel      : data[i] = |data[i]|
*
*
* PERFORMANCE NOTES
*
* - Block operations automatically handle alignment
* - 4x loop unrolling for aligned paths
* - Non-temporal stores for large fills (write-only operations)
* - No NT stores for RMW operations (add, mul, etc.)
* - Binary operations are latency-bound, not bandwidth-bound
*
 ************************************************************************/

#ifndef CASPI_BLOCKS_H
#define CASPI_BLOCKS_H

#include <array>
#include <cmath>

#include "caspi_LoadStore.h"
#include "caspi_Operations.h"

namespace CASPI
{
    namespace SIMD
    {
        /**
         * @brief Kernel namespace for operation encapsulation.
         *
         * Contains kernels that provide both SIMD and scalar implementations
         * of common operations. Used by block processors for automatic dispatch.
         */
        namespace kernels
        {
            /**
             * @brief Addition kernel: result = a + b
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct AddKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return add (a, b); }
                    T operator() (T a, T b) const { return a + b; }
            };

            /**
             * @brief Subtraction kernel: result = a - b
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct SubKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return sub (a, b); }
                    T operator() (T a, T b) const { return a - b; }
            };

            /**
             * @brief Multiplication kernel: result = a * b
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct MulKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a, simd_type b) const { return mul (a, b); }
                    T operator() (T a, T b) const { return a * b; }
            };

            /**
             * @brief Scaling kernel: result = a * factor
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct ScaleKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type vec;
                    T scalar;
                    explicit ScaleKernel (T factor) : vec (set1<T> (factor)), scalar (factor) {}
                    simd_type operator() (simd_type a) const { return mul (a, vec); }
                    T operator() (T a) const { return a * scalar; }
            };

            /**
             * @brief Fill kernel: returns constant value
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct FillKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type vec;
                    T scalar;
                    explicit FillKernel (T value) : vec (set1<T> (value)), scalar (value) {}
                    simd_type simd_value() const { return vec; }
                    T scalar_value() const { return scalar; }
            };

            /**
             * @brief Multiply-accumulate kernel: result += a * b
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct MACKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type acc, simd_type a, simd_type b) const { return mul_add (a, b, acc); }
                    T operator() (T acc, T a, T b) const { return acc + a * b; }
            };

            /**
             * @brief Linear interpolation kernel: result = a + t * (b - a)
             *
             * @tparam T         Floating-point type
             * @param t         Interpolation factor; t=0 returns a, t=1 returns b
             */
            template <typename T>
            struct LerpKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type t_vec;
                    T t_scalar;
                    explicit LerpKernel (T t) : t_vec (set1<T> (t)), t_scalar (t) {}
                    simd_type operator() (simd_type a, simd_type b) const { return mul_add (sub (b, a), t_vec, a); }
                    T operator() (T a, T b) const { return a + t_scalar * (b - a); }
            };

            /**
             * @brief Clamping kernel: result = clamp(x, min, max)
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct ClampKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type min_vec, max_vec;
                    T min_scalar, max_scalar;
                    ClampKernel (T lo, T hi) : min_vec (set1<T> (lo)), max_vec (set1<T> (hi)), min_scalar (lo), max_scalar (hi) {}
                    simd_type operator() (simd_type v) const { return min (max (v, min_vec), max_vec); }
                    T operator() (T v) const
                    {
                        if (v < min_scalar)
                            v = min_scalar;
                        if (v > max_scalar)
                            v = max_scalar;
                        return v;
                    }
            };

            /**
             * @brief Absolute value kernel: result = |a|
             *
             * @tparam T         Floating-point type
             */
            template <typename T>
            struct AbsKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value, "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                    simd_type operator() (simd_type a) const { return abs (a); }
                    T operator() (T a) const { return std::fabs (a); }
            };

            /**
             * @brief Copy-with-gain kernel: dst[i] = src[i] * gain
             *
             * @tparam T  Floating-point type.
             */
            template <typename T>
            struct CopyGainKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value,
                                         "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type gain_vec;
                    T gain_scalar;

                    explicit CopyGainKernel (T g) : gain_vec (set1<T> (g)), gain_scalar (g) {}

                    simd_type operator() (simd_type src) const { return mul (src, gain_vec); }
                    T operator() (T src) const { return src * gain_scalar; }
            };

            /**
             * @brief Accumulate-with-gain kernel: dst[i] = dst[i] + src[i] * gain
             *
             * Uses mul_add when FMA is available.
             *
             * @tparam T  Floating-point type.
             */
            template <typename T>
            struct AccumGainKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value,
                                         "SIMD kernels only support floating-point types");
                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    simd_type gain_vec;
                    T gain_scalar;

                    explicit AccumGainKernel (T g) : gain_vec (set1<T> (g)), gain_scalar (g) {}

                    // Binary: (dst, src) → dst + src*gain
                    simd_type operator() (simd_type dst, simd_type src) const
                    {
                        return mul_add (src, gain_vec, dst);
                    }
                    T operator() (T dst, T src) const { return dst + src * gain_scalar; }
            };

            /**
             * @brief Compile-time Horner polynomial kernel.
             *
             * @tparam T  Scalar type (float or double).
             * @tparam N  Polynomial degree (number of terms = N+1).
             */
            template <typename T, std::size_t N>
            struct PolyKernel
            {
                    CASPI_STATIC_ASSERT (std::is_floating_point<T>::value,
                                         "PolyKernel only supports floating-point types");
                    CASPI_STATIC_ASSERT (N >= 1,
                                         "PolyKernel degree must be at least 1");

                    using simd_type = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;

                    // N+1 coefficients, index 0 = constant, index N = leading term
                    std::array<T, N + 1> coeffs;

                    /**
                     * @brief Construct from a brace-initialiser list.
                     *
                     * @param init  Coefficients lowest-to-highest degree.
                     *              Must contain exactly N+1 values.
                     *
                     * @code
                     * PolyKernel<float, 2> p({1.0f, 3.0f, 2.0f}); // 2x² + 3x + 1
                     * @endcode
                     */
                    explicit PolyKernel (std::initializer_list<T> init)
                    {
                        if (init.size() != N + 1)
                        {
                            // In a header-only lib we cannot throw without pulling in
                            // <stdexcept>. Use assert in debug, and silently zero-fill
                            // in release to avoid undefined behaviour.
#if defined(CASPI_DEBUG)
                            // CASPI_ASSERT would be ideal here but we avoid the
                            // circular include; a plain assert is sufficient.
                            for (std::size_t i = 0; i < N + 1; ++i)
                                coeffs[i] = T (0);
#endif
                        }
                        std::size_t idx = 0;
                        for (T v : init)
                        {
                            if (idx <= N)
                                coeffs[idx++] = v;
                        }
                        // Zero-fill any missing trailing coefficients
                        for (; idx <= N; ++idx)
                            coeffs[idx] = T (0);
                    }

                    /**
                     * @brief Construct from a std::array.
                     */
                    explicit PolyKernel (const std::array<T, N + 1>& c) : coeffs (c) {}

                    // -----------------------------------------------------------------------
                    // SIMD evaluation — Horner's method unrolled at compile time
                    // -----------------------------------------------------------------------

                    /**
                     * @brief Evaluate polynomial at each lane of a SIMD vector.
                     *
                     * Evaluates p(x) = c[N] * x^N + ... + c[1] * x + c[0] via Horner.
                     * Produces N mul_add operations, each of which uses hardware FMA
                     * when available.
                     *
                     * @param x  Input SIMD vector.
                     * @return   p(x) evaluated per lane.
                     */
                    simd_type operator() (simd_type x) const noexcept
                    {
                        // Start from the highest-degree coefficient and work down.
                        // acc = c[N]
                        // acc = acc * x + c[N-1]
                        // ...
                        // acc = acc * x + c[0]
                        auto acc = set1<T> (coeffs[N]);
                        for (std::size_t k = N; k-- > 0;)
                            acc = mul_add (acc, x, set1<T> (coeffs[k]));
                        return acc;
                    }

                    // -----------------------------------------------------------------------
                    // Scalar evaluation — identical algorithm, plain arithmetic
                    // -----------------------------------------------------------------------

                    /**
                     * @brief Evaluate polynomial at a scalar value.
                     *
                     * @param x  Input scalar.
                     * @return   p(x).
                     */
                    T operator() (T x) const noexcept
                    {
                        T acc = coeffs[N];
                        for (std::size_t k = N; k-- > 0;)
                            acc = acc * x + coeffs[k];
                        return acc;
                    }
            };
        } // namespace kernels

        // ----------------------------------------------------------------------------
        // Coefficient tables — all double precision.
        //
        // Source annotations:
        //   [C]  Cephes (S. L. Moshier) https://netlib.org/cephes
        //   [S]  SLEEF                   https://sleef.org
        //   [B]  Boost.Math
        // ----------------------------------------------------------------------------

        namespace coeffs
        {

            /// sin(x) ≈ x * P(x²) on [-π/2, π/2], P of degree 5.  Source: [C] sin.c
            inline constexpr std::array<double, 6> sin_d5 = { { 1.0,
                                                                -1.6666666666666665052e-1,
                                                                8.3333333333331650314e-3,
                                                                -1.9841269841201840457e-4,
                                                                2.7557319223985880784e-6,
                                                                -2.5052106798274584544e-8 } };

            /// cos(x) ≈ P(x²) on [-π/2, π/2], P of degree 5.  Source: [C] cos.c
            inline constexpr std::array<double, 6> cos_d5 = { { 1.0,
                                                                -4.9999999999999999759e-1,
                                                                4.1666666666666664811e-2,
                                                                -1.3888888888888872993e-3,
                                                                2.4801587301585605359e-5,
                                                                -2.7557319223472284322e-7 } };

            /// tanh(x) ≈ x * P(x²) on [-2, 2], P of degree 3 (float precision).
            /// Source: Minimax Remez, cross-checked [B].
            inline constexpr std::array<double, 4> tanh_d3 = { { 1.0,
                                                                 -3.3333333333331114e-1,
                                                                 1.3333333333207403e-1,
                                                                 -5.3968253968002491e-2 } };

            /// tanh(x) ≈ x * P(x²) on [-2, 2], P of degree 7 (double precision).
            /// Max error ~3e-13.  Source: [S] tanhd2.c
            inline constexpr std::array<double, 8> tanh_d7 = { { 1.0,
                                                                 -3.3333333333333337034e-1,
                                                                 1.3333333333333252036e-1,
                                                                 -5.3968253968245345830e-2,
                                                                 2.1869488536155748960e-2,
                                                                 -8.8632369985788490983e-3,
                                                                 3.5921924242902374958e-3,
                                                                 -1.4558343870769948509e-3 } };

            /// log mantissa: log(x) ≈ 2*m*P(m²) + e*ln(2), m=(x-1)/(x+1), [1/√2,√2].
            /// P of degree 5.  Source: [C] log.c
            inline constexpr std::array<double, 6> log_mantissa_d5 = { { 2.0,
                                                                         6.6666666666666707354e-1,
                                                                         4.0000000000000300830e-1,
                                                                         2.8571428571412978085e-1,
                                                                         2.2222222222236930526e-1,
                                                                         1.5384615384548996803e-1 } };

            /// 2^x ≈ P(x) on [0, 1].
            /// float uses degree 5 (max err ~1.7e-4), double uses degree 11 (max err ~2.7e-11).
            /// These are Taylor series (exact coefficients = ln(2)^k / k!).
            /// Source: computed directly from the Taylor definition.
            // degree-5 subset (float)
            inline constexpr std::array<double, 6> exp2_frac_d5 = { { 1.0,
                                                                      6.9314718055994529e-01,
                                                                      2.4022650695910069e-01,
                                                                      5.5504108664821576e-02,
                                                                      9.6181291076284769e-03,
                                                                      1.3333558146428441e-03 } };

            // degree-11 for double precision on [0,1] (~2.7e-11 error)
            inline constexpr std::array<double, 12> exp2_frac_d11 = { { 1.0,
                                                                        6.9314718055994529e-01,
                                                                        2.4022650695910069e-01,
                                                                        5.5504108664821576e-02,
                                                                        9.6181291076284769e-03,
                                                                        1.3333558146428441e-03,
                                                                        1.5403530393381606e-04,
                                                                        1.5252733804059838e-05,
                                                                        1.3215486790144305e-06,
                                                                        1.0178086009239696e-07,
                                                                        7.0549116208011209e-09,
                                                                        4.4455382718708101e-10 } };

        } // namespace coeffs

        // ----------------------------------------------------------------------------
        // Tag dispatch helpers for tanh degree selection
        // ----------------------------------------------------------------------------

        namespace detail
        {
            // float → degree 3, double → degree 7
            template <typename T>
            struct TanhDegree
            {
                    static constexpr std::size_t value = 3;
            };
            template <>
            struct TanhDegree<double>
            {
                    static constexpr std::size_t value = 7;
            };
        } // namespace detail

        // ----------------------------------------------------------------------------
        // Generic factory functions
        // ----------------------------------------------------------------------------

        /**
         * @brief sin kernel: caller computes sin(x) ≈ x * poly(x²).
         * Degree 5 for both float and double (coefficients differ).
         * Domain: [-π/2, π/2] after range reduction.
         */
        template <typename T>
        kernels::PolyKernel<T, 5> sin_poly()
        {
            std::array<T, 6> c;
            for (std::size_t i = 0; i < 6; ++i)
                c[i] = static_cast<T> (coeffs::sin_d5[i]);
            return kernels::PolyKernel<T, 5> (c);
        }

        /**
         * @brief cos kernel: caller computes cos(x) ≈ poly(x²).
         * Degree 5.  Domain: [-π/2, π/2].
         */
        template <typename T>
        kernels::PolyKernel<T, 5> cos_poly()
        {
            std::array<T, 6> c;
            for (std::size_t i = 0; i < 6; ++i)
                c[i] = static_cast<T> (coeffs::cos_d5[i]);
            return kernels::PolyKernel<T, 5> (c);
        }

        /**
         * @brief tanh kernel: caller computes tanh(x) ≈ x * poly(x²).
         * Degree 3 for float (~4e-4), degree 7 for double (~3e-13).
         * Domain: [-2, 2].
         */
        template <typename T>
        kernels::PolyKernel<T, detail::TanhDegree<T>::value> tanh_poly()
        {
            constexpr std::size_t Deg = detail::TanhDegree<T>::value;
            std::array<T, Deg + 1> c;

            if constexpr (Deg == 3)
            {
                for (std::size_t i = 0; i <= 3; ++i)
                    c[i] = static_cast<T> (coeffs::tanh_d3[i]);
            }
            else
            {
                for (std::size_t i = 0; i <= 7; ++i)
                    c[i] = static_cast<T> (coeffs::tanh_d7[i]);
            }
            return kernels::PolyKernel<T, Deg> (c);
        }

        /**
         * @brief log mantissa kernel: caller computes log(x) ≈ 2*m*poly(m²) + e*ln(2).
         * Degree 5.  Domain: [1/√2, √2] after range reduction.
         */
        template <typename T>
        kernels::PolyKernel<T, 5> log_mantissa_poly()
        {
            std::array<T, 6> c;
            for (std::size_t i = 0; i < 6; ++i)
                c[i] = static_cast<T> (coeffs::log_mantissa_d5[i]);
            return kernels::PolyKernel<T, 5> (c);
        }

        // exp2 degree selector: float→5, double→11
        template <typename T>
        struct Exp2Degree
        {
                static constexpr std::size_t value = 5;
        };
        template <>
        struct Exp2Degree<double>
        {
                static constexpr std::size_t value = 11;
        };

        /**
         * @brief exp2 fractional kernel: evaluates 2^x for x ∈ [0, 1].
         *
         * float:  degree 5,  max error ~1.7e-4 on [0, 1].
         * double: degree 11, max error ~2.7e-11 on [0, 1].
         *
         * These are exact Taylor series (coefficients = ln(2)^k / k!).
         * Caller applies the integer part via std::ldexp or bit manipulation.
         */
        template <typename T>
        kernels::PolyKernel<T, Exp2Degree<T>::value> exp2_frac_poly()
        {
            constexpr std::size_t Deg = Exp2Degree<T>::value;
            std::array<T, Deg + 1> c;
            if constexpr (Deg == 5)
            {
                for (std::size_t i = 0; i <= 5; ++i)
                    c[i] = static_cast<T> (coeffs::exp2_frac_d5[i]);
            }
            else
            {
                for (std::size_t i = 0; i <= 11; ++i)
                    c[i] = static_cast<T> (coeffs::exp2_frac_d11[i]);
            }
            return kernels::PolyKernel<T, Deg> (c);
        }

        /**
         * @brief Binary in-place block operation: dst[i] = kernel(dst[i], src[i])
         *
         * Generic prologue/SIMD/epilogue driver for two-operand in-place ops.
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Kernel type providing:
         *                   - simd_type operator()(simd_type a, simd_type b) const
         *                   - T operator()(T a, T b) const
         * @param dst        Destination array (modified in-place)
         * @param src        Source array (read-only)
         * @param count      Number of elements to process
         * @param kernel     Kernel instance encapsulating SIMD and scalar logic
         *
         * @details
         * The function:
         *  1) Processes a scalar prologue up to the SIMD alignment boundary.
         *  2) Executes a SIMD main loop using aligned or unaligned loads/stores
         *     with 4x unrolling where possible.
         *  3) Finishes with a scalar epilogue for the remaining elements.
         *
         * @note If either pointer is null or count == 0 the function returns
         *       without side effects.
         *
         * @example
         * @code
         * SIMD::block_op_binary<float>(dst, src, n, SIMD::kernels::AddKernel<float>());
         * // equivalent to: for (i) dst[i] = dst[i] + src[i];
         * @endcode
         */
        template <typename T, typename Kernel>
        void block_op_binary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (src == nullptr) || (count == 0))
            {
                return;
            }

            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            if (count < Width)
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[i] = kernel (dst[i], src[i]);
                }
                return;
            }

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
                dst[i] = kernel (dst[i], src[i]);

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned     = Strategy::is_aligned<Alignment> (src + i);

            if (dst_aligned && src_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (dst + i + Width), load_aligned<T> (src + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (dst + i + Width * 2), load_aligned<T> (src + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (dst + i + Width * 3), load_aligned<T> (src + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src + i)));
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (dst + i), load_unaligned<T> (src + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src[i]);
            }
        }

        /**
         * @brief Unary block operation: dst[i] = kernel(src[i])
         *
         * Driver for producing an output buffer from a single input buffer.
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Kernel type providing:
         *                   - simd_type operator()(simd_type a) const
         *                   - T operator()(T a) const
         * @param dst        Destination array (write-only)
         * @param src        Source array (read-only)
         * @param count      Number of elements to process
         * @param kernel     Kernel instance encapsulating SIMD and scalar logic
         *
         * @note dst and src may alias if the kernel supports in-place operation;
         *       otherwise they must be distinct.
         *
         * @example
         * @code
         * SIMD::block_op_unary<float>(dst, src, n, SIMD::kernels::CopyGainKernel<float>(gain));
         * // Produces dst[i] = src[i] * gain;
         * @endcode
         */
        template <typename T, typename Kernel>
        void block_op_unary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (src == nullptr) || (count == 0))
            {
                return;
            }
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (src[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src_aligned     = Strategy::is_aligned<Alignment> (src + i);

            if (dst_aligned && src_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (src + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (src + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (src + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (src + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (src + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (src + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (src[i]);
            }
        }

        /**
         * @brief In-place unary block operation: data[i] = kernel(data[i])
         *
         * Applies a unary kernel in-place with the same prologue/SIMD/epilogue flow.
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Kernel type providing:
         *                   - simd_type operator()(simd_type a) const
         *                   - T operator()(T a) const
         * @param data       Data array to be modified in-place
         * @param count      Number of elements to process
         * @param kernel     Kernel instance
         *
         * @note Returns immediately if data == nullptr or count == 0.
         *
         * @example
         * @code
         * SIMD::block_op_inplace<float>(samples, n, SIMD::kernels::ScaleKernel<float>(0.5f));
         * // scales samples in-place by 0.5
         * @endcode
         */
        template <typename T, typename Kernel>
        void block_op_inplace (T* CASPI_RESTRICT data, std::size_t count, const Kernel& kernel)
        {
            if ((data == nullptr) || (count == 0))
                return;

            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
            for (; i < prologue_count; ++i)
            {
                data[i] = kernel (data[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

            if (aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (data + i, kernel (load_aligned<T> (data + i)));
                    store_aligned (data + i + Width, kernel (load_aligned<T> (data + i + Width)));
                    store_aligned (data + i + Width * 2, kernel (load_aligned<T> (data + i + Width * 2)));
                    store_aligned (data + i + Width * 3, kernel (load_aligned<T> (data + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (data + i, kernel (load_aligned<T> (data + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (data + i, kernel (load_unaligned<T> (data + i)));
                }
            }

            for (; i < count; ++i)
            {
                data[i] = kernel (data[i]);
            }
        }

        /**
         * @brief Fill block operation: dst[i] = kernel.scalar_value()
         *
         * Optimised fill routine that can use non-temporal (NT) stores for large,
         * aligned buffers to avoid polluting caches. Handles prologue and epilogue.
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Fill kernel type providing:
         *                   - simd_type simd_value() const
         *                   - T scalar_value() const
         * @param dst        Destination array to fill
         * @param count      Number of elements to write
         * @param kernel     Kernel instance containing the fill value
         *
         * @note If the buffer is aligned and sufficiently large (Strategy::nt_store_threshold),
         *       NT stores (stream_store) are used with a final store_fence() call.
         * @note If dst == nullptr or count == 0 nothing happens.
         *
         * @example
         * @code
         * SIMD::block_op_fill<float>(buffer, n, SIMD::kernels::FillKernel<float>(0.0f));
         * @endcode
         */
        template <typename T, typename Kernel>
        inline void block_op_fill (T* CASPI_RESTRICT dst, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (count == 0))
            {
                return;
            }
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }

            const std::size_t simd_end     = i + ((count - i) / Width) * Width;
            const bool aligned             = Strategy::is_aligned<Alignment> (dst + i);
            const auto vec                 = kernel.simd_value();
            const std::size_t nt_threshold = Strategy::nt_store_threshold<T>();

            if (aligned)
            {
#if defined(CASPI_HAS_SSE)
                if ((simd_end - i) >= nt_threshold)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        stream_store (dst + i, vec);
                        stream_store (dst + i + Width, vec);
                        stream_store (dst + i + Width * 2, vec);
                        stream_store (dst + i + Width * 3, vec);
                    }
                    for (; i < simd_end; i += Width)
                    {
                        stream_store (dst + i, vec);
                    }

                    store_fence();
                }
                else
#endif
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (dst + i, vec);
                        store_aligned (dst + i + Width, vec);
                        store_aligned (dst + i + Width * 2, vec);
                        store_aligned (dst + i + Width * 3, vec);
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (dst + i, vec);
                    }
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, vec);
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel.scalar_value();
            }
        }

        /**
         * @brief Ternary block operation: dst[i] = kernel(dst[i], src1[i], src2[i])
         *
         * Driver for operations requiring three inputs where the first is also the
         * destination (e.g., multiply-accumulate).
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Kernel type providing:
         *                   - simd_type operator()(simd_type acc, simd_type a, simd_type b) const
         *                   - T operator()(T acc, T a, T b) const
         * @param dst        Destination array (also used as first operand)
         * @param src1       First source array
         * @param src2       Second source array
         * @param count      Number of elements to process
         * @param kernel     Kernel instance
         *
         * @note All alignment logic mirrors the binary/inplace drivers.
         *
         * @example
         * @code
         * SIMD::block_op_ternary<float>(dst, a, b, n, SIMD::kernels::MACKernel<float>());
         * // equivalent to: for i: dst[i] += a[i] * b[i];
         * @endcode
         */
        template <typename T, typename Kernel>
        inline void block_op_ternary (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count, const Kernel& kernel)
        {
            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool src1_aligned    = Strategy::is_aligned<Alignment> (src1 + i);
            const bool src2_aligned    = Strategy::is_aligned<Alignment> (src2 + i);

            if (dst_aligned && src1_aligned && src2_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src1 + i), load_aligned<T> (src2 + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (dst + i + Width), load_aligned<T> (src1 + i + Width), load_aligned<T> (src2 + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (dst + i + Width * 2), load_aligned<T> (src1 + i + Width * 2), load_aligned<T> (src2 + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (dst + i + Width * 3), load_aligned<T> (src1 + i + Width * 3), load_aligned<T> (src2 + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (dst + i), load_aligned<T> (src1 + i), load_aligned<T> (src2 + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (dst + i), load_unaligned<T> (src1 + i), load_unaligned<T> (src2 + i)));
                }
            }

            for (; i < count; ++i)
            {
                dst[i] = kernel (dst[i], src1[i], src2[i]);
            }
        }

        /**
         * @brief Binary output block operation: dst[i] = kernel(a[i], b[i])
         *
         * Driver for operations producing an output buffer from two input buffers.
         *
         * @tparam T         Element type (floating point)
         * @tparam Kernel    Kernel type providing:
         *                   - simd_type operator()(simd_type a, simd_type b) const
         *                   - T operator()(T a, T b) const
         * @param dst        Destination array (write-only)
         * @param a          First source array
         * @param b          Second source array
         * @param count      Number of elements to process
         * @param kernel     Kernel instance
         *
         * @note The function is null-safe: returns immediately when any input is null
         *       or count == 0.
         *
         * @example
         * @code
         * SIMD::block_op_binary_out<float>(dst, a, b, n, SIMD::kernels::AddKernel<float>());
         * // produces dst[i] = a[i] + b[i];
         * @endcode
         */
        template <typename T, typename Kernel>
        inline void block_op_binary_out (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, std::size_t count, const Kernel& kernel)
        {
            if ((dst == nullptr) || (a == nullptr) || (b == nullptr) || (count == 0))
            {
                return;
            }

            constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
            constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
            std::size_t i                   = 0;

            const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (dst), count);
            for (; i < prologue_count; ++i)
            {
                dst[i] = kernel (a[i], b[i]);
            }

            const std::size_t simd_end = i + ((count - i) / Width) * Width;
            const bool dst_aligned     = Strategy::is_aligned<Alignment> (dst + i);
            const bool a_aligned       = Strategy::is_aligned<Alignment> (a + i);
            const bool b_aligned       = Strategy::is_aligned<Alignment> (b + i);

            if (dst_aligned && a_aligned && b_aligned)
            {
                const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                for (; i < unroll_end; i += Width * 4)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (a + i), load_aligned<T> (b + i)));
                    store_aligned (dst + i + Width, kernel (load_aligned<T> (a + i + Width), load_aligned<T> (b + i + Width)));
                    store_aligned (dst + i + Width * 2, kernel (load_aligned<T> (a + i + Width * 2), load_aligned<T> (b + i + Width * 2)));
                    store_aligned (dst + i + Width * 3, kernel (load_aligned<T> (a + i + Width * 3), load_aligned<T> (b + i + Width * 3)));
                }
                for (; i < simd_end; i += Width)
                {
                    store_aligned (dst + i, kernel (load_aligned<T> (a + i), load_aligned<T> (b + i)));
                }
            }
            else
            {
                for (; i < simd_end; i += Width)
                {
                    store_unaligned (dst + i, kernel (load_unaligned<T> (a + i), load_unaligned<T> (b + i)));
                }
            }

            for (; i < count; ++i)
                dst[i] = kernel (a[i], b[i]);
        }

        /**
         * @brief High-level block operations namespace.
         *
         * Provides convenient functions for common array operations.
         */
        namespace ops
        {
            /**
             * @brief In-place addition: dst[i] += src[i]
             *
             * @tparam T         Element type
             * @param dst        Destination array
             * @param src        Source array to add
             * @param count      Number of elements
             */
            template <typename T>
            void add (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::AddKernel<T>());
            }

            /**
             * @brief In-place subtraction: dst[i] -= src[i]
             *
             * @tparam T         Element type
             * @param dst        Destination array
             * @param src        Source array to subtract
             * @param count      Number of elements
             */
            template <typename T>
            void sub (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::SubKernel<T>());
            }

            /**
             * @brief In-place multiplication: dst[i] *= src[i]
             *
             * @tparam T         Element type
             * @param dst        Destination array
             * @param src        Source array to multiply
             * @param count      Number of elements
             */
            template <typename T>
            void mul (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                block_op_binary (dst, src, count, kernels::MulKernel<T>());
            }

            /**
             * @brief In-place scaling: data[i] *= factor
             *
             * @tparam T         Element type
             * @param data       Data array to scale
             * @param count      Number of elements
             * @param factor     Scaling factor
             */
            template <typename T>
            void scale (T* CASPI_RESTRICT data, std::size_t count, T factor)
            {
                block_op_inplace (data, count, kernels::ScaleKernel<T> (factor));
            }

            /**
             * @brief Copy array: dst[i] = src[i]
             *
             * @tparam T         Element type
             * @param dst        Destination array
             * @param src        Source array
             * @param count      Number of elements
             */
            template <typename T>
            void copy (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src, std::size_t count)
            {
                std::memcpy (dst, src, count * sizeof (T));
            }

            /**
             * @brief Fill array: dst[i] = value
             *
             * Dispatches between std::fill_n (small buffer, stays in L1) and
             * block_op_fill with NT stores (large buffer, avoids cache pollution).
             *
             * The threshold is the runtime L1 size, queried once on first call. On first call, this is blocking.
             *
             * @tparam T     Element type.
             * @param dst    Destination array.
             * @param count  Number of elements.
             * @param value  Fill value.
             */
            template <typename T>
            void fill (T* CASPI_RESTRICT dst, std::size_t count, T value)
            {
                if (dst == nullptr || count == 0)
                    return;

                // NT stores are beneficial when working set > L1.
                // Use runtime threshold so the decision adapts to the actual CPU.
                const std::size_t threshold = Strategy::nt_store_threshold_runtime<T>();

                if (count <= threshold)
                {
                    // Small buffer: stays in L1, temporal stores are fine.
                    // std::fill_n compiles to a single rep stosd / DC ZVA on most targets.
                    std::fill_n (dst, count, value);
                }
                else
                {
                    // Large buffer: bypass cache with NT stores.
                    block_op_fill (dst, count, kernels::FillKernel<T> (value));
                }
            }

            /**
             * @brief Multiply-accumulate: dst[i] += src1[i] * src2[i]
             *
             * Uses FMA when available for better performance.
             *
             * @tparam T         Element type
             * @param dst        Destination array (accumulator)
             * @param src1       First source array
             * @param src2       Second source array
             * @param count      Number of elements
             */
            template <typename T>
            void mac (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT src1, const T* CASPI_RESTRICT src2, std::size_t count)
            {
                block_op_ternary (dst, src1, src2, count, kernels::MACKernel<T>());
            }

            /**
             * @brief Linear interpolation: dst[i] = a[i] + t * (b[i] - a[i])
             *
             * @tparam T         Element type
             * @param dst        Destination array
             * @param a          First input array
             * @param b          Second input array
             * @param t          Interpolation factor (0 to 1)
             * @param count      Number of elements
             */
            template <typename T>
            void lerp (T* CASPI_RESTRICT dst, const T* CASPI_RESTRICT a, const T* CASPI_RESTRICT b, T t, std::size_t count)
            {
                block_op_binary_out (dst, a, b, count, kernels::LerpKernel<T> (t));
            }

            /**
             * @brief In-place clamping: data[i] = clamp(data[i], min_val, max_val)
             *
             * @tparam T         Element type
             * @param data       Data array to clamp
             * @param min_val    Minimum value
             * @param max_val    Maximum value
             * @param count      Number of elements
             */
            template <typename T>
            void clamp (T* CASPI_RESTRICT data, T min_val, T max_val, std::size_t count)
            {
                block_op_inplace (data, count, kernels::ClampKernel<T> (min_val, max_val));
            }

            /**
             * @brief In-place absolute value: data[i] = |data[i]| (float specialization)
             *
             * @param data       Data array
             * @param count      Number of elements
             */
            inline void abs (float* CASPI_RESTRICT data, std::size_t count)
            {
                if (data == nullptr || count == 0)
                    return;

                constexpr std::size_t Width     = Strategy::min_simd_width<float>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<float>();
                std::size_t i                   = 0;

                const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
                for (; i < prologue_count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }

                const std::size_t simd_end = i + ((count - i) / Width) * Width;
                const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

                if (aligned)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<float> (data + i)));
                        store_aligned (data + i + Width, SIMD::abs (load_aligned<float> (data + i + Width)));
                        store_aligned (data + i + Width * 2, SIMD::abs (load_aligned<float> (data + i + Width * 2)));
                        store_aligned (data + i + Width * 3, SIMD::abs (load_aligned<float> (data + i + Width * 3)));
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<float> (data + i)));
                    }
                }
                else
                {
                    for (; i < simd_end; i += Width)
                    {
                        store_unaligned (data + i, SIMD::abs (load_unaligned<float> (data + i)));
                    }
                }

                for (; i < count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }
            }

            /**
             * @brief In-place absolute value: data[i] = |data[i]| (double specialization)
             *
             * @param data       Data array
             * @param count      Number of elements
             */
            inline void abs (double* CASPI_RESTRICT data, std::size_t count)
            {
                if (data == nullptr || count == 0)
                    return;

                constexpr std::size_t Width     = Strategy::min_simd_width<double>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<double>();
                std::size_t i                   = 0;

                const std::size_t prologue_count = std::min (Strategy::samples_to_alignment<Alignment> (data), count);
                for (; i < prologue_count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }

                const std::size_t simd_end = i + ((count - i) / Width) * Width;
                const bool aligned         = Strategy::is_aligned<Alignment> (data + i);

                if (aligned)
                {
                    const std::size_t unroll_end = i + ((simd_end - i) / (Width * 4)) * (Width * 4);
                    for (; i < unroll_end; i += Width * 4)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<double> (data + i)));
                        store_aligned (data + i + Width, SIMD::abs (load_aligned<double> (data + i + Width)));
                        store_aligned (data + i + Width * 2, SIMD::abs (load_aligned<double> (data + i + Width * 2)));
                        store_aligned (data + i + Width * 3, SIMD::abs (load_aligned<double> (data + i + Width * 3)));
                    }
                    for (; i < simd_end; i += Width)
                    {
                        store_aligned (data + i, SIMD::abs (load_aligned<double> (data + i)));
                    }
                }
                else
                {
                    for (; i < simd_end; i += Width)
                    {
                        store_unaligned (data + i, SIMD::abs (load_unaligned<double> (data + i)));
                    }
                }

                for (; i < count; ++i)
                {
                    data[i] = std::fabs (data[i]);
                }
            }

            /**
             * @brief In-place absolute value (generic template)
             *
             * @tparam T         Element type
             * @param data       Data array
             * @param count      Number of elements
             */
            template <typename T>
            void abs (T* CASPI_RESTRICT data, std::size_t count)
            {
                block_op_inplace (data, count, kernels::AbsKernel<T>());
            }

            /**
             * @brief Find minimum element in array.
             *
             * @tparam T         Element type
             * @param data       Input array
             * @param count      Number of elements
             * @return          Minimum element value, or T(0) if empty
             */
            template <typename T>
            T find_min (const T* data, std::size_t count)
            {
                if (data == nullptr || (count == 0))
                {
                    return T (0);
                }

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    if (data[i] < result)
                    {
                        result = data[i];
                    }
                }

                if (i + Width <= count)
                {
                    simd_t vmin = set1<T> (result);
                    for (; i + Width <= count; i += Width)
                    {
                        vmin = min (vmin, load<T> (data + i));
                    }
                    result = hmin (vmin);
                }

                for (; i < count; ++i)
                {
                    if (data[i] < result)
                    {
                        result = data[i];
                    }
                }

                return result;
            }

            /**
             * @brief Find maximum element in array.
             *
             * @tparam T         Element type
             * @param data       Input array
             * @param count      Number of elements
             * @return          Maximum element value, or T(0) if empty
             */
            template <typename T>
            T find_max (const T* data, std::size_t count)
            {
                if (data == nullptr || (count == 0))
                {
                    return T (0);
                }

                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = data[0];
                std::size_t i = 1;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    if (data[i] > result)
                    {
                        result = data[i];
                    }
                }

                if (i + Width <= count)
                {
                    simd_t vmax = set1<T> (result);
                    for (; i + Width <= count; i += Width)
                    {
                        vmax = max (vmax, load<T> (data + i));
                    }
                    result = hmax (vmax);
                }

                for (; i < count; ++i)
                {
                    if (data[i] > result)
                    {
                        result = data[i];
                    }
                }

                return result;
            }

            /**
             * @brief Sum all elements in array.
             *
             * @tparam T         Element type
             * @param data       Input array
             * @param count      Number of elements
             * @return          Sum of all elements
             */
            template <typename T>
            T sum (const T* data, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (data),
                    count);

                for (; i < prologue_end; ++i)
                {
                    result += data[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));
                    for (; i + Width <= count; i += Width)
                        vsum = SIMD::add (vsum, load<T> (data + i));
                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                {
                    result += data[i];
                }

                return result;
            }

            /**
             * @brief Compute dot product: sum(a[i] * b[i])
             *
             * Uses FMA when available for better accuracy and performance.
             *
             * @tparam T         Element type
             * @param a          First input array
             * @param b          Second input array
             * @param count      Number of elements
             * @return          Dot product
             */
            template <typename T>
            T dot_product (const T* a, const T* b, std::size_t count)
            {
                using simd_t                = typename Strategy::simd_type<T, Strategy::min_simd_width<T>::value>::type;
                constexpr std::size_t Width = Strategy::min_simd_width<T>::value;

                T result      = T (0);
                std::size_t i = 0;

                const std::size_t prologue_end = std::min (
                    Strategy::samples_to_alignment<Strategy::simd_alignment<T>()> (a),
                    count);

                for (; i < prologue_end; ++i)
                {
                    result += a[i] * b[i];
                }

                if (i + Width <= count)
                {
                    simd_t vsum = set1<T> (T (0));
                    for (; i + Width <= count; i += Width)
                        vsum = mul_add (load<T> (a + i), load<T> (b + i), vsum);
                    result += hsum (vsum);
                }

                for (; i < count; ++i)
                    result += a[i] * b[i];

                return result;
            }
            /**
             * @brief Single-pass scaled copy: dst[i] = src[i] * gain
             *
             * Cheaper than copy() + scale() for large buffers:
             * one pass over src, one write to dst (2 × bandwidth instead of 3).
             *
             * @tparam T     Element type.
             * @param dst    Destination array.
             * @param src    Source array.
             * @param count  Number of elements.
             * @param gain   Scalar gain factor.
             */
            template <typename T>
            void copy_with_gain (T* CASPI_RESTRICT dst,
                                 const T* CASPI_RESTRICT src,
                                 std::size_t count,
                                 T gain)
            {
                block_op_unary (dst, src, count, kernels::CopyGainKernel<T> (gain));
            }

            /**
             * @brief Single-pass accumulate with gain: dst[i] += src[i] * gain
             *
             * Common in audio mixing: add a send at a given level to a bus.
             * Uses FMA when available. Single pass: no intermediate buffer needed.
             *
             * @tparam T     Element type.
             * @param dst    Accumulator array (modified in-place).
             * @param src    Source array.
             * @param count  Number of elements.
             * @param gain   Scalar gain to apply to src before accumulating.
             */
            template <typename T>
            void accumulate_with_gain (T* CASPI_RESTRICT dst,
                                       const T* CASPI_RESTRICT src,
                                       std::size_t count,
                                       T gain)
            {
                block_op_binary (dst, src, count, kernels::AccumGainKernel<T> (gain));
            }

            /**
             * @brief Stereo pan: single source, two outputs, independent gains.
             *
             * left[i]  = src[i] * gain_l
             * right[i] = src[i] * gain_r
             *
             * Both outputs are computed in a single pass over src.
             * This halves the read bandwidth vs two separate copy_with_gain calls.
             *
             * @tparam T       Element type.
             * @param left     Left output array.
             * @param right    Right output array.
             * @param src      Source (mono) array.
             * @param count    Number of frames.
             * @param gain_l   Left channel gain.
             * @param gain_r   Right channel gain.
             */
            template <typename T>
            void pan (T* CASPI_RESTRICT left,
                      T* CASPI_RESTRICT right,
                      const T* CASPI_RESTRICT src,
                      std::size_t count,
                      T gain_l,
                      T gain_r)
            {
                if (left == nullptr || right == nullptr || src == nullptr || count == 0)
                    return;

                constexpr std::size_t Width     = Strategy::min_simd_width<T>::value;
                constexpr std::size_t Alignment = Strategy::simd_alignment<T>();
                std::size_t i                   = 0;

                const auto gl_vec = set1<T> (gain_l);
                const auto gr_vec = set1<T> (gain_r);

                // Prologue
                const std::size_t prologue = std::min (
                    Strategy::samples_to_alignment<Alignment> (src), count);
                for (; i < prologue; ++i)
                {
                    left[i]  = src[i] * gain_l;
                    right[i] = src[i] * gain_r;
                }

                // SIMD body
                const std::size_t simd_end = i + ((count - i) / Width) * Width;
                for (; i < simd_end; i += Width)
                {
                    auto s = load<T> (src + i);
                    store (left + i, SIMD::mul (s, gl_vec));
                    store (right + i, SIMD::mul (s, gr_vec));
                }

                // Epilogue
                for (; i < count; ++i)
                {
                    left[i]  = src[i] * gain_l;
                    right[i] = src[i] * gain_r;
                }
            }

            /**
             * @brief Apply sin approximation to a buffer: dst[i] = sin(src[i])
             *
             * Inputs must be range-reduced to [-π/2, π/2].
             * In-place is valid: dst == src is allowed.
             *
             * Degree-5 Taylor polynomial from coeffs::sin_d5.
             * Max absolute error: ~5.6e-8 (float), ~5.6e-8 (double) on [-π/2, π/2].
             * These are speed-optimised approximations, not full libm precision.
             *
             * @tparam T     float or double.
             * @param dst    Output buffer.
             * @param src    Input buffer (phase in [-π/2, π/2]).
             * @param count  Number of samples.
             */
            template <typename T>
            void sin_block (T* CASPI_RESTRICT dst,
                            const T* CASPI_RESTRICT src,
                            std::size_t count)
            {
                static_assert (coeffs::sin_d5.size() == 6,
                               "sin_block: coeffs::sin_d5 size mismatch — update loop if changed");

                // Broadcast-once as constexpr-visible locals.
                // The compiler will emit vbroadcastss/vmovddup at function entry,
                // not inside the loop body.
                static constexpr T c0 = static_cast<T> (coeffs::sin_d5[0]);
                static constexpr T c1 = static_cast<T> (coeffs::sin_d5[1]);
                static constexpr T c2 = static_cast<T> (coeffs::sin_d5[2]);
                static constexpr T c3 = static_cast<T> (coeffs::sin_d5[3]);
                static constexpr T c4 = static_cast<T> (coeffs::sin_d5[4]);
                static constexpr T c5 = static_cast<T> (coeffs::sin_d5[5]);

                if (dst == nullptr || src == nullptr || count == 0)
                    return;

                for (std::size_t i = 0; i < count; ++i)
                {
                    const T x = src[i];
                    const T u = x * x;
                    // Horner: sin(x) ≈ x * (c0 + u*(c1 + u*(c2 + u*(c3 + u*(c4 + u*c5)))))
                    dst[i] = x * (c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5)))));
                }
            }

            /**
             * @brief Apply cos approximation to a buffer: dst[i] = cos(src[i])
             *
             * Inputs must be range-reduced to [-π/2, π/2].
             *
             * Degree-5 Taylor polynomial from coeffs::cos_d5.
             * Max absolute error: ~4.6e-7 (float), ~4.6e-7 (double) on [-π/2, π/2].
             *
             * @tparam T     float or double.
             * @param dst    Output buffer.
             * @param src    Input buffer (phase in [-π/2, π/2]).
             * @param count  Number of samples.
             */
            template <typename T>
            void cos_block (T* CASPI_RESTRICT dst,
                            const T* CASPI_RESTRICT src,
                            std::size_t count)
            {
                static_assert (coeffs::cos_d5.size() == 6,
                               "cos_block: coeffs::cos_d5 size mismatch — update loop if changed");

                static constexpr T c0 = static_cast<T> (coeffs::cos_d5[0]);
                static constexpr T c1 = static_cast<T> (coeffs::cos_d5[1]);
                static constexpr T c2 = static_cast<T> (coeffs::cos_d5[2]);
                static constexpr T c3 = static_cast<T> (coeffs::cos_d5[3]);
                static constexpr T c4 = static_cast<T> (coeffs::cos_d5[4]);
                static constexpr T c5 = static_cast<T> (coeffs::cos_d5[5]);

                if (dst == nullptr || src == nullptr || count == 0)
                    return;

                for (std::size_t i = 0; i < count; ++i)
                {
                    const T x = src[i];
                    const T u = x * x;
                    // Horner: cos(x) ≈ c0 + u*(c1 + u*(c2 + u*(c3 + u*(c4 + u*c5))))
                    dst[i] = c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * c5))));
                }
            }

            /**
             * @brief Apply tanh approximation to a buffer: dst[i] = tanh(src[i])
             *
             * float (degree 3, coeffs::tanh_d3):
             *   max error ~4e-4, valid for |x| <= 0.65.
             * double (degree 7, coeffs::tanh_d7):
             *   max error ~8.7e-8 at |x|=0.6, ~2.7e-11 at |x|=0.27.
             *   See kTanhDomainD in the test file for the boundary for a given tolerance.
             *
             * @tparam T     float or double.
             * @param dst    Output buffer.
             * @param src    Input buffer.
             * @param count  Number of samples.
             */
            template <typename T>
            void tanh_block (T* CASPI_RESTRICT dst,
                             const T* CASPI_RESTRICT src,
                             std::size_t count)
            {
                if (dst == nullptr || src == nullptr || count == 0)
                    return;

                if constexpr (std::is_same_v<T, float>)
                {
                    static_assert (coeffs::tanh_d3.size() == 4,
                                   "tanh_block<float>: coeffs::tanh_d3 size mismatch");

                    static constexpr T c0 = static_cast<T> (coeffs::tanh_d3[0]);
                    static constexpr T c1 = static_cast<T> (coeffs::tanh_d3[1]);
                    static constexpr T c2 = static_cast<T> (coeffs::tanh_d3[2]);
                    static constexpr T c3 = static_cast<T> (coeffs::tanh_d3[3]);

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const T x = src[i];
                        const T u = x * x;
                        // Horner: tanh(x) ≈ x * (c0 + u*(c1 + u*(c2 + u*c3)))
                        dst[i] = x * (c0 + u * (c1 + u * (c2 + u * c3)));
                    }
                }
                else
                {
                    static_assert (coeffs::tanh_d7.size() == 8,
                                   "tanh_block<double>: coeffs::tanh_d7 size mismatch");

                    static constexpr T c0 = static_cast<T> (coeffs::tanh_d7[0]);
                    static constexpr T c1 = static_cast<T> (coeffs::tanh_d7[1]);
                    static constexpr T c2 = static_cast<T> (coeffs::tanh_d7[2]);
                    static constexpr T c3 = static_cast<T> (coeffs::tanh_d7[3]);
                    static constexpr T c4 = static_cast<T> (coeffs::tanh_d7[4]);
                    static constexpr T c5 = static_cast<T> (coeffs::tanh_d7[5]);
                    static constexpr T c6 = static_cast<T> (coeffs::tanh_d7[6]);
                    static constexpr T c7 = static_cast<T> (coeffs::tanh_d7[7]);

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const T x = src[i];
                        const T u = x * x;
                        // Horner: tanh(x) ≈ x * (c0 + u*(c1 + u*(c2 + u*(c3 + u*(c4 + u*(c5 + u*(c6 + u*c7)))))))
                        dst[i] = x * (c0 + u * (c1 + u * (c2 + u * (c3 + u * (c4 + u * (c5 + u * (c6 + u * c7)))))));
                    }
                }
            }

        } // namespace ops
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_BLOCKS_H
