#include "app.h"

#include "log.h"
#include "strings.h"
#include "ui/overlay.h"
#include "ui/settings.h"
#include "util.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <string>

namespace {
constexpr UINT_PTR kQuitTimerId = 0x52;
constexpr UINT_PTR kSaveTimerId = 0x53;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"WindowsChargingReminder";
}  // namespace

App::App() = default;
App::~App() = default;

int App::Run(HINSTANCE instance, bool startInTray, int quitAfterSeconds) {
    instance_ = instance;

    logging::Init(util::LogDir());
    LOG_INFO(L"Windows Charging Reminder 0.1.0 starting");
    LOG_INFO(L"config: " + util::ConfigPath());
    LOG_INFO(L"log: " + logging::CurrentFile());

#if defined(_M_ARM64) || defined(__aarch64__)
    LOG_INFO(L"arch: arm64");
#else
    LOG_INFO(L"arch: x64");
#endif

    config_.Load();
    strings::SetLanguage(config_.Get().language);
    LOG_INFO(L"config loaded");

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = SUCCEEDED(com);
    if (!comInitialized_) {
        LOG_WARN(L"CoInitializeEx failed: " +
                 util::HexDword(static_cast<uint32_t>(com)));
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateMainWindow()) {
        LOG_ERROR(L"CreateMainWindow failed: " + std::to_wstring(GetLastError()));
        MessageBoxW(nullptr, strings::Tr(L"无法创建主窗口。").c_str(),
                    strings::Tr(L"Windows 充电提示").c_str(),
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    if (!tray_.Add(window_, instance_)) {
        LOG_ERROR(L"Tray icon add failed");
        MessageBoxW(nullptr,
                    strings::Tr(L"无法创建托盘图标。请确认 Windows 资源管理器正在运行。").c_str(),
                    strings::Tr(L"Windows 充电提示").c_str(),
                    MB_ICONERROR | MB_OK);
        DestroyWindow(window_);
        return 1;
    }
    LOG_INFO(L"tray icon added");

    audio_.Start();
    LOG_INFO(L"audio player started");

    std::wstring powerError;
    const bool powerStarted = power_.Start(
        window_, config_.Get(),
        [this](const PowerEvent& event) { HandlePowerEvent(event); },
        &powerError);

    if (!powerStarted)
        LOG_ERROR(L"Power monitor start failed: " + powerError);
    else if (!powerError.empty())
        LOG_WARN(L"Power monitor warnings: " + powerError);
    else
        LOG_INFO(L"power monitor started");

    ApplyAutostartFromConfig();
    UpdateTrayTip();
    LOG_INFO(L"tray tip updated");

    if (quitAfterSeconds > 0) {
        SetTimer(window_, kQuitTimerId,
                 static_cast<UINT>(quitAfterSeconds) * 1000, nullptr);
    }

    if (!startInTray)
        ShowSettings();

    LOG_INFO(L"entering message loop");

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Shutdown();
    logging::Shutdown();

    if (comInitialized_)
        CoUninitialize();

    return 0;
}

bool App::CreateMainWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &App::WndProcStatic;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = windowClass.hIcon;

    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW, kWindowClass, L"Windows 充电提示", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance_, this);

    return window_ != nullptr;
}

LRESULT CALLBACK App::WndProcStatic(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
    App* self = reinterpret_cast<App*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (self == nullptr)
        return DefWindowProcW(window, message, wParam, lParam);

    return self->WndProc(window, message, wParam, lParam);
}

LRESULT App::WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        LOG_INFO(L"Explorer restarted, recreating tray icon");
        tray_.Remove();
        tray_.Add(window_, instance_);
        UpdateTrayTip();
        return 0;
    }

    if (message == WM_POWERBROADCAST) {
        power_.HandleMessage(message, wParam, lParam);
        return TRUE;
    }

    if (message == WM_TIMER && wParam == kQuitTimerId) {
        Shutdown();
        DestroyWindow(window);
        return 0;
    }

    if (message == WM_TIMER && wParam == kSaveTimerId) {
        KillTimer(window, kSaveTimerId);
        config_.Save();
        return 0;
    }

    if (message == WM_TIMER && wParam == kPollTimerId) {
        power_.HandleMessage(message, wParam, lParam);
        return 0;
    }

    if (message == kMsgAcDcCallback || message == kMsgBatteryCallback) {
        power_.HandleMessage(message, wParam, lParam);
        return 0;
    }

    switch (message) {
        case kMsgShowStatus:
            ShowSettings();
            return 0;

        case kTrayMessage: {
            const UINT mouse = static_cast<UINT>(lParam);
            if (mouse == WM_RBUTTONUP || mouse == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                const UINT command = tray_.ShowMenu(
                    window, point, IsMuted(), IsAutoStart());
                if (command != 0)
                    OnTrayCommand(command);
                return 0;
            }
            if (mouse == WM_LBUTTONDBLCLK) {
                ShowSettings();
                return 0;
            }
            return 0;
        }

        case WM_COMMAND:
            OnTrayCommand(LOWORD(wParam));
            return 0;

        case WM_SETTINGCHANGE:
            if (lParam != 0) {
                const auto* area = reinterpret_cast<const wchar_t*>(lParam);
                if (area != nullptr && wcscmp(area, L"ImmersiveColorSet") == 0) {
                    LOG_INFO(L"system theme changed");
                    if (settings_)
                        settings_->Invalidate();
                }
            }
            return 0;

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (wParam != 0) {
                Shutdown();
                DestroyWindow(window);
            }
            return 0;

        case WM_CLOSE:
            Shutdown();
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void App::Shutdown() {
    if (quitting_)
        return;

    quitting_ = true;
    SaveConfigNow();
    settings_.reset();
    overlay_.reset();
    power_.Stop();
    audio_.Shutdown();
    tray_.Remove();
    LOG_INFO(L"shutdown complete");
}

void App::HandlePowerEvent(const PowerEvent& event) {
    LOG_INFO(L"power event: " + std::wstring(PowerEventName(event.type)) +
             L" percent=" + std::to_wstring(event.percent) +
             L" threshold=" + std::to_wstring(event.threshold) +
             L" source=" + event.source);

    UpdateTrayTip();

    if (settings_)
        settings_->Invalidate();

    if (IsMuted()) {
        LOG_INFO(L"muted, skipped");
        return;
    }

    if (config_.Get().followDnd && IsDoNotDisturb()) {
        LOG_INFO(L"system do-not-disturb, skipped");
        return;
    }

    const Config& config = config_.Get();

    if (config.overlayEnabled) {
        ShowOverlay(EventTitle(event), EventText(event), event.percent,
                    power_.IsOnAc());
    }

    if (config.soundEnabled && SoundEventEnabled(event.type)) {
        const std::wstring path = SoundPathFor(event.type);
        if (!path.empty()) {
            const double volume = (static_cast<double>(config.masterVolume) / 100.0) *
                                  (static_cast<double>(EventVolume(event.type)) / 100.0);
            audio_.Play(path, volume);
        } else {
            LOG_WARN(L"no sound file available");
        }
    }

    if (config.notifyEnabled) {
        DWORD flags = NIIF_INFO;
        if (event.type == PowerEventType::Low ||
            event.type == PowerEventType::Critical ||
            event.type == PowerEventType::Abnormal) {
            flags = NIIF_WARNING;
        }

        bool notificationSound = false;
        if (config.notifySoundMode == L"on") {
            notificationSound = true;
        } else if (config.notifySoundMode == L"off") {
            notificationSound = false;
        } else {
            // Auto: the bubble stays silent while the app's own sound is on.
            notificationSound =
                !config.soundEnabled || config.masterVolume == 0;
        }

        if (!notificationSound)
            flags |= NIIF_NOSOUND;

        tray_.ShowBalloon(EventTitle(event), EventText(event), flags);
    }
}

void App::ShowStatusBalloon() {
    std::wstring text = strings::Tr(power_.IsOnAc() ? L"已插入电源"
                                                    : L"未插入电源");
    if (power_.Percent() >= 0) {
        text += L"\n" + strings::Tr(L"当前电量 ") +
                std::to_wstring(power_.Percent()) + L"%";
    }
    if (IsMuted())
        text += L"\n" + strings::Tr(L"已静音");

    tray_.ShowBalloon(strings::Tr(L"Windows 充电提示"), text,
                      NIIF_INFO | NIIF_NOSOUND);
}

void App::UpdateTrayTip() {
    std::wstring tip = strings::Tr(L"Windows 充电提示") + L" - ";
    tip += power_.IsOnAc() ? L"AC" : L"DC";
    if (power_.Percent() >= 0)
        tip += L" " + std::to_wstring(power_.Percent()) + L"%";
    if (IsMuted())
        tip += L" " + strings::Tr(L"[静音]");
    tray_.SetTooltip(tip);
}

bool App::EventEnabled(PowerEventType type) const {
    const Config& config = config_.Get();
    switch (type) {
        case PowerEventType::Plugged: return config.eventPlug;
        case PowerEventType::Unplugged: return config.eventUnplug;
        case PowerEventType::Full: return config.eventFull;
        case PowerEventType::Low: return config.eventLow;
        case PowerEventType::Critical: return config.eventCritical;
        case PowerEventType::Abnormal: return config.eventAbnormal;
        default: return false;
    }
}

bool App::SoundEventEnabled(PowerEventType type) const {
    const Config& config = config_.Get();
    switch (type) {
        case PowerEventType::Plugged: return config.soundEventPlug;
        case PowerEventType::Unplugged: return config.soundEventUnplug;
        case PowerEventType::Full: return config.soundEventFull;
        case PowerEventType::Low: return config.soundEventLow;
        case PowerEventType::Critical: return config.soundEventCritical;
        case PowerEventType::Abnormal: return config.soundEventAbnormal;
        default: return false;
    }
}

int App::EventVolume(PowerEventType type) const {
    const Config& config = config_.Get();
    switch (type) {
        case PowerEventType::Plugged: return config.volumePlug;
        case PowerEventType::Unplugged: return config.volumeUnplug;
        case PowerEventType::Full: return config.volumeFull;
        case PowerEventType::Low: return config.volumeLow;
        case PowerEventType::Critical: return config.volumeCritical;
        case PowerEventType::Abnormal: return config.volumeAbnormal;
        default: return 100;
    }
}

std::wstring App::SoundPathFor(PowerEventType type) const {
    const Config& config = config_.Get();
    const std::wstring* custom = nullptr;
    const wchar_t* defaultName = L"Windows Ding.wav";

    switch (type) {
        case PowerEventType::Plugged:
            custom = &config.soundPlug;
            defaultName = L"Windows Hardware Insert.wav";
            break;
        case PowerEventType::Unplugged:
            custom = &config.soundUnplug;
            defaultName = L"Windows Hardware Remove.wav";
            break;
        case PowerEventType::Full:
            custom = &config.soundFull;
            defaultName = L"Windows Background.wav";
            break;
        case PowerEventType::Low:
            custom = &config.soundLow;
            defaultName = L"Windows Background.wav";
            break;
        case PowerEventType::Critical:
            custom = &config.soundCritical;
            defaultName = L"Windows Foreground.wav";
            break;
        case PowerEventType::Abnormal:
            custom = &config.soundAbnormal;
            defaultName = L"Windows Foreground.wav";
            break;
        default:
            break;
    }

    if (custom != nullptr && !custom->empty()) {
        if (util::FileExists(*custom))
            return *custom;
        LOG_WARN(L"custom sound missing, using default: " + *custom);
    }

    std::wstring path = util::GetSystemMediaPath(defaultName);
    if (util::FileExists(path))
        return path;

    path = util::GetSystemMediaPath(L"Windows Ding.wav");
    if (util::FileExists(path))
        return path;

    return std::wstring();
}

std::wstring App::EventTitle(const PowerEvent& event) const {
    const Config& config = config_.Get();

    switch (event.type) {
        case PowerEventType::Plugged:
            return strings::Tr(L"已插入电源");
        case PowerEventType::Unplugged:
            return strings::Tr(L"已拔出电源");
        case PowerEventType::Full:
            if (config.notifyShowThreshold && event.threshold < 100) {
                return strings::Tr(L"电量已充至 ") +
                       std::to_wstring(event.threshold) + L"%";
            }
            return strings::Tr(L"电池已充满");
        case PowerEventType::Low:
            return strings::Tr(L"电池电量较低");
        case PowerEventType::Critical:
            return strings::Tr(L"电池电量不足");
        case PowerEventType::Abnormal:
            return strings::Tr(L"充电异常");
        default:
            return strings::Tr(L"Windows 充电提示");
    }
}

std::wstring App::EventText(const PowerEvent& event) const {
    const Config& config = config_.Get();

    switch (event.type) {
        case PowerEventType::Plugged:
            if (!config.notifyShowPercent)
                return std::wstring();
            if (event.percent >= 0 && event.percent >= config.fullPercent)
                return strings::Tr(L"电池已充满");
            if (event.percent >= 0)
                return strings::Tr(L"当前电量 ") +
                       std::to_wstring(event.percent) + L"%";
            return std::wstring();

        case PowerEventType::Unplugged:
        case PowerEventType::Low:
        case PowerEventType::Critical:
            if (!config.notifyShowPercent || event.percent < 0)
                return std::wstring();
            return strings::Tr(L"当前电量 ") +
                   std::to_wstring(event.percent) + L"%";

        case PowerEventType::Full:
            if (config.notifyShowThreshold)
                return strings::Tr(L"电量已充至 ") +
                       std::to_wstring(event.threshold) + L"%";
            return std::wstring();

        case PowerEventType::Abnormal:
            return strings::Tr(L"请检查充电器连接状态");

        default:
            return std::wstring();
    }
}

bool App::IsMuted() const {
    return config_.Get().muteUntil > util::UnixNow();
}

bool App::IsDoNotDisturb() const {
    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state)))
        return false;

    switch (state) {
        case QUNS_NOT_PRESENT:
        case QUNS_BUSY:
        case QUNS_RUNNING_D3D_FULL_SCREEN:
        case QUNS_PRESENTATION_MODE:
        case QUNS_QUIET_TIME:
            return true;
        default:
            return false;
    }
}

void App::OnTrayCommand(UINT command) {
    Config& config = config_.Get();

    switch (command) {
        case kCmdSettings:
            ShowSettings();
            break;

        case kCmdMute1Hour:
        case kCmdMute2Hours:
        case kCmdMute4Hours:
        case kCmdMute8Hours: {
            const int64_t seconds = command == kCmdMute1Hour
                                        ? 3600
                                        : (command == kCmdMute2Hours
                                               ? 2 * 3600
                                               : (command == kCmdMute4Hours
                                                      ? 4 * 3600
                                                      : 8 * 3600));
            config.muteUntil = util::UnixNow() + seconds;
            config_.Save();
            UpdateTrayTip();
            LOG_INFO(L"muted for " + util::FormatDuration(seconds));
            tray_.ShowBalloon(strings::Tr(L"Windows 充电提示"),
                              strings::Tr(L"已静音 ") +
                                  strings::Duration(seconds),
                              NIIF_INFO | NIIF_NOSOUND);
            break;
        }

        case kCmdMuteCustom:
            ShowSettings();
            if (settings_)
                settings_->ShowMuteSection();
            break;

        case kCmdUnmute:
            config.muteUntil = 0;
            config_.Save();
            UpdateTrayTip();
            LOG_INFO(L"unmuted");
            tray_.ShowBalloon(strings::Tr(L"Windows 充电提示"),
                              strings::Tr(L"已取消静音"),
                              NIIF_INFO | NIIF_NOSOUND);
            break;

        case kCmdAutostart: {
            const bool enable = !IsAutoStart();
            SetAutoStartEnabled(enable);
            tray_.ShowBalloon(
                strings::Tr(L"Windows 充电提示"),
                strings::Tr(enable ? L"已启用开机启动" : L"已关闭开机启动"),
                NIIF_INFO | NIIF_NOSOUND);
            break;
        }

        case kCmdOpenConfig:
            OpenPath(config_.Path());
            break;

        case kCmdOpenLogs:
            util::EnsureDirectory(util::LogDir());
            OpenPath(util::LogDir());
            break;

        case kCmdExit:
            Shutdown();
            DestroyWindow(window_);
            break;

        default:
            break;
    }
}

void App::SetAutoStart(bool enabled) {
    HKEY key = nullptr;
    const LSTATUS open = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
        &key, nullptr);

    if (open != ERROR_SUCCESS) {
        LOG_ERROR(L"RegCreateKeyEx(Run) failed: " + std::to_wstring(open));
        return;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring value =
            L"\"" + util::GetModulePath() + L"\" --tray";
        status = RegSetValueExW(
            key, kRunValue, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }

    RegCloseKey(key);

    if (status == ERROR_SUCCESS) {
        LOG_INFO(enabled ? L"autostart enabled" : L"autostart disabled");
    } else {
        LOG_ERROR(L"autostart update failed: " + std::to_wstring(status));
    }
}

bool App::IsAutoStart() const {
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr,
        buffer, &size);
    return status == ERROR_SUCCESS;
}

void App::ApplyAutostartFromConfig() {
    SetAutoStart(config_.Get().startWithWindows);
}

void App::OpenPath(const std::wstring& path) {
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) > 32)
        return;

    if (util::FileExists(path)) {
        ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(),
                      nullptr, SW_SHOWNORMAL);
    } else {
        LOG_ERROR(L"ShellExecute failed for: " + path);
    }
}

void App::SaveConfigSoon() {
    if (window_ != nullptr && IsWindow(window_))
        SetTimer(window_, kSaveTimerId, 500, nullptr);
}

void App::SaveConfigNow() {
    if (window_ != nullptr && IsWindow(window_))
        KillTimer(window_, kSaveTimerId);

    if (!config_.Save())
        LOG_ERROR(L"config save failed");
}

void App::ApplyPowerConfig() {
    power_.Stop();

    std::wstring error;
    const bool started = power_.Start(
        window_, config_.Get(),
        [this](const PowerEvent& event) { HandlePowerEvent(event); },
        &error);

    if (!started)
        LOG_ERROR(L"power monitor restart failed: " + error);
    else if (!error.empty())
        LOG_WARN(L"power monitor warnings: " + error);

    UpdateTrayTip();
}

void App::SetAutoStartEnabled(bool enabled) {
    SetAutoStart(enabled);
    config_.Get().startWithWindows = enabled;
    SaveConfigSoon();
}

bool App::IsAutoStartEnabled() const {
    return IsAutoStart();
}

void App::ShowSettings() {
    if (!settings_)
        settings_ = std::make_unique<SettingsWindow>(this);

    if (!settings_->EnsureCreated(instance_, window_)) {
        LOG_ERROR(L"settings window creation failed");
        MessageBoxW(nullptr, strings::Tr(L"无法创建设置窗口。").c_str(),
                    strings::Tr(L"Windows 充电提示").c_str(),
                    MB_ICONERROR | MB_OK);
        return;
    }

    settings_->Show();
}

void App::OnLanguageChanged() {
    UpdateTrayTip();
    if (settings_)
        settings_->Invalidate();
}

void App::PlayPreview(PowerEventType type) {
    const Config& config = config_.Get();
    const std::wstring path = SoundPathFor(type);
    if (path.empty()) {
        LOG_WARN(L"preview sound not found");
        return;
    }

    const double volume = (static_cast<double>(config.masterVolume) / 100.0) *
                          (static_cast<double>(EventVolume(type)) / 100.0);
    audio_.Play(path, volume);
}

void App::PreviewNotification() {
    std::wstring body = L"当前电量 80%";
    tray_.ShowBalloon(L"已插入电源", body,
                      NIIF_INFO | NIIF_NOSOUND);
}

void App::PreviewOverlay() {
    ShowOverlay(L"已插入电源", L"当前电量 80%", 80, true);
}

void App::ExportLogs() {
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                                    &desktop))) {
        LOG_ERROR(L"cannot find Desktop folder");
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64];
    swprintf(name, 64, L"WindowsChargingReminder-logs-%04u%02u%02u-%02u%02u%02u",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond);

    const std::wstring destination =
        util::JoinPath(desktop, name);
    CoTaskMemFree(desktop);

    if (!util::EnsureDirectory(destination)) {
        LOG_ERROR(L"cannot create export folder: " + destination);
        return;
    }

    const std::wstring logFile = logging::CurrentFile();
    if (!logFile.empty())
        CopyFileW(logFile.c_str(),
                  util::JoinPath(destination, L"app.log").c_str(), FALSE);

    CopyFileW(config_.Path().c_str(),
              util::JoinPath(destination, L"config.ini").c_str(), FALSE);

    LOG_INFO(L"logs exported to: " + destination);
    OpenPath(destination);
}

void App::ResetConfiguration() {
    config_.Get() = Config{};
    SetAutoStart(false);
    config_.Save();
    ApplyPowerConfig();
    UpdateTrayTip();

    if (settings_)
        settings_->OnConfigReloaded();
    if (overlay_)
        overlay_->Hide();

    LOG_INFO(L"configuration reset to defaults");
}

void App::EnsureOverlay() {
    if (overlay_)
        return;

    overlay_ = std::make_unique<Overlay>();
    if (!overlay_->EnsureCreated(instance_)) {
        LOG_ERROR(L"overlay window creation failed");
        overlay_.reset();
    }
}

void App::ShowOverlay(const std::wstring& title, const std::wstring& body,
                      int percent, bool charging) {
    EnsureOverlay();
    if (overlay_)
        overlay_->Show(config_.Get(), title, body, percent, charging);
}
