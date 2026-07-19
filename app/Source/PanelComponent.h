// PanelComponent — clean vector recreation of the XLOC2 front panel.
//
// Geometry lives in PanelLayout (normalised 0..1 panel coordinates) so the
// whole panel can be re-skinned / matched to photos by editing one struct.
// Interaction:
//   encoders   vertical drag or mouse wheel = detents; press = encoder push
//              (down on mouseDown, up on mouseUp — so drag-while-pressed
//              behaves like the hardware's push+turn)
//   buttons    A/X/B/Y/Z click (down/up tracked)
//   keyboard   Up/Down = right encoder, Left/Right = left encoder,
//              Return = right push, Escape = left push, A/B/X/Y/Z = buttons
//   jacks      Eurorack sockets with live meter rings; click opens the
//              routing panel focused on that jack
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "EmuEngine.h"
#include "OledComponent.h"
#include "RoutingPanel.h"

// All panel geometry in one place, in normalised panel units (fractions of
// panel width / height). Tweak here when Struan's real panel art arrives.
struct PanelLayout {
  // 16HP Eurorack portrait: ~81mm x 128.5mm
  float aspect = 81.0f / 128.5f;

  juce::Rectangle<float> oled{0.125f, 0.075f, 0.75f, 0.205f};

  juce::Point<float> encL{0.26f, 0.36f}, encR{0.74f, 0.36f};
  float encRadius = 0.082f;  // fraction of panel width

  // NOTE: button grouping is an assumption (A/B by the left encoder, X/Y by
  // the right, Z centred under the screen) — adjust freely.
  juce::Point<float> btnA{0.075f, 0.325f}, btnB{0.075f, 0.395f};
  juce::Point<float> btnX{0.925f, 0.325f}, btnY{0.925f, 0.395f};
  juce::Point<float> btnZ{0.50f, 0.36f};
  float btnRadius = 0.030f;

  // jack grid: 5 rows x 4 cols (TR1-4 / CVIN1-4 / CVIN5-8 / OUT A-D / OUT E-H)
  float jackTopY = 0.505f;
  float jackRowPitch = 0.0985f;
  float jackLeftX = 0.155f;
  float jackColPitch = 0.23f;
  float jackRadius = 0.047f;  // fraction of panel width (cell half-size)

  float brandingY = 0.028f;  // centre of "CALSYNTH  XLOC2" line
};

class PanelComponent : public juce::Component, private juce::Timer {
 public:
  explicit PanelComponent(EmuEngine& engine);
  ~PanelComponent() override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  bool keyStateChanged(bool isKeyDown) override;
  void parentHierarchyChanged() override;

 private:
  class Encoder;
  class PanelButton;
  class Jack;

  void timerCallback() override;
  void openRouting(JackId focus);
  juce::Rectangle<float> panelBounds() const;
  juce::Rectangle<int> place(juce::Point<float> centre, float radiusFrac) const;

  EmuEngine& engine_;
  PanelLayout layout_;

  OledComponent oled_;
  std::unique_ptr<Encoder> encL_, encR_;
  juce::OwnedArray<PanelButton> buttons_;
  juce::OwnedArray<Jack> jacks_;
  RoutingPanel routing_;
  juce::TextButton ioButton_{"I/O"};

  // keyboard state tracking (press AND release for held buttons)
  struct KeyBinding { int keyCode; int altKeyCode; int button; bool down; };
  std::array<KeyBinding, 7> keys_;
  bool lastAudioActive_ = true;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelComponent)
};
