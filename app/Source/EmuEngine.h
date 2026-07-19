// EmuEngine — owns the firmware emulation and binds it to a real audio device.
//
// Threading model: ALL emulator access happens on the audio thread (the
// firmware was written for a single-core MCU; the emu core is not
// thread-safe). UI actions (encoder turns, button presses, routing edits)
// are posted through a lock-free FIFO and drained at the top of each audio
// callback. The UI reads the screen via a triple-buffered snapshot the audio
// thread publishes once per completed frame.
//
// Clocking: virtual firmware time advances with the audio clock.
// usPerSample = 1e6 / sampleRate; per sample we accumulate microseconds and
// emu::step_us() the integral part — the 16.666 kHz core ISR (and thus DAC
// updates) interleaves with sample generation exactly like real hardware.
// DAC outputs are zero-order-held between core ISRs, which matches the
// module's actual output behavior.
//
// No-device fallback: when no audio device can be opened (no interface, CI,
// headless X server) a juce::HighResolutionTimer ticks every 1 ms and steps
// the firmware 1000 us per tick, so the UI stays fully usable — just without
// CV/trigger I/O. The fallback thread and the audio thread never run
// concurrently: audioDeviceAboutToStart stops the timer (blocking until any
// in-flight tick completes) before the first audio callback, and
// audioDeviceStopped restarts it.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "CoreLoader.h"

// Jack identifiers for routing.
enum class JackId : int {
  CvOut1 = 0, /* ... */ CvOut8 = 7,   // firmware DAC outs A..H
  CvIn1 = 8,  /* ... */ CvIn8 = 15,   // firmware CV ins 1..8
  TrigIn1 = 16, /* ... */ TrigIn4 = 19,
  AudioOutL = 20, AudioOutR = 21,     // phase 3 (audio graph)
  AudioInL = 22, AudioInR = 23,
  None = -1,
};

struct JackRouting {
  int deviceChannel = -1;  // hardware interface channel, -1 = unrouted
  float gain = 1.0f;       // volts <-> sample scale, per-jack
  float offsetVolts = 0.0f;
};

struct RoutingConfig {
  // Interface full-scale in volts: sample value 1.0 == this many volts.
  // ES-8/ES-9 ≈ 10.0 out / 10.0 in (approx; per-jack gain trims the rest).
  float outFullScaleVolts = 10.0f;
  float inFullScaleVolts = 10.0f;
  std::array<JackRouting, 8> cvOut;    // DAC A..H -> device output channel
  std::array<JackRouting, 8> cvIn;     // device input channel -> CV in
  std::array<JackRouting, 4> trigIn;   // device input channel -> trigger in
  float trigRiseVolts = 1.6f;          // comparator with hysteresis
  float trigFallVolts = 0.8f;
};

// ---- built-in test bench (signal generators + scope) ----
enum class GenWave : int {
  Sine = 0, Triangle, SawUp, SawDown, Square,
  SHRandom, SmoothRandom, WhiteNoise, DC
};

struct CvGenConfig {
  bool enabled = false;
  int dest = -1;          // 0..7 = CV In 1..8, -1 = none
  int wave = 0;           // GenWave
  float freqHz = 1.0f;    // 0.01 .. 5000 (ignored for DC)
  float minVolts = -5.0f;
  float maxVolts = 5.0f;  // DC level = maxVolts
};

struct TrigGenConfig {
  bool enabled = false;
  int dest = -1;          // 0..3 = TR 1..4, -1 = none
  float rateHz = 2.0f;    // 0.05 .. 100
  float pulseMs = 10.0f;  // 1 .. 100
};

// WAV player: a decoded audio file fed to a CV-in jack at the emulation
// quantum, exactly like an external signal would arrive.
struct WavPlayerConfig {
  juce::String path;       // last loaded file (persisted; re-decoded at boot)
  int dest = -1;           // 0..7 = CV In 1..8, -1 = none
  float peakVolts = 5.0f;  // full-scale sample (+-1.0) maps to +- this
  bool loop = false;
  bool playing = false;    // transport state (persisted)
};

// Fully decoded, mono-mixed audio file (message thread creates, emu thread
// reads via a shared_ptr swapped under the bench lock).
struct WavData {
  std::vector<float> mono;      // -1..1
  double sampleRate = 44100.0;  // file rate; resampled at the quantum
};

// Scope source indexing: 0..7 = CV In 1..8, 8..15 = CV Out A..H,
// 16..19 = TR 1..4.
struct TestBenchConfig {
  std::array<CvGenConfig, 8> cv;
  std::array<TrigGenConfig, 4> trig;
  WavPlayerConfig wav;
  // Scope settings ride along so they persist with the generators; the
  // engine only uses scopeSource, the rest is UI state.
  int scopeSource = 8;    // CV Out A
  int scopeWindow = 3;    // index into the UI's time-window list
  bool scopeAutoZoom = false;
  bool scopeSync = true;
};

class EmuEngine : public juce::AudioIODeviceCallback {
 public:
  EmuEngine();
  ~EmuEngine() override;

  // ---- firmware core (dynamically loaded module) ----
  // The firmware lives in a dlopen'd core module (phz_core.so/.dylib); the
  // engine drives it exclusively through its C API. All core management runs
  // on the message thread and suspends the emulation clocks around the swap,
  // so the audio/fallback threads never see a half-loaded core.
  //
  // Hot reload semantics: eeprom_flush -> unload -> load new -> boot. The
  // firmware state survives via the state dir on disk (a reload is a clean
  // reboot of the module, like power-cycling the hardware).
  // Returns false and leaves the engine core-less on failure (`error` says
  // why, e.g. an api_version mismatch).
  bool loadCore(const juce::File& coreFile, juce::String& error);
  bool reloadCore(juce::String& error);  // reload the currently loaded file
  bool coreLoaded() const { return api_ != nullptr; }
  juce::File coreFile() const { return core_.loadedFile(); }
  juce::String coreVersion() const;    // fw_version of the loaded core
  juce::String coreBuildInfo() const;  // build_info of the loaded core
  // Auto-reload: watch the loaded file's mtime (1 s timer) and reload when
  // it changes (debounced until the file stops changing).
  void setAutoReload(bool on);
  bool autoReload() const { return autoReload_; }
  // Called on the message thread after every load attempt (manual or auto):
  // (success, error message when failed).
  std::function<void(bool, const juce::String&)> onCoreChanged;

  // Opens audio and starts driving the loaded core; `savedAudioState` (from
  // AudioDeviceManager::createStateXml) restores the previous device
  // selection. Call once at startup from the message thread, after
  // loadCore(). If no device opens, the 1 kHz fallback clock takes over.
  void start(const juce::XmlElement* savedAudioState = nullptr);
  void stop();

  juce::AudioDeviceManager& deviceManager() { return deviceManager_; }

  // Where EEPROM/SD/routing state lives (userApplicationDataDirectory/...).
  static juce::File stateDir();

  // True while a real audio device is driving the emulation.
  bool audioDeviceActive() const {
    return audioActive_.load(std::memory_order_relaxed);
  }

  // ---- UI -> audio-thread control events ----
  void postButton(int button, bool down);         // emu::Button values
  void postEncoder(bool rightEncoder, int detents);

  // ---- routing (message thread; applied atomically per block) ----
  RoutingConfig getRouting() const;
  void setRouting(const RoutingConfig& r);

  // ---- test bench (message thread; applied atomically per block) ----
  TestBenchConfig getTestBench() const;
  void setTestBench(const TestBenchConfig& b);
  void postTrigFire(int genRow);  // manual single pulse (ignores enable)
  void postGenSync();             // phase-reset all generators

  // True when a generator currently overrides that jack's routed input.
  bool genDrivesCvIn(int ch) const {
    return ((genCvMask_.load(std::memory_order_relaxed) >> ch) & 1u) != 0;
  }
  bool genDrivesTrig(int ch) const {
    return ((genTrigMask_.load(std::memory_order_relaxed) >> ch) & 1u) != 0;
  }

  // ---- scope (message thread) ----
  // Copies up to maxN most recent capture samples (oldest first) of the
  // currently selected scope source; returns the count and the capture
  // period in seconds (device sample period, or 62.5 us in fallback mode).
  int readScope(float* dest, int maxN, double& dtSeconds) const;

  // ---- wav player (message thread) ----
  void setWavData(std::shared_ptr<const WavData> data);  // nullptr = unload
  void wavSeekSeconds(double seconds);
  double wavPositionSeconds() const {
    return wavPosShared_.load(std::memory_order_relaxed);
  }
  double wavLengthSeconds() const {
    return wavLen_.load(std::memory_order_relaxed);
  }

  // ---- monitor output (hear what the module hears, computer speakers) ----
  // A second, output-only device fed from a lock-free ring the emulation
  // thread fills; never affects emulation determinism.
  juce::AudioDeviceManager& monitorDeviceManager() { return monitorManager_; }
  void setMonitorEnabled(bool on);
  bool monitorEnabled() const {
    return monitorEnabled_.load(std::memory_order_relaxed);
  }
  bool monitorAvailable() const;  // true when an output device is open
  void setMonitorVolume(float v) {
    monitorVolume_.store(v, std::memory_order_relaxed);
  }

  // ---- screen snapshot for UI (message thread) ----
  // Copies latest completed 1024-byte page buffer; returns frame counter.
  uint64_t readScreen(uint8_t* dest1024) const;

  // Live jack values for UI meters (volts), updated ~per block.
  float cvOutVolts(int ch) const { return cvOutMeter_[(size_t)ch].load(std::memory_order_relaxed); }
  float cvInVolts(int ch) const { return cvInMeter_[(size_t)ch].load(std::memory_order_relaxed); }
  bool trigInHigh(int ch) const { return trigDisp_[(size_t)ch].load(std::memory_order_relaxed); }

  // AudioIODeviceCallback
  void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                        int numInputChannels,
                                        float* const* outputChannelData,
                                        int numOutputChannels, int numSamples,
                                        const juce::AudioIODeviceCallbackContext&) override;
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;

 private:
  struct ControlEvent {
    enum Type { Button, Encoder, TrigFire, GenSync } type;
    int a;  // button id / encoder index (0=L,1=R) / trig gen row
    int b;  // down / detents / pulse length us
  };

  struct CvGenState {
    double phase = 0.0;
    uint32_t rng = 1;
    float rndA = 0.0f, rndB = 0.0f;  // S&H / smooth-random segment endpoints
  };
  struct TrigGenState {
    double phase = 0.0;
    double pulseS = 0.0;  // seconds of pulse remaining (>0 = high)
    bool high = false;
  };

  struct FallbackClock : juce::HighResolutionTimer {
    explicit FallbackClock(EmuEngine& e) : engine(e) {}
    void hiResTimerCallback() override { engine.fallbackTick(); }
    EmuEngine& engine;
  };

  struct MonitorCallback : juce::AudioIODeviceCallback {
    explicit MonitorCallback(EmuEngine& e) : engine(e) {}
    void audioDeviceIOCallbackWithContext(
        const float* const*, int, float* const* out, int numOut,
        int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
    EmuEngine& engine;
    double deviceRate = 48000.0;
    uint64_t readPos = 0;     // monitor-thread-owned ring read index
    double readFrac = 0.0;
    bool primed = false;
  };

  struct CoreWatcher : juce::Timer {
    explicit CoreWatcher(EmuEngine& e) : engine(e) {}
    void timerCallback() override { engine.autoReloadPoll(); }
    EmuEngine& engine;
  };

  // Stop/restart the emulation clocks (audio callback + fallback timer)
  // around a core swap. Both block until any in-flight tick has finished.
  void suspendClocks();
  void resumeClocks();
  void autoReloadPoll();

  void drainControlEvents();
  void publishScreen();
  void fallbackTick();
  // One generation quantum: evaluate enabled generators + wav player and
  // push their values into the emu (overriding routed inputs). Runs on the
  // emu thread.
  void applyGenerators(const TestBenchConfig& b, const WavData* wav, double dt);
  void captureScope();  // append current source value to the ring

  juce::AudioDeviceManager deviceManager_;
  juce::AbstractFifo controlFifo_{256};
  std::array<ControlEvent, 256> controlEvents_;

  mutable juce::SpinLock routingLock_;
  RoutingConfig routing_;

  // test bench
  mutable juce::SpinLock benchLock_;
  TestBenchConfig bench_;
  std::array<CvGenState, 8> cvGen_{};
  std::array<TrigGenState, 4> trigGen_{};
  std::array<float, 8> cvInNow_{};  // last volts applied per CV-in jack
  std::atomic<uint32_t> genCvMask_{0}, genTrigMask_{0};

  // scope ring buffer: single writer (emu thread), UI reads at ~30 Hz
  static constexpr int kScopeSize = 1 << 18;  // 262144 (>5 s @ 48 kHz)
  static constexpr uint64_t kScopeMask = kScopeSize - 1;
  std::vector<float> scopeBuf_ = std::vector<float>((size_t)kScopeSize, 0.0f);
  std::atomic<uint64_t> scopeWrite_{0};
  std::atomic<int> scopeSource_{8};
  std::atomic<double> scopeDt_{1.0 / 16000.0};

  // wav player state (emu thread) + UI-visible atomics
  std::shared_ptr<const WavData> wavData_;  // guarded by benchLock_
  double wavPos_ = 0.0;                     // in file samples
  std::atomic<double> wavSeek_{-1.0};       // pending seek in seconds, <0 none
  std::atomic<double> wavPosShared_{0.0}, wavLen_{0.0};

  // monitor: SPSC ring, emu thread writes, monitor device reads
  static constexpr int kMonSize = 1 << 15;
  static constexpr uint64_t kMonMask = kMonSize - 1;
  std::vector<float> monBuf_ = std::vector<float>((size_t)kMonSize, 0.0f);
  std::atomic<uint64_t> monWrite_{0};
  juce::AudioDeviceManager monitorManager_;
  MonitorCallback monitorCb_{*this};
  std::atomic<bool> monitorEnabled_{false};
  std::atomic<float> monitorVolume_{0.7f};

  double usPerSample_ = 1e6 / 48000.0;
  double usAccum_ = 0.0;
  bool booted_ = false;   // core loaded AND boot() has run
  bool started_ = false;  // start() has run (audio/fallback clocks active)

  // dynamically loaded firmware core; api_ is only swapped on the message
  // thread while the clocks are suspended, so emu-thread access is safe.
  CoreLoader core_;
  const Xloc2CoreApi* api_ = nullptr;
  CoreWatcher coreWatcher_{*this};
  bool autoReload_ = false;
  juce::Time watchedTime_;  // last mtime seen by the watcher (debounce)

  FallbackClock fallback_{*this};
  std::atomic<bool> audioActive_{false};
  std::atomic<bool> stopping_{false};

  // screen triple buffer
  mutable juce::SpinLock screenLock_;
  std::array<uint8_t, 1024> screenCopy_{};
  std::atomic<uint64_t> screenFrames_{0};

  std::array<std::atomic<float>, 8> cvOutMeter_{};
  std::array<std::atomic<float>, 8> cvInMeter_{};
  std::array<std::atomic<bool>, 4> trigDisp_{};
  std::array<bool, 4> trigState_{};
};
