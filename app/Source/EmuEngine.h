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

class EmuEngine : public juce::AudioIODeviceCallback {
 public:
  EmuEngine();
  ~EmuEngine() override;

  // Boots firmware (state dir under userApplicationDataDirectory) and opens
  // audio; `savedAudioState` (from AudioDeviceManager::createStateXml) restores
  // the previous device selection. Safe to call once at startup from the
  // message thread. If no device opens, the 1 kHz fallback clock takes over.
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
    enum Type { Button, Encoder } type;
    int a;  // button id / encoder index (0=L,1=R)
    int b;  // down / detents
  };

  struct FallbackClock : juce::HighResolutionTimer {
    explicit FallbackClock(EmuEngine& e) : engine(e) {}
    void hiResTimerCallback() override { engine.fallbackTick(); }
    EmuEngine& engine;
  };

  void drainControlEvents();
  void publishScreen();
  void fallbackTick();

  juce::AudioDeviceManager deviceManager_;
  juce::AbstractFifo controlFifo_{256};
  std::array<ControlEvent, 256> controlEvents_;

  mutable juce::SpinLock routingLock_;
  RoutingConfig routing_;

  double usPerSample_ = 1e6 / 48000.0;
  double usAccum_ = 0.0;
  bool booted_ = false;

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
