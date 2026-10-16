#pragma once

namespace pacing {
void Init();
// Frame limiter entry points: the game's own frame tick when it could be
// hooked (UseGameFrames(true)), Present otherwise. Each sleeps to hold the
// FPS limit.
void OnPresent();
void OnGameFrame();
void UseGameFrames(bool use);
// Called once per frame by the simulator hook: true if a simulation step is
// due, measured in real time against Framerate.SimulationHz.
bool ConsumeSimStep();
// True once the 30 fps cap is lifted (needs the sim hook, or ForceUnlock).
bool Unlocked();
void SetUnlocked(bool unlocked);
}
