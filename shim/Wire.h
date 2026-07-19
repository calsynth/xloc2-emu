// Minimal Wire (I2C) stub: XLOC2 emulator has no I2C devices attached.
#pragma once

#include <cstdint>
#include <cstddef>

class TwoWire {
 public:
  void begin() {}
  void begin(uint8_t address) { (void)address; }
  void end() {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t address) { (void)address; }
  // 2 = NACK on address (no device present)
  uint8_t endTransmission(bool stop = true) { (void)stop; return 2; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  uint8_t requestFrom(uint8_t, uint8_t, bool stop = true) { (void)stop; return 0; }
  int available() { return 0; }
  int read() { return -1; }
};

extern TwoWire Wire;
extern TwoWire Wire1;
extern TwoWire Wire2;
