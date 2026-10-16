// Native call tracer (debug). Every script native is registered through
// 0x82B1C0 with a descriptor naming it and giving two wrappers (script VM and
// direct). With [Debug] TraceNatives=1 each wrapper gets a small stub that logs
// the first call to it, with time and caller, then jumps to the real code. One
// run gives a timeline of which natives the game uses when, e.g. to find what
// drives a startup screen.
#include "trace.h"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "config.h"
#include "log.h"

namespace trace {

constexpr uintptr_t kRegister = 0x82B1C0;  // thiscall(registry, id, desc*), ret 8
static const uint8_t kRegisterBytes[] = {0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x14, 0x00, 0x57};

struct Native {
  const char* name;
  bool vm;          // script-VM wrapper (else direct)
  void* original;   // MinHook trampoline, the stub jumps through this
  volatile LONG called;
};

static std::vector<Native*> g_natives;
static uint8_t* g_stubs = nullptr;  // executable page(s) for stubs
static size_t g_stubsUsed = 0;
constexpr size_t kStubSize = 32;
constexpr size_t kStubArea = 1 << 20;  // room for 32768 stubs
static volatile LONG g_pending = 0;
static LARGE_INTEGER g_start, g_freq;

using Register_t = int(__fastcall*)(void* registry, void* edx, uint32_t id, uint32_t* desc);
static Register_t oRegister;

static void __cdecl FirstCall(Native* n, uintptr_t ret) {
  if (n->called || InterlockedExchange(&n->called, 1)) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  LOG("Native %s%s first call at %.3f s from %06X", n->name, n->vm ? " (vm)" : "",
      double(now.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart), unsigned(ret));
}

// Stub: pushfd; pushad; push [esp+0x24] (return address); push n;
//       call FirstCall; add esp, 8; popad; popfd; jmp [&n->original]
static uint8_t* MakeStub(Native* n) {
  if (g_stubsUsed + kStubSize > kStubArea) return nullptr;
  uint8_t* s = g_stubs + g_stubsUsed;
  g_stubsUsed += kStubSize;
  uint8_t* p = s;
  *p++ = 0x9C;                                     // pushfd
  *p++ = 0x60;                                     // pushad
  *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x24;  // push dword [esp+0x24]
  *p++ = 0x68; memcpy(p, &n, 4); p += 4;           // push n
  *p++ = 0xE8;                                     // call FirstCall
  int32_t rel = int32_t(reinterpret_cast<uintptr_t>(&FirstCall) - reinterpret_cast<uintptr_t>(p + 4));
  memcpy(p, &rel, 4); p += 4;
  *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08;           // add esp, 8
  *p++ = 0x61;                                     // popad
  *p++ = 0x9D;                                     // popfd
  *p++ = 0xFF; *p++ = 0x25;                        // jmp dword [abs]
  void** slot = &n->original;
  memcpy(p, &slot, 4); p += 4;
  return s;
}

static void Trace(void* target, const char* name, bool vm) {
  if (!target) return;
  auto* n = new Native{name, vm, nullptr, 0};
  uint8_t* stub = MakeStub(n);
  if (!stub) return;
  if (MH_CreateHook(target, stub, &n->original) != MH_OK) return;  // shared, too small, or already traced
  MH_QueueEnableHook(target);
  g_natives.push_back(n);
  InterlockedExchange(&g_pending, 1);
}

static int __fastcall hkRegister(void* registry, void* edx, uint32_t id, uint32_t* desc) {
  int r = oRegister(registry, edx, id, desc);
  __try {
    const char* name = reinterpret_cast<const char*>(desc[5]);
    Trace(reinterpret_cast<void*>(desc[0]), name, true);
    Trace(reinterpret_cast<void*>(desc[1]), name, false);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return r;
}

void OnFrame() {
  if (g_pending && InterlockedExchange(&g_pending, 0)) {
    MH_STATUS s = MH_ApplyQueued();
    LOG("Trace: %u native wrappers traced (%s)", unsigned(g_natives.size()), MH_StatusToString(s));
  }
}

bool Install() {
  if (!g_cfg.traceNatives) return true;
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);
  auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  bool inImage = kRegister + sizeof(kRegisterBytes) <= uintptr_t(base) + nt->OptionalHeader.SizeOfImage;
  if (!inImage || memcmp(reinterpret_cast<void*>(kRegister), kRegisterBytes, sizeof(kRegisterBytes)) != 0) {
    LOG("Trace: native registrar does not match, not tracing");
    return false;
  }
  g_stubs = static_cast<uint8_t*>(VirtualAlloc(nullptr, kStubArea, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!g_stubs) return false;
  MH_STATUS s = MH_CreateHook(reinterpret_cast<void*>(kRegister), reinterpret_cast<LPVOID>(&hkRegister),
                              reinterpret_cast<LPVOID*>(&oRegister));
  LOG("Trace: hook native registrar %s (times below are seconds since load)", MH_StatusToString(s));
  return s == MH_OK;
}

}  // namespace trace
