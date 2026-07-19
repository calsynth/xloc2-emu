// Emulated O_C hardware drivers: ADC (CV inputs), SH1106 OLED, DAC readback,
// FS root wiring, MIDI/SPI/Wire/MTP globals, FreqMeasure, AudioIO.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <LittleFS.h>
#include <MTP_Teensy.h>
#include <USBHost_t36.h>
#include <MIDI.h>
#include <Audio.h>
#include <smalloc.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "OC_ADC.h"
#include "OC_DAC.h"
#include "OC_calibration.h"
#include "OC_gpio.h"
#include "OC_io.h"
#include "src/drivers/display.h"
#include "src/drivers/SH1106_128x64_driver.h"
#include "src/drivers/FreqMeasure/OC_FreqMeasure.h"
#include "AudioIO.h"

#include "../core/emu.h"
#include "../core/emu_internal.h"

// ---------------------------------------------------------------------------
// global bus/peripheral objects
// ---------------------------------------------------------------------------
SPIClass SPI;
SPIClass SPI1;
SPIClass SPI2;
TwoWire Wire;
TwoWire Wire1;
TwoWire Wire2;
MTP_class MTP;
SDClass SD;
struct smalloc_pool extmem_smalloc_pool;

// ---------------------------------------------------------------------------
// host-backed FS shim implementation (FS.h)
// ---------------------------------------------------------------------------
const std::string& FS::rootdir() {
  if (root_.empty()) {
    root_ = emu::state_dir() + "/" + label_;
    ::mkdir(root_.c_str(), 0755);
  }
  return root_;
}

std::string FS::hostpath(const char* filepath) {
  std::string p = rootdir();
  if (filepath && filepath[0]) {
    if (filepath[0] != '/') p += "/";
    p += filepath;
  }
  return p;
}

static std::string basename_of(const std::string& p) {
  size_t pos = p.find_last_of('/');
  return pos == std::string::npos ? p : p.substr(pos + 1);
}

File FS::open(const char* filepath, uint8_t mode) {
  std::string hp = hostpath(filepath);
  struct stat st;
  bool exists = (::stat(hp.c_str(), &st) == 0);

  auto impl = std::make_shared<EmuFileImpl>();
  impl->path = hp;
  impl->name = basename_of(hp);

  if (exists && S_ISDIR(st.st_mode)) {
    impl->is_dir = true;
    DIR* d = opendir(hp.c_str());
    if (d) {
      while (struct dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
          continue;
        impl->entries.push_back(e->d_name);
      }
      closedir(d);
    }
    return File(impl);
  }

  const char* fmode = nullptr;
  switch (mode) {
    case FILE_READ:
      if (!exists) return File();
      fmode = "rb";
      break;
    case FILE_WRITE:
      fmode = exists ? "r+b" : "w+b";
      break;
    case FILE_WRITE_BEGIN:
      fmode = "w+b";
      break;
    default:
      return File();
  }
  impl->fp = fopen(hp.c_str(), fmode);
  if (!impl->fp) return File();
  if (mode == FILE_WRITE && impl->fp) fseek(impl->fp, 0, SEEK_END);
  return File(impl);
}

bool FS::exists(const char* filepath) {
  struct stat st;
  return ::stat(hostpath(filepath).c_str(), &st) == 0;
}

bool FS::remove(const char* filepath) {
  return ::remove(hostpath(filepath).c_str()) == 0;
}

bool FS::mkdir(const char* filepath) {
  return ::mkdir(hostpath(filepath).c_str(), 0755) == 0 || errno == EEXIST;
}

bool FS::rmdir(const char* filepath) {
  return ::rmdir(hostpath(filepath).c_str()) == 0;
}

bool FS::rename(const char* oldpath, const char* newpath) {
  return ::rename(hostpath(oldpath).c_str(), hostpath(newpath).c_str()) == 0;
}

File File::openNextFile(uint8_t mode) {
  if (!impl_ || !impl_->is_dir) return File();
  while (impl_->dir_pos < impl_->entries.size()) {
    std::string child = impl_->path + "/" + impl_->entries[impl_->dir_pos++];
    struct stat st;
    if (::stat(child.c_str(), &st) != 0) continue;
    auto ci = std::make_shared<EmuFileImpl>();
    ci->path = child;
    ci->name = basename_of(child);
    if (S_ISDIR(st.st_mode)) {
      ci->is_dir = true;
      DIR* d = opendir(child.c_str());
      if (d) {
        while (struct dirent* e = readdir(d)) {
          if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
          ci->entries.push_back(e->d_name);
        }
        closedir(d);
      }
    } else {
      ci->fp = fopen(child.c_str(), mode == FILE_READ ? "rb" : "r+b");
      if (!ci->fp) continue;
    }
    return File(ci);
  }
  return File();
}

// ---------------------------------------------------------------------------
// MIDI globals (Main.cpp defines these on hardware; Main.cpp is excluded)
// ---------------------------------------------------------------------------
USBHost thisUSB;
USBHub hub1(thisUSB);
MIDIDevice_BigBuffer usbHostMIDI[2]{
    MIDIDevice_BigBuffer(thisUSB),
    MIDIDevice_BigBuffer(thisUSB),
};
HardwareSerial Serial8;
MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDI1);

// ---------------------------------------------------------------------------
// OC::ADC emulation (replaces OC_ADC.cpp)
//
// Value pipeline (must match OC_ADC.h):
//   raw_[ch]      = raw12 << kAdcSmoothBits      (12-bit code, 8 frac bits)
//   smoothed_[ch] = 4:1 one-pole of raw_
//   value(ch)     = offset[ch] - (smoothed_[ch] >> 8)
// with calibration offset[ch] = _ADC_OFFSET = floor(4096 * 2/3) = 2730.
//
// volts -> code:  raw12 = _ADC_OFFSET - volts * 409.6
//   409.6 codes/V comes from requiring value_to_pitch(value(1V)) == 12 << 7:
//   pitch = value * kDefaultPitchCVScale >> 12 = value * 15360 / 4096
//   => value(1V) = 1536 * 4096 / 15360 = 409.6
// ---------------------------------------------------------------------------
ADC_CHANNEL ADC_CHANNEL_1 = 0, ADC_CHANNEL_2 = 1, ADC_CHANNEL_3 = 2,
            ADC_CHANNEL_4 = 3;
ADC_CHANNEL ADC_CHANNEL_5 = 4, ADC_CHANNEL_6 = 5, ADC_CHANNEL_7 = 6,
            ADC_CHANNEL_8 = 7;

namespace OC {

/*static*/ ADC::CalibrationData* ADC::calibration_data_;
/*static*/ uint32_t ADC::raw_[ADC_CHANNEL_COUNT];
/*static*/ uint32_t ADC::smoothed_[ADC_CHANNEL_COUNT];

static constexpr float kAdcCodesPerVolt = 4096.0f / 10.0f;  // 409.6

FLASHMEM
/*static*/ void ADC::Init(CalibrationData* calibration_data, bool flip180) {
  if (flip180) {
    ADC_CHANNEL temp1 = ADC_CHANNEL_1, temp2 = ADC_CHANNEL_2,
                temp3 = ADC_CHANNEL_3, temp4 = ADC_CHANNEL_4;
    ADC_CHANNEL_1 = ADC_CHANNEL_8;
    ADC_CHANNEL_2 = ADC_CHANNEL_7;
    ADC_CHANNEL_3 = ADC_CHANNEL_6;
    ADC_CHANNEL_4 = ADC_CHANNEL_5;
    ADC_CHANNEL_5 = temp4;
    ADC_CHANNEL_6 = temp3;
    ADC_CHANNEL_7 = temp2;
    ADC_CHANNEL_8 = temp1;
  }

  calibration_data_ = calibration_data;

  static constexpr uint16_t kOffset =
      (uint16_t)((float)(1 << kAdcResolution) * 0.6666667f);
  std::fill(raw_, raw_ + ADC_CHANNEL_COUNT, kOffset << kAdcSmoothBits);
  std::fill(smoothed_, smoothed_ + ADC_CHANNEL_COUNT, kOffset << kAdcSmoothBits);
}

/*static*/ void ADC::ADC33131D_Vref_calibrate() { /* instant on host */ }
/*static*/ void ADC::Init_DMA() {}
/*static*/ void ADC::DMA_ISR() {}

/*static*/ void ADC::Scan_DMA() {
  // Match hardware update cadence: new samples land every 3rd core tick.
  static int ratelimit = 0;
  if (++ratelimit < 3) return;
  ratelimit = 0;

  static constexpr uint16_t kOffset =
      (uint16_t)((float)(1 << kAdcResolution) * 0.6666667f);

  // Panel CV k (0-based) is logical channel ADC_CHANNEL_(k+1); the globals
  // are remapped by Pinout_Detect (XLOC2) and flip180.
  const ADC_CHANNEL map[8] = {ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
                              ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6,
                              ADC_CHANNEL_7, ADC_CHANNEL_8};
  for (int ch = 0; ch < ADC_CHANNEL_COUNT; ++ch) {
    float raw12f = (float)kOffset - emu::cv_volts_[ch] * kAdcCodesPerVolt;
    int32_t raw12 = (int32_t)(raw12f + 0.5f);
    if (raw12 < 0) raw12 = 0;
    if (raw12 > 4095) raw12 = 4095;
    uint32_t value = (uint32_t)raw12 << kAdcSmoothBits;
    raw_[map[ch]] = value;
    smoothed_[map[ch]] =
        (smoothed_[map[ch]] * (kAdcSmoothing - 1) + value) / kAdcSmoothing;
  }
}

/*static*/ void ADC::Read(IOFrame* ioframe) {
  for (int channel = 0; channel < ADC_CHANNEL_COUNT; ++channel) {
    ioframe->cv.values[channel] = value(static_cast<ADC_CHANNEL>(channel));
    ioframe->cv.pitch_values[channel] =
        value_to_pitch(ioframe->cv.values[channel]);
  }
}

/*static*/ void ADC::CalibratePitch(int32_t c2, int32_t c4) {
  if (c2 < c4) {
    int32_t scale = (24 * 128 * 4096L) / (c4 - c2);
    calibration_data_->pitch_cv_scale = scale;
  }
}

// XLOC2 hardware ID voltage: 0.30V => CalSynthXL, DAC_20Vpp, Large_OLED.
/*static*/ float ADC::Read_ID_Voltage() { return 0.30f; }

}  // namespace OC

// ---------------------------------------------------------------------------
// DAC readback: emu::get_cv_out
//
// DAC code -> volts: invert the per-channel calibration table
// calibrated_octaves[ch][0..10]. With DAC_20Vpp (XLOC2) each table step is
// 2V and kOctaveZero = 5 is 0V, so:
//   volts = 2 * (table_pos(code) - 5)
// where table_pos is the (fractional) position of `code` obtained by linear
// interpolation between adjacent table entries. Note OC_DAC Update() inverts
// the wire format (MAX_VALUE - value) for non-inverted DACs, but values_[]
// (read via DAC::value()) is the logical code the calibration tables map, so
// no un-inversion is needed here.
// ---------------------------------------------------------------------------
namespace emu {

float get_cv_out(int channel) {
  if (channel < 0 || channel >= DAC_CHANNEL_COUNT) return 0.0f;
  const DAC_CHANNEL map[8] = {DAC_CHANNEL_A, DAC_CHANNEL_B, DAC_CHANNEL_C,
                              DAC_CHANNEL_D, DAC_CHANNEL_E, DAC_CHANNEL_F,
                              DAC_CHANNEL_G, DAC_CHANNEL_H};
  const int idx = map[channel];
  const uint32_t code = OC::DAC::value((size_t)idx);
  const uint16_t* t = OC::calibration_data.dac.calibrated_octaves[idx];

  const float volts_per_step = DAC_20Vpp ? 2.0f : 1.0f;
  const int zero = OC::DAC::kOctaveZero;

  // find enclosing table segment (extrapolate from end segments)
  int seg = 0;
  for (int i = 0; i < OCTAVES; ++i) {
    if (code >= t[i]) seg = i;
  }
  float span = (float)t[seg + 1] - (float)t[seg];
  if (span <= 0.0f) span = 1.0f;
  float pos = (float)seg + ((float)code - (float)t[seg]) / span;
  return (pos - (float)zero) * volts_per_step;
}

}  // namespace emu

// ---------------------------------------------------------------------------
// SH1106 display driver -> emu screen buffer
// ---------------------------------------------------------------------------
/*static*/ void SH1106_128x64_Driver::Init() {
  memset(emu::screen_, 0, sizeof(emu::screen_));
}

/*static*/ void SH1106_128x64_Driver::Clear() {
  memset(emu::screen_, 0, sizeof(emu::screen_));
  emu::screen_dirty_ = true;
}

/*static*/ void SH1106_128x64_Driver::Flush() {}

/*static*/ bool SH1106_128x64_Driver::SendPage(uint_fast8_t index,
                                               const uint8_t* data) {
  if (index < kNumPages) {
    memcpy(emu::screen_ + index * kPageSize, data, kPageSize);
    if (index == kNumPages - 1) {
      emu::screen_dirty_ = true;
      ++emu::screen_frames_;
    }
  }
  return true;
}

/*static*/ void SH1106_128x64_Driver::SPI_send(void*, size_t) {}
/*static*/ void SH1106_128x64_Driver::AdjustOffset(uint8_t) {}
/*static*/ void SH1106_128x64_Driver::ChangeSpeed(uint32_t) {}
/*static*/ void SH1106_128x64_Driver::SetFlipMode(bool) {}
/*static*/ void SH1106_128x64_Driver::SetContrast(uint8_t) {}

// Let the emu core check display back-pressure (used by the busy-wait pump).
static bool display_writeable() { return display::frame_buffer.writeable() > 0; }
static struct DisplayHookInit {
  DisplayHookInit() { emu::display_writeable_fn = &display_writeable; }
} display_hook_init;

// ---------------------------------------------------------------------------
// FreqMeasure (Teensy 4.1 variant) — no-op: no frequency counter hardware.
// ---------------------------------------------------------------------------
FreqMeasureClass FreqMeasure;

void FreqMeasureClass::begin(uint8_t pin) {
  (void)pin;
  running = true;
}
uint8_t FreqMeasureClass::available() { return 0; }
uint32_t FreqMeasureClass::read() { return 1; }
void FreqMeasureClass::end() { running = false; }
void FreqMeasureClass::isr() {}
/*static*/ FreqMeasureClass* FreqMeasureClass::pin_inst[4];

// ---------------------------------------------------------------------------
// AudioIO (AudioIO.cpp excluded — no audio hardware yet)
// ---------------------------------------------------------------------------
namespace OC {
namespace AudioIO {

static AudioInputI2S2 dummy_input;
static AudioOutputI2S2 dummy_output;

AudioStream& InputStream(int interface) {
  (void)interface;
  return dummy_input;
}
AudioStream& OutputStream() { return dummy_output; }
void Init() {}

}  // namespace AudioIO
}  // namespace OC
