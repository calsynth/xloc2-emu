#include "PanelComponent.h"

#include "../../core/emu.h"

namespace {
constexpr uint32_t kWindowBg = 0xff0b0d10;
constexpr uint32_t kPanelTop = 0xff20242b;
constexpr uint32_t kPanelBottom = 0xff15181d;
constexpr uint32_t kPanelEdge = 0xff31363f;
constexpr uint32_t kSilk = 0xffaeb6c2;
constexpr uint32_t kAccent = 0xff5a8dee;

juce::Colour meterColour(float volts) {
  const float mag = juce::jlimit(0.0f, 1.0f, std::abs(volts) / 10.0f);
  if (volts >= 0.0f)
    return juce::Colour(0xffffb04d).interpolatedWith(juce::Colour(0xffff5a3c), mag);
  return juce::Colour(0xff64c7ff).interpolatedWith(juce::Colour(0xff3c62ff), mag);
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

  void mouseWheelMove(const juce::MouseEvent&,
                      const juce::MouseWheelDetails& wheel) override {
    const int n = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (n != 0) turn(n);
  }

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 1.0f;

    // shadow / base
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(c.x - r, c.y - r + 2.0f, r * 2.0f, r * 2.0f);

    const float kr = pressed_ ? r * 0.94f : r * 0.97f;
    juce::ColourGradient grad(juce::Colour(0xff3a3f48), c.x - kr * 0.5f,
                              c.y - kr * 0.7f, juce::Colour(0xff181b20),
                              c.x + kr * 0.4f, c.y + kr * 0.9f, true);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);

    // knurl ticks
    g.setColour(juce::Colour(0xff0d0f12));
    for (int i = 0; i < 24; ++i) {
      const float a = angle_ + (float)i * juce::MathConstants<float>::twoPi / 24.0f;
      const float x1 = c.x + std::sin(a) * kr * 0.86f;
      const float y1 = c.y - std::cos(a) * kr * 0.86f;
      const float x2 = c.x + std::sin(a) * kr * 0.99f;
      const float y2 = c.y - std::cos(a) * kr * 0.99f;
      g.drawLine(x1, y1, x2, y2, 1.4f);
    }

    // top face + indicator
    g.setColour(juce::Colour(pressed_ ? 0xff23272e : 0xff2b3038));
    g.fillEllipse(c.x - kr * 0.78f, c.y - kr * 0.78f, kr * 1.56f, kr * 1.56f);
    g.setColour(juce::Colour(kAccent));
    const float ix = c.x + std::sin(angle_) * kr * 0.62f;
    const float iy = c.y - std::cos(angle_) * kr * 0.62f;
    g.drawLine(c.x + std::sin(angle_) * kr * 0.2f,
               c.y - std::cos(angle_) * kr * 0.2f, ix, iy, 2.5f);
  }

 private:
  void turn(int detents) {
    engine_.postEncoder(right_, detents);
    angle_ += (float)detents * juce::MathConstants<float>::twoPi / 24.0f;
    repaint();
  }

  EmuEngine& engine_;
  bool right_;
  bool pressed_ = false;
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

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const auto c = b.getCentre();
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;

    g.setColour(juce::Colour(0xff0e1013));  // bezel
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    const float cr = pressed_ ? r * 0.74f : r * 0.8f;
    juce::ColourGradient grad(
        juce::Colour(pressed_ ? 0xff2c313a : 0xff444a55), c.x - cr * 0.4f,
        c.y - cr * 0.6f, juce::Colour(pressed_ ? 0xff181b20 : 0xff23272e),
        c.x + cr * 0.4f, c.y + cr * 0.8f, true);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);

    if (pressed_) {
      g.setColour(juce::Colour(kAccent).withAlpha(0.8f));
      g.drawEllipse(c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f, 1.5f);
    }
  }

 private:
  EmuEngine& engine_;
  int button_;
  bool pressed_ = false;
};

// ---------------------------------------------------------------------------
// Jack — 3.5mm socket with a live meter ring; click opens routing
// ---------------------------------------------------------------------------
class PanelComponent::Jack : public juce::Component {
 public:
  enum class Kind { TrigIn, CvIn, CvOut };

  Jack(EmuEngine& engine, Kind kind, int index, JackId id,
       std::function<void(JackId)> onClick)
      : engine_(engine), kind_(kind), index_(index), id_(id),
        onClick_(std::move(onClick)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void updateMeter() {
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
    if (getLocalBounds().contains(e.getPosition()) && onClick_) onClick_(id_);
  }

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float cell = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;

    // meter ring
    const float mr = cell * 0.92f;
    g.setColour(juce::Colour(0xff23272e));
    g.drawEllipse(c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 2.0f);
    if (kind_ == Kind::TrigIn) {
      if (shownHigh_) {
        g.setColour(juce::Colour(0xff6cf07a));
        g.drawEllipse(c.x - mr, c.y - mr, mr * 2.0f, mr * 2.0f, 2.4f);
      }
    } else if (std::abs(shownVolts_) > 0.02f) {
      // 12 o'clock = 0 V; positive sweeps clockwise, negative anticlockwise
      const float sweep = juce::jlimit(-1.0f, 1.0f, shownVolts_ / 10.0f) *
                          juce::MathConstants<float>::pi;
      juce::Path arc;
      arc.addCentredArc(c.x, c.y, mr, mr, 0.0f, sweep < 0 ? sweep : 0.0f,
                        sweep < 0 ? 0.0f : sweep, true);
      g.setColour(meterColour(shownVolts_));
      g.strokePath(arc, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // hex nut
    const float nut = cell * 0.68f;
    juce::Path hex;
    hex.addPolygon({c.x, c.y}, 6, nut, juce::MathConstants<float>::pi / 6.0f);
    juce::ColourGradient grad(juce::Colour(0xff3d434d), c.x - nut, c.y - nut,
                              juce::Colour(0xff1a1d22), c.x + nut, c.y + nut,
                              false);
    g.setGradientFill(grad);
    g.fillPath(hex);
    g.setColour(juce::Colour(0xff0c0e11));
    g.strokePath(hex, juce::PathStrokeType(1.0f));

    // barrel + hole
    const float barrel = cell * 0.46f;
    g.setColour(juce::Colour(0xff2a2f37));
    g.fillEllipse(c.x - barrel, c.y - barrel, barrel * 2.0f, barrel * 2.0f);
    const float hole = cell * 0.30f;
    g.setColour(juce::Colours::black);
    g.fillEllipse(c.x - hole, c.y - hole, hole * 2.0f, hole * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(c.x - barrel, c.y - barrel, barrel * 2.0f, barrel * 2.0f, 1.0f);
  }

 private:
  EmuEngine& engine_;
  Kind kind_;
  int index_;
  JackId id_;
  std::function<void(JackId)> onClick_;
  float shownVolts_ = 0.0f;
  bool shownHigh_ = false;
};

// ---------------------------------------------------------------------------
// PanelComponent
// ---------------------------------------------------------------------------
PanelComponent::PanelComponent(EmuEngine& engine)
    : engine_(engine), oled_(engine), routing_(engine) {
  encL_ = std::make_unique<Encoder>(engine_, false);
  encR_ = std::make_unique<Encoder>(engine_, true);
  addAndMakeVisible(oled_);
  addAndMakeVisible(*encL_);
  addAndMakeVisible(*encR_);

  // order matches paint(): A, B, X, Y, Z
  for (int b : {emu::BUTTON_A, emu::BUTTON_B, emu::BUTTON_X, emu::BUTTON_Y,
                emu::BUTTON_Z})
    addAndMakeVisible(buttons_.add(new PanelButton(engine_, b)));

  auto onJack = [this](JackId id) { openRouting(id); };
  // grid order: row-major — TR1..4, CV in 1..4, CV in 5..8, OUT A..D, OUT E..H
  for (int i = 0; i < 4; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::TrigIn, i,
                        (JackId)((int)JackId::TrigIn1 + i), onJack));
  for (int i = 0; i < 8; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::CvIn, i,
                        (JackId)((int)JackId::CvIn1 + i), onJack));
  for (int i = 0; i < 8; ++i)
    jacks_.add(new Jack(engine_, Jack::Kind::CvOut, i,
                        (JackId)((int)JackId::CvOut1 + i), onJack));
  for (auto* j : jacks_) addAndMakeVisible(j);

  routing_.setVisible(false);
  routing_.onClose = [this] {
    routing_.setVisible(false);
    grabKeyboardFocus();
  };
  addChildComponent(routing_);

  ioButton_.setTooltip("Audio device & CV routing");
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
  setSize(660, 1040);
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
  auto area = getLocalBounds().toFloat().reduced(14.0f);
  float h = area.getHeight();
  float w = h * layout_.aspect;
  if (w > area.getWidth()) {
    w = area.getWidth();
    h = w / layout_.aspect;
  }
  return {area.getCentreX() - w * 0.5f, area.getCentreY() - h * 0.5f, w, h};
}

juce::Rectangle<int> PanelComponent::place(juce::Point<float> centre,
                                           float radiusFrac) const {
  const auto pb = panelBounds();
  const float r = radiusFrac * pb.getWidth();
  return juce::Rectangle<float>(pb.getX() + centre.x * pb.getWidth() - r,
                                pb.getY() + centre.y * pb.getHeight() - r,
                                r * 2.0f, r * 2.0f)
      .getSmallestIntegerContainer();
}

void PanelComponent::resized() {
  const auto pb = panelBounds();
  const auto& L = layout_;

  oled_.setBounds(juce::Rectangle<float>(pb.getX() + L.oled.getX() * pb.getWidth(),
                                         pb.getY() + L.oled.getY() * pb.getHeight(),
                                         L.oled.getWidth() * pb.getWidth(),
                                         L.oled.getHeight() * pb.getHeight())
                      .getSmallestIntegerContainer());

  encL_->setBounds(place(L.encL, L.encRadius));
  encR_->setBounds(place(L.encR, L.encRadius));

  const juce::Point<float> btnPos[5] = {L.btnA, L.btnB, L.btnX, L.btnY, L.btnZ};
  for (int i = 0; i < 5; ++i) buttons_[i]->setBounds(place(btnPos[i], L.btnRadius));

  for (int i = 0; i < jacks_.size(); ++i) {
    const int row = i / 4, col = i % 4;
    jacks_[i]->setBounds(place({L.jackLeftX + (float)col * L.jackColPitch,
                                L.jackTopY + (float)row * L.jackRowPitch},
                               L.jackRadius));
  }

  const int rw = juce::jmin(440, getWidth() - 30);
  routing_.setBounds(getWidth() - rw, 0, rw, getHeight());
  ioButton_.setBounds(getWidth() - 56, 6, 48, 22);
}

void PanelComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kWindowBg));
  const auto pb = panelBounds();
  const auto& L = layout_;

  // anodized panel
  juce::ColourGradient grad(juce::Colour(kPanelTop), pb.getX(), pb.getY(),
                            juce::Colour(kPanelBottom), pb.getX(), pb.getBottom(),
                            false);
  g.setGradientFill(grad);
  g.fillRoundedRectangle(pb, 6.0f);
  g.setColour(juce::Colour(kPanelEdge));
  g.drawRoundedRectangle(pb, 6.0f, 1.2f);

  // mounting screws
  const float sr = pb.getWidth() * 0.014f;
  const juce::Point<float> screws[4] = {
      {pb.getX() + sr * 3.0f, pb.getY() + sr * 3.0f},
      {pb.getRight() - sr * 3.0f, pb.getY() + sr * 3.0f},
      {pb.getX() + sr * 3.0f, pb.getBottom() - sr * 3.0f},
      {pb.getRight() - sr * 3.0f, pb.getBottom() - sr * 3.0f}};
  for (const auto& s : screws) {
    g.setColour(juce::Colour(0xff41474f));
    g.fillEllipse(s.x - sr, s.y - sr, sr * 2.0f, sr * 2.0f);
    g.setColour(juce::Colour(0xff14161a));
    g.drawLine(s.x - sr * 0.6f, s.y, s.x + sr * 0.6f, s.y, 1.4f);
  }

  // branding
  const float by = pb.getY() + L.brandingY * pb.getHeight();
  g.setColour(juce::Colour(kSilk));
  g.setFont(juce::Font(juce::FontOptions(pb.getWidth() * 0.030f)));
  g.drawText("CALSYNTH", (int)pb.getX(), (int)(by - 10.0f),
             (int)(pb.getWidth() * 0.5f), 20, juce::Justification::centred);
  g.setFont(juce::Font(juce::FontOptions(pb.getWidth() * 0.040f, juce::Font::bold)));
  g.drawText("XLOC2", (int)(pb.getX() + pb.getWidth() * 0.5f), (int)(by - 11.0f),
             (int)(pb.getWidth() * 0.5f), 22, juce::Justification::centred);

  // silk labels
  auto label = [&](juce::Point<float> centre, float belowFrac,
                   const juce::String& text, float size) {
    g.setFont(juce::Font(juce::FontOptions(size)));
    const float x = pb.getX() + centre.x * pb.getWidth();
    const float y = pb.getY() + centre.y * pb.getHeight() +
                    belowFrac * pb.getWidth();
    g.drawText(text, (int)(x - 40.0f), (int)y, 80, 14,
               juce::Justification::centred);
  };
  g.setColour(juce::Colour(kSilk));
  const float small = pb.getWidth() * 0.026f;
  label(L.btnA, L.btnRadius + 0.006f, "A", small);
  label(L.btnB, L.btnRadius + 0.006f, "B", small);
  label(L.btnX, L.btnRadius + 0.006f, "X", small);
  label(L.btnY, L.btnRadius + 0.006f, "Y", small);
  label(L.btnZ, L.btnRadius + 0.006f, "Z", small);

  static const char* jackNames[20] = {
      "TR1", "TR2", "TR3", "TR4", "CV1", "CV2", "CV3", "CV4",
      "CV5", "CV6", "CV7", "CV8", "A",   "B",   "C",   "D",
      "E",   "F",   "G",   "H"};
  g.setColour(juce::Colour(kSilk).withAlpha(0.85f));
  for (int i = 0; i < 20; ++i) {
    const int row = i / 4, col = i % 4;
    label({L.jackLeftX + (float)col * L.jackColPitch,
           L.jackTopY + (float)row * L.jackRowPitch},
          L.jackRadius - 0.004f, jackNames[i], small);
  }

  // clock-source status
  if (!lastAudioActive_) {
    g.setColour(juce::Colour(0xff8a93a1));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("no audio device - internal 1 kHz clock", 0,
               getHeight() - 20, getWidth(), 16, juce::Justification::centred);
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
