@echo off
rem Builds build\ReDusted.asi with Visual Studio (Build Tools) for x86.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSOUT=%TEMP%\redusted_vs.txt"
"%VSWHERE%" -latest -products * -property installationPath > "%VSOUT%" 2>nul
set /p VS=<"%VSOUT%"
if not defined VS (
  echo Visual Studio with the C++ tools was not found.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1 || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
echo Built %~dp0build\ReDusted.asi
