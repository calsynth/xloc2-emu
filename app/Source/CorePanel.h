// CorePanel — slide-over panel for managing the loaded firmware core:
// shows the running core's version/build info, lists the core modules in
// <stateDir>/cores/, and offers Load / Reload / Open-folder plus an
// auto-reload toggle (reload whenever the loaded file changes on disk —
// the fast edit->build->see loop).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EmuEngine.h"

class CorePanel : public juce::Component {
 public:
  explicit CorePanel(EmuEngine& engine);

  std::function<void()> onClose;

  // Re-read the loaded core's info and the cores folder listing. Called when
  // the panel is shown and after every (auto-)reload.
  void refresh();

  void paint(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;

 private:
  void loadSelected();
  void reloadCurrent();
  void showResult(bool ok, const juce::String& error);

  EmuEngine& engine_;
  juce::Array<juce::File> cores_;  // combo item i+1 -> cores_[i]

  juce::Label title_;
  juce::TextButton closeButton_{"X"};
  juce::Label versionLabel_, buildLabel_, pathLabel_;
  juce::ComboBox coreList_;
  juce::TextButton loadButton_{"Load"};
  juce::TextButton reloadButton_{"Reload"};
  juce::TextButton folderButton_{"Folder"};
  juce::ToggleButton autoReload_{"Auto-reload when the core file changes"};
  juce::Label statusLabel_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorePanel)
};
