#pragma once

namespace game {
// Finds the game's own code by signature and patches it. Features whose
// signatures are not found are disabled and logged, never guessed.
void Install();
}
