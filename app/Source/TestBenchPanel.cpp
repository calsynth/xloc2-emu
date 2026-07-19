#include "TestBenchPanel.h"

#include "RoutingPanel.h"  // xloc2::routingFile()

// ---------------------------------------------------------------------------
// JSON persistence (routing.json, key "testbench")
// ---------------------------------------------------------------------------
namespace xloc2 {

static juce::var benchToVar(const TestBenchConfig& cfg) {
  auto* tb = new juce::DynamicObject();
  tb->setProperty("scopeSource", cfg.scopeSource);
  tb->setProperty("scopeWindow", cfg.scopeWindow);
  tb->setProperty("scopeAutoZoom", cfg.scopeAutoZoom);
  tb->setProperty("scopeSync", cfg.scopeSync);
  juce::Array<juce::var> cv;
  for (const auto& g : cfg.cv) {
    auto* o = new juce::DynamicObject();
    o->setProperty("enabled", g.enabled);
    o->setProperty("dest", g.dest);
    o->setProperty("wave", g.wave);
    o->setProperty("freqHz", g.freqHz);
    o->setProperty("minVolts", g.minVolts);
    o->setProperty("maxVolts", g.maxVolts);
    cv.add(juce::var(o));
  }
  tb->setProperty("cv", juce::var(cv));
  juce::Array<juce::var> trig;
  for (const auto& g : cfg.trig) {
    auto* o = new juce::DynamicObject();
    o->setProperty("enabled", g.enabled);
    o->setProperty("dest", g.dest);
    o->setProperty("rateHz", g.rateHz);
    o->setProperty("pulseMs", g.pulseMs);
    trig.add(juce::var(o));
  }
  tb->setProperty("trig", juce::var(trig));
  return juce::var(tb);
}

void saveTestBench(const TestBenchConfig& cfg) {
  // Read-modify-write: RoutingPanel owns the other keys in this file.
  auto file = routingFile();
  juce::var root = juce::JSON::parse(file.loadFileAsString());
  juce::DynamicObject* o = root.getDynamicObject();
  if (o == nullptr) {
    o = new juce::DynamicObject();
    root = juce::var(o);
  }
  o->setProperty("testbench", benchToVar(cfg));
  file.getParentDirectory().createDirectory();
  file.replaceWithText(juce::JSON::toString(root));
}

bool loadTestBench(TestBenchConfig& cfg) {
  auto file = routingFile();
  if (!file.existsAsFile()) return false;
  auto root = juce::JSON::parse(file.loadFileAsString());
  auto* o = root.getDynamicObject();
  if (o == nullptr) return false;
  auto* tb = o->getProperty("testbench").getDynamicObject();
  if (tb == nullptr) return false;

  auto num = [](juce::DynamicObject* obj, const char* name, float fb) {
    const auto p = obj->getProperty(name);
    return p.isVoid() ? fb : (float)(double)p;
  };
  cfg.scopeSource = (int)tb->getProperty("scopeSource");
  cfg.scopeWindow = (int)tb->getProperty("scopeWindow");
  cfg.scopeAutoZoom = (bool)tb->getProperty("scopeAutoZoom");
  cfg.scopeSync = (bool)tb->getProperty("scopeSync");
  if (auto* a = tb->getProperty("cv").getArray())
    for (int i = 0; i < 8 && i < a->size(); ++i)
      if (auto* g = a->getReference(i).getDynamicObject()) {
        auto& d = cfg.cv[(size_t)i];
        d.enabled = (bool)g->getProperty("enabled");
        d.dest = (int)g->getProperty("dest");
        d.wave = (int)g->getProperty("wave");
        d.freqHz = num(g, "freqHz", d.freqHz);
        d.minVolts = num(g, "minVolts", d.minVolts);
        d.maxVolts = num(g, "maxVolts", d.maxVolts);
      }
  if (auto* a = tb->getProperty("trig").getArray())
    for (int i = 0; i < 4 && i < a->size(); ++i)
      if (auto* g = a->getReference(i).getDynamicObject()) {
        auto& d = cfg.trig[(size_t)i];
        d.enabled = (bool)g->getProperty("enabled");
        d.dest = (int)g->getProperty("dest");
        d.rateHz = num(g, "rateHz", d.rateHz);
        d.pulseMs = num(g, "pulseMs", d.pulseMs);
      }
  return true;
}

}  // namespace xloc2

namespace {
constexpr uint32_t kBg = 0xff14171d;
constexpr uint32_t kScopeBg = 0xff0c0f14;
constexpr uint32_t kGrid = 0xff232a35;
constexpr uint32_t kGridZero = 0xff3a4454;
constexpr uint32_t kTrace = 0xff4ade80;
constexpr uint32_t kText = 0xffc9d0dc;
constexpr uint32_t kDim = 0xff8791a1;

const char* kScopeSourceNames[20] = {
    "CV In 1", "CV In 2", "CV In 3", "CV In 4", "CV In 5", "CV In 6",
    "CV In 7", "CV In 8", "CV Out A", "CV Out B", "CV Out C", "CV Out D",
    "CV Out E", "CV Out F", "CV Out G", "CV Out H", "TR 1", "TR 2", "TR 3",
    "TR 4"};

const double kWindowSeconds[5] = {0.010, 0.050, 0.200, 1.0, 5.0};
const char* kWindowNames[5] = {"10 ms", "50 ms", "200 ms", "1 s", "5 s"};

const char* kWaveNames[9] = {"Sine",       "Triangle",   "Saw Up",
                             "Saw Down",   "Square",     "S&H Random",
                             "Smooth Rnd", "White Noise", "DC"};

void styleChip(juce::TextButton& b) {
  b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222731));
  b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3c62b0));
  b.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
  b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void styleTitle(juce::Label& l, const char* text) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(juce::FontOptions(12.0f, juce::Font::bold));
  l.setColour(juce::Label::textColourId, juce::Colour(kDim));
}
}  // namespace

// ---------------------------------------------------------------------------
// ScopeDisplay — waveform view + numeric readout
// ---------------------------------------------------------------------------
class TestBenchPanel::ScopeDisplay : public juce::Component,
                                     private juce::Timer {
 public:
  explicit ScopeDisplay(EmuEngine& engine) : engine_(engine) {
    startTimerHz(30);
  }

  void setWindowSeconds(double s) { windowS_ = s; }
  void setSync(bool b) { sync_ = b; }
  void setAutoZoom(bool b) { autoZoom_ = b; repaint(); }
  void setFrozen(bool b) { frozen_ = b; }

 private:
  void timerCallback() override {
    if (frozen_) return;
    double dt = 0.0;
    // probe dt first, then fetch up to two windows so the rising-edge sync
    // has pre-roll to search
    float probe;
    engine_.readScope(&probe, 1, dt);
    if (dt <= 0.0) return;
    const int winN = juce::jmax(16, (int)std::llround(windowS_ / dt));
    const int fetchN = juce::jmin(winN * 2, 1 << 18);
    if ((int)raw_.size() < fetchN) raw_.resize((size_t)fetchN);
    const int got = engine_.readScope(raw_.data(), fetchN, dt);
    if (got < 2) return;

    // stats + live readout use the newest data
    current_ = raw_[(size_t)(got - 1)];

    int start = juce::jmax(0, got - winN);  // default: latest window
    if (sync_ && got > winN) {
      float lo = raw_[0], hi = raw_[0];
      for (int i = 0; i < got; ++i) {
        lo = juce::jmin(lo, raw_[(size_t)i]);
        hi = juce::jmax(hi, raw_[(size_t)i]);
      }
      if (hi - lo > 0.05f) {  // only sync on non-flat signals
        const float level = (lo + hi) * 0.5f;
        for (int i = got - winN; i > 0; --i) {
          if (raw_[(size_t)(i - 1)] < level && raw_[(size_t)i] >= level) {
            start = i;
            break;
          }
        }
      }
    }
    const int n = juce::jmin(winN, got - start);
    disp_.assign(raw_.begin() + start, raw_.begin() + start + n);
    dispMin_ = dispMax_ = disp_.empty() ? 0.0f : disp_[0];
    for (float v : disp_) {
      dispMin_ = juce::jmin(dispMin_, v);
      dispMax_ = juce::jmax(dispMax_, v);
    }
    repaint();
  }

  void paint(juce::Graphics& g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(kScopeBg));
    g.fillRoundedRectangle(b, 4.0f);

    const auto plot = b.reduced(6.0f);
    // vertical range
    float vmin = -10.0f, vmax = 10.0f;
    if (autoZoom_ && dispMax_ > dispMin_) {
      const float pad = juce::jmax(0.25f, (dispMax_ - dispMin_) * 0.12f);
      vmin = dispMin_ - pad;
      vmax = dispMax_ + pad;
    }
    auto yOf = [&](float v) {
      return plot.getBottom() -
             (v - vmin) / (vmax - vmin) * plot.getHeight();
    };

    // grid: 10 time divisions, volts lines at nice steps
    g.setColour(juce::Colour(kGrid));
    for (int i = 1; i < 10; ++i) {
      const float x = plot.getX() + plot.getWidth() * (float)i / 10.0f;
      g.drawLine(x, plot.getY(), x, plot.getBottom(), 0.6f);
    }
    const float span = vmax - vmin;
    float step = 2.5f;
    while (span / step > 9.0f) step *= 2.0f;
    while (span / step < 3.0f && step > 0.01f) step *= 0.5f;
    for (float v = std::ceil(vmin / step) * step; v <= vmax; v += step) {
      if (std::abs(v) < step * 0.25f) continue;  // zero drawn separately
      const float y = yOf(v);
      g.drawLine(plot.getX(), y, plot.getRight(), y, 0.6f);
    }
    if (vmin < 0.0f && vmax > 0.0f) {
      g.setColour(juce::Colour(kGridZero));
      const float y0 = yOf(0.0f);
      g.drawLine(plot.getX(), y0, plot.getRight(), y0, 1.0f);
    }

    // trace
    const int n = (int)disp_.size();
    if (n >= 2) {
      g.setColour(juce::Colour(kTrace));
      const int w = juce::jmax(1, (int)plot.getWidth());
      if (n <= w * 2) {
        juce::Path p;
        for (int i = 0; i < n; ++i) {
          const float x =
              plot.getX() + plot.getWidth() * (float)i / (float)(n - 1);
          const float y = yOf(disp_[(size_t)i]);
          if (i == 0) p.startNewSubPath(x, y);
          else p.lineTo(x, y);
        }
        g.strokePath(p, juce::PathStrokeType(1.6f,
                                             juce::PathStrokeType::curved));
      } else {
        // min/max per pixel column (classic scope decimation)
        for (int px = 0; px < w; ++px) {
          const int i0 = (int)((int64_t)px * n / w);
          const int i1 = juce::jmax(i0 + 1, (int)((int64_t)(px + 1) * n / w));
          float lo = disp_[(size_t)i0], hi = lo;
          for (int i = i0; i < i1 && i < n; ++i) {
            lo = juce::jmin(lo, disp_[(size_t)i]);
            hi = juce::jmax(hi, disp_[(size_t)i]);
          }
          const float x = plot.getX() + (float)px;
          g.drawLine(x, yOf(hi) - 0.5f, x, yOf(lo) + 0.5f, 1.2f);
        }
      }
    }

    // numeric readout
    g.setColour(juce::Colour(kText));
    g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    g.drawText(juce::String(current_, 2) + " V",
               (int)plot.getX(), (int)plot.getY() + 2, (int)plot.getWidth() - 6,
               26, juce::Justification::topRight);
    g.setColour(juce::Colour(kDim));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("min " + juce::String(dispMin_, 2) + "   max " +
                   juce::String(dispMax_, 2),
               (int)plot.getX(), (int)plot.getY() + 28,
               (int)plot.getWidth() - 6, 14, juce::Justification::topRight);
    if (frozen_) {
      g.setColour(juce::Colour(0xff7fb2ff));
      g.drawText("FROZEN", (int)plot.getX() + 4, (int)plot.getY() + 2, 80, 14,
                 juce::Justification::topLeft);
    }
    if (autoZoom_) {
      g.setColour(juce::Colour(kDim));
      g.drawText(juce::String(vmin, 1) + " .. " + juce::String(vmax, 1) + " V",
                 (int)plot.getX() + 4, (int)plot.getBottom() - 16, 140, 14,
                 juce::Justification::bottomLeft);
    }
  }

  EmuEngine& engine_;
  std::vector<float> raw_, disp_;
  double windowS_ = 1.0;
  bool sync_ = true, autoZoom_ = false, frozen_ = false;
  float current_ = 0.0f, dispMin_ = 0.0f, dispMax_ = 0.0f;
};

// ---------------------------------------------------------------------------
// CvGenRow — one CV/audio-rate generator (two-line row)
// ---------------------------------------------------------------------------
class TestBenchPanel::CvGenRow : public juce::Component {
 public:
  CvGenRow(TestBenchPanel& owner, int index) : owner_(owner), index_(index) {
    idx_.setText(juce::String(index + 1), juce::dontSendNotification);
    idx_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    idx_.setColour(juce::Label::textColourId, juce::Colour(kDim));
    addAndMakeVisible(idx_);

    en_.onClick = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(en_);

    dest_.onChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(dest_);

    for (int w = 0; w < 9; ++w) wave_.addItem(kWaveNames[w], w + 1);
    wave_.setSelectedId(1, juce::dontSendNotification);
    wave_.onChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(wave_);

    freq_.setSliderStyle(juce::Slider::LinearHorizontal);
    freq_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 18);
    freq_.setRange(0.01, 5000.0, 0.0);
    freq_.setSkewFactorFromMidPoint(10.0);
    freq_.setNumDecimalPlacesToDisplay(2);
    freq_.setTextValueSuffix(" Hz");
    freq_.setValue(1.0, juce::dontSendNotification);
    freq_.onValueChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(freq_);

    auto initVolt = [this](juce::Slider& s, double v, const char* tip) {
      s.setSliderStyle(juce::Slider::LinearBar);
      s.setRange(-10.0, 10.0, 0.01);
      s.setNumDecimalPlacesToDisplay(2);
      s.setTextValueSuffix(" V");
      s.setValue(v, juce::dontSendNotification);
      s.setTooltip(tip);
      s.setColour(juce::Slider::trackColourId, juce::Colour(0xff2b3242));
      s.onValueChange = [this] { owner_.pushToEngine(); };
      addAndMakeVisible(s);
    };
    initVolt(min_, -5.0, "Range minimum (volts)");
    initVolt(max_, 5.0, "Range maximum (volts); DC level for DC waveform");

    auto initPreset = [this](juce::TextButton& b, const char* name,
                             double lo, double hi, const char* tip) {
      b.setButtonText(name);
      b.setTooltip(tip);
      styleChip(b);
      b.onClick = [this, lo, hi] {
        min_.setValue(lo, juce::dontSendNotification);
        max_.setValue(hi, juce::dontSendNotification);
        owner_.pushToEngine();
      };
      addAndMakeVisible(b);
    };
    initPreset(uni_, "0..5", 0.0, 5.0, "Unipolar 0..5 V");
    initPreset(bi_, "+-5", -5.0, 5.0, "Bipolar +-5 V");
    initPreset(full_, "+-10", -10.0, 10.0, "Full range +-10 V");
  }

  CvGenConfig get() const {
    CvGenConfig g;
    g.enabled = en_.getToggleState();
    g.dest = dest_.getSelectedId() - 2;  // id 1 = None = -1
    g.wave = wave_.getSelectedId() - 1;
    g.freqHz = (float)freq_.getValue();
    g.minVolts = (float)min_.getValue();
    g.maxVolts = (float)max_.getValue();
    if (g.maxVolts < g.minVolts) std::swap(g.minVolts, g.maxVolts);
    return g;
  }

  void set(const CvGenConfig& g) {
    en_.setToggleState(g.enabled, juce::dontSendNotification);
    pendingDest_ = g.dest;
    wave_.setSelectedId(juce::jlimit(0, 8, g.wave) + 1,
                        juce::dontSendNotification);
    freq_.setValue(g.freqHz, juce::dontSendNotification);
    min_.setValue(g.minVolts, juce::dontSendNotification);
    max_.setValue(g.maxVolts, juce::dontSendNotification);
  }

  int selectedDest() const {
    return pendingDest_ != -2 ? pendingDest_ : dest_.getSelectedId() - 2;
  }

  // Rebuild destination list, marking jacks taken by other rows.
  void setDestItems(uint32_t takenMask) {
    const int keep = selectedDest();
    pendingDest_ = -2;
    dest_.clear(juce::dontSendNotification);
    dest_.addItem("None", 1);
    for (int i = 0; i < 8; ++i) {
      const bool taken = ((takenMask >> i) & 1u) != 0 && i != keep;
      dest_.addItem("CV In " + juce::String(i + 1) +
                        (taken ? juce::String(" *") : juce::String()),
                    i + 2);
    }
    dest_.setSelectedId(keep + 2, juce::dontSendNotification);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(0, 1);
    auto l1 = r.removeFromTop(r.getHeight() / 2);
    en_.setBounds(l1.removeFromLeft(22));
    idx_.setBounds(l1.removeFromLeft(14));
    dest_.setBounds(l1.removeFromLeft(84).reduced(1));
    wave_.setBounds(l1.removeFromLeft(92).reduced(1));
    freq_.setBounds(l1.reduced(1, 0));
    r.removeFromLeft(36);  // indent line 2
    min_.setBounds(r.removeFromLeft(72).reduced(1, 1));
    max_.setBounds(r.removeFromLeft(72).reduced(1, 1));
    r.removeFromLeft(6);
    uni_.setBounds(r.removeFromLeft(42).reduced(1, 1));
    bi_.setBounds(r.removeFromLeft(38).reduced(1, 1));
    full_.setBounds(r.removeFromLeft(42).reduced(1, 1));
  }

 private:
  TestBenchPanel& owner_;
  int index_;
  int pendingDest_ = -2;  // dest cached between set() and setDestItems()
  juce::ToggleButton en_;
  juce::Label idx_;
  juce::ComboBox dest_, wave_;
  juce::Slider freq_, min_, max_;
  juce::TextButton uni_, bi_, full_;
};

// ---------------------------------------------------------------------------
// TrigGenRow — one trigger generator (single-line row)
// ---------------------------------------------------------------------------
class TestBenchPanel::TrigGenRow : public juce::Component {
 public:
  TrigGenRow(TestBenchPanel& owner, EmuEngine& engine, int index)
      : owner_(owner), engine_(engine), index_(index) {
    idx_.setText(juce::String(index + 1), juce::dontSendNotification);
    idx_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    idx_.setColour(juce::Label::textColourId, juce::Colour(kDim));
    addAndMakeVisible(idx_);

    en_.onClick = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(en_);

    dest_.onChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(dest_);

    rate_.setSliderStyle(juce::Slider::LinearHorizontal);
    rate_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
    rate_.setRange(0.05, 100.0, 0.0);
    rate_.setSkewFactorFromMidPoint(4.0);
    rate_.setNumDecimalPlacesToDisplay(2);
    rate_.setTextValueSuffix(" Hz");
    rate_.setValue(2.0, juce::dontSendNotification);
    rate_.onValueChange = [this] {
      updateBpm();
      owner_.pushToEngine();
    };
    addAndMakeVisible(rate_);

    bpm_.setFont(juce::FontOptions(10.0f));
    bpm_.setColour(juce::Label::textColourId, juce::Colour(kDim));
    addAndMakeVisible(bpm_);

    pulse_.setSliderStyle(juce::Slider::LinearBar);
    pulse_.setRange(1.0, 100.0, 1.0);
    pulse_.setTextValueSuffix(" ms");
    pulse_.setValue(10.0, juce::dontSendNotification);
    pulse_.setTooltip("Pulse duration");
    pulse_.setColour(juce::Slider::trackColourId, juce::Colour(0xff2b3242));
    pulse_.onValueChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(pulse_);

    fire_.setTooltip("Manual single trigger (works even when disabled)");
    styleChip(fire_);
    fire_.onClick = [this] { engine_.postTrigFire(index_); };
    addAndMakeVisible(fire_);

    updateBpm();
  }

  TrigGenConfig get() const {
    TrigGenConfig g;
    g.enabled = en_.getToggleState();
    g.dest = dest_.getSelectedId() - 2;
    g.rateHz = (float)rate_.getValue();
    g.pulseMs = (float)pulse_.getValue();
    return g;
  }

  void set(const TrigGenConfig& g) {
    en_.setToggleState(g.enabled, juce::dontSendNotification);
    pendingDest_ = g.dest;
    rate_.setValue(g.rateHz, juce::dontSendNotification);
    pulse_.setValue(g.pulseMs, juce::dontSendNotification);
    updateBpm();
  }

  int selectedDest() const {
    return pendingDest_ != -2 ? pendingDest_ : dest_.getSelectedId() - 2;
  }

  void setDestItems(uint32_t takenMask) {
    const int keep = selectedDest();
    pendingDest_ = -2;
    dest_.clear(juce::dontSendNotification);
    dest_.addItem("None", 1);
    for (int i = 0; i < 4; ++i) {
      const bool taken = ((takenMask >> i) & 1u) != 0 && i != keep;
      dest_.addItem("TR " + juce::String(i + 1) +
                        (taken ? juce::String(" *") : juce::String()),
                    i + 2);
    }
    dest_.setSelectedId(keep + 2, juce::dontSendNotification);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(0, 1);
    en_.setBounds(r.removeFromLeft(22));
    idx_.setBounds(r.removeFromLeft(14));
    dest_.setBounds(r.removeFromLeft(66).reduced(1));
    fire_.setBounds(r.removeFromRight(38).reduced(1, 1));
    pulse_.setBounds(r.removeFromRight(60).reduced(1, 1));
    bpm_.setBounds(r.removeFromRight(52));
    rate_.setBounds(r.reduced(1, 0));
  }

 private:
  void updateBpm() {
    const double hz = rate_.getValue();
    bpm_.setText(hz >= 0.5 && hz <= 10.0
                     ? juce::String(juce::roundToInt(hz * 60.0)) + " BPM"
                     : juce::String(),
                 juce::dontSendNotification);
  }

  TestBenchPanel& owner_;
  EmuEngine& engine_;
  int index_;
  int pendingDest_ = -2;
  juce::ToggleButton en_;
  juce::Label idx_, bpm_;
  juce::ComboBox dest_;
  juce::Slider rate_, pulse_;
  juce::TextButton fire_{"Fire"};
};

// ---------------------------------------------------------------------------
// TestBenchPanel
// ---------------------------------------------------------------------------
TestBenchPanel::TestBenchPanel(EmuEngine& engine) : engine_(engine) {
  styleTitle(scopeTitle_, "SCOPE");
  styleTitle(cvTitle_, "CV GENERATORS");
  styleTitle(trigTitle_, "TRIGGER GENERATORS");
  addAndMakeVisible(scopeTitle_);
  addAndMakeVisible(cvTitle_);
  addAndMakeVisible(trigTitle_);

  for (int i = 0; i < 20; ++i) scopeSource_.addItem(kScopeSourceNames[i], i + 1);
  scopeSource_.setSelectedId(9, juce::dontSendNotification);  // CV Out A
  scopeSource_.onChange = [this] { pushToEngine(); };
  addAndMakeVisible(scopeSource_);

  for (int i = 0; i < 5; ++i) scopeWindow_.addItem(kWindowNames[i], i + 1);
  scopeWindow_.setSelectedId(4, juce::dontSendNotification);  // 1 s
  scopeWindow_.onChange = [this] { pushToEngine(); };
  addAndMakeVisible(scopeWindow_);

  for (auto* b : {&freezeButton_, &syncButton_, &zoomButton_}) {
    b->setClickingTogglesState(true);
    styleChip(*b);
    addAndMakeVisible(*b);
  }
  freezeButton_.setTooltip("Freeze the display");
  freezeButton_.onClick = [this] { applyScopeSettings(); };
  syncButton_.setTooltip("Rising-edge sync (stable periodic waveforms)");
  syncButton_.setToggleState(true, juce::dontSendNotification);
  syncButton_.onClick = [this] { pushToEngine(); };
  zoomButton_.setTooltip("Auto-zoom the voltage scale (off = fixed +-10 V)");
  zoomButton_.onClick = [this] { pushToEngine(); };

  scope_ = std::make_unique<ScopeDisplay>(engine_);
  addAndMakeVisible(*scope_);

  syncAllButton_.setTooltip("Reset every generator's phase to 0");
  styleChip(syncAllButton_);
  syncAllButton_.onClick = [this] { engine_.postGenSync(); };
  addAndMakeVisible(syncAllButton_);

  for (int i = 0; i < 8; ++i)
    addAndMakeVisible(cvRows_.add(new CvGenRow(*this, i)));
  for (int i = 0; i < 4; ++i)
    addAndMakeVisible(trigRows_.add(new TrigGenRow(*this, engine_, i)));

  loadFromConfig(engine_.getTestBench());
}

TestBenchPanel::~TestBenchPanel() = default;

void TestBenchPanel::loadFromConfig(const TestBenchConfig& cfg) {
  updating_ = true;
  scopeSource_.setSelectedId(juce::jlimit(0, 19, cfg.scopeSource) + 1,
                             juce::dontSendNotification);
  scopeWindow_.setSelectedId(juce::jlimit(0, 4, cfg.scopeWindow) + 1,
                             juce::dontSendNotification);
  zoomButton_.setToggleState(cfg.scopeAutoZoom, juce::dontSendNotification);
  syncButton_.setToggleState(cfg.scopeSync, juce::dontSendNotification);
  for (int i = 0; i < 8; ++i) cvRows_[i]->set(cfg.cv[(size_t)i]);
  for (int i = 0; i < 4; ++i) trigRows_[i]->set(cfg.trig[(size_t)i]);
  refreshDestMarks();
  updating_ = false;
  applyScopeSettings();
}

void TestBenchPanel::applyScopeSettings() {
  scope_->setWindowSeconds(
      kWindowSeconds[juce::jlimit(0, 4, scopeWindow_.getSelectedId() - 1)]);
  scope_->setSync(syncButton_.getToggleState());
  scope_->setAutoZoom(zoomButton_.getToggleState());
  scope_->setFrozen(freezeButton_.getToggleState());
}

void TestBenchPanel::refreshDestMarks() {
  uint32_t cvTaken = 0, trigTaken = 0;
  for (auto* r : cvRows_) {
    const int d = r->selectedDest();
    if (d >= 0) cvTaken |= 1u << d;
  }
  for (auto* r : trigRows_) {
    const int d = r->selectedDest();
    if (d >= 0) trigTaken |= 1u << d;
  }
  for (auto* r : cvRows_) r->setDestItems(cvTaken);
  for (auto* r : trigRows_) r->setDestItems(trigTaken);
}

void TestBenchPanel::pushToEngine() {
  if (updating_) return;
  TestBenchConfig cfg;
  cfg.scopeSource = scopeSource_.getSelectedId() - 1;
  cfg.scopeWindow = scopeWindow_.getSelectedId() - 1;
  cfg.scopeAutoZoom = zoomButton_.getToggleState();
  cfg.scopeSync = syncButton_.getToggleState();
  for (int i = 0; i < 8; ++i) cfg.cv[(size_t)i] = cvRows_[i]->get();
  for (int i = 0; i < 4; ++i) cfg.trig[(size_t)i] = trigRows_[i]->get();
  engine_.setTestBench(cfg);
  xloc2::saveTestBench(cfg);
  refreshDestMarks();
  applyScopeSettings();
}

void TestBenchPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kBg));
  g.setColour(juce::Colour(0xff2a3140));
  g.fillRect(0, 0, 1, getHeight());
}

void TestBenchPanel::resized() {
  auto r = getLocalBounds().reduced(10, 8);

  scopeTitle_.setBounds(r.removeFromTop(16));
  auto c1 = r.removeFromTop(24);
  scopeSource_.setBounds(c1.removeFromLeft(120).reduced(1));
  scopeWindow_.setBounds(c1.removeFromLeft(82).reduced(1));
  freezeButton_.setBounds(c1.removeFromLeft(62).reduced(1, 1));
  syncButton_.setBounds(c1.removeFromLeft(52).reduced(1, 1));
  zoomButton_.setBounds(c1.removeFromLeft(52).reduced(1, 1));
  r.removeFromTop(4);
  scope_->setBounds(r.removeFromTop(juce::jmin(230, r.getHeight() / 3)));
  r.removeFromTop(8);

  auto h1 = r.removeFromTop(20);
  syncAllButton_.setBounds(h1.removeFromRight(70).reduced(0, 1));
  cvTitle_.setBounds(h1);
  const int cvRowH = juce::jlimit(38, 46, (r.getHeight() - 150) / 8);
  for (auto* row : cvRows_) row->setBounds(r.removeFromTop(cvRowH));
  r.removeFromTop(8);

  trigTitle_.setBounds(r.removeFromTop(18));
  for (auto* row : trigRows_)
    row->setBounds(r.removeFromTop(juce::jmin(26, r.getHeight() / 4)));
}
