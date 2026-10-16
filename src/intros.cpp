// Video skipping. The game plays its videos with Bink through its own player
// (From_Dust.exe 0x5E0060), which treats a video as finished once
// bink->FrameNum (+0xC) reaches bink->Frames (+0x8). A skipped video is cut to
// one frame right after BinkOpen, so the player ends it normally on the next
// frame. Looping videos are left alone: the player calls BinkSetWillLoop
// before the first frame, and a looping video gets its length back there.
//
// The first video of the session is the startup logos; every later one is a
// cutscene (e.g. the one after choosing a map).
#include "intros.h"
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include "config.h"
#include "log.h"

namespace intros {

struct BinkHeader {
  uint32_t width;
  uint32_t height;
  uint32_t frames;
  uint32_t frameNum;
};

using BinkOpen_t = BinkHeader*(WINAPI*)(const char* name, unsigned flags);
using BinkSetWillLoop_t = void(WINAPI*)(BinkHeader* bink, int loop);
static BinkOpen_t oBinkOpen;
static BinkSetWillLoop_t oBinkSetWillLoop;

static int g_videosOpened = 0;
static BinkHeader* g_cut = nullptr;  // last video we shortened
static uint32_t g_cutFrames = 0;     // its real length

static BinkHeader* WINAPI hkBinkOpen(const char* name, unsigned flags) {
  BinkHeader* bink = oBinkOpen(name, flags);
  if (!bink) return bink;
  int index = ++g_videosOpened;
  bool logos = index == 1;
  bool skip = logos ? g_cfg.skipIntroLogos : g_cfg.skipCutscenes;
  LOG("Video %d: %ux%u, %u frames (%s)%s", index, bink->width, bink->height, bink->frames,
      logos ? "startup logos" : "cutscene", skip && bink->frames > 1 ? ", skipped" : "");
  if (skip && bink->frames > 1) {
    g_cut = bink;
    g_cutFrames = bink->frames;
    bink->frames = 1;
  }
  return bink;
}

static void WINAPI hkBinkSetWillLoop(BinkHeader* bink, int loop) {
  if (loop && bink == g_cut) {
    bink->frames = g_cutFrames;
    g_cut = nullptr;
    LOG("Video: looping video, not skipped after all");
  }
  oBinkSetWillLoop(bink, loop);
}

bool Install() {
  if (!g_cfg.skipIntroLogos && !g_cfg.skipCutscenes) return true;
  MH_STATUS a = MH_CreateHookApi(L"binkw32", "_BinkOpen@8", reinterpret_cast<LPVOID>(&hkBinkOpen),
                                 reinterpret_cast<LPVOID*>(&oBinkOpen));
  MH_STATUS b = MH_CreateHookApi(L"binkw32", "_BinkSetWillLoop@8", reinterpret_cast<LPVOID>(&hkBinkSetWillLoop),
                                 reinterpret_cast<LPVOID*>(&oBinkSetWillLoop));
  LOG("Video: hooks %s (skip logos %d, cutscenes %d)", a == MH_OK && b == MH_OK ? "ok" : "FAILED",
      g_cfg.skipIntroLogos, g_cfg.skipCutscenes);
  return a == MH_OK && b == MH_OK;
}

}  // namespace intros
