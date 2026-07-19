// avr/pgmspace.h stub for host builds
#pragma once
#include <Arduino.h>
#define PGM_P const char*
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_float(p) (*(const float*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))
#define memcpy_P memcpy
#define strcmp_P strcmp
#define strlen_P strlen
