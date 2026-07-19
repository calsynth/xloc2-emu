#include "CorePanel.h"

#include "CoreLoader.h"

namespace {
constexpr juce::uint32 kBg = 0xf01c1e22;
constexpr juce::uint32 kText = 0xffc9ced8;
constexpr juce::uint32 kDim = 0xff8a93a1;
constexpr juce::uint32 kAccent = 0xff3c62b0;

void styleButton(juce::TextButton& b) {
  b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2d33));
  b.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
}
}  // namespace

CorePanel::CorePanel(EmuEngine& engine) : engine_(engine) {
  title_.setText("Firmware core", juce::dontSendNotification);
  title_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
  title_.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(title_);

  styleButton(closeButton_);
  closeButton_.onClick = [this] { if (onClose) onClose(); };
  addAndMakeVisible(closeButton_);

  versionLabel_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
  versionLabel_.setColour(juce::Label::textColourId, juce::Colour(kText));
  addAndMakeVisible(versionLabel_);

  buildLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
  buildLabel_.setColour(juce::Label::textColourId, juce::Colour(kDim));
  addAndMakeVisible(buildLabel_);

  pathLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
  pathLabel_.setColour(juce::Label::textColourId, juce::Colour(kDim));
  pathLabel_.setMinimumHorizontalScale(0.7f);
  addAndMakeVisible(pathLabel_);

  coreList_.setTextWhenNothingSelected("cores in " +
                                       CoreLoader::coresDir().getFullPathName());
  coreList_.setTextWhenNoChoicesAvailable("no cores in the cores folder");
  addAndMakeVisible(coreList_);

  styleButton(loadButton_);
  loadButton_.setTooltip("Load the core selected above (hot swap; state "
                         "survives via the state folder)");
  loadButton_.onClick = [this] { loadSelected(); };
  addAndMakeVisible(loadButton_);

  styleButton(reloadButton_);
  reloadButton_.setTooltip("Reload the currently loaded core file from disk");
  reloadButton_.onClick = [this] { reloadCurrent(); };
  addAndMakeVisible(reloadButton_);

  styleButton(folderButton_);
  folderButton_.setTooltip("Open the cores folder");
  folderButton_.onClick = [] {
    auto d = CoreLoader::coresDir();
    d.createDirectory();
    d.revealToUser();
  };
  addAndMakeVisible(folderButton_);

  autoReload_.setColour(juce::ToggleButton::textColourId, juce::Colour(kText));
  autoReload_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccent));
  autoReload_.onClick = [this] {
    engine_.setAutoReload(autoReload_.getToggleState());
  };
  addAndMakeVisible(autoReload_);

  statusLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
  statusLabel_.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(statusLabel_);

  refresh();
}

void CorePanel::refresh() {
  const bool loaded = engine_.coreLoaded();
  versionLabel_.setText(loaded ? engine_.coreVersion() : "no core loaded",
                        juce::dontSendNotification);
  buildLabel_.setText(loaded ? "built " + engine_.coreBuildInfo() : "",
                      juce::dontSendNotification);
  pathLabel_.setText(engine_.coreFile().getFullPathName(),
                     juce::dontSendNotification);
  autoReload_.setToggleState(engine_.autoReload(), juce::dontSendNotification);

  // repopulate the cores folder listing, keeping the selection if possible
  const auto selected = coreList_.getText();
  cores_.clear();
  coreList_.clear(juce::dontSendNotification);
  auto dir = CoreLoader::coresDir();
  cores_ = dir.findChildFiles(juce::File::findFiles, false,
                              "*" + juce::String(CoreLoader::coreExtension()));
  cores_.sort();
  for (int i = 0; i < cores_.size(); ++i)
    coreList_.addItem(cores_[(int)i].getFileName(), i + 1);
  for (int i = 0; i < coreList_.getNumItems(); ++i)
    if (coreList_.getItemText(i) == selected)
      coreList_.setSelectedItemIndex(i, juce::dontSendNotification);
}

void CorePanel::loadSelected() {
  const int idx = coreList_.getSelectedItemIndex();
  if (idx < 0 || idx >= cores_.size()) {
    showResult(false, "Select a core from the list first");
    return;
  }
  juce::String error;
  const bool ok = engine_.loadCore(cores_[idx], error);
  showResult(ok, error);
}

void CorePanel::reloadCurrent() {
  juce::String error;
  const bool ok = engine_.reloadCore(error);
  showResult(ok, error);
}

void CorePanel::showResult(bool ok, const juce::String& error) {
  statusLabel_.setColour(juce::Label::textColourId,
                         ok ? juce::Colour(0xff7fbf6f) : juce::Colour(0xffd97070));
  statusLabel_.setText(ok ? "Loaded " + engine_.coreVersion() : error,
                       juce::dontSendNotification);
  refresh();
}

void CorePanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kBg));
  g.setColour(juce::Colour(kAccent).withAlpha(0.6f));
  g.fillRect(0, 0, 2, getHeight());
}

void CorePanel::resized() {
  auto r = getLocalBounds().reduced(16, 12);
  auto header = r.removeFromTop(32);
  closeButton_.setBounds(header.removeFromRight(32).withHeight(26));
  title_.setBounds(header);
  r.removeFromTop(10);

  versionLabel_.setBounds(r.removeFromTop(28));
  buildLabel_.setBounds(r.removeFromTop(20));
  pathLabel_.setBounds(r.removeFromTop(18));
  r.removeFromTop(14);

  coreList_.setBounds(r.removeFromTop(26));
  r.removeFromTop(8);
  auto row = r.removeFromTop(28);
  const int bw = (row.getWidth() - 16) / 3;
  loadButton_.setBounds(row.removeFromLeft(bw));
  row.removeFromLeft(8);
  reloadButton_.setBounds(row.removeFromLeft(bw));
  row.removeFromLeft(8);
  folderButton_.setBounds(row);
  r.removeFromTop(10);
  autoReload_.setBounds(r.removeFromTop(24));
  r.removeFromTop(8);
  statusLabel_.setBounds(r.removeFromTop(64));
}

void CorePanel::visibilityChanged() {
  if (isVisible()) refresh();
}
