#include "PanelComponent.h"

#include "BinaryData.h"
#include "../../core/emu.h"

namespace {
constexpr uint32_t kWindowBg = 0xff17181b;  // letterbox / status margin
constexpr uint32_t kAccent = 0xff2f6fed;    // focus/indicator blue

juce::Colour meterColour(float volts) {
  const float mag = juce::jlimit(0.0f, 1.0f, std::abs(volts) / 10.0f);
  if (volts >= 0.0f)
    return juce::Colour(0xffff9a1f).interpolatedWith(juce::Colour(0xffe83a1f), mag);
  return juce::Colour(0xff2f9fe8).interpolatedWith(juce::Colour(0xff2848e8), mag);
}
}  // namespace

// ---------------------------------------------------------------------------
// Encoder — knurled knob; drag/wheel = detents, press = encoder push
// ---------------------------------------------------------------------------
class PanelComponent::Encoder : public juce::Component {
 public:
  Encoder(EmuEngine& engine, bool right) : engine_(engine), right_(right) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
  }

  void mouseDown(const juce::MouseEvent& e) override {
    pressed_ = true;
    dragAccumPx_ = 0.0f;
    lastY_ = e.position.y;
    engine_.postButton(right_ ? emu::ENC_R_PUSH : emu::ENC_L_PUSH, true);
    repaint();
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    const float dy = lastY_ - e.position.y;  // up = clockwise
    lastY_ = e.position.y;
    dragAccumPx_ += dy;
    constexpr float pxPerDetent = 11.0f;
    const int n = (int)(dragAccumPx_ / pxPerDetent);
    if (n != 0) {
      dragAccumPx_ -= (float)n * pxPerDetent;
      turn(n);
    }
  }

  void mouseUp(const juce::MouseEvent&) override {
    pressed_ = false;
    engine_.postButton(right_ ? emu::ENC_R_PUSH : emu::ENC_L_PUSH, false);
    repaint();
  }

  void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

  void mouseWheelMove(const juce::MouseEvent&,
                      const juce::MouseWheelDetails& wheel) override {
    const int n = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (n != 0) turn(n);
  }

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 1.0f;

    // soft drop shadow onto the white panel
    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillEllipse(c.x - r, c.y - r + 2.5f, r * 2.0f, r * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillEllipse(c.x - r - 1.5f, c.y - r + 1.0f, r * 2.0f + 3.0f, r * 2.0f + 3.0f);

    const float kr = pressed_ ? r * 0.94f : r * 0.97f;
    juce::ColourGradient grad(juce::Colour(0xff3a3f48), c.x - kr * 0.5f,
                              c.y - kr * 0.7f, juce::Colour(0xff14161a),
                              c.x + kr * 0.4f, c.y + kr * 0.9f, true);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);

    // knurl ticks
    g.setColour(juce::Colour(0xff05060a));
    for (int i = 0; i < 24; ++i) {
      const float a = angle_ + (float)i * juce::MathConstants<float>::twoPi / 24.0f;
      const float x1 = c.x + std::sin(a) * kr * 0.86f;
      const float y1 = c.y - std::cos(a) * kr * 0.86f;
      const float x2 = c.x + std::sin(a) * kr * 0.99f;
      const float y2 = c.y - std::cos(a) * kr * 0.99f;
      g.drawLine(x1, y1, x2, y2, 1.4f);
    }

    // top face + indicator
    g.setColour(juce::Colour(pressed_ ? 0xff1d2126 : 0xff272b32));
    g.fillEllipse(c.x - kr * 0.78f, c.y - kr * 0.78f, kr * 1.56f, kr * 1.56f);
    g.setColour(juce::Colour(0xffe8ecf4));
    const float ix = c.x + std::sin(angle_) * kr * 0.66f;
    const float iy = c.y - std::cos(angle_) * kr * 0.66f;
    g.drawLine(c.x + std::sin(angle_) * kr * 0.2f,
               c.y - std::cos(angle_) * kr * 0.2f, ix, iy, 2.5f);

    if (hover_ || pressed_) {
      g.setColour(juce::Colour(kAccent).withAlpha(pressed_ ? 0.9f : 0.55f));
      g.drawEllipse(c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.6f);
    }
  }

 private:
  void turn(int detents) {
    engine_.postEncoder(right_, detents);
    angle_ += (float)detents * juce::MathConstants<float>::twoPi / 24.0f;
    repaint();
  }

  EmuEngine& engine_;
  bool right_;
  bool pressed_ = false, hover_ = false;
  float lastY_ = 0.0f, dragAccumPx_ = 0.0f;
  float angle_ = 0.0f;
};

// ---------------------------------------------------------------------------
// PanelButton — tactile round button, held while the mouse is down
// ---------------------------------------------------------------------------
class PanelComponent::PanelButton : public juce::Component {
 public:
  PanelButton(EmuEngine& engine, int emuButton) : engine_(engine), button_(emuButton) {}

  void mouseDown(const juce::MouseEvent&) override {
    pressed_ = true;
    engine_.postButton(button_, true);
    repaint();
  }
  void mouseUp(const juce::MouseEvent&) override {
    pressed_ = false;
    engine_.postButton(button_, false);
    repaint();
  }
  void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const auto c = b.getCentre();
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;

    // shadow + dark collar covering the printed hole
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillEllipse(c.x - r * 0.94f, c.y - r * 0.94f + 1.5f, r * 1.88f, r * 1.88f);
    g.setColour(juce::Colour(0xff23262b));
    g.fillEllipse(c.x - r * 0.92f, c.y - r * 0.92f, r * 1.84f, r * 1.84f);

    // light grey tactile cap; pressed = darker + inset
    const float cr = pressed_ ? r * 0.68f : r * 0.76f;
    const float cy = pressed_ ? c.y + 0.6f : c.y;
    juce::ColourGradient grad(
        juce::Colour(pressed_ ? 0xffa7abb2 : 0xffe4e6ea), c.x - cr * 0.4f,
        cy - cr * 0.6f, juce::Colour(pressed_ ? 0xff83878e : 0xffb9bdc4),
        c.x + cr * 0.4f, cy + cr * 0.8f, true);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - cr, cy - cr, cr * 2.0f, cr * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(pressed_ ? 0.55f : 0.35f));
    g.drawEllipse(c.x - cr, cy - cr, cr * 2.0f, cr * 2.0f, 1.0f);

    if (hover_ && !pressed_) {
      g.setColour(juce::Colour(kAccent).withAlpha(0.6f));
      g.drawEllipse(c.x - r * 0.92f, c.y - r * 0.92f, r * 1.84f, r * 1.84f, 1.4f);
    }
    if (pressed_) {
      g.setColour(juce::Colour(kAccent).withAlpha(0.85f));
      g.drawEllipse(c.x - r * 0.92f, c.y - r * 0.92f, r * 1.84f, r * 1.84f, 1.6f);
    }
  }

 private:
  EmuEngine& engine_;
  int button_;
  bool pressed_ = false, hover_ = false;
};

// ---------------------------------------------------------------------------
// Jack — 3.5mm Thonkiconn-style socket with a live meter ring; click opens
// routing. Passive kind (audio/MIDI) is visual-only for now.
// ---------------------------------------------------------------------------
class PanelComponent::Jack : public juce::Component,
                             public juce::SettableTooltipClient {
 public:
  enum class Kind { TrigIn, CvIn, CvOut, Passive };

  Jack(EmuEngine& engine, Kind kind, int index, JackId id,
       std::function<void(JackId)> onClick)
      : engine_(engine), kind_(kind), index_(index), id_(id),
        onClick_(std::move(onClick)) {
    if (kind_ == Kind::Passive)
      setTooltip("coming in a later phase");
    else
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void updateMeter() {
    if (kind_ == Kind::Passive) return;
    if (kind_ == Kind::TrigIn) {
      const bool h = engine_.trigInHigh(index_);
      if (h != shownHigh_) { shownHigh_ = h; repaint(); }
      return;
    }
    const float v = kind_ == Kind::CvOut ? engine_.cvOutVolts(index_)
                                         : engine_.cvInVolts(index_);
    if (std::abs(v - shownVolts_) > 0.05f) { shownVolts_ = v; repaint(); }
  }

  void mouseUp(const juce::MouseEvent& e) override {
    if (kind_ == Kind::Passive) return;
    if (getLocalBounds().contains(e.getPosition()) && onClick_) onClick_(id_);
  }
  void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
  void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float cell = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;

    // live meter ring (glow) around the nut
    const float mr = cell * 0.90f;
    const bool active = std::abs(shownVolts_) > 0.05f;
    if (kind_ == Kind::TrigIn) {
      if (shownHigh_) {
        g.setColour(juce::Colour(0xff17b83a).withAlpha(0.35f));
        g.drawEllipse(c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 5.0f);
        g.setColour(juce::Colour(0xff17b83a));
        g.drawEllipse(c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 2.2f);
      }
    } else if (kind_ != Kind::Passive && active) {
      // 12 o'clock = 0 V; positive sweeps clockwise, negative anticlockwise
      const float sweep = juce::jlimit(-1.0f, 1.0f, shownVolts_ / 10.0f) *
                          juce::MathConstants<float>::pi;
      juce::Path arc;
      arc.addCentredArc(c.x, c.y, mr, mr, 0.0f, sweep < 0 ? sweep : 0.0f,
                        sweep < 0 ? 0.0f : sweep, true);
      const auto col = meterColour(shownVolts_);
      g.setColour(col.withAlpha(0.30f));
      g.strokePath(arc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
      g.setColour(col);
      g.strokePath(arc, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // hex nut (metallic, reads on white)
    const float nut = cell * 0.72f;
    juce::Path hex;
    hex.addPolygon({c.x, c.y}, 6, nut, juce::MathConstants<float>::pi / 6.0f);
    juce::ColourGradient grad(juce::Colour(0xffc7cbd1), c.x - nut, c.y - nut,
                              juce::Colour(0xff6f757e), c.x + nut, c.y + nut,
                              false);
    g.setGradientFill(grad);
    g.fillPath(hex);
    g.setColour(juce::Colour(0xff3a3e45));
    g.strokePath(hex, juce::PathStrokeType(1.1f));

    // barrel + hole
    const float barrel = cell * 0.46f;
    g.setColour(juce::Colour(0xff2a2f37));
    g.fillEllipse(c.x - barrel, c.y - barrel, barrel * 2.0f, barrel * 2.0f);
    const float hole = cell * 0.30f;
    g.setColour(juce::Colours::black);
    g.fillEllipse(c.x - hole, c.y - hole, hole * 2.0f, hole * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawEllipse(c.x - barrel, c.y - barrel, barrel * 2.0f, barrel * 2.0f, 1.0f);

    if (hover_ && kind_ != Kind::Passive) {
      g.setColour(juce::Colour(kAccent).withAlpha(0.55f));
      g.drawEllipse(c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 1.6f);
    }
  }

 private:
  EmuEngine& engine_;
  Kind kind_;
  int index_;
  JackId id_;
  std::function<void(JackId)> onClick_;
  float shownVolts_ = 0.0f;
  bool shownHigh_ = false, hover_ = false;
};

// ---------------------------------------------------------------------------
// PanelComponent
// ---------------------------------------------------------------------------
PanelComponent::PanelComponent(EmuEngine& engine)
    : engine_(engine), oled_(engine), routing_(engine) {
  panelArt_ = juce::ImageCache::getFromMemory(BinaryData::panel_png,
                                              BinaryData::panel_pngSize);

  encL_ = std::make_unique<Encoder>(engine_, false);
  encR_ = std::make_unique<Encoder>(engine_, true);
  addAndMakeVisible(oled_);
  addAndMakeVisible(*encL_);
  addAndMakeVisible(*encR_);

  // order matches resized(): A, B, X, Y, Z
  for (int b : {emu::BUTTON_A, emu::BUTTON_B, emu::BUTTON_X, emu::BUTTON_Y,
                emu::BUTTON_Z})
    addAndMakeVisible(buttons_.add(new PanelButton(engine_, b)));

  auto onJack = [this](JackId id) { openRouting(id); };
  // active jacks, column-major to match resized():
  // TRIG 1-4, CV IN 1-4, CV IN 5-8, CV OUT A-D, CV OUT E-H
  for (int i = 0; i < 4; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::TrigIn, i,
                        (JackId)((int)JackId::TrigIn1 + i), onJack));
  for (int i = 0; i < 8; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::CvIn, i,
                        (JackId)((int)JackId::CvIn1 + i), onJack));
  for (int i = 0; i < 8; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::CvOut, i,
                        (JackId)((int)JackId::CvOut1 + i), onJack));
  // passive (visual-only) jacks: AUDIO IN L/R, OUT L/R, MIDI IN/OUT
  for (int i = 0; i < 6; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::Passive, i, JackId::None, nullptr));
  for (auto* j : jacks_) addAndMakeVisible(j);

  routing_.setVisible(false);
  routing_.onClose = [this] {
    routing_.setVisible(false);
    grabKeyboardFocus();
  };
  addChildComponent(routing_);

  ioButton_.setTooltip("Audio device & CV routing");
  ioButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2d33));
  ioButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3f48));
  ioButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9ced8));
  ioButton_.onClick = [this] { openRouting(JackId::None); };
  addAndMakeVisible(ioButton_);

  keys_ = {{{'A', 'a', emu::BUTTON_A, false},
            {'B', 'b', emu::BUTTON_B, false},
            {'X', 'x', emu::BUTTON_X, false},
            {'Y', 'y', emu::BUTTON_Y, false},
            {'Z', 'z', emu::BUTTON_Z, false},
            {juce::KeyPress::returnKey, 0, emu::ENC_R_PUSH, false},
            {juce::KeyPress::escapeKey, 0, emu::ENC_L_PUSH, false}}};

  setWantsKeyboardFocus(true);
  setSize(650, 1000);
  startTimerHz(30);
}

PanelComponent::~PanelComponent() = default;

void PanelComponent::parentHierarchyChanged() {
  if (isShowing()) grabKeyboardFocus();
}

void PanelComponent::timerCallback() {
  for (auto* j : jacks_) j->updateMeter();
  const bool active = engine_.audioDeviceActive();
  if (active != lastAudioActive_) {
    lastAudioActive_ = active;
    repaint();
  }
}

void PanelComponent::openRouting(JackId focus) {
  routing_.setVisible(true);
  routing_.toFront(false);
  if (focus != JackId::None) routing_.focusJack(focus);
}

juce::Rectangle<float> PanelComponent::panelBounds() const {
  // reserve a status strip at the bottom; small margins elsewhere
  auto area = getLocalBounds().toFloat();
  area.removeFromBottom(26.0f);
  area = area.reduced(6.0f);
  float h = area.getHeight();
  float w = h * PanelLayout::aspect;
  if (w > area.getWidth()) {
    w = area.getWidth();
    h = w / PanelLayout::aspect;
  }
  return {area.getCentreX() - w * 0.5f, area.getCentreY() - h * 0.5f, w, h};
}

juce::Rectangle<int> PanelComponent::placeMm(juce::Point<float> centreMm,
                                             float radiusMm) const {
  const auto pb = panelBounds();
  const float s = pb.getWidth() / PanelLayout::widthMm;  // px per mm
  const float r = radiusMm * s;
  return juce::Rectangle<float>(pb.getX() + centreMm.x * s - r,
                                pb.getY() + centreMm.y * s - r,
                                r * 2.0f, r * 2.0f)
      .getSmallestIntegerContainer();
}

void PanelComponent::resized() {
  const auto pb = panelBounds();
  const auto& L = layout_;
  const float s = pb.getWidth() / PanelLayout::widthMm;

  // cache the artwork scaled to the current panel rect
  const int pw = juce::roundToInt(pb.getWidth());
  const int ph = juce::roundToInt(pb.getHeight());
  if (panelArt_.isValid() && pw > 0 && ph > 0 &&
      (panelScaled_.getWidth() != pw || panelScaled_.getHeight() != ph))
    panelScaled_ = panelArt_.rescaled(pw, ph, juce::Graphics::highResamplingQuality);

  oled_.setBounds(juce::Rectangle<float>(pb.getX() + L.oled.getX() * s,
                                         pb.getY() + L.oled.getY() * s,
                                         L.oled.getWidth() * s,
                                         L.oled.getHeight() * s)
                      .getSmallestIntegerContainer());

  encL_->setBounds(placeMm(L.encL, L.encRadius));
  encR_->setBounds(placeMm(L.encR, L.encRadius));

  const juce::Point<float> btnPos[5] = {L.btnA, L.btnB, L.btnX, L.btnY, L.btnZ};
  for (int i = 0; i < 5; ++i)
    buttons_[i]->setBounds(placeMm(btnPos[i], L.btnRadius));

  // active jacks: 5 columns x 4 rows, column-major (matches constructor)
  const float cols[5] = {L.colTrig, L.colCvIn14, L.colCvIn58, L.colCvOutAD,
                         L.colCvOutEH};
  int ji = 0;
  for (int col = 0; col < 5; ++col)
    for (int row = 0; row < 4; ++row)
      jacks_[ji++]->setBounds(placeMm({cols[col], L.jackRowY[row]}, L.jackRadius));
  // passive: AUDIO IN L/R, OUT L/R then MIDI IN/OUT
  for (int row = 0; row < 4; ++row)
    jacks_[ji++]->setBounds(placeMm({L.colAudio, L.jackRowY[row]}, L.jackRadius));
  jacks_[ji++]->setBounds(placeMm(L.midiIn, L.jackRadius));
  jacks_[ji++]->setBounds(placeMm(L.midiOut, L.jackRadius));

  const int rw = juce::jmin(440, getWidth() - 30);
  routing_.setBounds(getWidth() - rw, 0, rw, getHeight());
  ioButton_.setBounds(getWidth() - 54, getHeight() - 23, 46, 19);
}

void PanelComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kWindowBg));
  const auto pb = panelBounds();

  if (panelScaled_.isValid()) {
    // real panel artwork, pre-scaled 1:1 onto the panel rect
    g.setOpacity(1.0f);
    g.drawImageAt(panelScaled_, juce::roundToInt(pb.getX()),
                  juce::roundToInt(pb.getY()));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(pb.expanded(1.0f), 1.0f);
  } else {
    g.setColour(juce::Colours::white);
    g.fillRect(pb);
  }

  // status line lives in the reserved bottom margin, never over the art
  if (!lastAudioActive_) {
    g.setColour(juce::Colour(0xff8a93a1));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("no audio device - internal 1 kHz clock", 0, getHeight() - 22,
               getWidth(), 16, juce::Justification::centred);
  }
}

bool PanelComponent::keyPressed(const juce::KeyPress& key) {
  const int code = key.getKeyCode();
  if (code == juce::KeyPress::upKey) { engine_.postEncoder(true, 1); return true; }
  if (code == juce::KeyPress::downKey) { engine_.postEncoder(true, -1); return true; }
  if (code == juce::KeyPress::rightKey) { engine_.postEncoder(false, 1); return true; }
  if (code == juce::KeyPress::leftKey) { engine_.postEncoder(false, -1); return true; }
  // presses/releases of mapped buttons are handled in keyStateChanged();
  // claim the key here so it doesn't beep or reach the window
  for (const auto& k : keys_)
    if (code == k.keyCode || (k.altKeyCode != 0 && code == k.altKeyCode))
      return true;
  return false;
}

bool PanelComponent::keyStateChanged(bool) {
  bool any = false;
  for (auto& k : keys_) {
    const bool down =
        juce::KeyPress::isKeyCurrentlyDown(k.keyCode) ||
        (k.altKeyCode != 0 && juce::KeyPress::isKeyCurrentlyDown(k.altKeyCode));
    if (down != k.down) {
      k.down = down;
      engine_.postButton(k.button, down);
      any = true;
    }
  }
  return any;
}
