// CMSIS-DSP subset for host builds (plain C implementations).
// Shadows teensy-x86-stubs' arm_math.h (shim include dir comes first).
#pragma once

#include <cmath>
#include <cstdint>

typedef float float32_t;
typedef int32_t q31_t;
typedef int16_t q15_t;
typedef int8_t q7_t;

#ifndef PI
#define PI 3.14159265358979f
#endif

static inline int32_t __SSAT(int32_t val, uint32_t sat) {
  const int32_t max = (1 << (sat - 1)) - 1;
  const int32_t min = -(1 << (sat - 1));
  if (val > max) return max;
  if (val < min) return min;
  return val;
}

static inline uint32_t __USAT(int32_t val, uint32_t sat) {
  const int32_t max = (int32_t)((1u << sat) - 1);
  if (val > max) return (uint32_t)max;
  if (val < 0) return 0;
  return (uint32_t)val;
}

static inline float32_t arm_sin_f32(float32_t x) { return sinf(x); }
static inline float32_t arm_cos_f32(float32_t x) { return cosf(x); }

static inline q15_t arm_sin_q15(q15_t x) {
  // x: [0, 0x7FFF] maps to [0, 2*pi)
  float f = (float)x * (2.0f * 3.14159265358979f / 32768.0f);
  return (q15_t)__SSAT((q31_t)lrintf(sinf(f) * 32768.0f), 16);
}

static inline void arm_fill_f32(float32_t value, float32_t* pDst, uint32_t n) {
  while (n--) *pDst++ = value;
}

static inline void arm_fill_q15(q15_t value, q15_t* pDst, uint32_t n) {
  while (n--) *pDst++ = value;
}

static inline void arm_copy_f32(const float32_t* pSrc, float32_t* pDst, uint32_t n) {
  while (n--) *pDst++ = *pSrc++;
}

static inline void arm_add_f32(const float32_t* a, const float32_t* b,
                               float32_t* dst, uint32_t n) {
  while (n--) *dst++ = *a++ + *b++;
}

static inline void arm_sub_f32(const float32_t* a, const float32_t* b,
                               float32_t* dst, uint32_t n) {
  while (n--) *dst++ = *a++ - *b++;
}

static inline void arm_mult_f32(const float32_t* a, const float32_t* b,
                                float32_t* dst, uint32_t n) {
  while (n--) *dst++ = *a++ * *b++;
}

static inline void arm_scale_f32(const float32_t* src, float32_t scale,
                                 float32_t* dst, uint32_t n) {
  while (n--) *dst++ = *src++ * scale;
}

static inline void arm_clip_f32(const float32_t* src, float32_t* dst,
                                float32_t low, float32_t high, uint32_t n) {
  while (n--) {
    float32_t v = *src++;
    if (v < low) v = low;
    if (v > high) v = high;
    *dst++ = v;
  }
}

static inline void arm_add_q15(const q15_t* a, const q15_t* b, q15_t* dst, uint32_t n) {
  while (n--) *dst++ = (q15_t)__SSAT((q31_t)*a++ + (q31_t)*b++, 16);
}

static inline void arm_sub_q15(const q15_t* a, const q15_t* b, q15_t* dst, uint32_t n) {
  while (n--) *dst++ = (q15_t)__SSAT((q31_t)*a++ - (q31_t)*b++, 16);
}

static inline void arm_mult_q15(const q15_t* a, const q15_t* b, q15_t* dst, uint32_t n) {
  while (n--) *dst++ = (q15_t)__SSAT(((q31_t)*a++ * (q31_t)*b++) >> 15, 16);
}

static inline void arm_scale_q15(const q15_t* src, q15_t scaleFract, int8_t shift,
                                 q15_t* dst, uint32_t n) {
  const int32_t kShift = 15 - shift;
  while (n--)
    *dst++ = (q15_t)__SSAT(((q31_t)*src++ * (q31_t)scaleFract) >> kShift, 16);
}

static inline void arm_q15_to_float(const q15_t* src, float32_t* dst, uint32_t n) {
  while (n--) *dst++ = (float32_t)(*src++) / 32768.0f;
}

static inline void arm_float_to_q15(const float32_t* src, q15_t* dst, uint32_t n) {
  while (n--) {
    float32_t v = *src++ * 32768.0f;
    v += v > 0.0f ? 0.5f : -0.5f;
    *dst++ = (q15_t)__SSAT((q31_t)v, 16);
  }
}

static inline void arm_q15_to_q31(const q15_t* src, q31_t* dst, uint32_t n) {
  while (n--) *dst++ = ((q31_t)*src++) << 16;
}

static inline void arm_q31_to_q15(const q31_t* src, q15_t* dst, uint32_t n) {
  while (n--) *dst++ = (q15_t)(*src++ >> 16);
}
