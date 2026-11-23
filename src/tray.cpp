#include "tray.h"

#include "strings.h"

#include <algorithm>
#include <cwchar>

namespace {

template <size_t N>
void CopyText(wchar_t (&destination)[N], const std::wstring& text) {
    const size_t count = std::min(text.size(), N - 1);
    if (count > 0)
        std::wmemcpy(destination, text.c_str(), count);
    destination[count] = L'\0';
}

}  // namespace

Tray::~Tray() {
    Remove();
}

bool Tray::Add(HWND window, HINSTANCE instance) {
    if (added_)
        return true;

    icon_ = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (icon_ == nullptr)
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);

    data_.cbSize = sizeof(data_);
    data_.hWnd = window;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = kTrayMessage;
    data_.hIcon = icon_;
    CopyText(data_.szTip, strings::Tr(L"Windows 充电提示"));

    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    return added_;
}

void Tray::Remove() {
    if (!added_)
        return;

    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
    icon_ = nullptr;
}

void Tray::SetTooltip(const std::wstring& text) {
    if (!added_)
        return;

    data_.uFlags = NIF_TIP;
    CopyText(data_.szTip, text);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void Tray::ShowBalloon(const std::wstring& title, const std::wstring& text,
                       DWORD flags) {
    if (!added_)
        return;

    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = flags;
    data_.uTimeout = 5000;
    CopyText(data_.szInfoTitle, title);
    CopyText(data_.szInfo, text.empty() ? L"\u200B" : text);

    Shell_NotifyIconW(NIM_MODIFY, &data_);

    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.szInfo[0] = L'\0';
    data_.szInfoTitle[0] = L'\0';
}

UINT Tray::ShowMenu(HWND window, POINT point, bool muted, bool autostart) {
    HMENU root = CreatePopupMenu();
    HMENU muteMenu = CreatePopupMenu();
    if (root == nullptr || muteMenu == nullptr) {
        if (root) DestroyMenu(root);
        if (muteMenu) DestroyMenu(muteMenu);
        return 0;
    }

    AppendMenuW(muteMenu, MF_STRING, kCmdMute1Hour, strings::Tr(L"1 小时").c_str());
    AppendMenuW(muteMenu, MF_STRING, kCmdMute2Hours, strings::Tr(L"2 小时").c_str());
    AppendMenuW(muteMenu, MF_STRING, kCmdMute4Hours, strings::Tr(L"4 小时").c_str());
    AppendMenuW(muteMenu, MF_STRING, kCmdMute8Hours, strings::Tr(L"8 小时").c_str());
    AppendMenuW(muteMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(muteMenu, MF_STRING, kCmdMuteCustom, strings::Tr(L"自定义静音...").c_str());
    AppendMenuW(muteMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(muteMenu, MF_STRING | (muted ? MF_ENABLED : MF_GRAYED),
                kCmdUnmute, strings::Tr(L"取消静音").c_str());

    AppendMenuW(root, MF_STRING, kCmdSettings, strings::Tr(L"设置").c_str());
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(muteMenu),
                strings::Tr(L"静音").c_str());
    AppendMenuW(root, MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED),
                kCmdAutostart, strings::Tr(L"开机启动").c_str());
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, kCmdOpenConfig, strings::Tr(L"打开配置文件").c_str());
    AppendMenuW(root, MF_STRING, kCmdOpenLogs, strings::Tr(L"打开日志文件夹").c_str());
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, kCmdExit, strings::Tr(L"退出").c_str());

    SetForegroundWindow(window);
    const UINT command = TrackPopupMenuEx(
        root, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        point.x, point.y, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(root);

    return command;
}
