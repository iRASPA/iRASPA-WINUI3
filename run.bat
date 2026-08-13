@echo off
setlocal
if "%~1"=="" (set PLATFORM=x64) else (set PLATFORM=%~1)
if "%~2"=="" (set CONFIG=Debug) else (set CONFIG=%~2)
set TARGET=%~dp0build\%PLATFORM%\%CONFIG%
if not exist "%TARGET%\iRASPA.exe" (
  echo No build at %TARGET%.
  echo Usage: run.bat [x64^|ARM64] [Debug^|Release]
  exit /b 1
)
cd /d "%TARGET%"
start "" "iRASPA.exe"
rem ping rather than timeout: timeout aborts when stdin is redirected.
ping -n 3 127.0.0.1 >nul
tasklist /FI "IMAGENAME eq iRASPA.exe" | find /I "iRASPA.exe" >nul
if errorlevel 1 (
  echo iRASPA.exe exited immediately.
  echo If you see a Windows App Runtime error, install Windows App Runtime 1.5:
  echo   https://aka.ms/windowsappsdk/1.5/latest/windowsappruntimeinstall-x64.exe
  exit /b 1
)
echo iRASPA.exe started from %TARGET%.
