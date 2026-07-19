// Minimal SPI stub: firmware SPI traffic (DAC, OLED) is intercepted at a
// higher level; these calls only need to compile and be harmless.
#pragma once

#include <Arduino.h>  // Teensy's SPI.h pulls in Arduino.h/imxrt.h; firmware relies on it
#include <cstdint>

#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C

class SPISettings {
 public:
  SPISettings() {}
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) {
    (void)clock; (void)bitOrder; (void)dataMode;
  }
};

class SPIClass {
 public:
  void begin() {}
  void end() {}
  void beginTransaction(const SPISettings&) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t data) { return data; }
  uint16_t transfer16(uint16_t data) { return data; }
  void transfer(void* buf, size_t count) { (void)buf; (void)count; }
  void setMOSI(uint8_t) {}
  void setMISO(uint8_t) {}
  void setSCK(uint8_t) {}
  void setCS(uint8_t) {}
  void usingInterrupt(uint8_t) {}
};

extern SPIClass SPI;
extern SPIClass SPI1;
extern SPIClass SPI2;
