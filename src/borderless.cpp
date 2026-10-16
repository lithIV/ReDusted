// Borderless windowed: the game is run in its own windowed mode, and every
// attempt to give the main window a frame, move it, or change the desktop
// resolution is redirected so the window covers the chosen monitor.
#include "borderless.h"
#include <MinHook.h>
#include <vector>
#include "config.h"
#include "log.h"

namespace borderless {

static HWND g_hwnd = nullptr;
static bool g_applying = false;

using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using SetWindowLongA_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongW_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using ChangeDisplaySettingsA_t = LONG(WINAPI*)(DEVMODEA*, DWORD);
using ChangeDisplaySettingsW_t = LONG(WINAPI*)(DEVMODEW*, DWORD);
using ChangeDisplaySettingsExA_t = LONG(WINAPI*)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
using ChangeDisplaySettingsExW_t = LONG(WINAPI*)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

static CreateWindowExA_t oCreateWindowExA;
static CreateWindowExW_t oCreateWindowExW;
static SetWindowLongA_t oSetWindowLongA;
static SetWindowLongW_t oSetWindowLongW;
static SetWindowPos_t oSetWindowPos;
static MoveWindow_t oMoveWindow;
static ChangeDisplaySettingsA_t oChangeDisplaySettingsA;
static ChangeDisplaySettingsW_t oChangeDisplaySettingsW;
static ChangeDisplaySettingsExA_t oChangeDisplaySettingsExA;
static ChangeDisplaySettingsExW_t oChangeDisplaySettingsExW;

static constexpr DWORD kFrameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
static constexpr DWORD kFrameExStyles = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_TOPMOST;

static BOOL CALLBACK CollectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM out) {
  reinterpret_cast<std::vector<HMONITOR>*>(out)->push_back(mon);
  return TRUE;
}

static RECT TargetRect(HWND hwnd, int x, int y) {
  HMONITOR mon = nullptr;
  if (g_cfg.monitor > 0) {
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&mons));
    if (g_cfg.monitor <= static_cast<int>(mons.size())) mon = mons[g_cfg.monitor - 1];
  }
  if (!mon) {
    mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY)
               : MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTOPRIMARY);
  }
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  RECT r = mi.rcMonitor;
  if (g_cfg.width > 0 && g_cfg.height > 0) {
    // Custom size, centred on the monitor.
    int cx = (r.left + r.right - g_cfg.width) / 2;
    int cy = (r.top + r.bottom - g_cfg.height) / 2;
    r = RECT{cx, cy, cx + g_cfg.width, cy + g_cfg.height};
  }
  return r;
}

// Skips child, tool and zero-sized helper windows (D3D, DirectInput and
// overlays create hidden ones) so only the real game window is adopted.
static bool LooksLikeGameWindow(DWORD ex, DWORD style, HWND parent, int w, int h) {
  if (parent || (style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW)) return false;
  return w == CW_USEDEFAULT || (w >= 320 && h >= 200);
}

static void Apply(HWND hwnd) {
  if (!hwnd || g_applying) return;
  g_applying = true;
  LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
  oSetWindowLongW(hwnd, GWL_STYLE, (style & ~kFrameStyles) | WS_POPUP);
  oSetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~kFrameExStyles);
  RECT r = TargetRect(hwnd, 0, 0);
  oSetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
  g_applying = false;
}

static void Adopt(HWND hwnd) {
  if (!hwnd || g_hwnd) return;
  g_hwnd = hwnd;
  LOG("Borderless: game window %p", hwnd);
  Apply(hwnd);
}

static HWND WINAPI hkCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  bool game = !g_hwnd && LooksLikeGameWindow(ex, style, parent, w, h);
  if (game) {
    style = (style & ~kFrameStyles) | WS_POPUP;
    ex &= ~kFrameExStyles;
  }
  HWND hwnd = oCreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
  if (game) Adopt(hwnd);
  return hwnd;
}

static HWND WINAPI hkCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  bool game = !g_hwnd && LooksLikeGameWindow(ex, style, parent, w, h);
  if (game) {
    style = (style & ~kFrameStyles) | WS_POPUP;
    ex &= ~kFrameExStyles;
  }
  HWND hwnd = oCreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
  if (game) Adopt(hwnd);
  return hwnd;
}

static LONG Sanitize(int index, LONG value) {
  if (index == GWL_STYLE) return (value & ~kFrameStyles) | WS_POPUP;
  if (index == GWL_EXSTYLE) return value & ~kFrameExStyles;
  return value;
}

static LONG WINAPI hkSetWindowLongA(HWND hwnd, int index, LONG value) {
  if (hwnd == g_hwnd && !g_applying) value = Sanitize(index, value);
  return oSetWindowLongA(hwnd, index, value);
}

static LONG WINAPI hkSetWindowLongW(HWND hwnd, int index, LONG value) {
  if (hwnd == g_hwnd && !g_applying) value = Sanitize(index, value);
  return oSetWindowLongW(hwnd, index, value);
}

static BOOL WINAPI hkSetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
  if (hwnd == g_hwnd && !g_applying) {
    RECT r = TargetRect(hwnd, x, y);
    x = r.left;
    y = r.top;
    w = r.right - r.left;
    h = r.bottom - r.top;
    flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    if (after == HWND_TOPMOST) after = HWND_TOP;
  }
  return oSetWindowPos(hwnd, after, x, y, w, h, flags);
}

static BOOL WINAPI hkMoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
  if (hwnd == g_hwnd && !g_applying) {
    RECT r = TargetRect(hwnd, x, y);
    return oMoveWindow(hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, repaint);
  }
  return oMoveWindow(hwnd, x, y, w, h, repaint);
}

// Desktop resolution changes are what make alt-tab slow and shuffle other
// windows; in borderless mode the desktop keeps its native mode.
static LONG WINAPI hkChangeDisplaySettingsA(DEVMODEA*, DWORD) { return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hkChangeDisplaySettingsW(DEVMODEW*, DWORD) { return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hkChangeDisplaySettingsExA(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID) { return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hkChangeDisplaySettingsExW(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID) { return DISP_CHANGE_SUCCESSFUL; }

template <typename T>
static bool Hook(const wchar_t* module, const char* name, T detour, T* original) {
  MH_STATUS s = MH_CreateHookApi(module, name, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(original));
  if (s != MH_OK) LOG("Borderless: failed to hook %s (%s)", name, MH_StatusToString(s));
  return s == MH_OK;
}

bool Install() {
  if (!g_cfg.borderless) return true;
  bool ok = true;
  ok &= Hook(L"user32", "CreateWindowExA", hkCreateWindowExA, &oCreateWindowExA);
  ok &= Hook(L"user32", "CreateWindowExW", hkCreateWindowExW, &oCreateWindowExW);
  ok &= Hook(L"user32", "SetWindowLongA", hkSetWindowLongA, &oSetWindowLongA);
  ok &= Hook(L"user32", "SetWindowLongW", hkSetWindowLongW, &oSetWindowLongW);
  ok &= Hook(L"user32", "SetWindowPos", hkSetWindowPos, &oSetWindowPos);
  ok &= Hook(L"user32", "MoveWindow", hkMoveWindow, &oMoveWindow);
  ok &= Hook(L"user32", "ChangeDisplaySettingsA", hkChangeDisplaySettingsA, &oChangeDisplaySettingsA);
  ok &= Hook(L"user32", "ChangeDisplaySettingsW", hkChangeDisplaySettingsW, &oChangeDisplaySettingsW);
  ok &= Hook(L"user32", "ChangeDisplaySettingsExA", hkChangeDisplaySettingsExA, &oChangeDisplaySettingsExA);
  ok &= Hook(L"user32", "ChangeDisplaySettingsExW", hkChangeDisplaySettingsExW, &oChangeDisplaySettingsExW);
  LOG("Borderless: %s", ok ? "installed" : "partially installed");
  return ok;
}

HWND GameWindow() { return g_hwnd; }

}  // namespace borderless
