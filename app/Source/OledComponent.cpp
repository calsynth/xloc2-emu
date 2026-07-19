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

  // integer scale, centered
  const int scale = juce::jmax(1, juce::jmin(getWidth() / 128, getHeight() / 64));
  const int w = 128 * scale, h = 64 * scale;
  const juce::Rectangle<int> area((getWidth() - w) / 2, (getHeight() - h) / 2, w, h);

  // soft glow pass: the same frame, slightly enlarged and smoothed
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.setOpacity(0.28f);
    const auto glow = area.toFloat().expanded((float)scale * 0.9f);
    g.drawImage(frame_, glow, juce::RectanglePlacement::stretchToFit);
  }

  // crisp pixel pass (nearest-neighbour)
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setOpacity(1.0f);
    g.drawImage(frame_, area.toFloat(), juce::RectanglePlacement::stretchToFit);
  }

  // subtle scanlines between pixel rows
  if (scale >= 3) {
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    for (int y = 1; y < 64; ++y)
      g.fillRect(area.getX(), area.getY() + y * scale - 1, area.getWidth(), 1);
  }

  // faint glass edge
  g.setColour(juce::Colours::white.withAlpha(0.06f));
  g.drawRect(area.expanded(1), 1);
}
