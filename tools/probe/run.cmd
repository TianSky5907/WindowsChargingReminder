@echo off
cd /d "%~dp0"
if not exist probe.exe call build.cmd
if not exist probe.exe exit /b 1
probe.exe %*
echo.
pause
