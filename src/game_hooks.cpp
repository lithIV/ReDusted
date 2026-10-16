#include "game_hooks.h"
#include <windows.h>
#include <MinHook.h>
#include <cstring>
#include "config.h"
#include "framepacing.h"
#include "log.h"
#include "pattern.h"
#include "simulation.h"
#include "trace.h"

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

// The frame tick starts 0x6F bytes before the cap comparison:
//   push ecx; push ebp; push esi; push edi; mov esi,ecx; call ...
static constexpr int kTickStartFromCapSig = 0x6F;
static const uint8_t kTickPrologue[] = {0x51, 0x55, 0x56, 0x57, 0x8B, 0xF1, 0xE8};

using FrameTick_t = void(__fastcall*)(void* self, void* edx);
static FrameTick_t oFrameTick;

// Every frame of the game (menus included) goes through this tick, whatever
// path the renderer takes, so the frame limiter lives here.
static void __fastcall hkFrameTick(void* self, void* edx) {
  trace::OnFrame();
  pacing::OnGameFrame();
  oFrameTick(self, edx);
}

static bool HookFrameTick(HMODULE exe) {
  uint8_t* hit = FindPattern(exe, kFrameCapSig);
  if (!hit) return false;
  uint8_t* tick = hit - kTickStartFromCapSig;
  if (memcmp(tick, kTickPrologue, sizeof(kTickPrologue)) != 0) {
    LOG("Frame tick: unexpected code at %p, limiting in Present instead", tick);
    return false;
  }
  MH_STATUS s = MH_CreateHook(tick, reinterpret_cast<LPVOID>(&hkFrameTick), reinterpret_cast<LPVOID*>(&oFrameTick));
  LOG("Frame tick: hook %p %s", tick, s == MH_OK ? "ok" : MH_StatusToString(s));
  return s == MH_OK;
}

// Called if the simulator turns out to be a build the hook does not know:
// the comparison goes back to the game's own 30 fps.
static void RelockFrameCap() {
  if (g_cfg.forceUnlock) return;
  g_capThreshold[0] = 30.0f;
  pacing::SetUnlocked(false);
}

void Install() {
  HMODULE exe = GetModuleHandleW(nullptr);
  // Installed either way so the simulation rate shows up in the log.
  bool simFixed = sim::Install(&RelockFrameCap) && sim::InstallDriver(exe) && g_cfg.fixedSimulation;

  if (!simFixed && !g_cfg.forceUnlock) {
    LOG("Frame cap: kept at 30 fps because the simulation fix is not active (set ForceUnlock=1 to override)");
    pacing::SetUnlocked(false);
    return;
  }
  // Our own limiter in the frame tick does the pacing; the game's per-second
  // counter would only make frame times uneven, so its threshold is lifted out of reach.
  bool unlocked = PatchFrameCap(exe, 100000.0f);
  pacing::SetUnlocked(unlocked);
  if (unlocked) pacing::UseGameFrames(HookFrameTick(exe));
}

}  // namespace game
