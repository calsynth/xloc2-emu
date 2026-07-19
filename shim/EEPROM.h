// XLOC2 emulator: file-backed EEPROM emulation (Teensy 4.1: 4284 bytes).
// API-compatible subset of Teensy's EEPROM library including EERef/EEPtr,
// which the firmware's EEPROMStorage driver uses.
#pragma once

#include <cstdint>
#include <cstddef>

#define E2END 4283  // Teensy 4.1

namespace emu {
// Backing store; loaded from / flushed to a file by the emulator core.
uint8_t* eeprom_data();
void eeprom_dirty();
constexpr size_t kEepromSize = E2END + 1;
}  // namespace emu

struct EERef {
  EERef(int index) : index(index) {}
  operator uint8_t() const { return emu::eeprom_data()[index]; }
  EERef& operator=(uint8_t v) {
    emu::eeprom_data()[index] = v;
    emu::eeprom_dirty();
    return *this;
  }
  EERef& update(uint8_t v) {
    if (emu::eeprom_data()[index] != v) *this = v;
    return *this;
  }
  int index;
};

struct EEPtr {
  EEPtr(int index) : index(index) {}
  operator int() const { return index; }
  EEPtr& operator=(int v) { index = v; return *this; }
  bool operator!=(const EEPtr& o) const { return index != o.index; }
  EERef operator*() { return EERef(index); }
  EEPtr& operator++() { ++index; return *this; }
  EEPtr& operator--() { --index; return *this; }
  EEPtr operator++(int) { return EEPtr(index++); }
  EEPtr operator--(int) { return EEPtr(index--); }
  int index;
};

struct EEPROMClass {
  uint8_t read(int idx) { return emu::eeprom_data()[idx]; }
  void write(int idx, uint8_t val) {
    emu::eeprom_data()[idx] = val;
    emu::eeprom_dirty();
  }
  void update(int idx, uint8_t val) {
    if (emu::eeprom_data()[idx] != val) write(idx, val);
  }
  EERef operator[](int idx) { return EERef(idx); }
  int length() { return E2END + 1; }

  template <typename T>
  T& get(int idx, T& t) {
    uint8_t* ptr = (uint8_t*)&t;
    for (int i = 0; i < (int)sizeof(T); ++i) *ptr++ = read(idx + i);
    return t;
  }
  template <typename T>
  const T& put(int idx, const T& t) {
    const uint8_t* ptr = (const uint8_t*)&t;
    for (int i = 0; i < (int)sizeof(T); ++i) update(idx + i, *ptr++);
    return t;
  }
};

extern EEPROMClass EEPROM;
