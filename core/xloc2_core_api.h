// XLOC2 core module ABI — the only interface between the host app and a
// dynamically loaded firmware core (phz_core.so / phz_core.dylib).
//
// Plain C so any host (JUCE app, headless tests, scripts) can dlopen a core
// built from any Phazerville version without recompiling. The module exports
// exactly one symbol, xloc2_core_get_api(), returning a versioned struct of
// function pointers that wrap core/emu.h.
//
// ABI rules: bump XLOC2_CORE_API_VERSION on ANY change to the struct layout
// or to the meaning of an existing field. Hosts must refuse a core whose
// api_version differs from their own.
#ifndef XLOC2_CORE_API_H
#define XLOC2_CORE_API_H

#include <stdint.h>

#define XLOC2_CORE_API_VERSION 1u

// Panel buttons. Values match emu::Button (static_asserted in the impl).
enum {
  XLOC2_BTN_A = 0,
  XLOC2_BTN_B = 1,
  XLOC2_BTN_X = 2,
  XLOC2_BTN_Y = 3,
  XLOC2_BTN_Z = 4,
  XLOC2_BTN_ENC_L = 5,  // left encoder push
  XLOC2_BTN_ENC_R = 6,  // right encoder push
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Xloc2CoreApi {
  // ---- identity ----
  uint32_t api_version;    // == XLOC2_CORE_API_VERSION of the core's build
  const char* fw_version;  // OC::Strings::VERSION, e.g. "v2.0.1_XLOC2emu"
  const char* build_info;  // build date/time + optional git ref

  // ---- lifecycle ----
  // Boot the firmware (runs setup()). `state_dir` holds eeprom.bin, the
  // virtual SD card and LittleFS contents; state therefore survives a core
  // swap on disk. Call exactly once per loaded module instance.
  void (*boot)(const char* state_dir);
  // Advance virtual time by `us` microseconds, firing due timer "ISRs".
  void (*step_us)(uint64_t us);
  // One iteration of the firmware's loop() (menus, saves, housekeeping).
  void (*run_loop_once)(void);
  uint64_t (*now_us)(void);

  // ---- panel controls ----
  void (*set_button)(int button, int pressed);  // XLOC2_BTN_*, 1 = pressed
  void (*turn_encoder_left)(int detents);       // +cw / -ccw
  void (*turn_encoder_right)(int detents);

  // ---- CV / trigger I/O (volts at the panel jacks) ----
  void (*set_cv_in)(int channel, float volts);   // 0..7
  void (*set_trigger_in)(int channel, int high); // 0..3 (gate level)
  float (*get_cv_out)(int channel);              // 0..7

  // ---- display (128x64, SH1106 page layout: 8 pages x 128 cols) ----
  const uint8_t* (*screen_pages)(void);  // 1024 bytes, owned by the module
  int (*screen_dirty)(void);             // 1 once per new frame
  void (*screenshot_pbm)(const char* path);

  // ---- persistence ----
  void (*eeprom_flush)(void);  // write state_dir/eeprom.bin if dirty
} Xloc2CoreApi;

// The core module's sole exported entry point. The returned struct is static,
// valid for the lifetime of the loaded module.
const Xloc2CoreApi* xloc2_core_get_api(void);
typedef const Xloc2CoreApi* (*Xloc2CoreGetApiFn)(void);
#define XLOC2_CORE_ENTRY_POINT "xloc2_core_get_api"

#ifdef __cplusplus
}
#endif

#endif  // XLOC2_CORE_API_H
