// Teensy Audio Library stub for the XLOC2 emulator.
// API-compatible surface for the classes the Phazerville audio applets use;
// all update() bodies are silent no-ops in phase 1 (graph topology and
// parameter plumbing still work, so applets compile, instantiate and draw).
#pragma once

#include <Arduino.h>
#include <AudioStream.h>
#include <arm_math.h>

#include "synth_waveform.h"
#include "synth_whitenoise.h"

// ---------------------------------------------------------------------------
// sources
// ---------------------------------------------------------------------------
class AudioSynthWaveformDc : public AudioStream {
 public:
  AudioSynthWaveformDc() : AudioStream(0, nullptr) {}
  void amplitude(float n) { level_ = n; }
  void amplitude(float n, float ms) { level_ = n; (void)ms; }
  float read() const { return level_; }
  virtual void update() override {}
 private:
  float level_ = 0;
};

class AudioSynthKarplusStrong : public AudioStream {
 public:
  AudioSynthKarplusStrong() : AudioStream(0, nullptr) {}
  void noteOn(float frequency, float velocity) { (void)frequency; (void)velocity; }
  void noteOff(float velocity) { (void)velocity; }
  virtual void update() override {}
};

// ---------------------------------------------------------------------------
// i/o (no real audio hardware in the emulator)
// ---------------------------------------------------------------------------
class AudioInputI2S2 : public AudioStream {
 public:
  AudioInputI2S2() : AudioStream(0, nullptr) {}
  virtual void update() override {}
};

class AudioOutputI2S2 : public AudioStream {
 public:
  AudioOutputI2S2() : AudioStream(2, inputQueueArray_) {}
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[2];
};

class AudioInputUSB : public AudioStream {
 public:
  AudioInputUSB() : AudioStream(0, nullptr) {}
  float volume() { return 1.0f; }
  virtual void update() override {}
};

class AudioOutputUSB : public AudioStream {
 public:
  AudioOutputUSB() : AudioStream(2, inputQueueArray_) {}
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[2];
};

// ---------------------------------------------------------------------------
// mixers / analysis
// ---------------------------------------------------------------------------
class AudioMixer4 : public AudioStream {
 public:
  AudioMixer4() : AudioStream(4, inputQueueArray_) {
    for (auto& g : gains_) g = 1.0f;
  }
  void gain(unsigned int channel, float g) {
    if (channel < 4) gains_[channel] = g;
  }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[4];
  float gains_[4];
};

class AudioAnalyzePeak : public AudioStream {
 public:
  AudioAnalyzePeak() : AudioStream(1, inputQueueArray_) {}
  bool available() { return true; }
  float read() { return 0.0f; }
  float readPeakToPeak() { return 0.0f; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

class AudioAnalyzeRMS : public AudioStream {
 public:
  AudioAnalyzeRMS() : AudioStream(1, inputQueueArray_) {}
  bool available() { return true; }
  float read() { return 0.0f; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

class AudioRecordQueue : public AudioStream {
 public:
  AudioRecordQueue() : AudioStream(1, inputQueueArray_) {}
  void begin() { running_ = true; }
  void end() { running_ = false; }
  int available() { return 0; }
  void clear() {}
  int16_t* readBuffer() { return nullptr; }
  void freeBuffer() {}
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
  bool running_ = false;
};

class AudioPlayQueue : public AudioStream {
 public:
  AudioPlayQueue() : AudioStream(0, nullptr) {}
  int16_t* getBuffer() { return staging_; }
  void playBuffer() {}
  virtual void update() override {}
 private:
  int16_t staging_[AUDIO_BLOCK_SAMPLES] = {0};
};

// ---------------------------------------------------------------------------
// effects / filters
// ---------------------------------------------------------------------------
class AudioEffectFreeverb : public AudioStream {
 public:
  AudioEffectFreeverb() : AudioStream(1, inputQueueArray_) {}
  void roomsize(float n) { (void)n; }
  void damping(float n) { (void)n; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

class AudioEffectFreeverbStereo : public AudioStream {
 public:
  AudioEffectFreeverbStereo() : AudioStream(1, inputQueueArray_) {}
  void roomsize(float n) { (void)n; }
  void damping(float n) { (void)n; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

class AudioEffectWaveFolder : public AudioStream {
 public:
  AudioEffectWaveFolder() : AudioStream(2, inputQueueArray_) {}
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[2];
};

class AudioFilterLadder : public AudioStream {
 public:
  AudioFilterLadder() : AudioStream(3, inputQueueArray_) {}
  void frequency(float FC) { (void)FC; }
  void resonance(float reson) { (void)reson; }
  void octaveControl(float octaves) { (void)octaves; }
  void inputDrive(float drv) { (void)drv; }
  void passbandGain(float passbandgain) { (void)passbandgain; }
  void interpolationMethod(int im) { (void)im; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[3];
};

class AudioFilterStateVariable : public AudioStream {
 public:
  AudioFilterStateVariable() : AudioStream(2, inputQueueArray_) {}
  void frequency(float freq) { (void)freq; }
  void resonance(float q) { (void)q; }
  void octaveControl(float n) { (void)n; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[2];
};

class AudioEffectDelay : public AudioStream {
 public:
  AudioEffectDelay() : AudioStream(1, inputQueueArray_) {}
  void delay(uint8_t channel, float milliseconds) { (void)channel; (void)milliseconds; }
  void disable(uint8_t channel) { (void)channel; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

class AudioEffectBitcrusher : public AudioStream {
 public:
  AudioEffectBitcrusher() : AudioStream(1, inputQueueArray_) {}
  void bits(uint8_t b) { (void)b; }
  void sampleRate(float hz) { (void)hz; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};

// AudioAmplifier occasionally referenced by shared code
class AudioAmplifier : public AudioStream {
 public:
  AudioAmplifier() : AudioStream(1, inputQueueArray_) {}
  void gain(float n) { (void)n; }
  virtual void update() override {}
 private:
  audio_block_t* inputQueueArray_[1];
};
