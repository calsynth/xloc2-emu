// XLOC2 emulator: <malloc.h> shim.
// The firmware includes <malloc.h> and uses mallinfo() for its free-RAM
// display. glibc has both; macOS has neither. This header shadows the
// system one (shim/ is first on the include path): on Linux it defers to
// the real header, on Darwin it provides a minimal stand-in.
#pragma once

#if defined(__APPLE__)

#include <cstdlib>

// Minimal glibc-compatible mallinfo: the firmware only reads fordblks
// (free bytes) and uordblks (used bytes). Real numbers aren't available
// from Darwin's allocator in this form; zeros keep the RAM display sane.
struct mallinfo {
  int arena = 0;
  int ordblks = 0;
  int smblks = 0;
  int hblks = 0;
  int hblkhd = 0;
  int usmblks = 0;
  int fsmblks = 0;
  int uordblks = 0;
  int fordblks = 0;
  int keepcost = 0;
};

static inline struct mallinfo mallinfo() { return {}; }

#else
#include_next <malloc.h>
#endif
