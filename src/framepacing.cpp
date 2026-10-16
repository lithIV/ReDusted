#include "framepacing.h"
#include <windows.h>
#include "config.h"
#include "log.h"

namespace pacing {

static LARGE_INTEGER g_freq;
static LONGLONG g_lastPresent = 0;
static LONGLONG g_nextDeadline = 0;
static double g_simAccum = 0.0;
static int g_pendingSteps = 1;
static bool g_unlocked = false;

static LONGLONG Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

void Init() {
  QueryPerformanceFrequency(&g_freq);
  timeBeginPeriod(1);
}

static void Limit() {
  if (!g_unlocked || g_cfg.fpsLimit <= 0) return;
  const LONGLONG frame = g_freq.QuadPart / g_cfg.fpsLimit;
  LONGLONG now = Now();
  if (g_nextDeadline == 0 || now - g_nextDeadline > frame) {
    // First frame, or we fell far behind (loading, alt-tab): resync.
    g_nextDeadline = now + frame;
    return;
  }
  // Sleep most of the remaining time, then spin for precision.
  const LONGLONG twoMs = g_freq.QuadPart / 500;
  while (g_nextDeadline - now > twoMs) {
    Sleep(1);
    now = Now();
  }
  while (now < g_nextDeadline) {
    YieldProcessor();
    now = Now();
  }
  g_nextDeadline += frame;
}

void OnPresent() {
  Limit();

  LONGLONG now = Now();
  if (g_lastPresent != 0) {
    g_simAccum += double(now - g_lastPresent) / double(g_freq.QuadPart);
  }
  g_lastPresent = now;

  const double step = 1.0 / g_cfg.simHz;
  int steps = int(g_simAccum / step);
  if (steps > g_cfg.maxSimStepsPerFrame) {
    // After a long hitch, drop the backlog instead of fast-forwarding.
    steps = g_cfg.maxSimStepsPerFrame;
    g_simAccum = 0.0;
  } else {
    g_simAccum -= steps * step;
  }
  g_pendingSteps = steps;
}

int TakeSimSteps() {
  int steps = g_pendingSteps;
  g_pendingSteps = 0;
  return steps;
}

bool Unlocked() { return g_unlocked; }

void SetUnlocked(bool unlocked) {
  g_unlocked = unlocked;
  LOG("Frame pacing: %s (limit %d fps, simulation %d Hz)", unlocked ? "unlocked" : "locked to the game's 30 fps",
      g_cfg.fpsLimit, g_cfg.simHz);
}

}  // namespace pacing
