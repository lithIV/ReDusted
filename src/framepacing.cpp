#include "framepacing.h"
#include <windows.h>
#include "config.h"
#include "log.h"

namespace pacing {

static LARGE_INTEGER g_freq;
static LONGLONG g_lastPresent = 0;
static LONGLONG g_nextDeadline = 0;
static LONGLONG g_lastSimCall = 0;
static double g_simAccum = 0.0;
static bool g_unlocked = false;
static bool g_useGameFrames = false;

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

// Logs the real frame rate every few seconds, so a report can say what the
// limiter actually achieved.
static void Measure() {
  static LONGLONG windowStart = 0;
  static int frames = 0;
  LONGLONG now = Now();
  if (windowStart == 0) windowStart = now;
  ++frames;
  double secs = double(now - windowStart) / double(g_freq.QuadPart);
  if (secs >= 5.0) {
    LOG("Frame pacing: %.1f fps over the last %.0f s (thread %lu)", frames / secs, secs, GetCurrentThreadId());
    windowStart = now;
    frames = 0;
  }
}

static void Frame() {
  Limit();
  Measure();
}

void OnPresent() {
  if (!g_useGameFrames) Frame();
}

void OnGameFrame() {
  if (g_useGameFrames) Frame();
}

void UseGameFrames(bool use) {
  g_useGameFrames = use;
  LOG("Frame pacing: limiting %s", use ? "in the game's frame tick" : "in Present");
}

bool ConsumeSimStep() {
  const double step = 1.0 / g_cfg.simHz;
  LONGLONG now = Now();
  if (g_lastSimCall == 0) {
    g_lastSimCall = now;
    return true;
  }
  g_simAccum += double(now - g_lastSimCall) / double(g_freq.QuadPart);
  g_lastSimCall = now;
  // Frame times jitter around 1/simHz, so a step that is slightly early still
  // runs; the difference is carried, which keeps the long-run rate exact.
  if (g_simAccum < step * 0.9) return false;
  g_simAccum -= step;
  // At most one step fits in a frame, so below simHz the simulation slows down
  // (as the original game did) instead of building a backlog.
  if (g_simAccum > step) g_simAccum = step;
  return true;
}

bool Unlocked() { return g_unlocked; }

void SetUnlocked(bool unlocked) {
  g_unlocked = unlocked;
  LOG("Frame pacing: %s (limit %d fps, simulation %d Hz)", unlocked ? "unlocked" : "locked to the game's 30 fps",
      g_cfg.fpsLimit, g_cfg.simHz);
}

}  // namespace pacing
