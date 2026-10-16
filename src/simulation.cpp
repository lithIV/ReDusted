// Fixed-rate terrain/water/lava simulation.
//
// The simulation lives in GalileoSimulatorWater.dll, which the exe loads from
// the SSE3/SSSE3/SSE41 subfolder and creates through ordinal 1 (sCreate).
// SimulatorWater::UpdateSimulatorAndDisplay launches the simulation and display
// jobs, and SyncSimulator waits for them and, while the "simulate" flag at
// +0x930 is set, flips the double buffer (counter at +0x92c).
//
// The exe calls UpdateSimulatorAndDisplay from its simulation driver
// (0x455CC0), which has its own cap: it skips while it ran more than 35 times
// per second (2 s window). SyncSimulator, though, runs every frame. At 30 fps
// that is one update and one sync per frame; above it the driver runs in
// uneven bursts and every extra sync flips the buffers with no new step
// behind it, so water crawls and stutters.
//
// So the driver is run exactly when a step is due at SimulationHz (its own
// cap lifted), and a sync may only flip the buffers once per launched step.
// All three CPU builds of the DLL share the class layout and vtable (checked
// against the shipped PDBs).
#include "simulation.h"
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "config.h"
#include "framepacing.h"
#include "log.h"
#include "pattern.h"

namespace sim {

constexpr int kUpdateSlot = 53;  // UpdateSimulatorAndDisplay, thiscall, 17 dword args (ret 0x44)
constexpr int kSyncSlot = 55;    // SyncSimulator, thiscall, no args
constexpr size_t kSimulateFlag = 0x930;

struct UpdateArgs {
  uint32_t v[17];
};
static_assert(sizeof(UpdateArgs) == 0x44, "UpdateSimulatorAndDisplay pops 0x44 bytes");

// thiscall emulated with fastcall: `this` in ecx, edx unused, the rest on the stack.
using Update_t = uint32_t(__fastcall*)(uint8_t* self, void* edx, UpdateArgs args);
using Sync_t = void(__fastcall*)(uint8_t* self, void* edx);
using Driver_t = void(__fastcall*)(void* self, void* edx, uint32_t arg);
using Create_t = void*(__cdecl*)(void*, void*, void*, void*, void*);
using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);

static GetProcAddress_t oGetProcAddress;
static Create_t oCreate;
static Update_t oUpdate;
static Sync_t oSync;
static Driver_t oDriver;
static void (*g_onUnavailable)();
static bool g_launchedSinceSync = false;
static bool g_failed = false;

static volatile LONG& SimulateFlag(uint8_t* self) {
  return *reinterpret_cast<volatile LONG*>(self + kSimulateFlag);
}

// Call rates, logged every few seconds (also with FixedSimulation=0, to
// compare against the stock game).
static struct {
  LONGLONG start = 0;
  int drivers = 0, updates = 0, steps = 0, syncs = 0, flips = 0;
} g_stats;

static void LogStats() {
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  if (g_stats.start == 0) g_stats.start = now.QuadPart;
  double secs = double(now.QuadPart - g_stats.start) / double(freq.QuadPart);
  if (secs < 5.0) return;
  LOG("Simulation: per second %.1f driver runs, %.1f updates, %.1f steps, %.1f syncs, %.1f flips",
      g_stats.drivers / secs, g_stats.updates / secs, g_stats.steps / secs, g_stats.syncs / secs, g_stats.flips / secs);
  g_stats = {};
  g_stats.start = now.QuadPart;
}

static void __fastcall hkDriver(void* self, void* edx, uint32_t arg) {
  if (g_cfg.fixedSimulation && !pacing::ConsumeSimStep()) return;  // what the game's own skip does
  ++g_stats.drivers;
  oDriver(self, edx, arg);
}

static uint32_t __fastcall hkUpdate(uint8_t* self, void* edx, UpdateArgs args) {
  ++g_stats.updates;
  if (SimulateFlag(self) != 0) {
    ++g_stats.steps;
    g_launchedSinceSync = true;
  }
  LogStats();
  return oUpdate(self, edx, args);
}

static void __fastcall hkSync(uint8_t* self, void* edx) {
  ++g_stats.syncs;
  volatile LONG& flag = SimulateFlag(self);
  if (!g_cfg.fixedSimulation || g_launchedSinceSync || flag == 0) {
    if (flag != 0) ++g_stats.flips;
    g_launchedSinceSync = false;
    oSync(self, edx);
    return;
  }
  // Nothing was simulated since the last flip: wait, but keep the buffers.
  LONG saved = flag;
  flag = 0;
  oSync(self, edx);
  flag = saved;
}

static bool Contains(const uint8_t* code, size_t size, const char* hex, int* count = nullptr) {
  uint8_t pat[16];
  size_t n = 0;
  for (const char* p = hex; p[0] && p[1] && n < sizeof(pat); p += 2) {
    char byte[3] = {p[0], p[1], 0};
    pat[n++] = static_cast<uint8_t>(strtoul(byte, nullptr, 16));
  }
  int hits = 0;
  for (size_t i = 0; i + n <= size; ++i) {
    if (memcmp(code + i, pat, n) == 0) ++hits;
  }
  if (count) *count = hits;
  return hits > 0;
}

static bool InModule(HMODULE mod, void* p) {
  auto* base = reinterpret_cast<uint8_t*>(mod);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  return p >= base && p < base + nt->OptionalHeader.SizeOfImage;
}

// Only hooks code that looks exactly like the build this was written against.
static bool LooksRight(HMODULE mod, void* update, void* sync) {
  if (!InModule(mod, update) || !InModule(mod, sync)) return false;
  auto* u = static_cast<const uint8_t*>(update);
  auto* s = static_cast<const uint8_t*>(sync);
  int flagChecks = 0;
  return Contains(s, 0xA0, "83BE3009000000")             // cmp dword [esi+0x930], 0
         && Contains(s, 0xA0, "FF862C090000")            // inc dword [esi+0x92c]
         && Contains(u, 0xF00, "83BB3009000000", &flagChecks) && flagChecks == 1  // cmp dword [ebx+0x930], 0
         && Contains(u, 0xF00, "C24400");                // ret 0x44
}

static void Fail(const char* why) {
  LOG("Simulation hook: %s; the frame cap stays at 30 fps", why);
  g_failed = true;
  if (g_onUnavailable) g_onUnavailable();
}

static bool PatchSlot(void** slot, void* hook) {
  DWORD old;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
  *slot = hook;
  VirtualProtect(slot, sizeof(void*), old, &old);
  return true;
}

// The vtable lives in the DLL image, so patching it covers every instance and
// a reloaded DLL simply gets patched again on its next sCreate.
static void HookInstance(void* obj) {
  if (!obj || g_failed) return;
  void** vtable = *static_cast<void***>(obj);
  if (vtable[kUpdateSlot] == reinterpret_cast<void*>(&hkUpdate)) return;

  HMODULE mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(vtable), &mod);
  if (!mod || !LooksRight(mod, vtable[kUpdateSlot], vtable[kSyncSlot])) {
    Fail("unrecognised GalileoSimulatorWater.dll build");
    return;
  }
  oUpdate = reinterpret_cast<Update_t>(vtable[kUpdateSlot]);
  oSync = reinterpret_cast<Sync_t>(vtable[kSyncSlot]);
  if (!PatchSlot(&vtable[kSyncSlot], reinterpret_cast<void*>(&hkSync)) ||
      !PatchSlot(&vtable[kUpdateSlot], reinterpret_cast<void*>(&hkUpdate))) {
    Fail("could not patch the simulator vtable");
    return;
  }
  LOG("Simulation hook: active at %d Hz (module %p, update %p, sync %p)", g_cfg.simHz, mod, oUpdate, oSync);
}

static void* __cdecl hkCreate(void* a, void* b, void* c, void* d, void* e) {
  void* obj = oCreate(a, b, c, d, e);
  HookInstance(obj);
  return obj;
}

static bool IsSimulatorDll(HMODULE mod) {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
  const wchar_t* name = L"\\GalileoSimulatorWater.dll";
  size_t len = wcslen(name);
  return n >= len && _wcsicmp(path + n - len, name) == 0;
}

static FARPROC WINAPI hkGetProcAddress(HMODULE mod, LPCSTR proc) {
  FARPROC p = oGetProcAddress(mod, proc);
  // The exe asks for ordinal 1 (sCreate); hand it our wrapper instead.
  if (p && reinterpret_cast<uintptr_t>(proc) == 1 && IsSimulatorDll(mod)) {
    oCreate = reinterpret_cast<Create_t>(p);
    LOG("Simulation hook: sCreate %p wrapped", p);
    return reinterpret_cast<FARPROC>(&hkCreate);
  }
  return p;
}

// Simulation driver (From_Dust.exe 0x455CC0, thiscall, one stack arg). Its
// rate check `comiss xmm1, [35.0f]` sits at +0x79.
static const char* kDriverSig = "F3 0F 10 0D ?? ?? ?? ?? 83 EC 20 F6 05 ?? ?? ?? ?? 01 53 55 56 57 8B F1";
static constexpr int kDriverCapCheck = 0x79;
alignas(16) static float g_driverCap[4] = {100000.0f, 0, 0, 0};

bool InstallDriver(HMODULE exe) {
  uint8_t* driver = FindPattern(exe, kDriverSig);
  if (!driver) {
    LOG("Simulation driver: signature not found");
    return false;
  }
  uint8_t* check = driver + kDriverCapCheck;
  static const uint8_t kComiss[] = {0x0F, 0x2F, 0x0D};
  if (memcmp(check, kComiss, sizeof(kComiss)) != 0) {
    LOG("Simulation driver: unexpected code at %p", check);
    return false;
  }
  if (g_cfg.fixedSimulation) {
    // Lift the game's 35/s cap; the hook decides when the driver runs.
    auto addr = reinterpret_cast<uint32_t>(&g_driverCap[0]);
    DWORD old;
    if (!VirtualProtect(check + 3, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(check + 3, &addr, 4);
    VirtualProtect(check + 3, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), check + 3, 4);
  }
  MH_STATUS s = MH_CreateHook(driver, reinterpret_cast<LPVOID>(&hkDriver), reinterpret_cast<LPVOID*>(&oDriver));
  LOG("Simulation driver: hook %p %s", driver, s == MH_OK ? "ok" : MH_StatusToString(s));
  return s == MH_OK;
}

bool Install(void (*onUnavailable)()) {
  g_onUnavailable = onUnavailable;
  MH_STATUS s = MH_CreateHookApi(L"kernel32", "GetProcAddress", reinterpret_cast<LPVOID>(&hkGetProcAddress),
                                 reinterpret_cast<LPVOID*>(&oGetProcAddress));
  LOG("Simulation hook: GetProcAddress %s", s == MH_OK ? "hooked" : MH_StatusToString(s));
  return s == MH_OK;
}

}  // namespace sim
