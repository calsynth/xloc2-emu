// Arduino / Teensy core reimplementation on the emulator's virtual clock.
// NOTE: teensy-x86-stubs' Arduino.cpp / core_pins.cpp are deliberately NOT
// compiled; everything clock- or pin-related is implemented here against
// emu's deterministic virtual time.
#include <Arduino.h>
#include <EEPROM.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

#include "../core/emu.h"
#include "../core/emu_internal.h"

// ---------------------------------------------------------------------------
// Fake peripheral memory: the stub imxrt.h maps registers at the real i.MX RT
// addresses (0x40000000.., GPIO6-9 at 0x42000000.., ARM debug at 0xE0000000..).
// We mmap zero-filled memory there so firmware register pokes (LPSPI4, IOMUXC,
// GPIO ISR flags, DWT cycle counter, ...) are plain memory accesses.
// ---------------------------------------------------------------------------
__attribute__((constructor(101))) static void map_fake_peripherals() {
  struct Region { uintptr_t base; size_t size; };
  static const Region regions[] = {
      {0x40000000u, 0x00400000u},  // AIPS peripherals (LPSPI, IOMUXC, CCM, TMR...)
      {0x42000000u, 0x00010000u},  // GPIO6..9
      {0xE0000000u, 0x00100000u},  // ARM DWT/NVIC/SCB
  };
  for (const auto& r : regions) {
    void* p = mmap((void*)r.base, r.size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED && errno == EEXIST) {
      // Hot reload: a previously loaded core instance already mapped this
      // region (the mapping outlives dlclose). Replace it with a fresh
      // zero-filled one — the retired core is stopped before a new one is
      // loaded, and a freshly booting core expects reset registers anyway.
      p = mmap((void*)r.base, r.size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    }
    if (p != (void*)r.base) {
      fprintf(stderr,
              "emu: FATAL: cannot map fake peripheral region at %p (got %p)\n",
              (void*)r.base, p);
      abort();
    }
  }
}

// ---------------------------------------------------------------------------
// clock
// ---------------------------------------------------------------------------
uint32_t millis(void) {
  emu::host_pump();
  return (uint32_t)(emu::now_us_ / 1000ull);
}

unsigned long micros() {
  emu::host_pump();
  return (unsigned long)emu::now_us_;
}

void delay(uint32_t ms) { emu::step_us_internal((uint64_t)ms * 1000ull); }
void delayMicroseconds(uint32_t usec) { emu::step_us_internal(usec); }
void delayNanoseconds(uint32_t nsec) { (void)nsec; }

void yield() {}

// declared in stubs' Arduino.h
myyieldfn yield_impl = nullptr;
uint8_t yield_active_check_flags = 0;
volatile bool arduino_should_exit = false;
extern "C" uint8_t external_psram_size = 16;
void __disable_irq() {}
void __enable_irq() {}
void initialize_mock_arduino() {}

// Teensy heap symbols referenced by OC_core.cpp FreeRam()
extern "C" {
char _heap_end[1];
char* __brkval = _heap_end;
char _extram_start[1];
char _extram_end[1];
char _ebss[1];
char _estack[1];
void _reboot_Teensyduino_(void) {
  fprintf(stderr, "emu: _reboot_Teensyduino_() called\n");
  abort();
}
}

// ---------------------------------------------------------------------------
// pins
// ---------------------------------------------------------------------------
void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }

void digitalWrite(uint8_t pin, uint8_t val) { emu::pin_set_level(pin, val ? 1 : 0); }
void digitalWriteFast(uint8_t pin, uint8_t val) { digitalWrite(pin, val); }

uint8_t digitalRead(uint8_t pin) { return emu::pin_get_level(pin); }
uint8_t digitalReadFast(uint8_t pin) { return emu::pin_get_level(pin); }

void digitalToggle(uint8_t pin) { emu::pin_set_level(pin, !emu::pin_get_level(pin)); }
void digitalToggleFast(uint8_t pin) { digitalToggle(pin); }

void attachInterrupt(uint8_t pin, void (*function)(void), int mode) {
  emu::pin_attach_interrupt(pin, function, mode);
}
void detachInterrupt(uint8_t pin) { emu::pin_detach_interrupt(pin); }

// analog: the O_C ADC path is fully shimmed (OC::ADC), so this is only used
// by stray code; return midscale.
int analogRead(uint8_t pin) { (void)pin; return 512; }
void analogWrite(uint8_t pin, int val) { (void)pin; (void)val; }
void analogReference(uint8_t type) { (void)type; }
void analogReadRes(unsigned int bits) { (void)bits; }
void analogReadAveraging(unsigned int num) { (void)num; }
uint32_t analogWriteRes(uint32_t bits) { (void)bits; return 0; }
void analogWriteFrequency(uint8_t pin, float frequency) { (void)pin; (void)frequency; }

// ---------------------------------------------------------------------------
// misc Arduino API
// ---------------------------------------------------------------------------
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static unsigned long rand_state = 1;
void randomSeed(unsigned long seed) { rand_state = seed ? seed : 1; }
static inline uint32_t xorshift32() {
  uint32_t x = (uint32_t)rand_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rand_state = x;
  return x;
}
long random(long max) {
  if (max <= 0) return 0;
  return (long)(xorshift32() % (uint32_t)max);
}
long random(long min, long max) {
  if (min >= max) return min;
  return min + random(max - min);
}

// ---------------------------------------------------------------------------
// IntervalTimer (shim/IntervalTimer.h)
// ---------------------------------------------------------------------------
bool IntervalTimer::begin_us(callback_t callback, double usec) {
  end();
  slot_ = emu::timer_register(callback, usec);
  return slot_ >= 0;
}

void IntervalTimer::end() {
  if (slot_ >= 0) {
    emu::timer_unregister(slot_);
    slot_ = -1;
  }
}

// ---------------------------------------------------------------------------
// globals
// ---------------------------------------------------------------------------
EEPROMClass EEPROM;
EmuCrashReportClass CrashReport;
