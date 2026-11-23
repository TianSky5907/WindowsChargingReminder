#include "app.h"
#include "util.h"

#include <shellapi.h>
#include <windows.h>

#include <cstdlib>

namespace {

struct FindWindowData {
    const wchar_t* className;
    HWND window;
};

BOOL CALLBACK FindWindowProc(HWND window, LPARAM lParam) {
    auto* data = reinterpret_cast<FindWindowData*>(lParam);
    wchar_t name[128]{};
    if (GetClassNameW(window, name, 128) > 0 &&
        wcscmp(name, data->className) == 0) {
        data->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindow() {
    FindWindowData data{kWindowClass, nullptr};
    EnumWindows(&FindWindowProc, reinterpret_cast<LPARAM>(&data));
    return data.window;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    util::EnablePerMonitorDpiAwareness();

    bool startInTray = false;
    int quitAfterSeconds = 0;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--tray") == 0) {
                startInTray = true;
            } else if (wcscmp(argv[i], L"--quit-after") == 0 && i + 1 < argc) {
                quitAfterSeconds = _wtoi(argv[++i]);
                if (quitAfterSeconds < 1)
                    quitAfterSeconds = 0;
            }
        }
        LocalFree(argv);
    }

    HANDLE mutex = CreateMutexW(
        nullptr, FALSE, L"Local\\WindowsChargingReminder.SingleInstance");
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 20 && existing == nullptr; ++attempt) {
            existing = FindMainWindow();
            if (existing == nullptr)
                Sleep(100);
        }

        if (existing != nullptr)
            PostMessageW(existing, kMsgShowStatus, 0, 0);

        CloseHandle(mutex);
        return 0;
    }

    App app;
    const int result = app.Run(instance, startInTray, quitAfterSeconds);

    if (mutex != nullptr)
        CloseHandle(mutex);

    return result;
}
