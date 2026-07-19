// OledComponent — renders the emulated SH1106 128x64 display.
//
// Polls EmuEngine::readScreen() at 60 Hz and repaints only when the frame
// counter changes. Page buffer layout (identical to core/emu.cpp's
// screenshot_pbm): 8 pages x 128 columns, each byte is 8 vertical pixels,
// LSB = top row of the page:  pixel(x, y) = (pages[(y/8)*128 + x] >> (y&7)) & 1
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdint>

class EmuEngine;

class OledComponent : public juce::Component, private juce::Timer {
 public:
  explicit OledComponent(EmuEngine& engine);

  void paint(juce::Graphics& g) override;

 private:
  void timerCallback() override;
  void rebuildImage();

  EmuEngine& engine_;
  std::array<uint8_t, 1024> pages_{};
  uint64_t lastFrame_ = ~0ull;  // force initial rebuild
  juce::Image frame_{juce::Image::ARGB, 128, 64, true};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OledComponent)
};
