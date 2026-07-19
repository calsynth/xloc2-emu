// Minimal USBHost_t36 stub: no USB host devices in the emulator.
#pragma once

#include <cstdint>

class USBHost {
 public:
  void begin() {}
  void Task() {}
};

class USBHub {
 public:
  explicit USBHub(USBHost&) {}
};

class MIDIDevice {
 public:
  explicit MIDIDevice(USBHost&) {}
  operator bool() const { return false; }

  bool read(uint8_t channel = 0) { (void)channel; return false; }
  uint8_t getType() const { return 0; }
  uint8_t getChannel() const { return 0; }
  uint8_t getData1() const { return 0; }
  uint8_t getData2() const { return 0; }
  uint8_t getCable() const { return 0; }
  uint8_t* getSysExArray() const { static uint8_t buf[4] = {0}; return buf; }
  uint16_t getSysExArrayLength() const { return 0; }

  void send(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendNoteOn(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendNoteOff(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendControlChange(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendPitchBend(int, uint8_t, uint8_t = 0) {}
  void sendAfterTouch(uint8_t, uint8_t, uint8_t = 0) {}
  void sendAfterTouch(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendProgramChange(uint8_t, uint8_t, uint8_t = 0) {}
  void sendRealTime(uint8_t) {}
  void sendSysEx(uint32_t, const uint8_t*, bool = false, uint8_t = 0) {}
  void send_now() {}
};

class MIDIDevice_BigBuffer : public MIDIDevice {
 public:
  explicit MIDIDevice_BigBuffer(USBHost& host) : MIDIDevice(host) {}
};
