@echo off
setlocal
cd /d "%~dp0"

set "GXX=D:\Applications\msys2\ucrt64\bin\g++.exe"
if not exist "%GXX%" (
    echo Could not find %GXX%
    echo Edit build.cmd and point GXX at your g++.
    exit /b 1
)

"%GXX%" -std=c++17 -O2 -Wall -Wextra -s -static -o probe.exe probe.cpp -lpowrprof -luuid -luser32
if errorlevel 1 exit /b 1

echo Built probe.exe
