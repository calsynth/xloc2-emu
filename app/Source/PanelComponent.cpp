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
// Gesture model (mirrors the hardware):
//   drag                = plain turn (no push)
//   shift+drag          = push + turn
//   quick click         = short push (press, ~60 ms, release)
//   click & hold still  = long press (push held until mouse released)
//   mouse wheel / keys  = plain turns
class PanelComponent::Encoder : public juce::Component, private juce::Timer {
 public:
  Encoder(EmuEngine& engine, bool right) : engine_(engine), right_(right) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
  }

  void mouseDown(const juce::MouseEvent& e) override {
    gesture_ = Gesture::Undecided;
    dragAccumPx_ = 0.0f;
    movedPx_ = 0.0f;
    lastY_ = e.position.y;
    shiftAtDown_ = e.mods.isShiftDown();
    startTimer(300);  // stationary hold -> long press
    repaint();
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    const float dy = lastY_ - e.position.y;  // up = clockwise
    lastY_ = e.position.y;
    movedPx_ += std::abs(dy);
    if (gesture_ == Gesture::Undecided && movedPx_ > 4.0f) {
      stopTimer();
      if (shiftAtDown_) {
        gesture_ = Gesture::PushTurn;
        setPush(true);
      } else {
        gesture_ = Gesture::Turn;
      }
    }
    if (gesture_ != Gesture::Turn && gesture_ != Gesture::PushTurn &&
        gesture_ != Gesture::Hold)
      return;
    dragAccumPx_ += dy;
    constexpr float pxPerDetent = 11.0f;
    const int n = (int)(dragAccumPx_ / pxPerDetent);
    if (n != 0) {
      dragAccumPx_ -= (float)n * pxPerDetent;
      turn(n);
    }
  }

  void mouseUp(const juce::MouseEvent&) override {
    stopTimer();
    switch (gesture_) {
      case Gesture::Undecided: {
        // quick stationary click -> short push pulse
        setPush(true);
        auto safe = juce::Component::SafePointer<Encoder>(this);
        juce::Timer::callAfterDelay(60, [safe] {
          if (safe != nullptr) safe->setPush(false);
        });
        break;
      }
      case Gesture::PushTurn:
      case Gesture::Hold:
        setPush(false);
        break;
      default:
        break;
    }
    gesture_ = Gesture::Idle;
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

    // top face (no indicator line -- endless encoder, like the hardware)
    g.setColour(juce::Colour(pressed_ ? 0xff1d2126 : 0xff272b32));
    g.fillEllipse(c.x - kr * 0.78f, c.y - kr * 0.78f, kr * 1.56f, kr * 1.56f);

    if (hover_ || pressed_) {
      g.setColour(juce::Colour(kAccent).withAlpha(pressed_ ? 0.9f : 0.55f));
      g.drawEllipse(c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.6f);
    }
  }

 private:
  enum class Gesture { Idle, Undecided, Turn, PushTurn, Hold };

  void timerCallback() override {
    // held still past the threshold: this is a long press
    stopTimer();
    if (gesture_ == Gesture::Undecided) {
      gesture_ = Gesture::Hold;
      setPush(true);
    }
  }

  void setPush(bool down) {
    if (pushed_ == down) return;
    pushed_ = down;
    pressed_ = down;  // accent ring reflects what the firmware sees
    engine_.postButton(right_ ? emu::ENC_R_PUSH : emu::ENC_L_PUSH, down);
    repaint();
  }

  void turn(int detents) {
    engine_.postEncoder(right_, detents);
    angle_ += (float)detents * juce::MathConstants<float>::twoPi / 24.0f;
    repaint();
  }

  EmuEngine& engine_;
  bool right_;
  bool pressed_ = false, hover_ = false, pushed_ = false;
  bool shiftAtDown_ = false;
  Gesture gesture_ = Gesture::Idle;
  float lastY_ = 0.0f, dragAccumPx_ = 0.0f, movedPx_ = 0.0f;
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

  // Marks this jack as driven by an internal test-bench generator.
  void setInternal(bool b) {
    if (b != internal_) { internal_ = b; repaint(); }
  }
  Kind kind() const { return kind_; }
  int index() const { return index_; }

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

    // round knurled nut (metallic, reads on white)
    const float nut = cell * 0.68f;
    juce::ColourGradient grad(juce::Colour(0xffc7cbd1), c.x - nut, c.y - nut,
                              juce::Colour(0xff6f757e), c.x + nut, c.y + nut,
                              false);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - nut, c.y - nut, nut * 2.0f, nut * 2.0f);
    // knurling: short radial ticks around the rim
    g.setColour(juce::Colour(0xff575d66));
    for (int i = 0; i < 28; ++i) {
      const float a = (float)i * juce::MathConstants<float>::twoPi / 28.0f;
      g.drawLine(c.x + std::sin(a) * nut * 0.86f, c.y - std::cos(a) * nut * 0.86f,
                 c.x + std::sin(a) * nut * 0.985f, c.y - std::cos(a) * nut * 0.985f,
                 1.0f);
    }
    g.setColour(juce::Colour(0xff3a3e45));
    g.drawEllipse(c.x - nut, c.y - nut, nut * 2.0f, nut * 2.0f, 1.1f);

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

    // internal-source marker: violet dot when a test-bench generator drives
    // this jack (signal is internal, not from the audio interface)
    if (internal_) {
      const float dr = cell * 0.16f;
      const float dx = c.x + mr * 0.74f, dy = c.y - mr * 0.74f;
      g.setColour(juce::Colour(0xff9b5cf6));
      g.fillEllipse(dx - dr, dy - dr, dr * 2.0f, dr * 2.0f);
      g.setColour(juce::Colours::white.withAlpha(0.7f));
      g.drawEllipse(dx - dr, dy - dr, dr * 2.0f, dr * 2.0f, 0.8f);
    }
  }

 private:
  EmuEngine& engine_;
  Kind kind_;
  int index_;
  JackId id_;
  std::function<void(JackId)> onClick_;
  float shownVolts_ = 0.0f;
  bool shownHigh_ = false, hover_ = false, internal_ = false;
};

// ---------------------------------------------------------------------------
// PanelComponent
// ---------------------------------------------------------------------------
PanelComponent::PanelComponent(EmuEngine& engine)
    : engine_(engine), oled_(engine), routing_(engine), bench_(engine) {
  // Vector panel art (text outlined as paths -> crisp at any scale); the
  // PNG stays as a fallback if the SVG fails to parse.
  panelSvg_ = juce::Drawable::createFromImageData(BinaryData::panel_svg,
                                                  BinaryData::panel_svgSize);
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

  addAndMakeVisible(bench_);
  benchButton_.setTooltip("Show/hide the test bench (scope + generators)");
  benchButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2d33));
  benchButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3c62b0));
  benchButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9ced8));
  benchButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
  benchButton_.setClickingTogglesState(true);
  benchButton_.setToggleState(true, juce::dontSendNotification);
  benchButton_.onClick = [this] {
    benchVisible_ = benchButton_.getToggleState();
    bench_.setVisible(benchVisible_);
    resized();
    repaint();
  };
  addAndMakeVisible(benchButton_);

  keys_ = {{{'A', 'a', emu::BUTTON_A, false},
            {'B', 'b', emu::BUTTON_B, false},
            {'X', 'x', emu::BUTTON_X, false},
            {'Y', 'y', emu::BUTTON_Y, false},
            {'Z', 'z', emu::BUTTON_Z, false},
            {juce::KeyPress::returnKey, 0, emu::ENC_R_PUSH, false},
            {juce::KeyPress::escapeKey, 0, emu::ENC_L_PUSH, false}}};

  setWantsKeyboardFocus(true);
  setSize(naturalWidth(), naturalHeight());
  startTimerHz(30);
}

PanelComponent::~PanelComponent() = default;

void PanelComponent::parentHierarchyChanged() {
  if (isShowing()) grabKeyboardFocus();
}

void PanelComponent::timerCallback() {
  for (auto* j : jacks_) {
    j->updateMeter();
    if (j->kind() == Jack::Kind::CvIn)
      j->setInternal(engine_.genDrivesCvIn(j->index()));
    else if (j->kind() == Jack::Kind::TrigIn)
      j->setInternal(engine_.genDrivesTrig(j->index()));
  }
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

// The column height is driven by the sidebar's natural (scroll-free) content
// height; the panel scales so both columns are exactly equal height.
int PanelComponent::naturalWidth() const {
  const int colH = bench_.preferredHeight();
  const int panelW = juce::roundToInt((float)colH * PanelLayout::aspect);
  return 2 * kOuterMargin + panelW + kGap + kBenchW;
}

int PanelComponent::naturalHeight() const {
  return kOuterMargin + bench_.preferredHeight() + kBottomPad + kStripH;
}

juce::Rectangle<float> PanelComponent::panelBounds() const {
  // Both columns keep their natural size; extra window space becomes
  // symmetric margin so the two-column group stays centred and neat.
  const float ph = (float)bench_.preferredHeight();
  const float pw = (float)juce::roundToInt(ph * PanelLayout::aspect);
  const float groupW =
      pw + (benchVisible_ ? (float)(kGap + kBenchW) : 0.0f);
  const float x = juce::jmax((float)kOuterMargin,
                             ((float)getWidth() - groupW) * 0.5f);
  const float extraY =
      juce::jmax(0.0f, (float)(getHeight() - naturalHeight()));
  return {x, (float)kOuterMargin + extraY * 0.5f, pw, ph};
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

void PanelComponent::setRenderScale(float scale) {
  if (std::abs(scale - renderScale_) < 0.001f) return;
  renderScale_ = scale;
  refreshPanelCache();
  repaint();
}

// Rasterize the panel art at the EFFECTIVE pixel size — the natural-layout
// rect times the content-scaling transform (and any desktop scale) — so the
// cached image maps ~1:1 onto physical pixels and never blurs.
void PanelComponent::refreshPanelCache() {
  const auto pb = panelBounds();
  float deviceScale = 1.0f;
  if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    deviceScale = (float)d->scale;
  const float eff = juce::jmax(0.05f, renderScale_ * deviceScale);
  const int pw = juce::roundToInt(pb.getWidth() * eff);
  const int ph = juce::roundToInt(pb.getHeight() * eff);
  if (pw <= 0 || ph <= 0 ||
      (panelScaled_.getWidth() == pw && panelScaled_.getHeight() == ph))
    return;
  if (panelSvg_ != nullptr) {
    // rasterize the vector art at target size (the SVG has no background
    // rect, so paint the aluminum white first)
    juce::Image img(juce::Image::ARGB, pw, ph, true);
    juce::Graphics gi(img);
    gi.fillAll(juce::Colours::white);
    panelSvg_->drawWithin(gi,
                          juce::Rectangle<float>(0, 0, (float)pw, (float)ph),
                          juce::RectanglePlacement::stretchToFit, 1.0f);
    panelScaled_ = img;
  } else if (panelArt_.isValid()) {
    panelScaled_ =
        panelArt_.rescaled(pw, ph, juce::Graphics::highResamplingQuality);
  }
}

void PanelComponent::resized() {
  const auto pb = panelBounds();
  const auto& L = layout_;
  const float s = pb.getWidth() / PanelLayout::widthMm;

  refreshPanelCache();

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

  // sidebar: same y and height as the panel, across the gutter
  bench_.setBounds(juce::roundToInt(pb.getRight()) + kGap,
                   juce::roundToInt(pb.getY()), kBenchW,
                   juce::roundToInt(pb.getHeight()));

  const int rw = juce::jmin(440, getWidth() - 30);
  routing_.setBounds(getWidth() - rw, 0, rw, getHeight());

  // TEST / I/O chips centred in the bottom strip
  const int chipW = 70, chipH = 26, chipGap = 10;
  const int chipY = getHeight() - kStripH + (kStripH - chipH) / 2;
  const int chipX = (getWidth() - (2 * chipW + chipGap)) / 2;
  benchButton_.setBounds(chipX, chipY, chipW, chipH);
  ioButton_.setBounds(chipX + chipW + chipGap, chipY, chipW, chipH);
}

void PanelComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kWindowBg));
  const auto pb = panelBounds();

  if (panelScaled_.isValid()) {
    // Real panel artwork. The cache is rasterized at the effective pixel
    // size (natural px * content scale * desktop scale); drawing it into the
    // natural-size rect through the transform maps it ~1:1 onto physical
    // pixels, keeping the vector art crisp at any window scale.
    juce::Graphics::ScopedSaveState ss(g);
    g.setOpacity(1.0f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(panelScaled_, pb, juce::RectanglePlacement::stretchToFit);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(pb.expanded(1.0f), 1.0f);
  } else {
    g.setColour(juce::Colours::white);
    g.fillRect(pb);
  }

  // status line lives in the left corner of the bottom strip
  if (!lastAudioActive_) {
    g.setColour(juce::Colour(0xff8a93a1));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("no audio device - internal 1 kHz clock", 12,
               getHeight() - kStripH, juce::jmax(60, getWidth() / 2 - 100),
               kStripH, juce::Justification::centredLeft);
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
