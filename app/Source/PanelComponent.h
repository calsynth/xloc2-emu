// PanelComponent — photo-faithful XLOC2 front panel.
//
// The background is the real panel artwork (app/Assets/panel.png, embedded
// via BinaryData) drawn 1:1 onto the panel rect. All control geometry lives
// in PanelLayout in MILLIMETRES from the panel's top-left corner (panel is
// 80.90 x 128.50 mm) and is converted to pixels against the fitted panel
// rect, so widgets land exactly on the printed holes.
//
// Interaction:
//   encoders   vertical drag or mouse wheel = detents; press = encoder push
//              (down on mouseDown, up on mouseUp — so drag-while-pressed
//              behaves like the hardware's push+turn)
//   buttons    A/X/B/Y/Z click (down/up tracked)
//   keyboard   Up/Down = right encoder, Left/Right = left encoder,
//              Return = right push, Escape = left push, A/B/X/Y/Z = buttons
//   jacks      CV/TRIG sockets show live meter rings; click opens the
//              routing panel focused on that jack. Audio/MIDI jacks are
//              visual-only until a later phase (tooltip says so).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "EmuEngine.h"
#include "OledComponent.h"
#include "RoutingPanel.h"
#include "TestBenchPanel.h"

// All panel geometry in one place, in millimetres from the panel top-left.
// Values supplied by Calsynth to match the production panel artwork.
struct PanelLayout {
  static constexpr float widthMm = 80.90f;
  static constexpr float heightMm = 128.50f;
  static constexpr float aspect = widthMm / heightMm;

  // Screen aperture (black rect in the art); OLED fills it.
  juce::Rectangle<float> oled{21.35f, 15.68f, 38.19f, 19.99f};

  // 5.1 mm button holes (letter printed beside each in the art)
  juce::Point<float> btnA{12.51f, 14.97f}, btnB{68.38f, 14.97f};
  juce::Point<float> btnX{12.51f, 30.21f}, btnY{68.38f, 30.21f};
  juce::Point<float> btnZ{40.44f, 51.79f};
  float btnRadius = 3.6f;  // component half-size, mm (cap + shadow)

  // Encoder shafts; the knob overhangs the 7.2-7.5 mm hole (~13 mm dia)
  juce::Point<float> encL{12.50f, 49.88f}, encR{68.37f, 49.88f};
  float encRadius = 7.3f;  // component half-size, mm

  // Jack columns (6.2 mm holes) and the shared 4-row grid
  float colTrig = 17.90f;
  float colCvIn14 = 30.28f, colCvIn58 = 40.44f;
  float colCvOutAD = 52.82f, colCvOutEH = 62.98f;
  float colAudio = 75.68f;
  float jackRowY[4] = {75.91f, 87.97f, 100.03f, 112.10f};

  // MIDI (visual-only)
  juce::Point<float> midiIn{5.05f, 100.03f}, midiOut{5.21f, 112.10f};

  float jackRadius = 5.0f;  // component half-size, mm (nut + meter ring)
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
  int benchWidth() const;  // current test-bench sidebar width (0 = collapsed)
  juce::Rectangle<float> panelBounds() const;
  // mm-centre + mm half-size -> pixel bounds on the fitted panel rect
  juce::Rectangle<int> placeMm(juce::Point<float> centreMm, float radiusMm) const;

  EmuEngine& engine_;
  PanelLayout layout_;

  std::unique_ptr<juce::Drawable> panelSvg_;  // vector artwork (preferred)
  juce::Image panelArt_;     // raster artwork fallback (BinaryData PNG)
  juce::Image panelScaled_;  // cached render for the current panel rect

  OledComponent oled_;
  std::unique_ptr<Encoder> encL_, encR_;
  juce::OwnedArray<PanelButton> buttons_;
  juce::OwnedArray<Jack> jacks_;
  RoutingPanel routing_;
  TestBenchPanel bench_;
  bool benchVisible_ = true;
  juce::TextButton ioButton_{"I/O"};
  juce::TextButton benchButton_{"TEST"};
  juce::TooltipWindow tooltips_{this};

  // keyboard state tracking (press AND release for held buttons)
  struct KeyBinding { int keyCode; int altKeyCode; int button; bool down; };
  std::array<KeyBinding, 7> keys_;
  bool lastAudioActive_ = true;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelComponent)
};
