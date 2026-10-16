// Voice-line subtitles, step 1: logging only. Records which text entries the
// game looks up (and for which native), their text, subtitle flag, delay and
// sound key, and when sounds start, so one playthrough shows how a spoken line
// ties its text to its sound. See the README's "Voice lines and text" notes.
#include "subtitles.h"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include "config.h"
#include "log.h"

#pragma intrinsic(_ReturnAddress)

namespace subtitles {

// From_Dust.exe (2011-09-05). Each target is checked against its bytes before
// it is hooked.
constexpr uintptr_t kGetEntry = 0x82F7A0;    // text manager vtable slot 10: thiscall(id) -> entry
constexpr uintptr_t kSoundStart = 0x68C290;  // thiscall(holder, key, flags) -> instance, ret 8
constexpr uintptr_t kLanguage = 0x9384E4;    // current language index
static const uint8_t kGetEntryBytes[] = {0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8};
static const uint8_t kSoundStartBytes[] = {0x83, 0xEC, 0x18, 0x53, 0x33, 0xDB, 0x33, 0xC0, 0xF6, 0x41, 0x04, 0x02};

// Text entry layout (from the TXT_* natives).
constexpr size_t kSubtitleFlag = 0x08;  // byte
constexpr size_t kLangSlots = 0x10;     // 24 bytes per language: +0 text, +8 sound object
constexpr size_t kLangSlotSize = 24;
constexpr size_t kDelay = 0x30C;        // float

using GetEntry_t = uint8_t*(__fastcall*)(void* mgr, void* edx, uint32_t id);
using SoundStart_t = void*(__fastcall*)(void* holder, void* edx, uint32_t key, uint32_t flags);
static GetEntry_t oGetEntry;
static SoundStart_t oSoundStart;

static std::set<std::pair<uint32_t, uintptr_t>> g_seenLookups;
static std::set<uint32_t> g_seenSounds;
static std::set<uint32_t> g_voiceSounds;  // sound keys that belong to a text entry

static int Language() { return *reinterpret_cast<int*>(kLanguage); }

// Copies a wide string as UTF-8 into `out` (line breaks become spaces), or
// "?" if it can't be read.
static void ReadText(const wchar_t* w, char* out, int size) {
  strcpy_s(out, size, "?");
  __try {
    if (!w) return;
    wchar_t buf[256];
    int n = 0;
    while (n < 255 && w[n]) {
      buf[n] = (w[n] == L'\n' || w[n] == L'\r') ? L' ' : w[n];
      ++n;
    }
    buf[n] = 0;
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, size, nullptr, nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

static bool ReadEntry(uint8_t* e, int lang, uint8_t* flag, float* delay, const wchar_t** text, uint32_t* sound) {
  __try {
    uint8_t* slot = e + kLangSlots + lang * kLangSlotSize;
    *flag = e[kSubtitleFlag];
    *delay = *reinterpret_cast<float*>(e + kDelay);
    *text = *reinterpret_cast<const wchar_t**>(slot);
    uint32_t* snd = *reinterpret_cast<uint32_t**>(slot + 8);
    *sound = (snd && snd != reinterpret_cast<uint32_t*>(-1)) ? snd[1] : 0;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static uint8_t* __fastcall hkGetEntry(void* mgr, void* edx, uint32_t id) {
  uint8_t* e = oGetEntry(mgr, edx, id);
  auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (!e || !g_seenLookups.insert({id, caller}).second) return e;
  uint8_t flag = 0;
  float delay = 0;
  const wchar_t* text = nullptr;
  uint32_t sound = 0;
  if (!ReadEntry(e, Language(), &flag, &delay, &text, &sound)) return e;
  char utf8[512];
  ReadText(text, utf8, sizeof(utf8));
  if (sound) g_voiceSounds.insert(sound);
  LOG("Text %08X from %06X: subtitle=%u delay=%.2f sound=%08X \"%s\"", id, unsigned(caller), flag, delay, sound,
      utf8);
  return e;
}

static void* __fastcall hkSoundStart(void* holder, void* edx, uint32_t key, uint32_t flags) {
  void* inst = oSoundStart(holder, edx, key, flags);
  bool voice = g_voiceSounds.count(key) != 0;
  // Every start of a voice sound; other sounds only the first time.
  if (voice || g_seenSounds.insert(key).second) {
    LOG("Sound %08X start%s (flags 0x%X, from %06X, instance %p)", key, voice ? " [voice line]" : "", flags,
        unsigned(reinterpret_cast<uintptr_t>(_ReturnAddress())), inst);
  }
  return inst;
}

template <typename T>
static bool HookChecked(uintptr_t at, const uint8_t* bytes, size_t n, T detour, T* original, const char* name) {
  auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  bool inImage = at >= uintptr_t(base) && at + n <= uintptr_t(base) + nt->OptionalHeader.SizeOfImage;
  if (!inImage || memcmp(reinterpret_cast<void*>(at), bytes, n) != 0) {
    LOG("Subtitles: %s at %06X does not match, not hooked", name, unsigned(at));
    return false;
  }
  MH_STATUS s = MH_CreateHook(reinterpret_cast<void*>(at), reinterpret_cast<LPVOID>(detour),
                              reinterpret_cast<LPVOID*>(original));
  LOG("Subtitles: hook %s %s", name, s == MH_OK ? "ok" : MH_StatusToString(s));
  return s == MH_OK;
}

bool Install() {
  if (!g_cfg.subtitleLog) return true;
  bool ok = HookChecked(kGetEntry, kGetEntryBytes, sizeof(kGetEntryBytes), &hkGetEntry, &oGetEntry, "text lookup");
  ok &= HookChecked(kSoundStart, kSoundStartBytes, sizeof(kSoundStartBytes), &hkSoundStart, &oSoundStart,
                    "sound start");
  return ok;
}

}  // namespace subtitles
