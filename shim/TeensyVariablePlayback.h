// Stub of the Phazerville-pinned teensy-variable-playback fork.
// SD wav playback is a no-op in phase 1 (no audio rendering); the API
// surface matches what the WAV player applets call.
#pragma once

#include <Arduino.h>
#include <AudioStream.h>

enum play_start : uint8_t {
  play_start_sample = 0,
  play_start_loop = 1,
};

class AudioPlaySdResmp : public AudioStream {
 public:
  AudioPlaySdResmp() : AudioStream(0, nullptr) {}

  void enableInterpolation(bool enable) { (void)enable; }
  void setBufferInPSRAM(bool enable) { (void)enable; }

  bool playWav(const char* filename) { (void)filename; playing_ = false; return false; }
  bool playRaw(const char* filename, uint16_t numChannels = 1) {
    (void)filename; (void)numChannels; return false;
  }
  void play() { playing_ = ready_; }
  void stop() { playing_ = false; }
  bool isPlaying() const { return playing_; }
  bool available() const { return ready_; }
  void retrigger() {}
  void syncTrig() {}

  void setPlaybackRate(float rate) { rate_ = rate; }
  float getBPM() { return 120.0f; }
  void matchTempo(float target_bpm) { (void)target_bpm; }
  void setBeatStart(uint32_t beat) { (void)beat; }
  void setLoopStart(uint32_t sample) { (void)sample; }
  void setPlayStart(play_start mode) { (void)mode; }
  uint32_t getPosition() { return 0; }
  uint32_t positionMillis() { return 0; }
  uint32_t lengthMillis() { return 0; }

  virtual void update() override {}

 private:
  bool playing_ = false;
  bool ready_ = false;
  float rate_ = 1.0f;
};
