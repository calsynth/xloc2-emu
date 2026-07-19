// Headless boot test for the XLOC2 emulator, driven through the dlopen'd
// core module (the same path the desktop app uses).
//
// Boots the firmware, runs ~4s of virtual time, screenshots the display,
// exercises an encoder turn and a button press (verifying the screen
// changes), then performs a full core reload cycle (flush, dlclose, dlopen a
// fresh copy, boot) to prove hot reload works.
//
// Usage: headless [path/to/phz_core.so]
// Default: phz_core.so/.dylib next to the executable, then ./build, then cwd.
#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "xloc2_core_api.h"

#if defined(__APPLE__)
static const char* kCoreName = "phz_core.dylib";
#else
static const char* kCoreName = "phz_core.so";
#endif

static std::string exe_dir() {
#if defined(__linux__)
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    std::string p(buf);
    const size_t slash = p.rfind('/');
    if (slash != std::string::npos) return p.substr(0, slash);
  }
#endif
  return ".";
}

static std::string find_core(int argc, char** argv) {
  if (argc > 1) return argv[1];
  const std::string candidates[] = {
      exe_dir() + "/" + kCoreName,
      std::string("./build/") + kCoreName,
      std::string("./") + kCoreName,
  };
  for (const auto& c : candidates)
    if (access(c.c_str(), R_OK) == 0) return c;
  return candidates[0];
}

struct Core {
  void* handle = nullptr;
  const Xloc2CoreApi* api = nullptr;
};

static bool load_core(const std::string& path, Core& core) {
  core.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!core.handle) {
    fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
    return false;
  }
  auto get_api = (Xloc2CoreGetApiFn)dlsym(core.handle, XLOC2_CORE_ENTRY_POINT);
  if (!get_api) {
    fprintf(stderr, "FAIL: dlsym(%s): %s\n", XLOC2_CORE_ENTRY_POINT, dlerror());
    return false;
  }
  core.api = get_api();
  if (!core.api || core.api->api_version != XLOC2_CORE_API_VERSION) {
    fprintf(stderr, "FAIL: core api_version %u != host %u\n",
            core.api ? core.api->api_version : 0u, XLOC2_CORE_API_VERSION);
    return false;
  }
  return true;
}

static const Xloc2CoreApi* g_api = nullptr;

static void run_ms(uint64_t ms) {
  for (uint64_t i = 0; i < ms; ++i) {
    g_api->step_us(1000);
    g_api->run_loop_once();
  }
}

static int count_lit(const uint8_t* pages) {
  int n = 0;
  for (int i = 0; i < 1024; ++i) n += __builtin_popcount(pages[i]);
  return n;
}

int main(int argc, char** argv) {
  int failures = 0;

  printf("== XLOC2 emulator headless boot test (dlopen) ==\n");
  const std::string core_path = find_core(argc, argv);
  Core core;
  if (!load_core(core_path, core)) return 1;
  g_api = core.api;
  printf("core: %s\n  fw_version: %s\n  build_info: %s\n", core_path.c_str(),
         core.api->fw_version, core.api->build_info);

  g_api->boot("./state");

  // ~4 seconds of virtual time: splash screens play out, app menu comes up.
  run_ms(4000);

  uint8_t before[1024];
  memcpy(before, g_api->screen_pages(), 1024);
  int lit = count_lit(before);
  g_api->screenshot_pbm("boot.pbm");
  printf("boot screen: %d lit pixels (screenshot: boot.pbm)\n", lit);
  if (lit > 0) {
    printf("PASS: boot screen non-blank\n");
  } else {
    printf("FAIL: boot screen blank\n");
    ++failures;
  }

  // Encoder turn: should move a cursor / show feedback on screen. Some
  // reactions are transient (popups), so watch the whole window.
  g_api->turn_encoder_right(2);
  bool enc_changed = false;
  for (int i = 0; i < 300; ++i) {
    run_ms(1);
    if (memcmp(before, g_api->screen_pages(), 1024) != 0) {
      enc_changed = true;
      if (i < 60) g_api->screenshot_pbm("after_encoder.pbm");
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
  memcpy(before_btn, g_api->screen_pages(), 1024);
  g_api->set_button(XLOC2_BTN_ENC_R, 1);
  run_ms(50);
  g_api->set_button(XLOC2_BTN_ENC_R, 0);
  bool btn_changed = false;
  for (int i = 0; i < 300; ++i) {
    run_ms(1);
    if (memcmp(before_btn, g_api->screen_pages(), 1024) != 0) {
      btn_changed = true;
      break;
    }
  }
  g_api->set_button(XLOC2_BTN_A, 1);
  run_ms(100);
  g_api->set_button(XLOC2_BTN_A, 0);
  run_ms(100);
  if (btn_changed) {
    printf("PASS: screen changed after button press\n");
  } else {
    printf("FAIL: screen unchanged after button press\n");
    ++failures;
  }
  g_api->screenshot_pbm("after_button.pbm");

  // ---- hot reload cycle: flush, unload, load a fresh copy, boot again ----
  // (mirrors what the app does on "Reload"; state survives via ./state)
  g_api->eeprom_flush();
  g_api = nullptr;
  dlclose(core.handle);
  Core core2;
  if (!load_core(core_path, core2)) {
    printf("FAIL: core reload (dlopen after dlclose)\n");
    return failures + 1;
  }
  g_api = core2.api;
  g_api->boot("./state");
  run_ms(4000);
  const int lit2 = count_lit(g_api->screen_pages());
  if (lit2 > 0) {
    printf("PASS: core reload cycle boots (%d lit pixels)\n", lit2);
  } else {
    printf("FAIL: blank screen after core reload cycle\n");
    ++failures;
  }

  printf("== %s (%d failure%s) ==\n", failures ? "FAIL" : "PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
