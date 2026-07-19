// Teensy USB device MIDI stub (usbMIDI object).
#pragma once

#include <cstdint>

class usb_midi_class {
 public:
  // Teensyduino message type constants (member style: usbMIDI.NoteOn ...)
  enum MidiTypes : uint8_t {
    InvalidType = 0x00,
    NoteOff = 0x80,
    NoteOn = 0x90,
    AfterTouchPoly = 0xA0,
    ControlChange = 0xB0,
    ProgramChange = 0xC0,
    AfterTouchChannel = 0xD0,
    PitchBend = 0xE0,
    SystemExclusive = 0xF0,
    TimeCodeQuarterFrame = 0xF1,
    SongPosition = 0xF2,
    SongSelect = 0xF3,
    TuneRequest = 0xF6,
    Clock = 0xF8,
    Start = 0xFA,
    Continue = 0xFB,
    Stop = 0xFC,
    ActiveSensing = 0xFE,
    SystemReset = 0xFF,
  };

  bool read(uint8_t channel = 0) { (void)channel; return false; }
  uint8_t getType() { return 0; }
  uint8_t getCable() { return 0; }
  uint8_t getChannel() { return 0; }
  uint8_t getData1() { return 0; }
  uint8_t getData2() { return 0; }
  uint8_t* getSysExArray() { static uint8_t buf[4] = {0}; return buf; }
  uint16_t getSysExArrayLength() { return 0; }

  void send(uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel,
            uint8_t cable = 0) {
    (void)type; (void)data1; (void)data2; (void)channel; (void)cable;
  }
  void sendNoteOn(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendNoteOff(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendControlChange(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendProgramChange(uint8_t, uint8_t, uint8_t = 0) {}
  void sendAfterTouch(uint8_t, uint8_t, uint8_t = 0) {}
  void sendAfterTouchPoly(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
  void sendPitchBend(int, uint8_t, uint8_t = 0) {}
  void sendRealTime(uint8_t) {}
  void sendSysEx(uint32_t length, const uint8_t* data,
                 bool hasTerm = false, uint8_t cable = 0) {
    (void)length; (void)data; (void)hasTerm; (void)cable;
  }
  void send_now() {}
  void setHandleNoteOn(void (*)(uint8_t, uint8_t, uint8_t)) {}
  void setHandleNoteOff(void (*)(uint8_t, uint8_t, uint8_t)) {}
};

extern usb_midi_class usbMIDI;
