#include "config.h"
#include <windows.h>
#include "log.h"

Config g_cfg;

static int ReadInt(const wchar_t* path, const wchar_t* sec, const wchar_t* key, int def) {
  return static_cast<int>(GetPrivateProfileIntW(sec, key, def, path));
}

void LoadConfig(const wchar_t* path) {
  Config& c = g_cfg;
  c.borderless = ReadInt(path, L"Display", L"Borderless", c.borderless) != 0;
  c.monitor = ReadInt(path, L"Display", L"Monitor", c.monitor);
  c.width = ReadInt(path, L"Display", L"Width", c.width);
  c.height = ReadInt(path, L"Display", L"Height", c.height);

  c.fpsLimit = ReadInt(path, L"Framerate", L"FPSLimit", c.fpsLimit);
  c.vsync = ReadInt(path, L"Framerate", L"VSync", c.vsync) != 0;
  c.simHz = ReadInt(path, L"Framerate", L"SimulationHz", c.simHz);
  c.maxSimStepsPerFrame = ReadInt(path, L"Framerate", L"MaxSimStepsPerFrame", c.maxSimStepsPerFrame);
  c.forceUnlock = ReadInt(path, L"Framerate", L"ForceUnlock", c.forceUnlock) != 0;

  c.log = ReadInt(path, L"Debug", L"Log", c.log) != 0;

  if (c.simHz <= 0) c.simHz = 30;
  if (c.maxSimStepsPerFrame < 1) c.maxSimStepsPerFrame = 1;
  if (c.fpsLimit < 0) c.fpsLimit = 0;
}
