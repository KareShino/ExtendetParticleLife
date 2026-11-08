@echo off
setlocal
rem Build with CMake (uses PATH cmake, otherwise the one bundled with Visual Studio)
cd /d "%~dp0"
set "CMAKE_EXE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE_EXE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE_EXE%" -S . -B build -A x64 || exit /b 1
"%CMAKE_EXE%" --build build --config Release --parallel || exit /b 1
echo.
echo Build OK: %~dp0build\Release\ParticleLife.exe
