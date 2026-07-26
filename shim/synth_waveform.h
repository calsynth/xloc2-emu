// Forwarder: the real vendored Teensy Audio header. This file exists (rather
// than being deleted) because shim/ is first on the include path and used to
// hold a silent stub; keeping a forwarder guarantees no stale stub can shadow
// the real DSP on any checkout.
#pragma once
#include "../third_party/teensy-audio/synth_waveform.h"
