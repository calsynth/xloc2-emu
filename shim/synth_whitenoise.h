// Teensy Audio synth_whitenoise stub.
#pragma once

#include <AudioStream.h>

class AudioSynthNoiseWhite : public AudioStream {
 public:
  AudioSynthNoiseWhite() : AudioStream(0, nullptr) {}
  void amplitude(float n) { level_ = n; }
  virtual void update() override {}

 private:
  float level_ = 0;
};
