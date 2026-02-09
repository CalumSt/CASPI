#ifndef CASPI_SIMD_H
#define CASPI_SIMD_H

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


* @file caspi_SIMD.h
* @author CS Islay
* @brief A platform detection header.
*
************************************************************************/

#include "caspi_Platform.h"
#include <cstdint>

namespace CASPI
{
    namespace SIMD
    {
        /*---------------------------------
          Base SIMD Types
        ---------------------------------*/

#if defined(CASPI_HAS_AVX)
        using float32x8 = __m256;
#endif

#if defined(CASPI_HAS_SSE)
        using float32x4 = __m128;
        using int32x4   = __m128i;
#elif defined(CASPI_HAS_NEON)
#include <arm_neon.h>
        using float32x4 = float32x4_t;
        using int32x4   = int32x4_t;
#elif defined(CASPI_ARCH_WASM)
#include <wasm_simd128.h>
        using float32x4 = v128_t;
        using int32x4   = v128_t;
#else
        struct float32x4 { float data[4]; };
        struct int32x4   { int32_t data[4]; };
#endif

/*---------------------------------
  Load / Store
---------------------------------*/
inline float32x4 load(const float* ptr) {
#if defined(CASPI_HAS_SSE)
    return _mm_loadu_ps(ptr);
#elif defined(CASPI_HAS_NEON)
    return vld1q_f32(ptr);
#elif defined(CASPI_ARCH_WASM)
    return wasm_v128_load(ptr);
#else
    float32x4 v;
    for (int i = 0; i < 4; i++) v.data[i] = ptr[i];
    return v;
#endif
}

inline void store(float* ptr, float32x4 v) {
#if defined(CASPI_HAS_SSE)
    _mm_storeu_ps(ptr, v);
#elif defined(CASPI_HAS_NEON)
    vst1q_f32(ptr, v);
#elif defined(CASPI_ARCH_WASM)
    wasm_v128_store(ptr, v);
#else
    for (int i = 0; i < 4; i++) ptr[i] = v.data[i];
#endif
}

/*---------------------------------
  float32x4
---------------------------------*/
inline float32x4 make_float32x4(float a, float b, float c, float d) {
#if defined(CASPI_HAS_SSE)
    return _mm_set_ps(d, c, b, a);
#elif defined(CASPI_HAS_NEON)
    float vals[4] = {a, b, c, d};
    return vld1q_f32(vals);
#elif defined(CASPI_ARCH_WASM)
    float vals[4] = {a, b, c, d};
    return wasm_v128_load(vals);
#else
    float32x4 v;
    v.data[0] = a; v.data[1] = b; v.data[2] = c; v.data[3] = d;
    return v;
#endif
}

inline float32x4 make_float32x4_from_array(const float* ptr)
{
    return load(ptr);
}

/*---------------------------------
  float32x8 (AVX only)
---------------------------------*/
#if defined(CASPI_HAS_AVX)
inline float32x8 make_float32x8(float a, float b, float c, float d,
                                float e, float f, float g, float h) {
    return _mm256_set_ps(h, g, f, e, d, c, b, a); // reverse order
}

inline float32x8 make_float32x8_from_array(const float* ptr) {
    return _mm256_loadu_ps(ptr);
}
#endif

/*---------------------------------
  int32x4
---------------------------------*/
inline int32x4 make_int32x4(int32_t a, int32_t b, int32_t c, int32_t d) {
#if defined(CASPI_HAS_SSE)
    return _mm_set_epi32(d, c, b, a);
#elif defined(CASPI_HAS_NEON)
    int32_t vals[4] = {a, b, c, d};
    return vld1q_s32(vals);
#elif defined(CASPI_ARCH_WASM)
    int32_t vals[4] = {a, b, c, d};
    return wasm_v128_load(vals);
#else
    int32x4 v;
    v.data[0] = a; v.data[1] = b; v.data[2] = c; v.data[3] = d;
    return v;
#endif
}

inline int32x4 make_int32x4_from_array(const int32_t* ptr) {
#if defined(CASPI_HAS_SSE)
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
#elif defined(CASPI_HAS_NEON)
    return vld1q_s32(ptr);
#elif defined(CASPI_ARCH_WASM)
    return wasm_v128_load(ptr);
#else
    int32x4 v;
    for (int i = 0; i < 4; i++) v.data[i] = ptr[i];
    return v;
#endif
}


/*---------------------------------
  Basic Arithmetic
---------------------------------*/
inline float32x4 add(float32x4 a, float32x4 b) {
#if defined(CASPI_HAS_SSE)
    return _mm_add_ps(a, b);
#elif defined(CASPI_HAS_NEON)
    return vaddq_f32(a, b);
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_add(a, b);
#else
    float32x4 r;
    for (int i = 0; i < 4; i++) r.data[i] = a.data[i] + b.data[i];
    return r;
#endif
}

inline float32x4 sub(float32x4 a, float32x4 b) {
#if defined(CASPI_HAS_SSE)
    return _mm_sub_ps(a, b);
#elif defined(CASPI_HAS_NEON)
    return vsubq_f32(a, b);
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_sub(a, b);
#else
    float32x4 r;
    for (int i = 0; i < 4; i++) r.data[i] = a.data[i] - b.data[i];
    return r;
#endif
}

inline float32x4 mul(float32x4 a, float32x4 b) {
#if defined(CASPI_HAS_SSE)
    return _mm_mul_ps(a, b);
#elif defined(CASPI_HAS_NEON)
    return vmulq_f32(a, b);
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_mul(a, b);
#else
    float32x4 r;
    for (int i = 0; i < 4; i++) r.data[i] = a.data[i] * b.data[i];
    return r;
#endif
}

/* Optional FMA operation */
#if defined(CASPI_HAS_FMA)
inline float32x4 fma(float32x4 a, float32x4 b, float32x4 c) {
#if defined(CASPI_HAS_AVX) || defined(CASPI_HAS_SSE)
    return _mm_fmadd_ps(a, b, c); // SSE/FMA intrinsics
#else
    float32x4 r;
    for (int i = 0; i < 4; i++) r.data[i] = a.data[i] * b.data[i] + c.data[i];
    return r;
#endif
}
#endif

/*---------------------------------
  Lane-wise Initialization
---------------------------------*/
/**
 * @brief Broadcast a float scalar to all lanes.
 */
inline float32x4 set1(float x) {
#if defined(CASPI_HAS_SSE)
    return _mm_set1_ps(x);
#elif defined(CASPI_HAS_NEON)
    return vdupq_n_f32(x);
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_splat(x);
#else
    float32x4 v; v.data[0]=v.data[1]=v.data[2]=v.data[3]=x; return v;
#endif
}

/**
 * @brief Broadcast an int scalar to all lanes.
 */
inline int32x4 set1(int x) {
#if defined(CASPI_HAS_SSE)
    return _mm_set1_epi32(x);
#elif defined(CASPI_HAS_NEON)
    return vdupq_n_s32(x);
#elif defined(CASPI_ARCH_WASM)
    return wasm_i32x4_splat(x);
#else
    int32x4 v; v.data[0]=v.data[1]=v.data[2]=v.data[3]=x; return v;
#endif
}

/*---------------------------------
  Comparisons / Masking
---------------------------------*/
/**
 * @brief Compare equal per-lane floats.
 */
inline float32x4 cmp_eq(float32x4 a, float32x4 b) {
#if defined(CASPI_HAS_SSE)
    return _mm_cmpeq_ps(a,b);
#elif defined(CASPI_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_f32(a,b));
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_eq(a,b);
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=(a.data[i]==b.data[i])?-1:0; return r;
#endif
}

/**
 * @brief Compare less-than per-lane floats.
 */
inline float32x4 cmp_lt(float32x4 a, float32x4 b) {
#if defined(CASPI_HAS_SSE)
    return _mm_cmplt_ps(a,b);
#elif defined(CASPI_HAS_NEON)
    return vreinterpretq_f32_u32(vcltq_f32(a,b));
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_lt(a,b);
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=(a.data[i]<b.data[i])?-1:0; return r;
#endif
}

/*---------------------------------
  Blend / Select
---------------------------------*/
/**
 * @brief Select per-lane: mask? b : a
 */
inline float32x4 blend(float32x4 a, float32x4 b, float32x4 mask) {
#if defined(CASPI_HAS_SSE)
    return _mm_or_ps(_mm_and_ps(mask,b), _mm_andnot_ps(mask,a));
#elif defined(CASPI_HAS_NEON)
    return vbslq_f32(vreinterpretq_u32_f32(mask),b,a);
#elif defined(CASPI_ARCH_WASM)
    return wasm_v128_bitselect(b,a,mask);
#else
    float32x4 r;
    for(int i=0;i<4;i++)
        r.data[i] = (reinterpret_cast<const uint32_t*>(&mask.data[i])[0]) ? b.data[i] : a.data[i];
    return r;
#endif
}

/*---------------------------------
  Horizontal Operations
---------------------------------*/
/**
 * @brief Sum all lanes of a float32x4 vector.
 */
inline float hsum(float32x4 v) {
#if defined(CASPI_HAS_SSE3)
    __m128 t = _mm_hadd_ps(v,v);
    t = _mm_hadd_ps(t,t);
    float out; _mm_store_ss(&out,t); return out;
#else
    float tmp[4]; store(tmp,v); return tmp[0]+tmp[1]+tmp[2]+tmp[3];
#endif
}

/**
 * @brief Maximum of all lanes of a float32x4 vector.
 */
inline float hmax(float32x4 v) {
#if defined(CASPI_HAS_SSE3)
    __m128 t1=_mm_max_ps(v,_mm_movehl_ps(v,v));
    __m128 t2=_mm_max_ss(t1,_mm_shuffle_ps(t1,t1,1));
    float out; _mm_store_ss(&out,t2); return out;
#else
    float tmp[4]; store(tmp,v);
    float out=tmp[0]; for(int i=1;i<4;i++) if(tmp[i]>out) out=tmp[i]; return out;
#endif
}

/**
 * @brief Minimum of all lanes of a float32x4 vector.
 */
inline float hmin(float32x4 v) {
#if defined(CASPI_HAS_SSE3)
    __m128 t1=_mm_min_ps(v,_mm_movehl_ps(v,v));
    __m128 t2=_mm_min_ss(t1,_mm_shuffle_ps(t1,t1,1));
    float out; _mm_store_ss(&out,t2); return out;
#else
    float tmp[4]; store(tmp,v);
    float out=tmp[0]; for(int i=1;i<4;i++) if(tmp[i]<out) out=tmp[i]; return out;
#endif
}

/*---------------------------------
  Lane-wise Math
---------------------------------*/
/**
 * @brief Negate all lanes.
 */
inline float32x4 negate(float32x4 a) {
#if defined(CASPI_HAS_SSE)
    return _mm_sub_ps(_mm_setzero_ps(),a);
#elif defined(CASPI_HAS_NEON)
    return vnegq_f32(a);
#elif defined(CASPI_ARCH_WASM)
    return wasm_f32x4_neg(a);
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=-a.data[i]; return r;
#endif
}

/**
 * @brief Absolute value of all lanes.
 */
inline float32x4 abs(float32x4 a) {
#if defined(CASPI_HAS_SSE)
    return _mm_andnot_ps(_mm_set1_ps(-0.f),a);
#elif defined(CASPI_HAS_NEON)
    return vabsq_f32(a);
#elif defined(CASPI_ARCH_WASM)
    // No direct intrinsic in WASM, fallback
    float32x4 r; float tmp[4]; store(tmp,a); for(int i=0;i<4;i++) r.data[i]=std::fabs(tmp[i]); return r;
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=std::fabs(a.data[i]); return r;
#endif
}

/**
 * @brief Square root of all lanes.
 */
inline float32x4 sqrt(float32x4 a) {
#if defined(CASPI_HAS_SSE)
    return _mm_sqrt_ps(a);
#elif defined(CASPI_HAS_NEON)
    return vsqrtq_f32(a);
#elif defined(CASPI_ARCH_WASM)
    float tmp[4]; store(tmp,a); for(int i=0;i<4;i++) tmp[i]=std::sqrt(tmp[i]); return load(tmp);
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=std::sqrt(a.data[i]); return r;
#endif
}

/*---------------------------------
  Type Conversions
---------------------------------*/
/**
 * @brief Convert float32x4 to int32x4.
 */
        inline int32x4 to_int(float32x4 a) {
#if defined(CASPI_HAS_SSE)
    return _mm_cvttps_epi32(a); // truncate toward zero
#elif defined(CASPI_HAS_NEON)
    return vcvtq_s32_f32(vrndq_f32(a)); // truncate toward zero
#elif defined(CASPI_ARCH_WASM)
    return wasm_i32x4_trunc_sat_f32x4_s(a); // truncate toward zero
#else
    int32x4 r;
    for(int i=0;i<4;i++) r.data[i] = static_cast<int32_t>(a.data[i]); // scalar trunc
    return r;
#endif
}

/**
 * @brief Convert int32x4 to float32x4.
 */
inline float32x4 to_float(int32x4 a) {
#if defined(CASPI_HAS_SSE)
    return _mm_cvtepi32_ps(a);
#else
    float32x4 r; for(int i=0;i<4;i++) r.data[i]=static_cast<float>(a.data[i]); return r;
#endif
}

    } // namespace caspi::simd
}



#endif // CASPI_SIMD_H
