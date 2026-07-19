// LittleFS stub backed by the host FS shim.
#pragma once

#include "FS.h"

class LittleFS : public FS {
 public:
  LittleFS() : FS("lfs") {}
  bool begin() { return true; }
  bool quickFormat() { return true; }
  bool lowLevelFormat(char = '.') { return true; }
};

class LittleFS_Program : public FS {
 public:
  LittleFS_Program() : FS("lfs") {}
  bool begin(uint32_t size) { (void)size; return true; }
  bool quickFormat() { return true; }
  bool lowLevelFormat(char = '.') { return true; }
};

class LittleFS_RAM : public LittleFS_Program {};
