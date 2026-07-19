// XLOC2 emulator core API.
// The emulator owns a virtual clock (microseconds). Firmware "ISRs"
// (IntervalTimer callbacks) fire deterministically as time is stepped.
// The host frontend (headless test, desktop app) uses this API to boot the
// firmware, advance time, poke controls, and read the screen / CV outputs.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace emu {

// ---- lifecycle ----
// Boot the firmware: runs setup() exactly like Teensy's main().
// `state_dir` holds EEPROM.bin and the virtual SD card contents.
void boot(const std::string& state_dir);
// Advance virtual time, firing due timer callbacks in priority order.
void step_us(uint64_t us);
// Run the firmware's loop() once (call regularly, like the real main loop).
void run_loop_once();
uint64_t now_us();

// ---- timers (used by the IntervalTimer shim) ----
int timer_register(void (*cb)(), double period_us);
void timer_unregister(int slot);

// ---- panel controls (XLOC2: 2 encoders w/ push, buttons A/B/X/Y/Z, 4 trigs) ----
enum Button { BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_Z,
              ENC_L_PUSH, ENC_R_PUSH };
void set_button(Button b, bool pressed);
void turn_encoder_left(int detents);   // +cw, -ccw (quadrature emitted over time)
void turn_encoder_right(int detents);

// ---- CV / trigger I/O (volts at the panel jacks) ----
void set_cv_in(int channel, float volts);        // 0..7
void set_trigger_in(int channel, bool high);     // 0..3 (gate level)
float get_cv_out(int channel);                   // 0..7, from DAC state + calibration

// ---- display ----
// 128x64, 1 bit per pixel, row-major bytes as SH1106 pages (8 pages x 128 cols).
const uint8_t* screen_pages();      // 1024 bytes, page layout
bool screen_dirty();                // true once per new frame
void screenshot_pbm(const std::string& path);  // dump as portable bitmap

// ---- EEPROM backing (shim/EEPROM.h hooks) ----
void eeprom_flush();  // write to state_dir/eeprom.bin if dirty

}  // namespace emu
