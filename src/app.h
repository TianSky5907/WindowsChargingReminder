#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "audio.h"
#include "config.h"
#include "power.h"
#include "tray.h"

inline constexpr wchar_t kWindowClass[] = L"WindowsChargingReminder.MainWindow";
inline constexpr UINT kMsgShowStatus = WM_APP + 30;

class SettingsWindow;
class Overlay;

class App {
public:
    App();
    ~App();

    int Run(HINSTANCE instance, bool startInTray, int quitAfterSeconds);

    Config& MutableConfig() { return config_.Get(); }
    const Config& GetConfig() const { return config_.Get(); }

    void SaveConfigSoon();
    void SaveConfigNow();
    void ApplyPowerConfig();

    void SetAutoStartEnabled(bool enabled);
    bool IsAutoStartEnabled() const;

    void ShowSettings();
    void OnLanguageChanged();
    void PlayPreview(PowerEventType type);
    void PreviewNotification();
    void PreviewOverlay();
    void ExportLogs();
    void ResetConfiguration();

    PowerStats PowerStatistics() const { return power_.GetStats(); }
    void ResetPowerStatistics() { power_.ResetStats(); }

    bool IsOnAc() const { return power_.IsOnAc(); }
    int Percent() const { return power_.Percent(); }

private:
    static LRESULT CALLBACK WndProcStatic(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow();
    void OnTrayCommand(UINT command);
    void HandlePowerEvent(const PowerEvent& event);
    void ShowStatusBalloon();
    void UpdateTrayTip();
    void Shutdown();

    bool IsMuted() const;
    bool IsDoNotDisturb() const;

    bool EventEnabled(PowerEventType type) const;
    bool SoundEventEnabled(PowerEventType type) const;
    int EventVolume(PowerEventType type) const;
    std::wstring SoundPathFor(PowerEventType type) const;
    std::wstring EventTitle(const PowerEvent& event) const;
    std::wstring EventText(const PowerEvent& event) const;

    void SetAutoStart(bool enabled);
    bool IsAutoStart() const;
    void ApplyAutostartFromConfig();
    void OpenPath(const std::wstring& path);

    void EnsureOverlay();
    void ShowOverlay(const std::wstring& title, const std::wstring& body,
                     int percent, bool charging);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    ConfigStore config_;
    Tray tray_;
    AudioPlayer audio_;
    PowerMonitor power_;
    std::unique_ptr<SettingsWindow> settings_;
    std::unique_ptr<Overlay> overlay_;

    bool comInitialized_ = false;
    bool quitting_ = false;
};
