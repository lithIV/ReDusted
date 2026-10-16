# FromDustFix

A runtime mod for the PC version of *From Dust* (2011). It requires your own
copy of the game; no game files are included in this repository.

## Features

| Feature | Status |
|---|---|
| Borderless windowed | Done (hooks window creation/styling and D3D9 device creation) |
| `.ini` settings | Done (`dist/FromDustFix.ini`) |
| 30 fps cap removal | Done, but held back by default until the simulation fix lands |
| Frame limiter / vsync control | Done (active once unlocked) |
| Fixed-rate terrain/water simulation (60+ fps without fast physics) | In progress: the sim lives in `GalileoSimulatorWater.dll` |
| Subtitles for voice lines | Planned |
| New levels | Planned |

## Install

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   (32-bit) into the game folder as `dinput8.dll` (the game imports DINPUT8).
2. Copy `FromDustFix.asi` and `FromDustFix.ini` next to `From_Dust.exe`.
3. Set the game to windowed mode for borderless.
4. Check `FromDustFix.log` if something doesn't work.

## Build

Cross-compile from Linux (32-bit MinGW):

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or with Visual Studio, configure a Win32 (x86) build with CMake.

## Reverse-engineering notes (From_Dust.exe, build of 2011-09-05)

- `0x5C1960` main frame tick: counts frames per one-second window and skips
  the frame while `frames / elapsed > 30.0f` (constant at `0x8A3104`, shared
  with other code). Patched by repointing the `comiss` operand at `0x5C19E3`.
- `0x5106F0` engine clock: real per-frame delta time stored at `0x92975C`,
  clamped to 1/6 s, scaled by `0x90C824` * `0x90C828`; fixed-dt override at
  `0x929764`. Passed to the main update at `0x5C1AC3`.
- `0x5D9CA0` loads `GalileoKernel.dll` (job system), `GalileoTexture.dll` and
  `GalileoSimulatorWater.dll` from a CPU-specific subfolder (`SSE41`/`SSSE3`/`SSE3`).
  The water sim is created via ordinal 1 (`0x91C238`) and destroyed via
  ordinal 2 (`0x91C23C`); the instance is kept at `[manager+0xC]`.
- Command-line options include `-fullscreen`, `-vsync`, `-msaa`, `-res`.

MinHook (BSD-2-Clause) is vendored in `external/minhook`.
