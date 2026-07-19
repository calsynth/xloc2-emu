// SD card stub backed by the host FS shim (state_dir/sdcard).
#pragma once

#include "FS.h"

#define BUILTIN_SDCARD 254

class SDClass : public FS {
 public:
  SDClass() : FS("sdcard") {}
  bool begin(uint8_t csPin = 10) { (void)csPin; return true; }
  bool mediaPresent() { return true; }
};

extern SDClass SD;
