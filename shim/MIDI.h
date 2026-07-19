// Minimal Arduino MIDI Library stub (types + no-op serial MIDI interface).
#pragma once

#include <cstdint>

#define MIDI_CHANNEL_OMNI 0
#define MIDI_CREATE_INSTANCE(Type, SerialPort, Name) \
  midi::SerialMIDI<Type> Name##_serial(SerialPort);  \
  midi::MidiInterface<midi::SerialMIDI<Type>> Name(Name##_serial);

namespace midi {

enum MidiType : uint8_t {
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
  Tick = 0xF9,
  Start = 0xFA,
  Continue = 0xFB,
  Stop = 0xFC,
  ActiveSensing = 0xFE,
  SystemReset = 0xFF,
};

using DataByte = uint8_t;
using Channel = uint8_t;

template <typename SerialPort>
class SerialMIDI {
 public:
  explicit SerialMIDI(SerialPort& port) : port_(port) {}
  SerialPort& port_;
};

template <typename Transport>
class MidiInterface {
 public:
  explicit MidiInterface(Transport& t) : transport_(t) {}

  void begin(int inChannel = 1) { (void)inChannel; }
  bool read() { return false; }

  MidiType getType() const { return InvalidType; }
  Channel getChannel() const { return 0; }
  DataByte getData1() const { return 0; }
  DataByte getData2() const { return 0; }
  uint8_t* getSysExArray() const { static uint8_t buf[4] = {0}; return buf; }
  unsigned getSysExArrayLength() const { return 0; }

  void send(MidiType, DataByte, DataByte, Channel) {}
  void sendNoteOn(DataByte, DataByte, Channel) {}
  void sendNoteOff(DataByte, DataByte, Channel) {}
  void sendControlChange(DataByte, DataByte, Channel) {}
  void sendPitchBend(int, Channel) {}
  void sendAfterTouch(DataByte, Channel) {}
  void sendAfterTouch(DataByte, DataByte, Channel) {}
  void sendProgramChange(DataByte, Channel) {}
  void sendRealTime(MidiType) {}
  void sendSysEx(unsigned, const uint8_t*, bool = false) {}
  void turnThruOn() {}
  void turnThruOff() {}

 private:
  Transport& transport_;
};

}  // namespace midi
