// Teensy Audio synth_waveform stub: API-compatible, silent output.
// (Phase 2 will replace the update() bodies with real synthesis.)
#pragma once

#include <Arduino.h>
#include <AudioStream.h>

#define WAVEFORM_SINE              0
#define WAVEFORM_SAWTOOTH          1
#define WAVEFORM_SQUARE            2
#define WAVEFORM_TRIANGLE          3
#define WAVEFORM_ARBITRARY         4
#define WAVEFORM_PULSE             5
#define WAVEFORM_SAWTOOTH_REVERSE  6
#define WAVEFORM_SAMPLE_HOLD       7
#define WAVEFORM_TRIANGLE_VARIABLE 8
#define WAVEFORM_BANDLIMIT_SAWTOOTH          9
#define WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE 10
#define WAVEFORM_BANDLIMIT_SQUARE           11
#define WAVEFORM_BANDLIMIT_PULSE            12

class AudioSynthWaveform : public AudioStream {
 public:
  AudioSynthWaveform() : AudioStream(0, nullptr) {}
  void begin(short t_type) { tone_type_ = t_type; }
  void begin(float t_amp, float t_freq, short t_type) {
    amplitude(t_amp); frequency(t_freq); begin(t_type);
  }
  void frequency(float freq) { freq_ = freq; }
  void amplitude(float n) { amp_ = n; }
  void offset(float n) { offset_ = n; }
  void phase(float angle) { phase_ = angle; }
  void pulseWidth(float n) { pw_ = n; }
  void arbitraryWaveform(const int16_t* data, float maxFreq) {
    arb_ = data; (void)maxFreq;
  }
  virtual void update() override {}

 private:
  float freq_ = 0, amp_ = 0, offset_ = 0, phase_ = 0, pw_ = 0.5f;
  short tone_type_ = 0;
  const int16_t* arb_ = nullptr;
};

class AudioSynthWaveformModulated : public AudioStream {
 public:
  AudioSynthWaveformModulated() : AudioStream(2, inputQueueArray_) {}
  void begin(short t_type) { tone_type_ = t_type; }
  void begin(float t_amp, float t_freq, short t_type) {
    amplitude(t_amp); frequency(t_freq); begin(t_type);
  }
  void frequency(float freq) { freq_ = freq; }
  void amplitude(float n) { amp_ = n; }
  void offset(float n) { offset_ = n; }
  void frequencyModulation(float octaves) { fm_octaves_ = octaves; }
  void phaseModulation(float degrees) { pm_degrees_ = degrees; }
  void arbitraryWaveform(const int16_t* data, float maxFreq) {
    arb_ = data; (void)maxFreq;
  }
  virtual void update() override {}

 private:
  audio_block_t* inputQueueArray_[2];
  float freq_ = 0, amp_ = 0, offset_ = 0, fm_octaves_ = 8, pm_degrees_ = 0;
  short tone_type_ = 0;
  const int16_t* arb_ = nullptr;
};
