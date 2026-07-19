// TestBenchPanel — built-in test bench sidebar: a scope on every CV/trig
// jack plus 8 CV signal generators and 4 trigger generators that can drive
// the emulated inputs directly (no external signals or audio interface
// needed). Generators override hardware-routed inputs for their destination
// jack. All settings persist in routing.json under "testbench".
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EmuEngine.h"

namespace xloc2 {
// Stored in the same routing.json as the routing config (key "testbench").
bool loadTestBench(TestBenchConfig& cfg);
void saveTestBench(const TestBenchConfig& cfg);
}  // namespace xloc2

class TestBenchPanel : public juce::Component {
 public:
  explicit TestBenchPanel(EmuEngine& engine);
  ~TestBenchPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  class ScopeDisplay;
  class CvGenRow;
  class TrigGenRow;

  void loadFromConfig(const TestBenchConfig& cfg);
  void pushToEngine();      // widgets -> engine + save
  void refreshDestMarks();  // annotate taken destinations in dropdowns
  void applyScopeSettings();

  EmuEngine& engine_;
  bool updating_ = false;

  juce::Label scopeTitle_, cvTitle_, trigTitle_;
  juce::ComboBox scopeSource_, scopeWindow_;
  juce::TextButton freezeButton_{"Freeze"}, syncButton_{"Sync"},
      zoomButton_{"Auto"};
  std::unique_ptr<ScopeDisplay> scope_;

  juce::TextButton syncAllButton_{"Sync all"};
  juce::OwnedArray<CvGenRow> cvRows_;
  juce::OwnedArray<TrigGenRow> trigRows_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestBenchPanel)
};
