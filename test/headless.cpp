// Headless boot test for the XLOC2 emulator.
// Boots the firmware, runs ~4s of virtual time, screenshots the display,
// then exercises an encoder turn and a button press and verifies the screen
// content changes.
#include <cstdio>
#include <cstring>
#include <string>

#include "../core/emu.h"

static void run_ms(uint64_t ms) {
  for (uint64_t i = 0; i < ms; ++i) {
    emu::step_us(1000);
    emu::run_loop_once();
  }
}

static int count_lit(const uint8_t* pages) {
  int n = 0;
  for (int i = 0; i < 1024; ++i) n += __builtin_popcount(pages[i]);
  return n;
}

int main() {
  int failures = 0;

  printf("== XLOC2 emulator headless boot test ==\n");
  emu::boot("./state");

  // ~4 seconds of virtual time: splash screens play out, app menu comes up.
  run_ms(4000);

  uint8_t before[1024];
  memcpy(before, emu::screen_pages(), 1024);
  int lit = count_lit(before);
  emu::screenshot_pbm("boot.pbm");
  printf("boot screen: %d lit pixels (screenshot: boot.pbm)\n", lit);
  if (lit > 0) {
    printf("PASS: boot screen non-blank\n");
  } else {
    printf("FAIL: boot screen blank\n");
    ++failures;
  }

  // Encoder turn: should move a cursor / show feedback on screen. Some
  // reactions are transient (popups), so watch the whole window.
  emu::turn_encoder_right(2);
  bool enc_changed = false;
  for (int i = 0; i < 300; ++i) {
    run_ms(1);
    if (memcmp(before, emu::screen_pages(), 1024) != 0) {
      enc_changed = true;
      if (i < 60) emu::screenshot_pbm("after_encoder.pbm");
    }
  }
  if (enc_changed) {
    printf("PASS: screen changed after right encoder turn\n");
  } else {
    printf("FAIL: screen unchanged after right encoder turn\n");
    ++failures;
  }

  // Button press: right encoder push (select mode -> persistent change),
  // then button A.
  uint8_t before_btn[1024];
  memcpy(before_btn, emu::screen_pages(), 1024);
  emu::set_button(emu::ENC_R_PUSH, true);
  run_ms(50);
  emu::set_button(emu::ENC_R_PUSH, false);
  bool btn_changed = false;
  for (int i = 0; i < 300; ++i) {
    run_ms(1);
    if (memcmp(before_btn, emu::screen_pages(), 1024) != 0) {
      btn_changed = true;
      break;
    }
  }
  emu::set_button(emu::BUTTON_A, true);
  run_ms(100);
  emu::set_button(emu::BUTTON_A, false);
  run_ms(100);
  if (btn_changed) {
    printf("PASS: screen changed after button press\n");
  } else {
    printf("FAIL: screen unchanged after button press\n");
    ++failures;
  }
  emu::screenshot_pbm("after_button.pbm");

  printf("== %s (%d failure%s) ==\n", failures ? "FAIL" : "PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
