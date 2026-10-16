#pragma once
#include <windows.h>

namespace sim {
// Runs the terrain/water simulation at Framerate.SimulationHz whatever the
// frame rate. The hooks attach when the game creates its simulator; if that
// build is not recognised then, `onUnavailable` is called (the frame cap is
// put back).
bool Install(void (*onUnavailable)());
// Hooks the exe's simulation driver so it runs at SimulationHz.
bool InstallDriver(HMODULE exe);
}
