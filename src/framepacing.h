#pragma once

namespace pacing {
void Init();
// Called once per presented frame: sleeps to hold the FPS limit and advances
// the simulation clock.
void OnPresent();
// Number of fixed simulation steps the next frame should run.
int TakeSimSteps();
// True once the 30 fps cap is lifted (needs the sim hook, or ForceUnlock).
bool Unlocked();
void SetUnlocked(bool unlocked);
}
