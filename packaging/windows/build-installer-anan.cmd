@echo off
rem AetherSDR for OpenHPSDR by N7DDC -- builds the Windows installer.
rem   build-installer-anan.cmd [DEPLOY_DIR]
rem DEPLOY_DIR: the folder the program runs from (default: the release folder
rem on the desktop). The build number is read from src\core\ForkBuild.h.
rem Needs Inno Setup 7 (or 6), https://jrsoftware.org/isinfo.php.
setlocal
set "HERE=%~dp0"
set "DEPLOY=%~1"
if "%DEPLOY%"=="" set "DEPLOY=%USERPROFILE%\Desktop\AetherSDR-Release"
for /f "tokens=3" %%v in ('findstr /c:"#define AETHER_FORK_BUILD" "%HERE%..\..\src\core\ForkBuild.h"') do set "BUILD=%%v"
set "ISCC="
for %%d in ("%ProgramFiles%\Inno Setup 7" "%ProgramFiles(x86)%\Inno Setup 7" "%LOCALAPPDATA%\Programs\Inno Setup 7" "%ProgramFiles(x86)%\Inno Setup 6" "%ProgramFiles%\Inno Setup 6" "%LOCALAPPDATA%\Programs\Inno Setup 6") do (
  if not defined ISCC if exist "%%~d\ISCC.exe" set "ISCC=%%~d\ISCC.exe"
)
if not defined ISCC (
  echo Inno Setup 7 or 6 not found. Install it from https://jrsoftware.org/isinfo.php
  exit /b 1
)
if not exist "%DEPLOY%\AetherSDR.exe" (
  echo AetherSDR.exe not found in "%DEPLOY%"
  exit /b 1
)
echo Build %BUILD%, files from "%DEPLOY%"
"%ISCC%" /DAPP_VERSION=%BUILD% /DDEPLOY_DIR="%DEPLOY%" "%HERE%installer-anan.iss"
if errorlevel 1 exit /b 1
echo The installer is in the repository root: AetherSDR-OpenHPSDR-N7DDC-build%BUILD%-win64-setup.exe
endlocal
