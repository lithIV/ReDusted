#include <windows.h>
#include <MinHook.h>
#include <string>
#include "borderless.h"
#include "config.h"
#include "framepacing.h"
#include "game_hooks.h"
#include "graphics.h"
#include "intros.h"
#include "subtitles.h"
#include "trace.h"
#include "log.h"

static std::wstring ModuleDir(HMODULE self) {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
  std::wstring dir(path, n);
  size_t slash = dir.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

static void Init(HMODULE self) {
  std::wstring dir = ModuleDir(self);
  LoadConfig((dir + L"\\ReDusted.ini").c_str());
  logx::Init((dir + L"\\ReDusted.log").c_str(), g_cfg.log);
  LOG("ReDusted loaded");

  if (MH_Initialize() != MH_OK) {
    LOG("MinHook failed to initialise, nothing installed");
    return;
  }
  pacing::Init();
  borderless::Install();
  graphics::Install();
  game::Install();
  intros::Install();
  subtitles::Install();
  trace::Install();
  MH_EnableHook(MH_ALL_HOOKS);
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    Init(self);
  }
  return TRUE;
}
