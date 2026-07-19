#include "OledComponent.h"

#include "EmuEngine.h"

namespace {
constexpr uint32_t kLitPixel = 0xffe9efff;   // cool white, slight blue cast
constexpr uint32_t kUnlitPixel = 0xff10141b; // near-black glass
constexpr uint32_t kBezel = 0xff05070a;
}  // namespace

OledComponent::OledComponent(EmuEngine& engine) : engine_(engine) {
  setOpaque(true);
  setInterceptsMouseClicks(false, false);
  startTimerHz(60);
}

void OledComponent::timerCallback() {
  const auto frame = engine_.readScreen(pages_.data());
  if (frame != lastFrame_) {
    lastFrame_ = frame;
    rebuildImage();
    repaint();
  }
}

void OledComponent::rebuildImage() {
  juce::Image::BitmapData bd(frame_, juce::Image::BitmapData::writeOnly);
  const juce::Colour lit(kLitPixel), unlit(kUnlitPixel);
  for (int y = 0; y < 64; ++y) {
    const int page = y >> 3;
    const int bit = y & 7;
    for (int x = 0; x < 128; ++x) {
      const bool on = ((pages_[(size_t)(page * 128 + x)] >> bit) & 1) != 0;
      bd.setPixelColour(x, y, on ? lit : unlit);
    }
  }
}

void OledComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kBezel));

  // fill the panel's screen aperture edge-to-edge (the aperture is ~2:1,
  // matching the 128x64 active area)
  const auto area = getLocalBounds().toFloat();
  const float scaleY = area.getHeight() / 64.0f;

  // soft glow pass: the same frame, slightly enlarged and smoothed
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.setOpacity(0.28f);
    g.drawImage(frame_, area.expanded(scaleY * 0.9f),
                juce::RectanglePlacement::stretchToFit);
  }

  // crisp pixel pass (nearest-neighbour)
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setOpacity(1.0f);
    g.drawImage(frame_, area, juce::RectanglePlacement::stretchToFit);
  }

  // subtle scanlines between pixel rows
  if (scaleY >= 3.0f) {
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    for (int y = 1; y < 64; ++y)
      g.fillRect(area.getX(), area.getY() + (float)y * scaleY - 1.0f,
                 area.getWidth(), 1.0f);
  }

  // faint glass edge
  g.setColour(juce::Colours::white.withAlpha(0.06f));
  g.drawRect(area, 1.0f);
}
