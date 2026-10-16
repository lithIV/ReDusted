# ReDusted

woohoo one of the first mods!!!

## Features

| Feature | Status |
|---|---|
| Borderless windowed | Done (hooks window creation/styling and D3D9 device creation) |
| `.ini` settings | Done (`dist/ReDusted.ini`) |
| 30 fps cap removal | Done |
| Frame limiter / vsync control | Done |
| Fixed-rate terrain/water simulation (60+ fps without fast physics) | Done, tested in game |
| Skip startup logos / cutscenes (separate ini toggles) | Done, tested in game |
| Subtitles for voice lines | Not needed: the game already shows the shaman's lines as text (checked in game) |
| New levels | Planned |

## Install

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   (32-bit) into the game folder as `dinput8.dll` (the game imports DINPUT8).
2. Copy `ReDusted.asi` and `ReDusted.ini` next to `From_Dust.exe`.
3. Set the game to windowed mode for borderless.
4. Check `ReDusted.log` if something doesn't work. With the simulation fix
   working it shows `Simulation hook: active at 30 Hz` once a level loads.

Things to look at in the first playtest at 60 fps: water, lava and sand should
flow at the same speed as at 30 fps, and the breath's take/drop rate should
feel the same. If something is off, set `FixedSimulation=0` to get the
original behaviour back, and include the log with the report.

## Build

On Windows with Visual Studio or the Build Tools (C++ workload), run
`build.bat`; it writes `build\ReDusted.asi`. `tests\run.bat` builds the
mod plus a fake simulator DLL and checks the fixed-rate simulation offline.

Cross-compile from Linux (32-bit MinGW):

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or with Visual Studio, configure a Win32 (x86) build with CMake.

## Reverse-engineering notes (From_Dust.exe, build of 2011-09-05)

### From_Dust.exe

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
- `0x4136E0` returns the simulator (`[[0x921534]+0xC]`).
- Command-line options include `-fullscreen`, `-vsync`, `-msaa`, `-res`.

### GalileoSimulatorWater.dll (PDBs ship with the game)

The SSE3, SSSE3 and SSE41 builds differ in code addresses but share the class
layout and the `SimulatorWater` vtable:

- Ordinal 1 `sCreate`, 2 `sDestroy`, 3 `sStaticInit`. The exe resolves them
  with `GetProcAddress` by ordinal.
- Vtable slot 53 `UpdateSimulatorAndDisplay` (thiscall, 17 args, `ret 0x44`)
  launches the frame's kernel jobs. The simulation jobs (`SimulatorGanja` and
  three `SimulatorWater` passes) only start while the "simulate" flag at
  `+0x930` is set. Display, particles, vegetation display, paths, sound and
  perception jobs run either way.
- Slot 55 `SyncSimulator` waits for those jobs and, if `+0x930` is set,
  increments the double-buffer frame counter at `+0x92C`.
- `Modify` (slot 9) applies brush edits (add/remove water and lava, soft and
  hard ground) straight to the current buffer.

The exe calls `UpdateSimulatorAndDisplay` from its simulation driver at
`0x455CC0` (thiscall, one arg, called from `0x41D859`). The driver has its own
cap: it skips while it ran more than 35 times a second (`35.0f` at `0x90C488`,
2 s window, check at `+0x79`). `SyncSimulator` runs every frame. At 30 fps
that is one update and one sync per frame. Above 30 fps the driver runs in
uneven bursts (about 35/s) and every extra sync flips the buffers with no new
step behind it, so the water crawls and stutters.

The fix:
- Hooks the driver and runs it only when a step is due at `SimulationHz`
  (real time, small early tolerance, deficit carried). The 35/s check is
  repointed at a huge threshold.
- Patches vtable slots 53 and 55 (via the `sCreate` wrapper from a
  `GetProcAddress` hook): a sync may flip the buffers only once per launched
  step, so syncs without a new step keep `+0x930` clear while they wait.
- Measured in game at 60 fps: 30 driver runs, 30 steps, 60 syncs and 30 flips
  per second, the same step and flip rate as the stock game at 30 fps.

If the DLL or the driver code does not match the expected instructions,
nothing is hooked and the 30 fps cap comes back. The frame limiter runs in the
game's own frame tick (`0x5C1960`), because Present hooks only saw the first
frame.

### Videos

The game plays videos with Bink through its own player (`0x5E0060`), which
opens them from the bigfile (`BinkOpen` with a custom IO, flags `0x02004400`)
and stops once `bink->FrameNum` (`+0xC`) reaches `bink->Frames` (`+0x8`,
check at `0x5E0163`). A skipped video is cut to one frame right after
`BinkOpen`. Looping videos get their length back in `BinkSetWillLoop`, which
the player calls before the first frame. The first video of a session is the
startup logos (1280x720, 179 frames); the cutscene after choosing a map is the
next one (532 frames).

The age-rating screen before the logos cannot be skipped: it is a loading
screen. The boot scripts call `WOR_LoadDynamicGroup` and then poll
`WOR_IsLoading` while the main-menu world loads (about 5 s), and the logos
follow once it is done. Running game time 10x faster during boot (engine time
scale `[0x90C828]`, which the clock init at `0x5104E0` resets to 1.0) did not
shorten it.

### Voice lines and text (for subtitles)

The game runs on Ubisoft's Jade/LyN engine. Gameplay scripts are compiled into
`AInative/bin/dll/dustai.win32o.f.dll` and call engine natives listed in
`AIFunctionsMapping.txt`. The exe registers each native with two wrappers
(stack-machine and direct) via `0x82B1C0`.

**Data.** Text lives uncompressed in `BinPC/Gen_Common.lin.bf` (around file
offset `0x2E8C0000`). Each entry has a key, then one record per language slot:
`u32 byte length` + UTF-16 text (unused slots say `NOT LOCALIZED`; English,
French, Italian, Spanish and German are filled). Text may contain markup such
as `<img src='bouton_LT'/>`. There are 65 voiced lines, all spoken by the
shaman, keyed `VOICE_M_EVTS_ME_<map>_<nn>_<nn>[m]_SHA<id>` (e.g.
`VOICE_M_EVTS_ME_MAP1_01_01_SHA485` = "And here we are, as on the first day").

**Text natives (exe).**

| Native | Wrapper(s) | What it reads |
|---|---|---|
| `TXT_SubtitleGet` | `0x66CDA0` / `0x66CD60` | byte at `entry+0x08` |
| `TXT_SoundGet` | `0x66CE80` / `0x66CE30` | `[entry + lang*24 + 0x18]` → sound object, key at `+4` |
| `TXT_DelayGet` | `0x66CF50` / `0x66CF20` | float at `entry+0x30C` |
| `TXT_cwstr` | `0x66B970` / `0x66B960` → `0x5CD6B0` | `[entry + lang*24 + 0x10]` = `wchar_t*` |
| `TXT_LangGet` | `0x66BBE0` / `0x66BBD0` | current language index at `0x9384E4` |

`entry` comes from the text manager at `[0x924F28]`, vtable slot 10 (`+0x28`),
which takes a text id. Each language slot in a loaded entry is 24 bytes, starting
at `+0x10`: the text pointer first, then the sound at `+8`.

The fields `TXT_OwnerGet`, `TargetGet`, `AnimGet`, `FacialGet` and
`PriorityGet` show the engine's dialogue model: a line has a speaker, a sound,
an animation and a priority. The script `SoundManager` has an
`m_subtitleSKM_Id` mixer preset (it ducks other audio while a line plays). The
UI draws text through Flash movies (`FIR_MovieElementSetWText`, tooltip fields
`TextRankId`/`DisplayTooltip` in the scripts). The engine also has a plain 2D
text renderer (`STR_Display_W`, sprite fonts).

**Drawing.** The exe already imports `d3dx9_43.dll`, so an `ID3DXFont` overlay
drawn from the existing `Present` hook needs no new dependency.

#### Result

A logging build (`[Subtitles] Log=1`) hooks the object lookup at `0x82F7A0`
(text manager vtable `0x8ECF28` slot 10; it serves every kind of loaded
object, not only text) and the sound start at `0x68C290` (thiscall: holder,
sound key, flags; used by `SND_Play` and `SND_InstanceRqPlay`). On the first
map, each shaman line is fetched for display through `TXT_cwstr` (lookup from
`0x5CD6CB`) at the same moment as `TXT_SoundGet` (from `0x66CE44`), 3 to 5
seconds apart, and the game shows it on screen. The English language slots of
these entries have no sound attached. So the voiced lines are already
subtitled, and no overlay is needed.

MinHook (BSD-2-Clause) is vendored in `external/minhook`.
