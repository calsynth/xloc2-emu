// XLOC2 emulator — JUCE application entry point.
#include <juce_gui_extra/juce_gui_extra.h>

#include "EmuEngine.h"
#include "PanelComponent.h"
#include "RoutingPanel.h"

class XLOC2Application : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return "XLOC2"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  bool moreThanOneInstanceAllowed() override { return false; }

  void initialise(const juce::String&) override {
    engine_ = std::make_unique<EmuEngine>();

    // restore routing + audio device selection saved by the RoutingPanel
    RoutingConfig cfg;
    juce::String audioXml;
    if (xloc2::loadRoutingState(cfg, audioXml)) engine_->setRouting(cfg);
    const auto savedAudioState = juce::parseXML(audioXml);  // may be null

    engine_->start(savedAudioState.get());
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *engine_);
  }

  void shutdown() override {
    mainWindow_ = nullptr;
    if (engine_ != nullptr) {
      xloc2::saveRoutingState(engine_->getRouting(), engine_->deviceManager());
      engine_->stop();
    }
    engine_ = nullptr;
  }

  void systemRequestedQuit() override { quit(); }

  class MainWindow : public juce::DocumentWindow {
   public:
    MainWindow(const juce::String& name, EmuEngine& engine)
        : DocumentWindow(name, juce::Colour(0xff0b0d10),
                         DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new PanelComponent(engine), true);
      setResizable(true, true);
      setResizeLimits(400, 630, 4096, 4096);
      centreWithSize(660, 1040);
      setVisible(true);
    }

    void closeButtonPressed() override {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

   private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

 private:
  std::unique_ptr<EmuEngine> engine_;
  std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(XLOC2Application)
