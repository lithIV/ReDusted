// D3D9 hooks: keep the device windowed for borderless mode, control the
// present interval once the frame cap is lifted, and drive the frame limiter.
#include "graphics.h"
#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>
#include "config.h"
#include "framepacing.h"
#include "log.h"

namespace graphics {

using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using SwapChainPresent_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);

// Vtable slots (d3d9.h declaration order).
constexpr int kCreateDeviceSlot = 16;
constexpr int kResetSlot = 16;
constexpr int kPresentSlot = 17;
constexpr int kSwapChainPresentSlot = 3;

static Direct3DCreate9_t oDirect3DCreate9;
static CreateDevice_t oCreateDevice;
static Reset_t oReset;
static Present_t oPresent;
static SwapChainPresent_t oSwapChainPresent;
// The game can present through the device or a swap chain, and the device's
// Present may itself go through the swap chain; pace each frame only once.
static bool g_inPresent = false;

static void FramePresented(const char* via) {
  static const char* logged = nullptr;
  if (logged != via) {
    logged = via;
    LOG("D3D9: frames presented via %s", via);
  }
  pacing::OnPresent();
}

static void FixParams(D3DPRESENT_PARAMETERS* pp) {
  if (!pp) return;
  if (g_cfg.borderless && !pp->Windowed) {
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
  }
  if (pacing::Unlocked()) {
    pp->PresentationInterval = g_cfg.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
  }
  LOG("D3D9: %ux%u windowed=%d interval=0x%x", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed,
      pp->PresentationInterval);
}

static void* VtableEntry(void* obj, int slot) {
  return (*reinterpret_cast<void***>(obj))[slot];
}

template <typename T>
static void HookOnce(void* target, T detour, T* original, const char* name) {
  if (*original) return;
  MH_STATUS s = MH_CreateHook(target, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(original));
  if (s == MH_OK) s = MH_EnableHook(target);
  LOG("D3D9: hook %s %s", name, s == MH_OK ? "ok" : MH_StatusToString(s));
}

static HRESULT STDMETHODCALLTYPE hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
  bool outer = !g_inPresent;
  g_inPresent = true;
  HRESULT hr = oPresent(dev, src, dst, wnd, dirty);
  if (outer) {
    g_inPresent = false;
    FramePresented("IDirect3DDevice9::Present");
  }
  return hr;
}

static HRESULT STDMETHODCALLTYPE hkSwapChainPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd,
                                                    const RGNDATA* dirty, DWORD flags) {
  bool outer = !g_inPresent;
  g_inPresent = true;
  HRESULT hr = oSwapChainPresent(sc, src, dst, wnd, dirty, flags);
  if (outer) {
    g_inPresent = false;
    FramePresented("IDirect3DSwapChain9::Present");
  }
  return hr;
}

static HRESULT STDMETHODCALLTYPE hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  FixParams(pp);
  return oReset(dev, pp);
}

static HRESULT STDMETHODCALLTYPE hkCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
  FixParams(pp);
  HRESULT hr = oCreateDevice(d3d, adapter, type, focus, flags, pp, out);
  if (SUCCEEDED(hr) && out && *out) {
    HookOnce(VtableEntry(*out, kResetSlot), &hkReset, &oReset, "Reset");
    HookOnce(VtableEntry(*out, kPresentSlot), &hkPresent, &oPresent, "Present");
    IDirect3DSwapChain9* sc = nullptr;
    if (SUCCEEDED((*out)->GetSwapChain(0, &sc)) && sc) {
      HookOnce(VtableEntry(sc, kSwapChainPresentSlot), &hkSwapChainPresent, &oSwapChainPresent, "SwapChain Present");
      sc->Release();
    }
  } else {
    LOG("D3D9: CreateDevice failed 0x%08lx", hr);
  }
  return hr;
}

static IDirect3D9* WINAPI hkDirect3DCreate9(UINT sdk) {
  IDirect3D9* d3d = oDirect3DCreate9(sdk);
  if (d3d) HookOnce(VtableEntry(d3d, kCreateDeviceSlot), &hkCreateDevice, &oCreateDevice, "CreateDevice");
  return d3d;
}

bool Install() {
  MH_STATUS s = MH_CreateHookApi(L"d3d9", "Direct3DCreate9", reinterpret_cast<LPVOID>(&hkDirect3DCreate9),
                                 reinterpret_cast<LPVOID*>(&oDirect3DCreate9));
  LOG("D3D9: hook Direct3DCreate9 %s", s == MH_OK ? "ok" : MH_StatusToString(s));
  return s == MH_OK;
}

}  // namespace graphics
