# Firmware patches (host/emulator build)

> The five edits below are also maintained as git-apply-compatible patch
> files in `patches/` (one per file, paths relative to the firmware source
> root). `scripts/build-core.sh --repo <url> --ref <ref>` shallow-clones any
> Phazerville version, applies them with `git apply --3way`, and builds a
> loadable core module (`phz_core-<ref>.so/.dylib`) the app can hot-load
> from its FW panel — no app rebuild needed. The vendored `firmware/` tree
> (upstream v2.0.1 + these edits) remains the default build source.

The goal is to run the *unmodified* Phazerville firmware; everything possible
is handled in `shim/` and `core/`. The following minimal edits to files under
`firmware/` were unavoidable because they contain ARM inline assembly or
ARM-ABI assumptions inside headers/TUs that cannot be shadowed (they are
reached via quoted, includer-relative `#include` paths). Every edit is guarded
by `#if defined(__arm__)` so hardware builds are byte-identical.

## firmware/software/src/util/util_math.h
`SSAT16`, `USAT16` (x2), `multiply_u32xu32_rshift24`,
`multiply_u32xu32_rshift`, `uhadd16` use ARM `ssat/usat/umull/uhadd16`
instructions. Added an `#else` branch with equivalent plain-C
implementations (64-bit widening multiply + clamps). Original ARM bodies
untouched under `#if defined(__arm__)`.

## firmware/software/src/src/extern/dspinst.h
The Teensy Audio DSP intrinsics header only had implementations for
`__ARM_ARCH_7EM__` and (partially) `KINETISL`; on other targets most
functions had *no return statement* (UB when called). Added a complete
`#if !defined(__arm__)` generic branch at the top (semantics per the ARM DSP
instruction definitions, matching newdigate's host port) and wrapped the
original content in `#else ... #endif // __arm__`.

## firmware/software/src/src/Audio/filter_variable2.cpp
`AudioFilterStateVariable2::update()` was only compiled under
`__ARM_ARCH_7EM__` / `KINETISL`, leaving the class abstract at link time on
host (missing vtable). Changed `#elif defined(KINETISL)` to `#else` so the
existing pass-through `update()` also builds on host. (No DSP yet on host —
phase 2 can lift the fixed/variable update paths to C.)

## firmware/software/src/Audio/AudioEffectModalResonator.h
FPU FPSCR flush-to-zero save/restore uses `vmrs`/`vmsr` inline asm. Wrapped
the three asm statements in `#if defined(__arm__)`.

## firmware/software/src/src/drivers/weegfx.cpp
`Graphics::print(uint32_t value, size_t width)` did not match its declaration
`print(uint32_t, unsigned)`; this only compiled on 32-bit ARM where
`size_t == unsigned int`. Changed the definition to `unsigned width`.
(Behavior identical on ARM.)

---

# Support-library patches (not firmware)

`/root/teensy-x86-stubs` (newdigate's Teensy-on-host stubs) was extended —
it is a porting aid, not firmware:

- `AudioStream.h/.cpp`: `AudioConnection` gained a default constructor and
  `connect(src, [out], dst, [in])` re-binding overloads returning `int`
  (matching the modern Teensy core API the audio applets use); members became
  pointers. Removed its local `min`/`max` templates (now provided by
  `shim/Arduino.h`); added `AudioNoInterrupts()/AudioInterrupts()` and a
  generic `CYCLE_COUNTER_APPROX_PERCENT`.
- `core_pins.h`: `_reboot_Teensyduino_` declared `extern "C"` (the firmware
  declares it that way).

# Emulator-side accommodations worth knowing about

- **First-run confirm prompt**: a factory-fresh module blocks inside
  `setup()` waiting for a button press ("Reset settings on EEPROM?").
  `emu::boot()` pre-seeds a minimal valid `GLOBALS.CFG` (PhzConfig format)
  on first boot of a state dir so headless boots run through. Delete the
  state dir to test, and script the buttons, if the true first-run flow is
  ever needed.
- **Busy-wait pump**: firmware busy-loops (splash screen countdown,
  "calibration saved" screens) spin on `millis()` + a full display frame
  buffer. On host, `millis()/micros()` (outside ISR context) advance virtual
  time until the frame buffer drains (`emu::host_pump()`), which is what the
  background ISRs would have done on hardware.
