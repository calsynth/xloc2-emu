#include "TestBenchPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>  // AudioFormatManager

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
  {
    auto* o = new juce::DynamicObject();
    o->setProperty("path", cfg.wav.path);
    o->setProperty("dest", cfg.wav.dest);
    o->setProperty("peakVolts", cfg.wav.peakVolts);
    o->setProperty("loop", cfg.wav.loop);
    o->setProperty("playing", cfg.wav.playing);
    tb->setProperty("wav", juce::var(o));
  }
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
  if (auto* w = tb->getProperty("wav").getDynamicObject()) {
    cfg.wav.path = w->getProperty("path").toString();
    cfg.wav.dest = (int)w->getProperty("dest");
    cfg.wav.peakVolts = num(w, "peakVolts", cfg.wav.peakVolts);
    cfg.wav.loop = (bool)w->getProperty("loop");
    cfg.wav.playing = (bool)w->getProperty("playing");
  }
  return true;
}

std::shared_ptr<const WavData> decodeAudioFile(const juce::File& f) {
  if (!f.existsAsFile()) return nullptr;
  juce::AudioFormatManager fm;
  fm.registerBasicFormats();  // wav, aiff, flac, ogg, mp3 (per platform)
  std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
  if (reader == nullptr || reader->lengthInSamples <= 0 ||
      reader->numChannels == 0)
    return nullptr;
  // cap at 10 minutes to keep memory sane; test files are small
  const auto len = (int)juce::jmin<juce::int64>(
      reader->lengthInSamples, (juce::int64)(reader->sampleRate * 600.0));
  juce::AudioBuffer<float> buf((int)reader->numChannels, len);
  reader->read(&buf, 0, len, 0, true, true);
  auto wd = std::make_shared<WavData>();
  wd->sampleRate = reader->sampleRate;
  wd->mono.resize((size_t)len);
  const float scale = 1.0f / (float)buf.getNumChannels();
  for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
    const float* src = buf.getReadPointer(ch);
    for (int i = 0; i < len; ++i)
      wd->mono[(size_t)i] += src[i] * scale;  // mono mix
  }
  return wd;
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
    const int dec = step < 1.0f ? (step < 0.25f ? 2 : 1) : 0;
    auto voltLabel = [&](float v, float y) {
      // gridline label on the left edge, skipped when it would collide
      // with the top readout area or the bottom edge
      if (y < plot.getY() + 8.0f || y > plot.getBottom() - 6.0f) return;
      juce::String t = juce::String(v, dec);
      if (v > 0.0f) t = "+" + t;
      g.setFont(juce::Font(juce::FontOptions(10.0f)));
      g.drawText(t, (int)plot.getX() + 3, (int)(y - 11.0f), 40, 11,
                 juce::Justification::bottomLeft);
    };
    for (float v = std::ceil(vmin / step) * step; v <= vmax; v += step) {
      if (std::abs(v) < step * 0.25f) continue;  // zero drawn separately
      const float y = yOf(v);
      g.drawLine(plot.getX(), y, plot.getRight(), y, 0.6f);
      g.setColour(juce::Colour(kDim).withAlpha(0.75f));
      voltLabel(v, y);
      g.setColour(juce::Colour(kGrid));
    }
    if (vmin < 0.0f && vmax > 0.0f) {
      g.setColour(juce::Colour(kGridZero));
      const float y0 = yOf(0.0f);
      g.drawLine(plot.getX(), y0, plot.getRight(), y0, 1.0f);
      g.setColour(juce::Colour(kDim));
      voltLabel(0.0f, y0);
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
    // time-axis extent, bottom-right
    {
      const juce::String t =
          windowS_ < 1.0
              ? "0 .. " + juce::String(juce::roundToInt(windowS_ * 1000.0)) +
                    " ms"
              : "0 .. " + juce::String(windowS_, windowS_ < 10.0 ? 0 : 0) +
                    " s";
      g.setColour(juce::Colour(kDim));
      g.setFont(juce::Font(juce::FontOptions(10.0f)));
      g.drawText(t, (int)plot.getRight() - 90, (int)plot.getBottom() - 13, 86,
                 12, juce::Justification::bottomRight);
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
    auto r = getLocalBounds().reduced(0, 2);
    auto l1 = r.removeFromTop(r.getHeight() / 2);
    en_.setBounds(l1.removeFromLeft(22));
    idx_.setBounds(l1.removeFromLeft(14));
    dest_.setBounds(l1.removeFromLeft(86).reduced(1));
    l1.removeFromLeft(4);
    wave_.setBounds(l1.removeFromLeft(96).reduced(1));
    l1.removeFromLeft(4);
    freq_.setBounds(l1.reduced(1, 0));
    r.removeFromTop(2);    // small gap between the two lines
    r.removeFromLeft(36);  // indent line 2
    min_.setBounds(r.removeFromLeft(74).reduced(1, 1));
    r.removeFromLeft(3);
    max_.setBounds(r.removeFromLeft(74).reduced(1, 1));
    r.removeFromLeft(14);  // group separation before the presets
    uni_.setBounds(r.removeFromLeft(46).reduced(1, 1));
    r.removeFromLeft(2);
    bi_.setBounds(r.removeFromLeft(42).reduced(1, 1));
    r.removeFromLeft(2);
    full_.setBounds(r.removeFromLeft(46).reduced(1, 1));
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
    auto r = getLocalBounds().reduced(0, 2);
    en_.setBounds(r.removeFromLeft(22));
    idx_.setBounds(r.removeFromLeft(14));
    dest_.setBounds(r.removeFromLeft(70).reduced(1));
    r.removeFromLeft(4);
    fire_.setBounds(r.removeFromRight(42).reduced(1, 1));
    r.removeFromRight(4);
    pulse_.setBounds(r.removeFromRight(62).reduced(1, 1));
    r.removeFromRight(4);
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
  styleTitle(wavTitle_, "WAV PLAYER");
  for (auto* l : {&scopeTitle_, &cvTitle_, &trigTitle_, &wavTitle_})
    content_.addAndMakeVisible(*l);

  for (int i = 0; i < 20; ++i) scopeSource_.addItem(kScopeSourceNames[i], i + 1);
  scopeSource_.setSelectedId(9, juce::dontSendNotification);  // CV Out A
  scopeSource_.onChange = [this] { pushToEngine(); };
  content_.addAndMakeVisible(scopeSource_);

  for (int i = 0; i < 5; ++i) scopeWindow_.addItem(kWindowNames[i], i + 1);
  scopeWindow_.setSelectedId(4, juce::dontSendNotification);  // 1 s
  scopeWindow_.onChange = [this] { pushToEngine(); };
  content_.addAndMakeVisible(scopeWindow_);

  for (auto* b : {&freezeButton_, &syncButton_, &zoomButton_}) {
    b->setClickingTogglesState(true);
    styleChip(*b);
    content_.addAndMakeVisible(*b);
  }
  freezeButton_.setTooltip("Freeze the display");
  freezeButton_.onClick = [this] { applyScopeSettings(); };
  syncButton_.setTooltip("Rising-edge sync (stable periodic waveforms)");
  syncButton_.setToggleState(true, juce::dontSendNotification);
  syncButton_.onClick = [this] { pushToEngine(); };
  zoomButton_.setTooltip("Auto-zoom the voltage scale (off = fixed +-10 V)");
  zoomButton_.onClick = [this] { pushToEngine(); };

  scope_ = std::make_unique<ScopeDisplay>(engine_);
  content_.addAndMakeVisible(*scope_);

  syncAllButton_.setTooltip("Reset every generator's phase to 0");
  styleChip(syncAllButton_);
  syncAllButton_.onClick = [this] { engine_.postGenSync(); };
  content_.addAndMakeVisible(syncAllButton_);

  for (int i = 0; i < 8; ++i)
    content_.addAndMakeVisible(cvRows_.add(new CvGenRow(*this, i)));
  for (int i = 0; i < 4; ++i)
    content_.addAndMakeVisible(trigRows_.add(new TrigGenRow(*this, engine_, i)));

  // ---- wav player ----
  styleChip(wavLoadButton_);
  wavLoadButton_.onClick = [this] { chooseWavFile(); };
  content_.addAndMakeVisible(wavLoadButton_);

  wavFileLabel_.setText("(no file)", juce::dontSendNotification);
  wavFileLabel_.setFont(juce::FontOptions(11.0f));
  wavFileLabel_.setColour(juce::Label::textColourId, juce::Colour(kText));
  wavFileLabel_.setMinimumHorizontalScale(0.7f);
  content_.addAndMakeVisible(wavFileLabel_);

  wavPlayButton_.setClickingTogglesState(true);
  styleChip(wavPlayButton_);
  wavPlayButton_.onClick = [this] {
    wavPlayButton_.setButtonText(wavPlayButton_.getToggleState() ? "Pause"
                                                                 : "Play");
    // restart from the top when playing again after the file ended
    if (wavPlayButton_.getToggleState() && !wavLoopButton_.getToggleState() &&
        engine_.wavLengthSeconds() > 0.0 &&
        engine_.wavPositionSeconds() >= engine_.wavLengthSeconds() - 1e-6)
      engine_.wavSeekSeconds(0.0);
    pushToEngine();
  };
  content_.addAndMakeVisible(wavPlayButton_);

  styleChip(wavStopButton_);
  wavStopButton_.onClick = [this] {
    wavPlayButton_.setToggleState(false, juce::dontSendNotification);
    wavPlayButton_.setButtonText("Play");
    engine_.wavSeekSeconds(0.0);
    pushToEngine();
  };
  content_.addAndMakeVisible(wavStopButton_);

  wavLoopButton_.setClickingTogglesState(true);
  styleChip(wavLoopButton_);
  wavLoopButton_.onClick = [this] { pushToEngine(); };
  content_.addAndMakeVisible(wavLoopButton_);

  wavPos_.setSliderStyle(juce::Slider::LinearHorizontal);
  wavPos_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  wavPos_.setRange(0.0, 1.0, 0.0);
  wavPos_.onValueChange = [this] {
    if (wavPos_.isMouseButtonDown())  // user scrub, not timer update
      engine_.wavSeekSeconds(wavPos_.getValue());
  };
  content_.addAndMakeVisible(wavPos_);

  wavTimeLabel_.setFont(juce::FontOptions(11.0f));
  wavTimeLabel_.setColour(juce::Label::textColourId, juce::Colour(kDim));
  wavTimeLabel_.setJustificationType(juce::Justification::centredRight);
  content_.addAndMakeVisible(wavTimeLabel_);

  wavDestLabel_.setText("to", juce::dontSendNotification);
  wavDestLabel_.setFont(juce::FontOptions(11.0f));
  wavDestLabel_.setColour(juce::Label::textColourId, juce::Colour(kDim));
  content_.addAndMakeVisible(wavDestLabel_);

  wavDest_.addItem("None", 1);
  for (int i = 0; i < 8; ++i)
    wavDest_.addItem("CV In " + juce::String(i + 1), i + 2);
  wavDest_.addItem("Audio In L", 10);
  wavDest_.addItem("Audio In R", 11);
  wavDest_.setItemEnabled(10, false);  // audio applets in a later phase
  wavDest_.setItemEnabled(11, false);
  wavDest_.setTooltip("Destination jack (Audio In: audio applets in a later phase)");
  wavDest_.setSelectedId(1, juce::dontSendNotification);
  wavDest_.onChange = [this] { pushToEngine(); };
  content_.addAndMakeVisible(wavDest_);

  wavLevelLabel_.setText("peak", juce::dontSendNotification);
  wavLevelLabel_.setFont(juce::FontOptions(11.0f));
  wavLevelLabel_.setColour(juce::Label::textColourId, juce::Colour(kDim));
  content_.addAndMakeVisible(wavLevelLabel_);

  wavLevel_.setSliderStyle(juce::Slider::LinearBar);
  wavLevel_.setRange(0.1, 10.0, 0.1);
  wavLevel_.setNumDecimalPlacesToDisplay(1);
  wavLevel_.setTextValueSuffix(" V");
  wavLevel_.setValue(5.0, juce::dontSendNotification);
  wavLevel_.setTooltip("Full-scale sample maps to +- this many volts");
  wavLevel_.setColour(juce::Slider::trackColourId, juce::Colour(0xff2b3242));
  wavLevel_.onValueChange = [this] { pushToEngine(); };
  content_.addAndMakeVisible(wavLevel_);

  monitorButton_.setClickingTogglesState(true);
  styleChip(monitorButton_);
  monitorButton_.setTooltip(
      "Hear what the module hears on the computer's speakers "
      "(separate output-only device)");
  monitorButton_.onClick = [this] {
    engine_.setMonitorEnabled(monitorButton_.getToggleState());
    refreshMonitorDevices();
  };
  content_.addAndMakeVisible(monitorButton_);

  monitorVol_.setSliderStyle(juce::Slider::LinearHorizontal);
  monitorVol_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  monitorVol_.setRange(0.0, 1.0, 0.0);
  monitorVol_.setValue(0.7, juce::dontSendNotification);
  monitorVol_.setTooltip("Monitor volume");
  monitorVol_.onValueChange = [this] {
    engine_.setMonitorVolume((float)monitorVol_.getValue());
  };
  content_.addAndMakeVisible(monitorVol_);

  monitorDevice_.setTooltip("Monitor output device");
  monitorDevice_.setTextWhenNoChoicesAvailable("(no output device)");
  monitorDevice_.onChange = [this] {
    if (updating_) return;
    auto setup = engine_.monitorDeviceManager().getAudioDeviceSetup();
    setup.outputDeviceName = monitorDevice_.getText();
    engine_.monitorDeviceManager().setAudioDeviceSetup(setup, true);
  };
  content_.addAndMakeVisible(monitorDevice_);

  addAndMakeVisible(content_);

  loadFromConfig(engine_.getTestBench());

  // restore the last wav file (decode on the message thread; files are small)
  {
    const auto cfg = engine_.getTestBench();
    if (cfg.wav.path.isNotEmpty()) setWavFile(juce::File(cfg.wav.path));
  }

  startTimerHz(10);
}

TestBenchPanel::~TestBenchPanel() = default;

void TestBenchPanel::timerCallback() {
  // wav position + time readout
  const double len = engine_.wavLengthSeconds();
  const double pos = engine_.wavPositionSeconds();
  if (len > 0.0) {
    if (!wavPos_.isMouseButtonDown()) {
      if (std::abs(wavPos_.getMaximum() - len) > 1e-9)
        wavPos_.setRange(0.0, len, 0.0);
      wavPos_.setValue(juce::jmin(pos, len), juce::dontSendNotification);
    }
    auto fmt = [](double s) {
      const int m = (int)s / 60;
      return juce::String(m) + ":" +
             juce::String(s - m * 60.0, 1).paddedLeft('0', 4);
    };
    wavTimeLabel_.setText(fmt(pos) + " / " + fmt(len),
                          juce::dontSendNotification);
  } else {
    wavTimeLabel_.setText(juce::String(), juce::dontSendNotification);
  }

  // monitor availability feedback
  if (monitorButton_.getToggleState() && !engine_.monitorAvailable())
    monitorButton_.setButtonText("Monitor (no device)");
  else
    monitorButton_.setButtonText("Monitor");
}

void TestBenchPanel::chooseWavFile() {
  chooser_ = std::make_unique<juce::FileChooser>(
      "Load audio file", wavFile_.exists() ? wavFile_.getParentDirectory()
                                           : juce::File(),
      "*.wav;*.aif;*.aiff;*.flac");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles,
                        [this](const juce::FileChooser& fc) {
                          const auto f = fc.getResult();
                          if (f.existsAsFile()) {
                            setWavFile(f);
                            pushToEngine();
                          }
                        });
}

void TestBenchPanel::setWavFile(const juce::File& f) {
  auto data = xloc2::decodeAudioFile(f);
  if (data == nullptr) {
    wavFileLabel_.setText("failed: " + f.getFileName(),
                          juce::dontSendNotification);
    return;
  }
  wavFile_ = f;
  const double secs = (double)data->mono.size() / data->sampleRate;
  wavFileLabel_.setText(f.getFileName() + "  (" + juce::String(secs, 1) +
                            " s, " + juce::String(data->sampleRate / 1000.0, 1) +
                            " kHz)",
                        juce::dontSendNotification);
  engine_.setWavData(std::move(data));
}

void TestBenchPanel::refreshMonitorDevices() {
  updating_ = true;
  monitorDevice_.clear(juce::dontSendNotification);
  if (auto* type = engine_.monitorDeviceManager().getCurrentDeviceTypeObject()) {
    const auto names = type->getDeviceNames(false);  // output devices
    for (int i = 0; i < names.size(); ++i)
      monitorDevice_.addItem(names[i], i + 1);
    if (auto* dev = engine_.monitorDeviceManager().getCurrentAudioDevice())
      monitorDevice_.setText(dev->getName(), juce::dontSendNotification);
  }
  updating_ = false;
}

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
  wavDest_.setSelectedId(juce::jlimit(-1, 7, cfg.wav.dest) + 2,
                         juce::dontSendNotification);
  wavLevel_.setValue(cfg.wav.peakVolts, juce::dontSendNotification);
  wavLoopButton_.setToggleState(cfg.wav.loop, juce::dontSendNotification);
  wavPlayButton_.setToggleState(cfg.wav.playing, juce::dontSendNotification);
  wavPlayButton_.setButtonText(cfg.wav.playing ? "Pause" : "Play");
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
  cfg.wav.path = wavFile_.existsAsFile() ? wavFile_.getFullPathName()
                                         : juce::String();
  cfg.wav.dest = wavDest_.getSelectedId() >= 2 && wavDest_.getSelectedId() <= 9
                     ? wavDest_.getSelectedId() - 2
                     : -1;
  cfg.wav.peakVolts = (float)wavLevel_.getValue();
  cfg.wav.loop = wavLoopButton_.getToggleState();
  cfg.wav.playing = wavPlayButton_.getToggleState();
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

// Section metrics shared by preferredHeight() and layoutContent().
namespace {
constexpr int kScopeH = 340;              // ~1.5x the original display height
constexpr int kCvRowH = 52, kCvGap = 6;   // two-line rows + inter-row gap
constexpr int kTrigRowH = 28, kTrigGap = 4;
}  // namespace

int TestBenchPanel::preferredHeight() const {
  int total = 10 + 18 + 28 + 6 + kScopeH + 16;              // scope section
  total += 24 + 8 * (kCvRowH + kCvGap) + 12;                // cv generators
  total += 22 + 4 * (kTrigRowH + kTrigGap) + 12;            // trig generators
  total += 22 + 28 + 28 + 26 + 4 + 26 + 10;                 // wav player
  return total;
}

void TestBenchPanel::resized() {
  layoutContent();
}

// Lay the sections out top-to-bottom. The sidebar is always given its
// natural height (the panel column scales to match), so nothing scrolls.
void TestBenchPanel::layoutContent() {
  const int scopeH = kScopeH;
  const int cvRowH = kCvRowH, cvGap = kCvGap;
  const int trigRowH = kTrigRowH, trigGap = kTrigGap;
  content_.setBounds(0, 0, juce::jmax(280, getWidth()), preferredHeight());

  auto r = content_.getLocalBounds().reduced(12, 10);

  scopeTitle_.setBounds(r.removeFromTop(18));
  auto c1 = r.removeFromTop(26);
  scopeSource_.setBounds(c1.removeFromLeft(130).reduced(1, 1));
  c1.removeFromLeft(4);
  scopeWindow_.setBounds(c1.removeFromLeft(86).reduced(1, 1));
  c1.removeFromLeft(4);
  freezeButton_.setBounds(c1.removeFromLeft(64).reduced(1, 2));
  c1.removeFromLeft(2);
  syncButton_.setBounds(c1.removeFromLeft(54).reduced(1, 2));
  c1.removeFromLeft(2);
  zoomButton_.setBounds(c1.removeFromLeft(54).reduced(1, 2));
  r.removeFromTop(6);
  scope_->setBounds(r.removeFromTop(scopeH));
  r.removeFromTop(16);

  auto h1 = r.removeFromTop(24);
  syncAllButton_.setBounds(h1.removeFromRight(72).reduced(0, 2));
  cvTitle_.setBounds(h1);
  for (auto* row : cvRows_) {
    row->setBounds(r.removeFromTop(cvRowH));
    r.removeFromTop(cvGap);
  }
  r.removeFromTop(12);

  trigTitle_.setBounds(r.removeFromTop(22));
  for (auto* row : trigRows_) {
    row->setBounds(r.removeFromTop(trigRowH));
    r.removeFromTop(trigGap);
  }
  r.removeFromTop(12);

  // ---- wav player ----
  wavTitle_.setBounds(r.removeFromTop(22));
  auto w1 = r.removeFromTop(28);
  wavLoadButton_.setBounds(w1.removeFromLeft(64).reduced(1, 3));
  w1.removeFromLeft(6);
  wavFileLabel_.setBounds(w1);
  auto w2 = r.removeFromTop(28);
  wavPlayButton_.setBounds(w2.removeFromLeft(56).reduced(1, 3));
  wavStopButton_.setBounds(w2.removeFromLeft(52).reduced(1, 3));
  wavLoopButton_.setBounds(w2.removeFromLeft(52).reduced(1, 3));
  w2.removeFromLeft(4);
  wavTimeLabel_.setBounds(w2.removeFromRight(96));
  wavPos_.setBounds(w2.reduced(0, 2));
  auto w3 = r.removeFromTop(26);
  wavDestLabel_.setBounds(w3.removeFromLeft(18));
  wavDest_.setBounds(w3.removeFromLeft(110).reduced(1, 2));
  w3.removeFromLeft(12);
  wavLevelLabel_.setBounds(w3.removeFromLeft(32));
  wavLevel_.setBounds(w3.removeFromLeft(76).reduced(1, 3));
  r.removeFromTop(4);
  auto w4 = r.removeFromTop(26);
  monitorButton_.setBounds(w4.removeFromLeft(122).reduced(1, 2));
  w4.removeFromLeft(4);
  monitorVol_.setBounds(w4.removeFromLeft(90).reduced(0, 2));
  w4.removeFromLeft(6);
  monitorDevice_.setBounds(w4.reduced(1, 2));
}
