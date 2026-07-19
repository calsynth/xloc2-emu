#include "EmuEngine.h"

#include <juce_core/juce_core.h>
#include <cmath>

namespace {
// Deterministic self-contained PRNG (xorshift32), one state per generator.
inline uint32_t xorshift32(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
// uniform in [-1, 1)
inline float frand(uint32_t& s) {
  return (float)((double)xorshift32(s) / 2147483648.0 - 1.0);
}
}  // namespace

EmuEngine::EmuEngine() {
  // Sensible default routing: CV outs 1..8 -> device outs 1..8, device ins
  // 1..8 -> CV ins. Overridden by routing.json (loaded in Main) when present.
  for (int i = 0; i < 8; ++i) {
    routing_.cvOut[(size_t)i].deviceChannel = i;
    routing_.cvIn[(size_t)i].deviceChannel = i;
  }
  for (int i = 0; i < 8; ++i) {
    auto& g = cvGen_[(size_t)i];
    g.rng = 0x9E3779B9u * (uint32_t)(i + 1) | 1u;  // per-generator seed
    g.rndB = frand(g.rng);
  }
}

EmuEngine::~EmuEngine() { stop(); }

juce::File EmuEngine::stateDir() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("Calsynth/XLOC2");
}

// ---------------------------------------------------------------------------
// firmware core management (message thread only)
// ---------------------------------------------------------------------------
void EmuEngine::suspendClocks() {
  // stopping_ keeps audioDeviceStopped() from restarting the fallback clock
  // while the callback is being detached.
  stopping_.store(true, std::memory_order_relaxed);
  deviceManager_.removeAudioCallback(this);  // blocks until callback returns
  fallback_.stopTimer();                     // blocks until tick returns
  stopping_.store(false, std::memory_order_relaxed);
}

void EmuEngine::resumeClocks() {
  if (!started_) return;
  deviceManager_.addAudioCallback(this);
  if (deviceManager_.getCurrentAudioDevice() == nullptr)
    fallback_.startTimer(1);
}

bool EmuEngine::loadCore(const juce::File& coreFile, juce::String& error) {
  suspendClocks();
  if (booted_ && api_ != nullptr) api_->eeprom_flush();
  booted_ = false;
  api_ = nullptr;
  core_.unload();

  bool ok = core_.load(coreFile, error);
  if (ok) {
    api_ = core_.api();
    auto dir = stateDir();
    dir.createDirectory();
    api_->boot(dir.getFullPathName().toRawUTF8());
    booted_ = true;
    watchedTime_ = core_.loadedFileTime();
    resumeClocks();
  }
  if (onCoreChanged) onCoreChanged(ok, error);
  return ok;
}

bool EmuEngine::reloadCore(juce::String& error) {
  const auto f = core_.loadedFile();
  if (f == juce::File()) {
    error = "No core loaded";
    return false;
  }
  return loadCore(f, error);
}

juce::String EmuEngine::coreVersion() const {
  return api_ != nullptr ? juce::String::fromUTF8(api_->fw_version) : juce::String();
}

juce::String EmuEngine::coreBuildInfo() const {
  return api_ != nullptr ? juce::String::fromUTF8(api_->build_info).trim()
                         : juce::String();
}

void EmuEngine::setAutoReload(bool on) {
  if (on == autoReload_) return;
  autoReload_ = on;
  watchedTime_ = core_.loadedFileTime();
  if (on) coreWatcher_.startTimer(1000);
  else coreWatcher_.stopTimer();
}

// 1 s message-thread poll: reload once the file's mtime changed AND has been
// stable for one poll (so we don't load a half-written file mid-build).
void EmuEngine::autoReloadPoll() {
  const auto f = core_.loadedFile();
  if (!autoReload_ || f == juce::File() || !f.existsAsFile()) return;
  const auto t = f.getLastModificationTime();
  if (t == core_.loadedFileTime()) return;  // unchanged since load
  if (t != watchedTime_) {
    watchedTime_ = t;  // changed — wait one poll for the write to settle
    return;
  }
  juce::String error;
  reloadCore(error);
}

void EmuEngine::start(const juce::XmlElement* savedAudioState) {
  started_ = true;

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
  coreWatcher_.stopTimer();
  setMonitorEnabled(false);
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
  fallback_.stopTimer();
  if (booted_ && api_ != nullptr) api_->eeprom_flush();
  started_ = false;
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

TestBenchConfig EmuEngine::getTestBench() const {
  juce::SpinLock::ScopedLockType l(benchLock_);
  return bench_;
}

void EmuEngine::setTestBench(const TestBenchConfig& b) {
  {
    juce::SpinLock::ScopedLockType l(benchLock_);
    bench_ = b;
  }
  scopeSource_.store(b.scopeSource, std::memory_order_relaxed);
  uint32_t cm = 0, tm = 0;
  for (const auto& g : b.cv)
    if (g.enabled && g.dest >= 0 && g.dest < 8) cm |= 1u << g.dest;
  if (b.wav.dest >= 0 && b.wav.dest < 8) cm |= 1u << b.wav.dest;
  for (const auto& g : b.trig)
    if (g.enabled && g.dest >= 0 && g.dest < 4) tm |= 1u << g.dest;
  genCvMask_.store(cm, std::memory_order_relaxed);
  genTrigMask_.store(tm, std::memory_order_relaxed);
}

void EmuEngine::setWavData(std::shared_ptr<const WavData> data) {
  double len = 0.0;
  if (data != nullptr && data->sampleRate > 0.0)
    len = (double)data->mono.size() / data->sampleRate;
  {
    juce::SpinLock::ScopedLockType l(benchLock_);
    wavData_ = std::move(data);
  }
  wavLen_.store(len, std::memory_order_relaxed);
  wavSeek_.store(0.0, std::memory_order_relaxed);  // restart from the top
}

void EmuEngine::wavSeekSeconds(double seconds) {
  wavSeek_.store(juce::jmax(0.0, seconds), std::memory_order_relaxed);
}

bool EmuEngine::monitorAvailable() const {
  return const_cast<juce::AudioDeviceManager&>(monitorManager_)
             .getCurrentAudioDevice() != nullptr;
}

void EmuEngine::setMonitorEnabled(bool on) {
  if (on == monitorEnabled_.load(std::memory_order_relaxed)) return;
  if (on) {
    // output-only, default system device; never touches the emulation device
    monitorManager_.initialiseWithDefaultDevices(0, 2);
    monitorManager_.addAudioCallback(&monitorCb_);
    monitorEnabled_.store(true, std::memory_order_relaxed);
  } else {
    monitorEnabled_.store(false, std::memory_order_relaxed);
    monitorManager_.removeAudioCallback(&monitorCb_);
    monitorManager_.closeAudioDevice();
  }
}

void EmuEngine::MonitorCallback::audioDeviceAboutToStart(
    juce::AudioIODevice* d) {
  deviceRate = d->getCurrentSampleRate();
  primed = false;
}

// Monitor thread: pull from the SPSC ring at the emulation quantum rate,
// nearest-sample resampled to the device rate. Read side only ever loads the
// write index; determinism of the emulation is unaffected.
void EmuEngine::MonitorCallback::audioDeviceIOCallbackWithContext(
    const float* const*, int, float* const* out, int numOut, int numSamples,
    const juce::AudioIODeviceCallbackContext&) {
  for (int ch = 0; ch < numOut; ++ch)
    juce::FloatVectorOperations::clear(out[ch], numSamples);

  const uint64_t w = engine.monWrite_.load(std::memory_order_acquire);
  const double emuRate = 1.0 / engine.scopeDt_.load(std::memory_order_relaxed);
  const double step = emuRate / juce::jmax(1.0, deviceRate);
  const float vol = engine.monitorVolume_.load(std::memory_order_relaxed);
  const uint64_t target = 4096;  // preferred latency in emu-quantum samples

  if (!primed) {
    if (w < target) return;  // wait until the ring has some audio
    readPos = w - target;
    readFrac = 0.0;
    primed = true;
  }
  // drift guard: if we fell too far behind (or ahead), snap back to target
  if (w > readPos + (uint64_t)kMonSize || w < readPos ||
      w > readPos + target * 4)
    readPos = (w > target) ? w - target : 0;

  for (int s = 0; s < numSamples; ++s) {
    float v = 0.0f;
    if (readPos < w) {
      v = engine.monBuf_[(size_t)(readPos & kMonMask)] * vol;
      readFrac += step;
      const auto whole = (uint64_t)readFrac;
      if (whole > 0) {
        readPos = juce::jmin(w, readPos + whole);
        readFrac -= (double)whole;
      }
    }
    for (int ch = 0; ch < numOut; ++ch) out[ch][s] = v;
  }
}

void EmuEngine::postTrigFire(int genRow) {
  float ms = 10.0f;
  {
    juce::SpinLock::ScopedLockType l(benchLock_);
    if (genRow >= 0 && genRow < 4) ms = bench_.trig[(size_t)genRow].pulseMs;
  }
  const auto scope = controlFifo_.write(1);
  if (scope.blockSize1 > 0)
    controlEvents_[(size_t)scope.startIndex1] = {ControlEvent::TrigFire, genRow,
                                                 (int)(ms * 1000.0f)};
}

void EmuEngine::postGenSync() {
  const auto scope = controlFifo_.write(1);
  if (scope.blockSize1 > 0)
    controlEvents_[(size_t)scope.startIndex1] = {ControlEvent::GenSync, 0, 0};
}

int EmuEngine::readScope(float* dest, int maxN, double& dtSeconds) const {
  dtSeconds = scopeDt_.load(std::memory_order_relaxed);
  const uint64_t w = scopeWrite_.load(std::memory_order_acquire);
  const uint64_t avail = std::min<uint64_t>(w, (uint64_t)kScopeSize);
  const int n = (int)std::min<uint64_t>((uint64_t)juce::jmax(0, maxN), avail);
  for (int i = 0; i < n; ++i)
    dest[i] = scopeBuf_[(size_t)((w - (uint64_t)n + (uint64_t)i) & kScopeMask)];
  return n;
}

// Runs on the emu thread only. One generation quantum of `dt` seconds.
void EmuEngine::applyGenerators(const TestBenchConfig& b, const WavData* wav,
                                double dt) {
  for (int i = 0; i < 8; ++i) {
    const auto& g = b.cv[(size_t)i];
    if (!g.enabled || g.dest < 0 || g.dest > 7) continue;
    auto& st = cvGen_[(size_t)i];
    float x;  // normalised -1..1
    const auto wave = (GenWave)g.wave;
    if (wave == GenWave::DC) {
      x = 1.0f;  // maps to maxVolts below
    } else if (wave == GenWave::WhiteNoise) {
      x = frand(st.rng);
    } else {
      double ph = st.phase + (double)g.freqHz * dt;
      if (ph >= 1.0) {
        ph -= std::floor(ph);
        st.rndA = st.rndB;  // new random segment for S&H / smooth
        st.rndB = frand(st.rng);
      }
      st.phase = ph;
      const float p = (float)ph;
      switch (wave) {
        case GenWave::Sine:
          x = std::sin(p * juce::MathConstants<float>::twoPi); break;
        case GenWave::Triangle:
          x = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
        case GenWave::SawUp:   x = 2.0f * p - 1.0f; break;
        case GenWave::SawDown: x = 1.0f - 2.0f * p; break;
        case GenWave::Square:  x = p < 0.5f ? 1.0f : -1.0f; break;
        case GenWave::SHRandom: x = st.rndB; break;
        case GenWave::SmoothRandom: {
          const float c =
              0.5f - 0.5f * std::cos(p * juce::MathConstants<float>::pi);
          x = st.rndA + (st.rndB - st.rndA) * c;
          break;
        }
        case GenWave::WhiteNoise:  // handled above the switch
        case GenWave::DC:
        default: x = 0.0f; break;
      }
    }
    const float v = g.minVolts + (x + 1.0f) * 0.5f * (g.maxVolts - g.minVolts);
    api_->set_cv_in(g.dest, v);
    cvInNow_[(size_t)g.dest] = v;
    cvInMeter_[(size_t)g.dest].store(v, std::memory_order_relaxed);
  }

  for (int t = 0; t < 4; ++t) {
    const auto& g = b.trig[(size_t)t];
    auto& st = trigGen_[(size_t)t];
    if (g.enabled && g.dest >= 0) {
      st.phase += (double)g.rateHz * dt;
      if (st.phase >= 1.0) {
        st.phase -= std::floor(st.phase);
        st.pulseS = (double)g.pulseMs * 1e-3;
      }
    }
    const bool high = st.pulseS > 0.0;  // manual Fire also raises pulseS
    if (st.pulseS > 0.0) st.pulseS -= dt;
    if (high != st.high) {
      st.high = high;
      if (g.dest >= 0 && g.dest < 4) {
        trigState_[(size_t)g.dest] = high;
        trigDisp_[(size_t)g.dest].store(high, std::memory_order_relaxed);
        api_->set_trigger_in(g.dest, high ? 1 : 0);
      }
    }
  }

  // ---- wav player: file samples -> CV-in jack, resampled at the quantum ----
  if (wav != nullptr && !wav->mono.empty() && wav->sampleRate > 0.0 &&
      b.wav.dest >= 0 && b.wav.dest < 8) {
    const double n = (double)wav->mono.size();
    const double sk = wavSeek_.exchange(-1.0, std::memory_order_relaxed);
    if (sk >= 0.0) wavPos_ = juce::jlimit(0.0, n, sk * wav->sampleRate);

    if (b.wav.playing && (wavPos_ < n || b.wav.loop)) {
      if (wavPos_ >= n) wavPos_ = std::fmod(wavPos_, n);
      const auto i0 = (size_t)wavPos_;
      const float frac = (float)(wavPos_ - (double)i0);
      const float s0 = wav->mono[i0];
      const float s1 = wav->mono[i0 + 1 < wav->mono.size()
                                     ? i0 + 1
                                     : (b.wav.loop ? 0 : i0)];
      const float samp = s0 + (s1 - s0) * frac;  // linear interpolation
      const float v = samp * b.wav.peakVolts;
      api_->set_cv_in(b.wav.dest, v);
      cvInNow_[(size_t)b.wav.dest] = v;
      cvInMeter_[(size_t)b.wav.dest].store(v, std::memory_order_relaxed);

      // feed the monitor ring with exactly what the module hears
      // (normalised back to +-1 by the peak level)
      if (monitorEnabled_.load(std::memory_order_relaxed)) {
        const uint64_t mw = monWrite_.load(std::memory_order_relaxed);
        monBuf_[(size_t)(mw & kMonMask)] = samp;
        monWrite_.store(mw + 1, std::memory_order_release);
      }

      wavPos_ += wav->sampleRate * dt;
      if (wavPos_ >= n && b.wav.loop) wavPos_ = std::fmod(wavPos_, n);
    }
    wavPosShared_.store(wavPos_ / wav->sampleRate, std::memory_order_relaxed);
  }
}

void EmuEngine::captureScope() {
  const int src = scopeSource_.load(std::memory_order_relaxed);
  float v = 0.0f;
  if (src >= 0 && src < 8) v = cvInNow_[(size_t)src];
  else if (src >= 8 && src < 16) v = api_->get_cv_out(src - 8);
  else if (src >= 16 && src < 20) v = trigState_[(size_t)(src - 16)] ? 5.0f : 0.0f;
  const uint64_t w = scopeWrite_.load(std::memory_order_relaxed);
  scopeBuf_[(size_t)(w & kScopeMask)] = v;
  scopeWrite_.store(w + 1, std::memory_order_release);
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
        api_->set_button(e.a, e.b);
      } else if (e.type == ControlEvent::Encoder) {
        if (e.a) api_->turn_encoder_right(e.b);
        else api_->turn_encoder_left(e.b);
      } else if (e.type == ControlEvent::TrigFire) {
        if (e.a >= 0 && e.a < 4)
          trigGen_[(size_t)e.a].pulseS =
              juce::jmax(trigGen_[(size_t)e.a].pulseS, (double)e.b * 1e-6);
      } else if (e.type == ControlEvent::GenSync) {
        for (auto& g : cvGen_) g.phase = 0.0;
        for (auto& t : trigGen_) t.phase = 0.0;
      }
    }
  };
  handle(scope.startIndex1, scope.blockSize1);
  handle(scope.startIndex2, scope.blockSize2);
}

void EmuEngine::publishScreen() {
  if (api_->screen_dirty() == 0) return;
  juce::SpinLock::ScopedTryLockType l(screenLock_);
  if (l.isLocked()) {
    std::memcpy(screenCopy_.data(), api_->screen_pages(), 1024);
    screenFrames_.fetch_add(1, std::memory_order_relaxed);
  }
}

// Fallback clock tick (1 ms of virtual time). Never runs concurrently with
// the audio callback — see header. The tick is subdivided into 16 quanta of
// 62.5 us (62/63 us alternating in emu time) so generator waveforms and the
// scope capture stay meaningful at audio-ish rates without a device.
void EmuEngine::fallbackTick() {
  if (!booted_) return;
  TestBenchConfig b;
  std::shared_ptr<const WavData> wd;
  {
    juce::SpinLock::ScopedLockType l(benchLock_);
    b = bench_;
    wd = wavData_;
  }
  drainControlEvents();
  scopeDt_.store(62.5e-6, std::memory_order_relaxed);
  for (int k = 0; k < 16; ++k) {
    applyGenerators(b, wd.get(), 62.5e-6);
    api_->step_us((k & 1) ? 63 : 62);  // 8*62 + 8*63 = 1000 us
    captureScope();
  }
  api_->run_loop_once();
  for (int i = 0; i < 8; ++i)
    cvOutMeter_[(size_t)i].store(api_->get_cv_out(i), std::memory_order_relaxed);
  publishScreen();
}

void EmuEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  fallback_.stopTimer();  // blocks until any in-flight tick has finished
  usPerSample_ = 1e6 / device->getCurrentSampleRate();
  usAccum_ = 0.0;
  scopeDt_.store(usPerSample_ * 1e-6, std::memory_order_relaxed);
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
  TestBenchConfig b;
  std::shared_ptr<const WavData> wd;
  {
    juce::SpinLock::ScopedLockType l(benchLock_);
    b = bench_;
    wd = wavData_;
  }
  const uint32_t genCv = genCvMask_.load(std::memory_order_relaxed);
  const uint32_t genTrig = genTrigMask_.load(std::memory_order_relaxed);
  const double dtSample = usPerSample_ * 1e-6;

  drainControlEvents();

  for (int s = 0; s < numSamples; ++s) {
    // ---- inputs: samples -> volts -> firmware (generators override) ----
    for (int i = 0; i < 8; ++i) {
      if ((genCv >> i) & 1u) continue;  // internal generator wins
      const auto& m = r.cvIn[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numIn) {
        const float v = in[m.deviceChannel][s] * r.inFullScaleVolts * m.gain +
                        m.offsetVolts;
        api_->set_cv_in(i, v);
        cvInNow_[(size_t)i] = v;
        if ((s & 63) == 0) cvInMeter_[(size_t)i].store(v, std::memory_order_relaxed);
      }
    }
    for (int i = 0; i < 4; ++i) {
      if ((genTrig >> i) & 1u) continue;  // internal generator wins
      const auto& m = r.trigIn[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numIn) {
        const float v = in[m.deviceChannel][s] * r.inFullScaleVolts * m.gain +
                        m.offsetVolts;
        const bool high = trigState_[(size_t)i] ? (v > r.trigFallVolts)
                                                : (v > r.trigRiseVolts);
        if (high != trigState_[(size_t)i]) {
          trigState_[(size_t)i] = high;
          trigDisp_[(size_t)i].store(high, std::memory_order_relaxed);
          api_->set_trigger_in(i, high ? 1 : 0);
        }
      }
    }

    // ---- internal test-bench generators + wav player ----
    applyGenerators(b, wd.get(), dtSample);

    // ---- advance firmware time by one sample ----
    usAccum_ += usPerSample_;
    const auto whole = (uint64_t)usAccum_;
    if (whole > 0) {
      api_->step_us(whole);
      usAccum_ -= (double)whole;
    }

    // ---- outputs: firmware DAC (ZOH) -> volts -> samples ----
    for (int i = 0; i < 8; ++i) {
      const auto& m = r.cvOut[(size_t)i];
      if (m.deviceChannel >= 0 && m.deviceChannel < numOut) {
        const float v = api_->get_cv_out(i);
        out[m.deviceChannel][s] =
            (v * m.gain + m.offsetVolts) / r.outFullScaleVolts;
        if ((s & 63) == 0) cvOutMeter_[(size_t)i].store(v, std::memory_order_relaxed);
      }
    }
    captureScope();
  }

  // firmware main loop, once per block (menus, saves, housekeeping)
  api_->run_loop_once();
  publishScreen();
}
