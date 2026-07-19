// Implementation of the C ABI boundary (core/xloc2_core_api.h): thin
// wrappers around the emu:: API. This TU is compiled into the phz_core
// module; everything except xloc2_core_get_api() has hidden visibility.
#include "xloc2_core_api.h"

#include "emu.h"
#include "OC_strings.h"

// Optional git ref / label baked in by the build (-DXLOC2_CORE_GIT_REF="...").
#ifndef XLOC2_CORE_GIT_REF
#define XLOC2_CORE_GIT_REF ""
#endif

namespace {

// Keep the public C constants in lockstep with emu::Button.
static_assert(XLOC2_BTN_A == (int)emu::BUTTON_A, "button ABI mismatch");
static_assert(XLOC2_BTN_B == (int)emu::BUTTON_B, "button ABI mismatch");
static_assert(XLOC2_BTN_X == (int)emu::BUTTON_X, "button ABI mismatch");
static_assert(XLOC2_BTN_Y == (int)emu::BUTTON_Y, "button ABI mismatch");
static_assert(XLOC2_BTN_Z == (int)emu::BUTTON_Z, "button ABI mismatch");
static_assert(XLOC2_BTN_ENC_L == (int)emu::ENC_L_PUSH, "button ABI mismatch");
static_assert(XLOC2_BTN_ENC_R == (int)emu::ENC_R_PUSH, "button ABI mismatch");

void api_boot(const char* state_dir) {
  emu::boot(state_dir != nullptr ? state_dir : ".");
}
void api_step_us(uint64_t us) { emu::step_us(us); }
void api_run_loop_once() { emu::run_loop_once(); }
uint64_t api_now_us() { return emu::now_us(); }

void api_set_button(int button, int pressed) {
  if (button >= XLOC2_BTN_A && button <= XLOC2_BTN_ENC_R)
    emu::set_button((emu::Button)button, pressed != 0);
}
void api_turn_encoder_left(int detents) { emu::turn_encoder_left(detents); }
void api_turn_encoder_right(int detents) { emu::turn_encoder_right(detents); }

void api_set_cv_in(int channel, float volts) { emu::set_cv_in(channel, volts); }
void api_set_trigger_in(int channel, int high) {
  emu::set_trigger_in(channel, high != 0);
}
float api_get_cv_out(int channel) { return emu::get_cv_out(channel); }

const uint8_t* api_screen_pages() { return emu::screen_pages(); }
int api_screen_dirty() { return emu::screen_dirty() ? 1 : 0; }
void api_screenshot_pbm(const char* path) {
  if (path != nullptr) emu::screenshot_pbm(path);
}

void api_eeprom_flush() { emu::eeprom_flush(); }

}  // namespace

extern "C" __attribute__((visibility("default")))
const Xloc2CoreApi* xloc2_core_get_api(void) {
  static const Xloc2CoreApi api = {
      XLOC2_CORE_API_VERSION,
      OC::Strings::VERSION,
      __DATE__ " " __TIME__
      " " XLOC2_CORE_GIT_REF,
      api_boot,
      api_step_us,
      api_run_loop_once,
      api_now_us,
      api_set_button,
      api_turn_encoder_left,
      api_turn_encoder_right,
      api_set_cv_in,
      api_set_trigger_in,
      api_get_cv_out,
      api_screen_pages,
      api_screen_dirty,
      api_screenshot_pbm,
      api_eeprom_flush,
  };
  return &api;
}
