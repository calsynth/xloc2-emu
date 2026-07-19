// RoutingPanel — slide-over panel with the audio device selector and the
// jack <-> hardware-channel routing table (per-jack gain/offset, global
// full-scale volts and trigger thresholds).
//
// All edits are pushed to EmuEngine::setRouting() immediately and persisted
// as JSON (together with the selected audio device) to
// <stateDir>/routing.json, the same directory the engine keeps EEPROM/SD in.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "EmuEngine.h"

namespace xloc2 {
// <userApplicationDataDirectory>/Calsynth/XLOC2/routing.json
juce::File routingFile();
// Returns true if the file existed and parsed; audioDeviceXml receives the
// saved AudioDeviceManager state (may stay empty).
bool loadRoutingState(RoutingConfig& cfg, juce::String& audioDeviceXml);
void saveRoutingState(const RoutingConfig& cfg, juce::AudioDeviceManager& dm);
}  // namespace xloc2

class RoutingPanel : public juce::Component,
                     private juce::ChangeListener {
 public:
  explicit RoutingPanel(EmuEngine& engine);
  ~RoutingPanel() override;

  // Scrolls the routing table to the given jack and highlights its row.
  void focusJack(JackId id);

  std::function<void()> onClose;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;

 private:
  class Row;

  void changeListenerCallback(juce::ChangeBroadcaster*) override;
  void refreshFromEngine();      // engine config -> UI
  void refreshChannelLists();    // repopulate combos from the current device
  void pushToEngine();           // UI -> engine config + save
  int rowIndexForJack(JackId id) const;

  EmuEngine& engine_;
  bool updating_ = false;  // guard against feedback while refreshing UI

  juce::Label title_;
  juce::TextButton closeButton_{"X"};
  juce::AudioDeviceSelectorComponent deviceSelector_;

  // global settings
  juce::Label outFsLabel_, inFsLabel_, trigRiseLabel_, trigFallLabel_;
  juce::Label outFs_, inFs_, trigRise_, trigFall_;

  juce::Label colHeader_;
  juce::Viewport viewport_;
  juce::Component tableContent_;
  juce::OwnedArray<Row> rows_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingPanel)
};
