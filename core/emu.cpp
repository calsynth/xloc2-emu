// XLOC2 emulator core: virtual clock, deterministic timer scheduler,
// virtual pin table, EEPROM persistence, screen access, panel controls.
#include "emu.h"
#include "emu_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

// Pull in the fake register file (mmap'd peripheral memory is set up in
// shim_arduino.cpp before main() runs).
#include <Arduino.h>
#include <EEPROM.h>
#include <imxrt.h>

// Firmware pin variables (OC_gpio.cpp) so we can map panel controls to the
// current (post-Pinout_Detect) pin assignment.
#include "OC_gpio.h"

namespace emu {

// ---------------------------------------------------------------------------
// state dir
// ---------------------------------------------------------------------------
static std::string state_dir_ = ".";
const std::string& state_dir() { return state_dir_; }

// ---------------------------------------------------------------------------
// virtual clock + timers
// ---------------------------------------------------------------------------
uint64_t now_us_ = 0;
int isr_depth_ = 0;
bool (*display_writeable_fn)() = nullptr;

struct Timer {
  void (*cb)() = nullptr;
  double period_us = 0;
  double next_due = 0;
  bool active = false;
  bool firing = false;
};
static constexpr int kMaxTimers = 16;
static Timer timers_[kMaxTimers];

int timer_register(void (*cb)(), double period_us) {
  for (int i = 0; i < kMaxTimers; ++i) {
    if (!timers_[i].active) {
      timers_[i].cb = cb;
      timers_[i].period_us = period_us;
      timers_[i].next_due = (double)now_us_ + period_us;
      timers_[i].firing = false;
      timers_[i].active = true;
      return i;
    }
  }
  return -1;
}

void timer_unregister(int slot) {
  if (slot >= 0 && slot < kMaxTimers) timers_[slot].active = false;
}

uint64_t now_us() { return now_us_; }

void step_us_internal(uint64_t us) {
  const uint64_t target = now_us_ + us;
  if (isr_depth_ > 0) {
    // Time passing inside an "ISR": don't fire (same/lower priority masked).
    now_us_ = target;
    return;
  }
  for (;;) {
    // earliest due timer at or before target; ties resolved by slot order
    // (CORE timer is registered before the UI timer, mirroring its higher
    // hardware priority).
    int best = -1;
    for (int i = 0; i < kMaxTimers; ++i) {
      if (!timers_[i].active || timers_[i].firing) continue;
      if (timers_[i].next_due <= (double)target &&
          (best < 0 || timers_[i].next_due < timers_[best].next_due))
        best = i;
    }
    if (best < 0) break;
    Timer& t = timers_[best];
    if ((double)now_us_ < t.next_due) now_us_ = (uint64_t)t.next_due;
    t.next_due += t.period_us;
    // Guard against a timer falling behind (shouldn't happen in practice).
    if (t.next_due < (double)now_us_) t.next_due = (double)now_us_ + t.period_us;
    t.firing = true;
    ++isr_depth_;
    t.cb();
    --isr_depth_;
    t.firing = false;
  }
  now_us_ = target;
}

void step_us(uint64_t us) { step_us_internal(us); }

// Advance time from millis()/micros() so that firmware busy-wait loops
// (splash screen countdown, "calibration saved" screens...) make progress:
// guarantee the display frame buffer regains a writeable slot, which takes
// ~9 core-ISR ticks (8 pages + flush) per frame.
static bool pumping_ = false;
void host_pump() {
  if (isr_depth_ > 0 || pumping_) return;
  pumping_ = true;
  // Always advance a little so elapsedMillis-style timeouts count up.
  step_us_internal(100);
  if (display_writeable_fn) {
    for (int i = 0; i < 40 && !display_writeable_fn(); ++i)
      step_us_internal(60);  // one core-ISR period
  }
  pumping_ = false;
}

// ---------------------------------------------------------------------------
// virtual pins
// ---------------------------------------------------------------------------
struct PinState {
  uint8_t level = 0;
  void (*isr)() = nullptr;
  int isr_mode = 0;
};
static PinState pins_[64];

// Latch a rising edge into the fake GPIO port ISR register the same way the
// i.MX RT edge-detect hardware would, so OC::DigitalInputs::Scan() (which
// reads port->ISR through the mmap'd register file) sees it.
static void latch_gpio_edge(uint8_t pin) {
  volatile uint32_t* portreg = digitalPinToPortReg(pin);  // &GPIOx_DR
  IMXRT_GPIO_t* port = (IMXRT_GPIO_t*)portreg;
  port->ISR |= digitalPinToBitMask(pin);
}

void pin_set_level(uint8_t pin, uint8_t level) {
  if (pin >= 64) return;
  PinState& p = pins_[pin];
  uint8_t old = p.level;
  if (old == level) return;
  p.level = level;
  if (level && !old) latch_gpio_edge(pin);  // rising edge latch (T4.1 mode)
  if (p.isr) {
    bool fire = (p.isr_mode == CHANGE) ||
                (p.isr_mode == RISING && level) ||
                (p.isr_mode == FALLING && !level);
    if (fire) {
      ++isr_depth_;
      p.isr();
      --isr_depth_;
    }
  }
}

uint8_t pin_get_level(uint8_t pin) { return pin < 64 ? pins_[pin].level : 0; }

void pin_attach_interrupt(uint8_t pin, void (*fn)(), int mode) {
  if (pin >= 64) return;
  pins_[pin].isr = fn;
  pins_[pin].isr_mode = mode;
}

void pin_detach_interrupt(uint8_t pin) {
  if (pin < 64) pins_[pin].isr = nullptr;
}

// ---------------------------------------------------------------------------
// panel controls
// ---------------------------------------------------------------------------
static uint8_t button_pin(Button b) {
  switch (b) {
    case BUTTON_A: return but_top;    // 29
    case BUTTON_B: return but_bot;    // 28
    case BUTTON_X: return but_top2;   // 20
    case BUTTON_Y: return but_bot2;   // 14
    case BUTTON_Z: return but_mid;    // 15
    case ENC_L_PUSH: return butL;     // 24
    case ENC_R_PUSH: return butR;     // 25
  }
  return 255;
}

void set_button(Button b, bool pressed) {
  // Buttons are INPUT_PULLUP + active low.
  uint8_t pin = button_pin(b);
  if (pin != 255) pin_set_level(pin, pressed ? 0 : 1);
}

// Emit quadrature so UI::Encoder::Poll (1kHz) sees each gray-code state at
// least twice (debounce shifts 8-bit history; Read wants stable transitions).
static void emit_detent(uint8_t pinA, uint8_t pinB, bool cw) {
  // idle state is (1,1). CW detent: A->0, B->0, A->1 (rising edge on A with
  // B low => +1), B->1. CCW: mirrored.
  struct Step { uint8_t a, b; };
  static const Step cw_seq[]  = { {0,1}, {0,0}, {1,0}, {1,1} };
  static const Step ccw_seq[] = { {1,0}, {0,0}, {0,1}, {1,1} };
  const Step* seq = cw ? cw_seq : ccw_seq;
  for (int s = 0; s < 4; ++s) {
    pin_set_level(pinA, seq[s].a);
    pin_set_level(pinB, seq[s].b);
    step_us_internal(3000);  // 3 UI polls per state
  }
}

void turn_encoder_left(int detents) {
  int n = detents < 0 ? -detents : detents;
  for (int i = 0; i < n; ++i) emit_detent(encL1, encL2, detents > 0);
}

void turn_encoder_right(int detents) {
  int n = detents < 0 ? -detents : detents;
  for (int i = 0; i < n; ++i) emit_detent(encR1, encR2, detents > 0);
}

// ---------------------------------------------------------------------------
// CV / trigger I/O
// ---------------------------------------------------------------------------
float cv_volts_[8] = {0, 0, 0, 0, 0, 0, 0, 0};

void set_cv_in(int channel, float volts) {
  if (channel >= 0 && channel < 8) cv_volts_[channel] = volts;
}

void set_trigger_in(int channel, bool high) {
  // XLOC2 (ADC33131D_Uses_FlexIO): trigger inputs are active HIGH with
  // rising-edge detection. TR pin numbers per OC_gpio.cpp.
  static const uint8_t* const tr_pins[4] = {&TR1, &TR2, &TR3, &TR4};
  if (channel >= 0 && channel < 4)
    pin_set_level(*tr_pins[channel], high ? 1 : 0);
}

// get_cv_out is implemented in shim_drivers.cpp (needs OC_DAC internals).

// ---------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------
uint8_t screen_[1024];
bool screen_dirty_ = false;
uint32_t screen_frames_ = 0;

const uint8_t* screen_pages() { return screen_; }

bool screen_dirty() {
  bool d = screen_dirty_;
  screen_dirty_ = false;
  return d;
}

void screenshot_pbm(const std::string& path) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  fprintf(f, "P1\n128 64\n");
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      int page = y / 8, bit = y & 7;
      int v = (screen_[page * 128 + x] >> bit) & 1;
      fputc(v ? '1' : '0', f);
    }
    fputc('\n', f);
  }
  fclose(f);
}

// ---------------------------------------------------------------------------
// EEPROM backing
// ---------------------------------------------------------------------------
static uint8_t eeprom_[kEepromSize];
static bool eeprom_dirty_flag_ = false;

uint8_t* eeprom_data() { return eeprom_; }
void eeprom_dirty() { eeprom_dirty_flag_ = true; }

static std::string eeprom_path() { return state_dir_ + "/eeprom.bin"; }

void eeprom_load() {
  memset(eeprom_, 0xff, sizeof(eeprom_));  // erased flash reads 0xff
  FILE* f = fopen(eeprom_path().c_str(), "rb");
  if (f) {
    size_t n = fread(eeprom_, 1, sizeof(eeprom_), f);
    (void)n;
    fclose(f);
  }
}

void eeprom_flush() {
  if (!eeprom_dirty_flag_) return;
  FILE* f = fopen(eeprom_path().c_str(), "wb");
  if (f) {
    fwrite(eeprom_, 1, sizeof(eeprom_), f);
    fclose(f);
    eeprom_dirty_flag_ = false;
  }
}

// ---------------------------------------------------------------------------
// boot
// ---------------------------------------------------------------------------
// Implemented in core/emu_main.cpp
void firmware_setup();

// First-run seeding: a factory-fresh module asks "Reset settings on EEPROM?"
// and blocks on a button press *inside setup()*, which a headless boot can't
// answer. Pre-seed a minimal valid global config (like a module that has
// completed its first boot) so setup() runs through. Format mirrors
// OC_apps.cpp SaveGlobalSettings(): METADATA_KEY = 1<<8, value packs
// current_app_id[0..15] (0 -> firmware falls back to the default app),
// encoder acceleration [16], and the v2.0 "valid" flag [17].
static void seed_first_run_config() {
  std::string cfg = state_dir_ + "/lfs/GLOBALS.CFG";
  FILE* f = fopen(cfg.c_str(), "rb");
  if (f) {
    fclose(f);
    return;  // config already exists
  }
  ::mkdir((state_dir_ + "/lfs").c_str(), 0755);
  f = fopen(cfg.c_str(), "wb");
  if (!f) return;
  const uint16_t key = 1 << 8;
  const uint64_t value = (1ull << 17) | (1ull << 16) | 0;
  uint64_t checksum = value;
  uint16_t record_count = 1;
  // 12-byte header: signature "PZ", record count, checksum
  uint8_t header[12] = {'P', 'Z'};
  memcpy(header + 2, &record_count, 2);
  memcpy(header + 4, &checksum, 8);
  fwrite(header, 1, 12, f);
  fwrite(&key, 2, 1, f);
  fwrite(&value, 8, 1, f);
  // empty "PX" data chunk
  uint8_t header2[12] = {'P', 'X'};
  fwrite(header2, 1, 12, f);
  fclose(f);
}

void boot(const std::string& state_dir) {
  state_dir_ = state_dir;
  ::mkdir(state_dir.c_str(), 0755);
  eeprom_load();
  seed_first_run_config();

  // Idle levels before the firmware polls anything:
  // buttons + encoder pins are INPUT_PULLUP, idle HIGH (XLOC2 pin map).
  for (uint8_t pin : {29, 28, 20, 14, 15,       // A B X Y Z
                      24, 25,                   // butL butR
                      30, 31, 36, 37})          // encL1/2 encR1/2
    pins_[pin].level = 1;
  // TR1..4 (0, 1, 23, 22) idle LOW (active high on XLOC2 hardware).

  firmware_setup();
}

}  // namespace emu
