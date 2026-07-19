#include "RoutingPanel.h"

// ---------------------------------------------------------------------------
// JSON persistence
// ---------------------------------------------------------------------------
namespace xloc2 {

juce::File routingFile() {
  return EmuEngine::stateDir().getChildFile("routing.json");
}

static juce::var jackToVar(const JackRouting& j) {
  auto* o = new juce::DynamicObject();
  o->setProperty("channel", j.deviceChannel);
  o->setProperty("gain", j.gain);
  o->setProperty("offset", j.offsetVolts);
  return juce::var(o);
}

static void jackFromVar(const juce::var& v, JackRouting& j) {
  if (auto* o = v.getDynamicObject()) {
    j.deviceChannel = (int)o->getProperty("channel");
    j.gain = (float)(double)o->getProperty("gain");
    j.offsetVolts = (float)(double)o->getProperty("offset");
  }
}

template <size_t N>
static juce::var jackArrayToVar(const std::array<JackRouting, N>& arr) {
  juce::Array<juce::var> a;
  for (const auto& j : arr) a.add(jackToVar(j));
  return juce::var(a);
}

template <size_t N>
static void jackArrayFromVar(const juce::var& v, std::array<JackRouting, N>& arr) {
  if (auto* a = v.getArray())
    for (size_t i = 0; i < N && i < (size_t)a->size(); ++i)
      jackFromVar(a->getReference((int)i), arr[i]);
}

void saveRoutingState(const RoutingConfig& cfg, juce::AudioDeviceManager& dm) {
  auto* o = new juce::DynamicObject();
  o->setProperty("outFullScaleVolts", cfg.outFullScaleVolts);
  o->setProperty("inFullScaleVolts", cfg.inFullScaleVolts);
  o->setProperty("trigRiseVolts", cfg.trigRiseVolts);
  o->setProperty("trigFallVolts", cfg.trigFallVolts);
  o->setProperty("cvOut", jackArrayToVar(cfg.cvOut));
  o->setProperty("cvIn", jackArrayToVar(cfg.cvIn));
  o->setProperty("trigIn", jackArrayToVar(cfg.trigIn));
  if (auto xml = dm.createStateXml())
    o->setProperty("audioDevice", xml->toString());

  auto file = routingFile();
  file.getParentDirectory().createDirectory();
  file.replaceWithText(juce::JSON::toString(juce::var(o)));
}

bool loadRoutingState(RoutingConfig& cfg, juce::String& audioDeviceXml) {
  auto file = routingFile();
  if (!file.existsAsFile()) return false;
  auto v = juce::JSON::parse(file.loadFileAsString());
  auto* o = v.getDynamicObject();
  if (o == nullptr) return false;

  auto num = [o](const char* name, float fallback) {
    const auto p = o->getProperty(name);
    return p.isVoid() ? fallback : (float)(double)p;
  };
  cfg.outFullScaleVolts = num("outFullScaleVolts", cfg.outFullScaleVolts);
  cfg.inFullScaleVolts = num("inFullScaleVolts", cfg.inFullScaleVolts);
  cfg.trigRiseVolts = num("trigRiseVolts", cfg.trigRiseVolts);
  cfg.trigFallVolts = num("trigFallVolts", cfg.trigFallVolts);
  jackArrayFromVar(o->getProperty("cvOut"), cfg.cvOut);
  jackArrayFromVar(o->getProperty("cvIn"), cfg.cvIn);
  jackArrayFromVar(o->getProperty("trigIn"), cfg.trigIn);
  audioDeviceXml = o->getProperty("audioDevice").toString();
  return true;
}

}  // namespace xloc2

// ---------------------------------------------------------------------------
// Row — one jack's routing controls
// ---------------------------------------------------------------------------
class RoutingPanel::Row : public juce::Component, private juce::Timer {
 public:
  Row(RoutingPanel& owner, JackId id, const juce::String& name, bool isOutput)
      : owner_(owner), id_(id), isOutput_(isOutput) {
    name_.setText(name, juce::dontSendNotification);
    name_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    name_.setColour(juce::Label::textColourId, juce::Colour(0xffb8c0cc));
    addAndMakeVisible(name_);

    channel_.onChange = [this] { owner_.pushToEngine(); };
    addAndMakeVisible(channel_);

    for (auto* l : {&gain_, &offset_}) {
      l->setEditable(false, true, false);
      l->setJustificationType(juce::Justification::centredRight);
      l->setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe4));
      l->setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1e26));
      l->onTextChange = [this] { owner_.pushToEngine(); };
      addAndMakeVisible(*l);
    }
  }

  JackId id() const { return id_; }
  bool isOutput() const { return isOutput_; }

  void setFrom(const JackRouting& j) {
    channel_.setSelectedId(j.deviceChannel + 2, juce::dontSendNotification);
    gain_.setText(juce::String(j.gain, 3), juce::dontSendNotification);
    offset_.setText(juce::String(j.offsetVolts, 2), juce::dontSendNotification);
  }

  JackRouting get() const {
    JackRouting j;
    j.deviceChannel = channel_.getSelectedId() - 2;  // id 1 == "(none)" == -1
    j.gain = gain_.getText().getFloatValue();
    if (std::abs(j.gain) < 1.0e-6f) j.gain = 1.0f;  // empty/zero field -> unity
    j.offsetVolts = offset_.getText().getFloatValue();
    return j;
  }

  void setChannelNames(const juce::StringArray& names) {
    const int keep = channel_.getSelectedId();
    channel_.clear(juce::dontSendNotification);
    channel_.addItem("(none)", 1);
    for (int i = 0; i < names.size(); ++i)
      channel_.addItem(names[i], i + 2);
    channel_.setSelectedId(keep > 0 ? keep : 1, juce::dontSendNotification);
  }

  void flash() {
    flashAlpha_ = 0.55f;
    startTimerHz(30);
  }

  void paint(juce::Graphics& g) override {
    if (flashAlpha_ > 0.0f) {
      g.setColour(juce::Colour(0xff5a8dee).withAlpha(flashAlpha_));
      g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }
  }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    name_.setBounds(r.removeFromLeft(64));
    offset_.setBounds(r.removeFromRight(52).reduced(1, 3));
    gain_.setBounds(r.removeFromRight(52).reduced(1, 3));
    channel_.setBounds(r.reduced(2, 2));
  }

 private:
  void timerCallback() override {
    flashAlpha_ -= 0.04f;
    if (flashAlpha_ <= 0.0f) {
      flashAlpha_ = 0.0f;
      stopTimer();
    }
    repaint();
  }

  RoutingPanel& owner_;
  JackId id_;
  bool isOutput_;
  juce::Label name_;
  juce::ComboBox channel_;
  juce::Label gain_, offset_;
  float flashAlpha_ = 0.0f;
};

// ---------------------------------------------------------------------------
// RoutingPanel
// ---------------------------------------------------------------------------
RoutingPanel::RoutingPanel(EmuEngine& engine)
    : engine_(engine),
      deviceSelector_(engine.deviceManager(), 0, 32, 0, 32,
                      false, false, false, true) {
  title_.setText("AUDIO & ROUTING", juce::dontSendNotification);
  title_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  title_.setColour(juce::Label::textColourId, juce::Colour(0xffe4e8ef));
  addAndMakeVisible(title_);

  closeButton_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(closeButton_);
  addAndMakeVisible(deviceSelector_);

  auto initLabel = [this](juce::Label& caption, const char* text,
                          juce::Label& value) {
    caption.setText(text, juce::dontSendNotification);
    caption.setFont(juce::FontOptions(11.0f));
    caption.setColour(juce::Label::textColourId, juce::Colour(0xff98a2b0));
    addAndMakeVisible(caption);
    value.setEditable(false, true, false);
    value.setJustificationType(juce::Justification::centredRight);
    value.setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe4));
    value.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1e26));
    value.onTextChange = [this] { pushToEngine(); };
    addAndMakeVisible(value);
  };
  initLabel(outFsLabel_, "Out full-scale (V)", outFs_);
  initLabel(inFsLabel_, "In full-scale (V)", inFs_);
  initLabel(trigRiseLabel_, "Trig rise (V)", trigRise_);
  initLabel(trigFallLabel_, "Trig fall (V)", trigFall_);

  colHeader_.setText("Jack        Device channel              Gain    Offset",
                     juce::dontSendNotification);
  colHeader_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
  colHeader_.setColour(juce::Label::textColourId, juce::Colour(0xff707a88));
  addAndMakeVisible(colHeader_);

  // rows: OUT A..H, CV IN 1..8, TR 1..4 (indices match JackId values)
  static const char* outNames[8] = {"OUT A", "OUT B", "OUT C", "OUT D",
                                    "OUT E", "OUT F", "OUT G", "OUT H"};
  for (int i = 0; i < 8; ++i)
    rows_.add(new Row(*this, (JackId)((int)JackId::CvOut1 + i), outNames[i], true));
  for (int i = 0; i < 8; ++i)
    rows_.add(new Row(*this, (JackId)((int)JackId::CvIn1 + i),
                      "CV IN " + juce::String(i + 1), false));
  for (int i = 0; i < 4; ++i)
    rows_.add(new Row(*this, (JackId)((int)JackId::TrigIn1 + i),
                      "TR " + juce::String(i + 1), false));
  for (auto* r : rows_) tableContent_.addAndMakeVisible(r);

  viewport_.setViewedComponent(&tableContent_, false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);

  engine_.deviceManager().addChangeListener(this);
  refreshFromEngine();
}

RoutingPanel::~RoutingPanel() {
  engine_.deviceManager().removeChangeListener(this);
}

void RoutingPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
  // device / sample-rate changed: refresh channel lists and persist selection
  refreshFromEngine();
  xloc2::saveRoutingState(engine_.getRouting(), engine_.deviceManager());
}

void RoutingPanel::refreshChannelLists() {
  juce::StringArray outNames, inNames;
  if (auto* dev = engine_.deviceManager().getCurrentAudioDevice()) {
    outNames = dev->getOutputChannelNames();
    inNames = dev->getInputChannelNames();
  }
  if (outNames.isEmpty())
    for (int i = 1; i <= 16; ++i) outNames.add("Out " + juce::String(i));
  if (inNames.isEmpty())
    for (int i = 1; i <= 16; ++i) inNames.add("In " + juce::String(i));

  for (auto* r : rows_)
    r->setChannelNames(r->isOutput() ? outNames : inNames);
}

void RoutingPanel::refreshFromEngine() {
  updating_ = true;
  refreshChannelLists();
  const auto cfg = engine_.getRouting();
  for (auto* r : rows_) {
    const int id = (int)r->id();
    if (id >= (int)JackId::TrigIn1)
      r->setFrom(cfg.trigIn[(size_t)(id - (int)JackId::TrigIn1)]);
    else if (id >= (int)JackId::CvIn1)
      r->setFrom(cfg.cvIn[(size_t)(id - (int)JackId::CvIn1)]);
    else
      r->setFrom(cfg.cvOut[(size_t)id]);
  }
  outFs_.setText(juce::String(cfg.outFullScaleVolts, 2), juce::dontSendNotification);
  inFs_.setText(juce::String(cfg.inFullScaleVolts, 2), juce::dontSendNotification);
  trigRise_.setText(juce::String(cfg.trigRiseVolts, 2), juce::dontSendNotification);
  trigFall_.setText(juce::String(cfg.trigFallVolts, 2), juce::dontSendNotification);
  updating_ = false;
}

void RoutingPanel::pushToEngine() {
  if (updating_) return;
  RoutingConfig cfg = engine_.getRouting();
  for (auto* r : rows_) {
    const int id = (int)r->id();
    if (id >= (int)JackId::TrigIn1)
      cfg.trigIn[(size_t)(id - (int)JackId::TrigIn1)] = r->get();
    else if (id >= (int)JackId::CvIn1)
      cfg.cvIn[(size_t)(id - (int)JackId::CvIn1)] = r->get();
    else
      cfg.cvOut[(size_t)id] = r->get();
  }
  auto pos = [](float v, float fallback) { return v > 0.0f ? v : fallback; };
  cfg.outFullScaleVolts = pos(outFs_.getText().getFloatValue(), 10.0f);
  cfg.inFullScaleVolts = pos(inFs_.getText().getFloatValue(), 10.0f);
  cfg.trigRiseVolts = trigRise_.getText().getFloatValue();
  cfg.trigFallVolts = trigFall_.getText().getFloatValue();
  if (cfg.trigFallVolts >= cfg.trigRiseVolts)
    cfg.trigFallVolts = cfg.trigRiseVolts * 0.5f;

  engine_.setRouting(cfg);
  xloc2::saveRoutingState(cfg, engine_.deviceManager());
}

int RoutingPanel::rowIndexForJack(JackId id) const {
  for (int i = 0; i < rows_.size(); ++i)
    if (rows_[i]->id() == id) return i;
  return -1;
}

void RoutingPanel::focusJack(JackId id) {
  const int idx = rowIndexForJack(id);
  if (idx < 0) return;
  auto* row = rows_[idx];
  viewport_.setViewPosition(0, juce::jmax(0, row->getY() - 60));
  row->flash();
}

void RoutingPanel::visibilityChanged() {
  if (isVisible()) refreshFromEngine();
}

void RoutingPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf0161a21));
  g.setColour(juce::Colour(0xff2a3140));
  g.fillRect(0, 0, 1, getHeight());
}

void RoutingPanel::resized() {
  auto r = getLocalBounds().reduced(12);

  auto top = r.removeFromTop(26);
  closeButton_.setBounds(top.removeFromRight(26).reduced(1));
  title_.setBounds(top);
  r.removeFromTop(4);

  deviceSelector_.setBounds(r.removeFromTop(juce::jmin(230, r.getHeight() / 3)));
  r.removeFromTop(6);

  auto g1 = r.removeFromTop(24);
  outFsLabel_.setBounds(g1.removeFromLeft(110));
  outFs_.setBounds(g1.removeFromLeft(52).reduced(0, 2));
  g1.removeFromLeft(10);
  inFsLabel_.setBounds(g1.removeFromLeft(104));
  inFs_.setBounds(g1.removeFromLeft(52).reduced(0, 2));
  auto g2 = r.removeFromTop(24);
  trigRiseLabel_.setBounds(g2.removeFromLeft(110));
  trigRise_.setBounds(g2.removeFromLeft(52).reduced(0, 2));
  g2.removeFromLeft(10);
  trigFallLabel_.setBounds(g2.removeFromLeft(104));
  trigFall_.setBounds(g2.removeFromLeft(52).reduced(0, 2));
  r.removeFromTop(4);

  colHeader_.setBounds(r.removeFromTop(16));
  viewport_.setBounds(r);

  const int rowH = 27;
  tableContent_.setSize(viewport_.getMaximumVisibleWidth(),
                        rowH * rows_.size());
  for (int i = 0; i < rows_.size(); ++i)
    rows_[i]->setBounds(0, i * rowH, tableContent_.getWidth(), rowH);
}
