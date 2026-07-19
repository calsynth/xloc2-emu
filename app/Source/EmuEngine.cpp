#include "EmuEngine.h"

#include <juce_core/juce_core.h>

#include "../../core/emu.h"

EmuEngine::EmuEngine() {
  // Sensible default routing: CV outs 1..8 -> device outs 1..8, device ins
  // 1..8 -> CV ins. Overridden by routing.json (loaded in Main) when present.
  for (int i = 0; i < 8; ++i) {
    routing_.cvOut[(size_t)i].deviceChannel = i;
    routing_.cvIn[(size_t)i].deviceChannel = i;
  }
}

EmuEngine::~EmuEngine() { stop(); }

juce::File EmuEngine::stateDir() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("Calsynth/XLOC2");
}

void EmuEngine::start(const juce::XmlElement* savedAudioState) {
  auto dir = stateDir();
  dir.createDirectory();
  emu::boot(dir.getFullPathName().toStdString());
  booted_ = true;

  deviceManager_.initialise(8, 8, savedAudioState, true);
  if (deviceManager_.getCurrentAudioDevice() == nullptr)
    deviceManager_.initialiseWithDefaultDevices(2, 2);  // modest retry

  deviceManager_.addAudioCallback(this);

  // No device at all (no interface / CI container): drive the firmware from
  // the fallback clock so the UI is still fully usable.
  if (deviceManager_.getCurrentAudioDevice() == nullptr)
    fallback_.startTimer(1);
}

void EmuEngine::stop() {
  stopping_.store(true, std::memory_order_relaxed);
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
  fallback_.stopTimer();
  if (booted_) emu::eeprom_flush();
  stopping_.store(false, std::memory_order_relaxed);
}

void EmuEngine::postButton(int button, bool down) {
  const auto scope = controlFifo_.write(1);
  if (scope.blockSize1 > 0)
    controlEvents_[(size_t)scope.startIndex1] = {ControlEvent::Button, button, down ? 1 : 0};
}

void EmuEngine::postEncoder(bool rightEncoder, int detents) {
  const auto scope = controlFifo_.write(1);
  if (scope.blockSize1 > 0)
    controlEvents_[(size_t)scope.startIndex1] = {ControlEvent::Encoder, rightEncoder ? 1 : 0, detents};
}

RoutingConfig EmuEngine::getRouting() const {
  juce::SpinLock::ScopedLockType l(routingLock_);
  return routing_;
}

void EmuEngine::setRouting(const RoutingConfig& r) {
  juce::SpinLock::ScopedLockType l(routingLock_);
  routing_ = r;
}

uint64_t EmuEngine::readScreen(uint8_t* dest1024) const {
  juce::SpinLock::ScopedLockType l(screenLock_);
  std::memcpy(dest1024, screenCopy_.data(), 1024);
  return screenFrames_.load(std::memory_order_relaxed);
}

void EmuEngine::drainControlEvents() {
  const auto scope = controlFifo_.read(controlFifo_.getNumReady());
  auto handle = [this](int start, int n) {
    for (int i = 0; i < n; ++i) {
      const auto& e = controlEvents_[(size_t)(start + i)];
      if (e.type == ControlEvent::Button) {
        emu::set_button((emu::Button)e.a, e.b != 0);
      } else {
        if (e.a) emu::turn_encoder_right(e.b);
        else emu::turn_encoder_left(e.b);
      }
    }
  };
  handle(scope.startIndex1, scope.blockSize1);
  handle(scope.startIndex2, scope.blockSize2);
}

void EmuEngine::publishScreen() {
  if (!emu::screen_dirty()) return;
  juce::SpinLock::ScopedTryLockType l(screenLock_);
  if (l.isLocked()) {
    std::memcpy(screenCopy_.data(), emu::screen_pages(), 1024);
    screenFrames_.fetch_add(1, std::memory_order_relaxed);
  }
}

// Fallback clock tick (1 ms of virtual time). Never runs concurrently with
// the audio callback — see header.
void EmuEngine::fallbackTick() {
  if (!booted_) return;
  drainControlEvents();
  emu::step_us(1000);
  emu::run_loop_once();
  for (int i = 0; i < 8; ++i)
    cvOutMeter_[(size_t)i].store(emu::get_cv_out(i), std::memory_order_relaxed);
  publishScreen();
}

void EmuEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  fallback_.stopTimer();  // blocks until any in-flight tick has finished
  usPerSample_ = 1e6 / device->getCurrentSampleRate();
  usAccum_ = 0.0;
  audioActive_.store(true, std::memory_order_relaxed);
}

void EmuEngine::audioDeviceStopped() {
  audioActive_.store(false, std::memory_order_relaxed);
  // Keep the firmware running while no device is open (device change, or the
  // user closed the device) — unless the whole engine is shutting down.
  if (booted_ && !stopping_.load(std::memory_order_relaxed))
    fallback_.startTimer(1);
}

void EmuEngine::audioDeviceIOCallbackWithContext(
    const float* const* in, int numIn, float* const* out, int numOut,
    int numSamples, const juce::AudioIODeviceCallbackContext&) {
  for (int ch = 0; ch < numOut; ++ch)
    juce::FloatVectorOperations::clear(out[ch], numSamples);
  if (!booted_) return;

  RoutingConfig r;
  {
    juce::SpinLock::ScopedLockType l(routingLock_);
    r = routing_;
  }

  drainControlEvents();

  for (int s = 0; s < numSamples; ++s) {
    // ---- inputs: samples -> volts -> firmware ----
    for (int i = 0; i < 8; ++i) {
      const auto& m = r.cvIn[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numIn) {
        const float v = in[m.deviceChannel][s] * r.inFullScaleVolts * m.gain +
                        m.offsetVolts;
        emu::set_cv_in(i, v);
        if ((s & 63) == 0) cvInMeter_[(size_t)i].store(v, std::memory_order_relaxed);
      }
    }
    for (int i = 0; i < 4; ++i) {
      const auto& m = r.trigIn[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numIn) {
        const float v = in[m.deviceChannel][s] * r.inFullScaleVolts * m.gain +
                        m.offsetVolts;
        const bool high = trigState_[(size_t)i] ? (v > r.trigFallVolts)
                                                : (v > r.trigRiseVolts);
        if (high != trigState_[(size_t)i]) {
          trigState_[(size_t)i] = high;
          trigDisp_[(size_t)i].store(high, std::memory_order_relaxed);
          emu::set_trigger_in(i, high);
        }
      }
    }

    // ---- advance firmware time by one sample ----
    usAccum_ += usPerSample_;
    const auto whole = (uint64_t)usAccum_;
    if (whole > 0) {
      emu::step_us(whole);
      usAccum_ -= (double)whole;
    }

    // ---- outputs: firmware DAC (ZOH) -> volts -> samples ----
    for (int i = 0; i < 8; ++i) {
      const auto& m = r.cvOut[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numOut) {
        const float v = emu::get_cv_out(i);
        out[m.deviceChannel][s] =
            (v * m.gain + m.offsetVolts) / r.outFullScaleVolts;
        if ((s & 63) == 0) cvOutMeter_[(size_t)i].store(v, std::memory_order_relaxed);
      }
    }
  }

  // firmware main loop, once per block (menus, saves, housekeeping)
  emu::run_loop_once();
  publishScreen();
}
