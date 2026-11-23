@echo off
setlocal
cd /d "%~dp0"

set /p VERSION=<"%~dp0VERSION"

if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" goto arm64
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" goto arm64

:x64
start "" "%~dp0dist\WindowsChargingReminder-%VERSION%-x64.exe" %*
exit /b 0

:arm64
start "" "%~dp0dist\WindowsChargingReminder-%VERSION%-arm64.exe" %*
exit /b 0
