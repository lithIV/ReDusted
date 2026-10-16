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
  bool fixedSimulation = true;  // run the simulation at simHz instead of once per frame
  bool forceUnlock = false;    // unlock even if the sim hook is missing (physics runs fast)

  // [Videos]
  bool skipIntroLogos = true;  // the startup logo video
  bool skipCutscenes = true;   // every later video, e.g. after choosing a map

  // [Subtitles]
  bool subtitleLog = false;  // log text lookups and sound starts (research)

  // [Debug]
  bool log = true;
  bool traceNatives = false;  // log the first call of every script native
};

extern Config g_cfg;
void LoadConfig(const wchar_t* iniPath);
