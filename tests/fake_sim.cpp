// Stand-in for GalileoSimulatorWater.dll (x86 MSVC only). The two virtual
// methods carry the same instructions the mod checks for, and count what they
// did in spare fields of the object:
//   +0x92c  buffer flips (one per simulation step, as in the real SyncSimulator)
//   +0x934  updates that launched simulation jobs
//   +0x938  updates in total
//   +0x93c  last value of the 17th update argument
#include <windows.h>
#include <cstring>

extern "C" {

__declspec(naked) static void FakeUpdate() {
  __asm {
    push ebx
    mov ebx, ecx
    cmp dword ptr [ebx + 0x930], 0
    je no_sim
    inc dword ptr [ebx + 0x934]
  no_sim:
    inc dword ptr [ebx + 0x938]
    mov eax, dword ptr [esp + 0x48]  ; 17th argument (after ebx and the return address)
    mov dword ptr [ebx + 0x93c], eax
    pop ebx
    ret 0x44
  }
}

__declspec(naked) static void FakeSync() {
  __asm {
    push esi
    mov esi, ecx
    cmp dword ptr [esi + 0x930], 0
    je no_flip
    inc dword ptr [esi + 0x92c]
  no_flip:
    pop esi
    ret
  }
}

static void Unused() {}

static void* const kVtable[63] = {
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)FakeUpdate, (void*)Unused,
    (void*)FakeSync, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused, (void*)Unused,
    (void*)Unused};

// Ordinal 1, cdecl with five arguments like the real one.
void* __cdecl sCreate(void*, void*, void*, void*, void*) {
  auto* obj = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x4000, MEM_COMMIT, PAGE_READWRITE));
  *reinterpret_cast<void* const**>(obj) = kVtable;
  *reinterpret_cast<int*>(obj + 0x930) = 1;  // simulation running
  return obj;
}
}
