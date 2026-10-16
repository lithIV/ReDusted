#include "game_hooks.h"
#include <windows.h>
#include <MinHook.h>
#include "config.h"
#include "framepacing.h"
#include "log.h"
#include "pattern.h"

namespace game {

// Main frame tick (From_Dust.exe 2011-09-05 build: 0x5C1960). It counts frames
// within a one-second window and skips the frame while
// frames / elapsed > 30.0, comparing against a 30.0f constant other code
// also reads. We repoint that one comparison at our own threshold.
//   mov eax,[frameCount]; test eax,eax; je; cvtsi2ss xmm1,eax;
//   divss xmm1,xmm0; comiss xmm1,[30.0f]; ja skip
static const char* kFrameCapSig = "A1 ?? ?? ?? ?? 85 C0 74 ?? F3 0F 2A C8 F3 0F 5E C8 0F 2F 0D ?? ?? ?? ?? 77";
static constexpr int kFrameCapOperand = 20;

// 16-byte aligned: comiss reads its operand as an xmmword.
alignas(16) static float g_capThreshold[4] = {30.0f, 0, 0, 0};

static bool WriteCode(void* at, const void* data, size_t size) {
  DWORD old;
  if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
  memcpy(at, data, size);
  VirtualProtect(at, size, old, &old);
  FlushInstructionCache(GetCurrentProcess(), at, size);
  return true;
}

static bool PatchFrameCap(HMODULE exe, float threshold) {
  uint8_t* hit = FindPattern(exe, kFrameCapSig);
  if (!hit) {
    LOG("Frame cap: signature not found, leaving the game's 30 fps cap in place");
    return false;
  }
  g_capThreshold[0] = threshold;
  auto addr = reinterpret_cast<uint32_t>(&g_capThreshold[0]);
  bool ok = WriteCode(hit + kFrameCapOperand, &addr, sizeof(addr));
  LOG("Frame cap: %p -> %.0f fps %s", hit, threshold, ok ? "patched" : "patch FAILED");
  return ok;
}

// TODO: the terrain/water/lava simulation lives in GalileoSimulatorWater.dll
// (loaded from the SSE3/SSSE3/SSE41 subfolder by the exe). Its step function
// needs to be identified before it can be hooked to run at a fixed rate via
// pacing::TakeSimSteps().
static bool InstallSimHook() {
  LOG("Simulation hook: not implemented yet");
  return false;
}

void Install() {
  HMODULE exe = GetModuleHandleW(nullptr);
  bool simFixed = InstallSimHook();

  if (!simFixed && !g_cfg.forceUnlock) {
    LOG("Frame cap: kept at 30 fps because the simulation fix is not active (set ForceUnlock=1 to override)");
    pacing::SetUnlocked(false);
    return;
  }
  // Our own limiter in Present does the pacing; the game's per-second
  // counter would only make frame times uneven, so its threshold is lifted out of reach.
  pacing::SetUnlocked(PatchFrameCap(exe, 100000.0f));
}

}  // namespace game
