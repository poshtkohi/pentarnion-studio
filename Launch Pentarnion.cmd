@echo off
setlocal
set "PENTA_APP=%~dp0build\windows-release\bin\pentarnion-studio.exe"
if not exist "%PENTA_APP%" (
  echo Build Release first using Code::Blocks or tools\build.ps1.
  pause
  exit /b 1
)
start "" "%PENTA_APP%"
