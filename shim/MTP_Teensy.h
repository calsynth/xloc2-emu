// Minimal MTP_Teensy stub.
#pragma once

class FS;

class MTP_class {
 public:
  void begin() {}
  void loop() {}
  int addFilesystem(FS&, const char*) { return 0; }
};

extern MTP_class MTP;
