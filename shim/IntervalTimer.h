// XLOC2 emulator: deterministic IntervalTimer.
// Shadows teensy-x86-stubs' thread-based version. Timers register with the
// emulator scheduler and fire when virtual time is stepped (emu::step_us),
// giving deterministic, sample-accurate interleaving of the firmware's
// 16.666 kHz core ISR and 1 kHz UI ISR.
#pragma once

#include <cstdint>

class IntervalTimer {
 public:
  using callback_t = void (*)();

  IntervalTimer() = default;
  ~IntervalTimer() { end(); }

  bool begin(callback_t callback, unsigned int usec) {
    return begin_us(callback, (double)usec);
  }
  bool begin(callback_t callback, int usec) {
    return begin_us(callback, (double)usec);
  }
  bool begin(callback_t callback, float usec) {
    return begin_us(callback, (double)usec);
  }
  bool begin(callback_t callback, double usec) {
    return begin_us(callback, usec);
  }
  void priority(uint8_t) {}
  void end();

 private:
  bool begin_us(callback_t callback, double usec);
  int slot_ = -1;
};
