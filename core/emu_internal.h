// Internal glue between the emulator core (core/emu.cpp) and the shim layer.
// Not part of the public emu.h API.
#pragma once

#include <cstdint>
#include <string>

namespace emu {

// ---- virtual clock / scheduler internals ----
extern uint64_t now_us_;
extern int isr_depth_;   // >0 while inside a fired timer callback

// Advance virtual time; fires due timer callbacks unless called from within
// one (then it just advances the clock, like time passing inside an ISR).
void step_us_internal(uint64_t us);

// Called from millis()/micros() (loop/setup context only): makes firmware
// busy-wait loops (splash screen, "saved!" screens) make forward progress by
// advancing virtual time until the display frame buffer has a writeable slot.
void host_pump();

// Set by shim_drivers.cpp: returns true if display::frame_buffer.writeable().
extern bool (*display_writeable_fn)();

// ---- virtual pin layer (implemented in core/emu.cpp) ----
// level: 0/1. Setting a level fires attachInterrupt handlers synchronously and
// latches rising edges into the fake GPIO ISR flag register (for the T4.1
// DigitalInputs::Scan path).
void pin_set_level(uint8_t pin, uint8_t level);
uint8_t pin_get_level(uint8_t pin);
void pin_attach_interrupt(uint8_t pin, void (*fn)(), int mode);
void pin_detach_interrupt(uint8_t pin);

// ---- emulated analog input voltages, panel jacks (volts) ----
extern float cv_volts_[8];        // CV inputs 1..8 (hardware channel order)

// ---- state dir ----
const std::string& state_dir();

// ---- eeprom persistence ----
void eeprom_load();

// ---- screen sink (filled by SH1106 shim in shim_drivers.cpp) ----
extern uint8_t screen_[1024];     // 8 pages x 128 cols
extern bool screen_dirty_;
extern uint32_t screen_frames_;   // completed frame counter

}  // namespace emu
