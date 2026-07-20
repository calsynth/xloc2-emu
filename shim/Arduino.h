// XLOC2 emulator: Arduino.h shim.
// Wraps teensy-x86-stubs' Arduino.h and supplements anything the
// Phazerville T4.1 build needs that the stubs lack.
#pragma once

// Use our deterministic IntervalTimer and suppress the stub's thread-based
// one (its include guard is __INTERVALTIMER_H__).
#include "IntervalTimer.h"
#define __INTERVALTIMER_H__

#include_next <Arduino.h>

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <type_traits>

#include <imxrt.h>
#include "usb_midi_shim.h"

// Teensy 4 core provides template min/max in Arduino.h (not macros); the
// firmware relies on unqualified min()/max() with mixed integer types.
// Return BY VALUE (the decayed common type): `decltype(a < b ? a : b)` is a
// reference when A == B, i.e. a dangling reference to the by-value
// parameters (clang diagnoses this; it was a real bug).
template <class A, class B>
constexpr typename std::common_type<A, B>::type min(A a, B b) {
  return a < b ? a : b;
}
template <class A, class B>
constexpr typename std::common_type<A, B>::type max(A a, B b) {
  return a > b ? a : b;
}
// CMSIS sync intrinsics (util/util_sync.h) — single-threaded host: trivial.
static inline void __DMB() {}
static inline uint32_t __LDREXW(volatile uint32_t* addr) { return *addr; }
static inline uint32_t __STREXW(uint32_t value, volatile uint32_t* addr) {
  *addr = value;
  return 0;
}
static inline void __CLREX() {}

// ---- attribute / section macros the stubs may not define ----
#ifndef FASTRUN
#define FASTRUN
#endif
#ifndef FLASHMEM
#define FLASHMEM
#endif
#ifndef DMAMEM
#define DMAMEM
#endif
#ifndef PROGMEM
#define PROGMEM
#endif

// Teensy CrashReport object (Main.cpp only; harmless to define)
struct EmuCrashReportClass {
  operator bool() const { return false; }
};
extern EmuCrashReportClass CrashReport;

// NVIC macros — no-ops on host
#ifndef NVIC_SET_PRIORITY
#define NVIC_SET_PRIORITY(irq, prio)
#endif
#ifndef NVIC_ENABLE_IRQ
#define NVIC_ENABLE_IRQ(irq)
#endif
#ifndef NVIC_DISABLE_IRQ
#define NVIC_DISABLE_IRQ(irq)
#endif
#ifndef NVIC_CLEAR_PENDING
#define NVIC_CLEAR_PENDING(irq)
#endif

// glibc's sys/types.h defines `uint`; Darwin's doesn't. Firmware uses it.
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned long ulong;

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

#ifndef extmem_calloc
#define extmem_calloc(nmemb, size) calloc((nmemb), (size))
#endif

extern "C" uint8_t external_psram_size;

#ifndef F_BUS_ACTUAL
#define F_BUS_ACTUAL 150000000
#endif
