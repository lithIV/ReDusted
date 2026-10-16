#pragma once

struct Config {
  // [Display]
  bool borderless = true;
  int monitor = 0;  // 0 = monitor the game window starts on, 1..N = specific monitor
  int width = 0;    // 0 = monitor's native size
  int height = 0;

  // [Framerate]
  int fpsLimit = 60;           // 0 = uncapped
  bool vsync = false;
  int simHz = 30;              // the rate the game simulation was designed for
  int maxSimStepsPerFrame = 4;  // avoids a death spiral after a hitch
  bool forceUnlock = false;    // unlock even if the sim hook is missing (physics runs fast)

  // [Debug]
  bool log = true;
};

extern Config g_cfg;
void LoadConfig(const wchar_t* iniPath);
