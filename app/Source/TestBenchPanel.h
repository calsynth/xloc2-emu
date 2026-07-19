// TestBenchPanel — built-in test bench sidebar: a scope on every CV/trig
// jack, 8 CV signal generators, 4 trigger generators and a WAV file player
// that can drive the emulated inputs directly (no external signals or audio
// interface needed). Generators/player override hardware-routed inputs for
// their destination jack. All settings persist in routing.json under
// "testbench". Content scrolls in a viewport when the window is short.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EmuEngine.h"

namespace xloc2 {
// Stored in the same routing.json as the routing config (key "testbench").
bool loadTestBench(TestBenchConfig& cfg);
void saveTestBench(const TestBenchConfig& cfg);
// Decode an audio file (wav/aiff/flac) fully into memory, mono-mixed.
// Returns nullptr on failure.
std::shared_ptr<const WavData> decodeAudioFile(const juce::File& f);
}  // namespace xloc2

class TestBenchPanel : public juce::Component, private juce::Timer {
 public:
  explicit TestBenchPanel(EmuEngine& engine);
  ~TestBenchPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  class ScopeDisplay;
  class CvGenRow;
  class TrigGenRow;

  void timerCallback() override;  // wav position + monitor availability
  void loadFromConfig(const TestBenchConfig& cfg);
  void pushToEngine();      // widgets -> engine + save
  void refreshDestMarks();  // annotate taken destinations in dropdowns
  void applyScopeSettings();
  void layoutContent();     // positions children inside content_
  void chooseWavFile();
  void setWavFile(const juce::File& f);
  void refreshMonitorDevices();

  EmuEngine& engine_;
  bool updating_ = false;

  juce::Viewport viewport_;
  juce::Component content_;

  juce::Label scopeTitle_, cvTitle_, trigTitle_, wavTitle_;
  juce::ComboBox scopeSource_, scopeWindow_;
  juce::TextButton freezeButton_{"Freeze"}, syncButton_{"Sync"},
      zoomButton_{"Auto"};
  std::unique_ptr<ScopeDisplay> scope_;

  juce::TextButton syncAllButton_{"Sync all"};
  juce::OwnedArray<CvGenRow> cvRows_;
  juce::OwnedArray<TrigGenRow> trigRows_;

  // wav player
  juce::TextButton wavLoadButton_{"Load..."};
  juce::Label wavFileLabel_;
  juce::TextButton wavPlayButton_{"Play"}, wavStopButton_{"Stop"},
      wavLoopButton_{"Loop"};
  juce::Slider wavPos_, wavLevel_;
  juce::Label wavTimeLabel_, wavDestLabel_, wavLevelLabel_;
  juce::ComboBox wavDest_;
  juce::TextButton monitorButton_{"Monitor"};
  juce::Slider monitorVol_;
  juce::ComboBox monitorDevice_;
  juce::File wavFile_;
  std::unique_ptr<juce::FileChooser> chooser_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestBenchPanel)
};
