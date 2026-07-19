// XLOC2 emulator — JUCE application entry point.
#include <juce_gui_extra/juce_gui_extra.h>

#include "EmuEngine.h"
#include "PanelComponent.h"
#include "RoutingPanel.h"
#include "TestBenchPanel.h"

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

    // restore test-bench generators + scope settings (same JSON file)
    TestBenchConfig bench;
    if (xloc2::loadTestBench(bench)) engine_->setTestBench(bench);

    // Load the firmware core module: <stateDir>/cores/active (symlink or
    // active.txt) first, then the core bundled with the app. On failure the
    // app still opens — the FW panel can load a core manually.
    juce::String coreError;
    const auto coreFile = CoreLoader::findDefaultCore();
    if (coreFile == juce::File())
      coreError = "No firmware core found.\nPut one in " +
                  CoreLoader::coresDir().getFullPathName() +
                  " or reinstall the app.";
    else
      engine_->loadCore(coreFile, coreError);

    engine_->start(savedAudioState.get());
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *engine_);

    if (!engine_->coreLoaded())
      juce::AlertWindow::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon, "Firmware core", coreError);

    // Developer convenience: XLOC2_AUTO_RELOAD=1 starts with auto-reload on,
    // so `cmake --build build --target phz_core` hot-swaps the running app.
    if (juce::SystemStats::getEnvironmentVariable("XLOC2_AUTO_RELOAD", "0") ==
        "1")
      engine_->setAutoReload(true);
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

  // Shell that shows the natural-size layout through a uniform scale
  // transform, centred in the window. Mouse hit-testing works through the
  // transform (JUCE maps event coordinates automatically), so the whole UI
  // stays usable on displays smaller than the natural 1331x1296 layout.
  class ScaledShell : public juce::Component {
   public:
    explicit ScaledShell(EmuEngine& engine) : panel_(engine) {
      panel_.setBounds(0, 0, panel_.naturalWidth(), panel_.naturalHeight());
      addAndMakeVisible(panel_);
    }

    PanelComponent& panel() { return panel_; }

    void paint(juce::Graphics& g) override {
      g.fillAll(juce::Colour(0xff17181b));
    }

    void resized() override {
      if (getWidth() <= 0 || getHeight() <= 0) return;
      const float nw = (float)panel_.naturalWidth();
      const float nh = (float)panel_.naturalHeight();
      const float s =
          juce::jmin((float)getWidth() / nw, (float)getHeight() / nh);
      const float tx = ((float)getWidth() - nw * s) * 0.5f;
      const float ty = ((float)getHeight() - nh * s) * 0.5f;
      panel_.setTransform(juce::AffineTransform::scale(s).translated(tx, ty));
      panel_.setRenderScale(s);
    }

   private:
    PanelComponent panel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaledShell)
  };

  class MainWindow : public juce::DocumentWindow {
   public:
    MainWindow(const juce::String& name, EmuEngine& engine)
        : DocumentWindow(name, juce::Colour(0xff17181b),
                         DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      auto* shell = new ScaledShell(engine);
      const int nw = shell->panel().naturalWidth();
      const int nh = shell->panel().naturalHeight();
      setContentOwned(shell, false);
      setResizable(true, true);
      setResizeLimits(640, 620, 8192, 8192);

      // default: the natural layout scaled to ~90% of the usable display
      // area, never above 1:1
      float s = 1.0f;
      if (auto* d =
              juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        const auto ua = d->userArea;
        s = juce::jmin(1.0f, 0.9f * (float)ua.getWidth() / (float)nw,
                       0.9f * (float)ua.getHeight() / (float)nh);
      }
      centreWithSize(juce::roundToInt((float)nw * s),
                     juce::roundToInt((float)nh * s));
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
