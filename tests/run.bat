@echo off
rem Builds the mod, a fake simulator DLL and a harness, then runs the harness.
setlocal
call "%~dp0..\build.bat" || exit /b 1
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSOUT=%TEMP%\redusted_vs.txt"
"%VSWHERE%" -latest -products * -property installationPath > "%VSOUT%" 2>nul
set /p VS=<"%VSOUT%"
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1 || exit /b 1
set "OUT=%~dp0..\build\test"
if not exist "%OUT%\sim" mkdir "%OUT%\sim"
copy /y "%~dp0..\build\ReDusted.asi" "%OUT%\" >nul
(
  echo [Display]
  echo Borderless=0
  echo [Framerate]
  echo SimulationHz=30
  echo [Debug]
  echo Log=1
) > "%OUT%\ReDusted.ini"
cl /nologo /O1 /MT /LD "%~dp0fake_sim.cpp" /Fo"%OUT%\\" /Fe"%OUT%\sim\GalileoSimulatorWater.dll" /link /DEF:"%~dp0fake_sim.def" /NOIMPLIB /NOEXP >nul || exit /b 1
cl /nologo /O1 /MT "%~dp0harness.cpp" /Fo"%OUT%\\" /Fe"%OUT%\harness.exe" /link winmm.lib >nul || exit /b 1
"%OUT%\harness.exe" "%OUT%"
set RC=%ERRORLEVEL%
echo ---- ReDusted.log
type "%OUT%\ReDusted.log"
exit /b %RC%
