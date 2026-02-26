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

#include <cmath>

#include "caspi_Intrinsics.h"
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
        } // namespace kernels

        /**
         * @brief Binary in-place block operation: dst[i] = kernel(dst[i], src[i])
         *
         * @tparam T         Element type
         * @tparam Kernel    Operation kernel
         * @param dst        Destination array (modified in-place)
         * @param src        Source array
         * @param count      Number of elements
         * @param kernel     Operation kernel
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
                    dst[i] = kernel(dst[i], src[i]);
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
         * @tparam T         Element type
         * @tparam Kernel    Operation kernel
         * @param dst        Destination array
         * @param src        Source array
         * @param count      Number of elements
         * @param kernel     Operation kernel
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
         * @tparam T         Element type
         * @tparam Kernel    Operation kernel
         * @param data       Data array (modified in-place)
         * @param count      Number of elements
         * @param kernel     Operation kernel
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
         * Uses non-temporal stores for large aligned buffers.
         *
         * @tparam T         Element type
         * @tparam Kernel    Fill kernel (must provide scalar_value() and simd_value())
         * @param dst        Destination array
         * @param count      Number of elements
         * @param kernel     Fill kernel
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
         * @tparam T         Element type
         * @tparam Kernel    Operation kernel
         * @param dst        Destination array (also used as first input)
         * @param src1       First source array
         * @param src2       Second source array
         * @param count      Number of elements
         * @param kernel     Operation kernel
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
         * @tparam T         Element type
         * @tparam Kernel    Operation kernel
         * @param dst        Destination array
         * @param a          First source array
         * @param b          Second source array
         * @param count      Number of elements
         * @param kernel     Operation kernel
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
             * @tparam T         Element type
             * @param dst        Destination array
             * @param count      Number of elements
             * @param value      Value to fill
             */
            template <typename T>
            void fill (T* CASPI_RESTRICT dst, std::size_t count, T value)
            {
                std::fill_n (dst, count, value);
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

        } // namespace ops
    } // namespace SIMD
} // namespace CASPI

#endif // CASPI_BLOCKS_H
