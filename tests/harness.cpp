// Loads ReDusted.asi and the fake simulator the way the game does, then
// drives frames the way the game does at 60 fps (an update every other frame,
// a sync every frame) and checks the buffers flip once per simulation step.
#include <windows.h>
#include <cstdio>

using Create_t = void*(__cdecl*)(void*, void*, void*, void*, void*);

static int Field(void* obj, int off) { return *reinterpret_cast<int*>(static_cast<char*>(obj) + off); }

// Calls slot 53 with 17 stack arguments through a vtable, as the exe does,
// and reports whether the callee left the stack balanced.
static bool CallUpdate(void* obj, unsigned last) {
  void* fn = (*reinterpret_cast<void***>(obj))[53];
  unsigned before, after;
  __asm {
    mov before, esp
    push last
    mov ecx, 16
  push_loop:
    push ecx
    loop push_loop
    mov ecx, obj
    call fn
    mov after, esp
  }
  return before == after;
}

static void CallSync(void* obj) {
  void* fn = (*reinterpret_cast<void***>(obj))[55];
  __asm {
    mov ecx, obj
    call fn
  }
}

static int RunFrames(void* obj, int frames, int frameMs, bool* balanced) {
  int flipsBefore = Field(obj, 0x92c);
  for (int i = 0; i < frames; ++i) {
    // The game at 60 fps: the simulation driver runs about every other frame,
    // the sync every frame.
    if (i % 2 == 0) *balanced &= CallUpdate(obj, 0xC0FFEE00 + i);
    CallSync(obj);
    Sleep(frameMs);
  }
  return Field(obj, 0x92c) - flipsBefore;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  char path[MAX_PATH];
  snprintf(path, sizeof(path), "%s\\ReDusted.asi", dir);
  if (!LoadLibraryA(path)) { printf("FAIL: could not load %s (%lu)\n", path, GetLastError()); return 1; }
  snprintf(path, sizeof(path), "%s\\sim\\GalileoSimulatorWater.dll", dir);
  HMODULE simDll = LoadLibraryA(path);
  if (!simDll) { printf("FAIL: could not load %s\n", path); return 1; }

  auto create = reinterpret_cast<Create_t>(GetProcAddress(simDll, MAKEINTRESOURCEA(1)));
  HMODULE owner = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(create), &owner);
  bool wrapped = owner && owner != simDll;
  printf("%s: GetProcAddress(sim, 1) returns the mod's wrapper\n", wrapped ? "ok" : "FAIL");

  void* obj = create(nullptr, nullptr, nullptr, nullptr, nullptr);
  bool hooked = (*reinterpret_cast<void***>(obj))[53] != nullptr &&
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>((*reinterpret_cast<void***>(obj))[53]), &owner) &&
                owner != simDll;
  printf("%s: simulator vtable patched\n", hooked ? "ok" : "FAIL");

  bool balanced = true;
  int flips = RunFrames(obj, 120, 1, &balanced);
  int updates = Field(obj, 0x938), launches = Field(obj, 0x934);
  printf("120 frames: %d updates, %d sim launches, %d buffer flips, last arg 0x%08X\n", updates, launches, flips,
         Field(obj, 0x93c));
  printf("simulate flag after the run: %d\n", Field(obj, 0x930));

  bool ok = wrapped && hooked && balanced;
  ok &= updates == 60 && launches == 60;
  ok &= flips == launches;                    // one flip per launched step, none from the extra syncs
  ok &= Field(obj, 0x93c) == int(0xC0FFEE00 + 118);
  ok &= Field(obj, 0x930) == 1;
  printf("%s: stack balanced across hooked calls\n", balanced ? "ok" : "FAIL");
  printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
