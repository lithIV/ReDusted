#pragma once
#include <windows.h>
#include <cstdint>

// IDA-style signature scan, e.g. "55 8B EC 83 E4 F0 ?? ?? 56".
// Searches the executable sections of `module`. Returns nullptr if not found
// or if the pattern matches more than once (an ambiguous match is unsafe to hook).
uint8_t* FindPattern(HMODULE module, const char* pattern);

// Follows a rel32 at `p` (e.g. the operand of E8/E9) to its absolute target.
inline uint8_t* ResolveRel32(uint8_t* p) {
  return p + 4 + *reinterpret_cast<int32_t*>(p);
}
