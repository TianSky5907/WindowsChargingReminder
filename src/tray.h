#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

inline constexpr UINT kTrayMessage = WM_APP + 20;

enum TrayCommand : UINT {
    kCmdSettings = 1000,
    kCmdMute1Hour = 1001,
    kCmdMute2Hours = 1002,
    kCmdMute4Hours = 1003,
    kCmdMute8Hours = 1004,
    kCmdMuteCustom = 1005,
    kCmdUnmute = 1006,
    kCmdAutostart = 1007,
    kCmdOpenConfig = 1008,
    kCmdOpenLogs = 1009,
    kCmdExit = 1010,
};

class Tray {
public:
    Tray() = default;
    ~Tray();

    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    bool Add(HWND window, HINSTANCE instance);
    void Remove();

    void SetTooltip(const std::wstring& text);
    void ShowBalloon(const std::wstring& title, const std::wstring& text, DWORD flags);

    UINT ShowMenu(HWND window, POINT point, bool muted, bool autostart);

private:
    NOTIFYICONDATAW data_{};
    HICON icon_ = nullptr;
    bool added_ = false;
};
