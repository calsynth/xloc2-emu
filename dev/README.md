# Fast applet development

Edit `dev/DevApplet.cpp`, rebuild, and the applet is running on the panel in
about a second. No hardware, no flashing, no 34-second wait.

```bash
cmake --build build --target phz_core      # ~1 s
```

The app reloads the core by itself. On the module, pick the applet called
**Dev** — that slot is your applet.

## Why it is fast

Every applet header is `#include`d into `applets/_config.h`, which is
`#include`d into `OC_apps.cpp`. That makes one ~63,000-line translation unit,
so editing *any* applet in the firmware tree recompiles all of it:

| what you touch | rebuild |
|---|---|
| `firmware/.../applets/AnyApplet.h` | **34 s** (recompiles `OC_apps.cpp`) |
| `dev/DevApplet.cpp` | **1 s** (one small TU + a 0.5 s relink) |

`applets/DevSlot.h` is a thin proxy registered in the applet registry as
"Dev". It forwards every call — `Start`, `Controller`, `View`, encoder,
button, save/load, help — to the applet in `dev/DevApplet.cpp`, which is
compiled separately. `OC_apps.cpp` never has to be rebuilt.

The whole thing is wrapped in `#ifdef EMULATOR`, so a PlatformIO build for a
real Teensy 4.1 never sees it.

## Writing the applet

Write it exactly as you would a real applet — the same base class, the same
`applet_name` / `applet_icon` macro style, the same helpers (`In`, `Out`,
`Clock`, `Gate`, `gfxPrint`, `EditMode`, `MoveCursor`, `Pack`/`Unpack`, …).
`firmware/software/src/applets/Boilerplate.h` is the canonical guide and
everything in it applies here.

Two differences, both at the bottom of the file:

- `SetHelp()` must be **public** (real applets keep it protected behind
  `APPLET_INTERFACE`). The proxy cannot reach a protected method through a
  base pointer, so `xloc_dev_applet_set_help()` exposes it.
- The two glue functions at the end create the instance and expose `SetHelp`.
  Leave them alone.

## Promoting it to a real applet

When you are happy with it:

1. Copy the class — and any `#define`s above it — into
   `firmware/software/src/applets/YourName.h`.
2. In `applets/_config.h` add `#include "YourName.h"` alongside the others,
   and a `DeclareApplet<YourName, <free id>, CAT_...>` line to the registry.
   IDs in use go up to 150; `200` is taken by the dev slot. Duplicate IDs are
   caught at compile time by a `static_assert`.
3. Move `SetHelp()` back under `protected:` if you want to match house style,
   and switch to `APPLET_INTERFACE(YourName, "YourName", ICON)` if you prefer.
4. One full rebuild (~35 s) to confirm it compiles inside the registry.
5. Commit and push per `EVERYDAYGITWORKFLOW.md`; CI cross-builds the real
   Teensy 4.1 hex so you can flash and confirm on hardware.

## Building a core from another Phazerville branch

`patches/dev_slot.patch` adds `DevSlot.h` and the two `_config.h` lines to any
Phazerville checkout, so the dev slot survives:

```bash
./scripts/build-core.sh --repo https://github.com/calsynth/phzdev --ref xloc-big-display
```

## Caveats

- One `DevApplet` instance is created per hemisphere slot, so putting "Dev" in
  two hemispheres at once gives you two independent instances — same as any
  other applet.
- The applet browser lists the slot as "Dev" (the registry reads a static
  name). The on-screen header shows whatever your applet's `applet_name()`
  returns, so you still see the real name while working.
- Audio applets use a different registry (`audio_applets/_config.h`) and are
  not covered by this slot yet. Same trick would work.
