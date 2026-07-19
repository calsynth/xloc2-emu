// sqrt_integer stub (host)
#pragma once
#include <cstdint>
#include <cmath>
static inline uint32_t sqrt_uint32(uint32_t in) { return (uint32_t)std::lround(std::sqrt((double)in)); }
static inline uint32_t sqrt_uint32_approx(uint32_t in) { return sqrt_uint32(in); }
