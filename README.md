# XLOC2 Emulator

A desktop emulation of the **Calsynth XLOC2** (Teensy 4.1 Ornament & Crime) running the
**real Phazerville Suite firmware** — not a reimplementation. The firmware source is
vendored unmodified (five one-line `#if defined(__arm__)`-guarded portability edits,
logged in `FIRMWARE_PATCHES.md`) and compiled natively against a hardware-shim layer.

The firmware even *thinks* it's an XLOC2: the emulated ADC returns the 0.30 V hardware-ID
voltage, so the firmware's own `Pinout_Detect()` enables the CalSynth XL configuration
(8 CV in / 8 CV out, ±10 V outputs, large OLED, Quadrants).

## Features

- Faithful panel: live OLED (rendered from the real SH1106 framebuffer), two encoders
  (drag / mouse-wheel / arrow keys), buttons A/B/X/Y/Z, 20 jacks with live meters.
- **CV/gate I/O through any DC-coupled audio interface**: routing matrix maps each jack
  to a hardware channel, with per-jack gain/offset and global full-scale calibration.
  Trigger inputs use a software comparator with hysteresis.
- Deterministic clocking: the firmware's 16.666 kHz core ISR and 1 kHz UI timer are
  driven sample-accurately from the audio callback (or a fallback clock with no device).
- Persistent state: EEPROM, PhzConfig and a virtual SD card live in
  `~/Library/Application Support/Calsynth/XLOC2` (macOS) / `~/.config/Calsynth/XLOC2` (Linux).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8 --target xloc2      # desktop app
cmake --build build -j8 --target headless   # headless firmware tests
./build/headless
```

Requirements: CMake ≥ 3.20, a C++17 compiler. macOS: Xcode command-line tools.
Linux: ALSA/X11 dev packages (see `.github/workflows/build.yml`). JUCE 8 is fetched
automatically on first configure.

## GitHub / CI

Push this repo to GitHub and Actions will run the headless firmware tests on Linux and
build an Apple Silicon `XLOC2.app` artifact on every push to `main`:

```sh
git remote add origin git@github.com:<you>/xloc2-emu.git
git push -u origin main
```

The app is unsigned; on first launch right-click → Open to pass Gatekeeper.

## Layout

- `firmware/` — vendored Phazerville source (upstream: djphazer/O_C-Phazerville)
- `shim/` — Arduino/Teensy API + peripheral shims (EEPROM, SPI, OLED, ADC/DAC, Audio stubs)
- `core/` — emulator core: virtual clock, deterministic timers, panel/CV/screen API (`emu.h`)
- `app/` — JUCE desktop app (panel UI, audio engine, routing)
- `test/` — headless boot/interaction tests
- `third_party/teensy-x86-stubs` — vendored host stubs (newdigate, MIT; with local additions)

## Status / roadmap

- [x] Firmware core native build + headless tests
- [x] Desktop app: panel, OLED, controls, CV routing (Linux-verified; macOS via CI)
- [ ] Audio applets DSP (currently compile but are silent stubs) — port Teensy
      AudioStream scheduling + DSP, map I2S stereo audio to interface channels
- [ ] MIDI (DIN + USB) to host MIDI ports
- [ ] Panel-art skin from real XLOC2 faceplate graphics
