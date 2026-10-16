#pragma once

namespace trace {
// Logs the first call of every script native when [Debug] TraceNatives=1.
bool Install();
// Enables the wrappers registered since the last frame (called from the frame tick).
void OnFrame();
}
