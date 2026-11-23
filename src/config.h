#pragma once

#include <cstdint>
#include <string>

struct Config {
    // General
    bool startWithWindows = false;
    bool startMinimized = true;
    std::wstring theme = L"system";  // system | light | dark
    std::wstring language = L"system";  // system | zh-CN | en-US
    int64_t muteUntil = 0;  // Unix seconds

    // Detection
    int pollMs = 500;
    bool useCallback = true;
    bool useBroadcast = false;
    bool useWindowFallback = false;
    bool useBatteryPercent = true;
    bool usePoll = false;

    // Events
    bool eventPlug = true;
    bool eventUnplug = true;
    bool eventFull = true;
    bool eventLow = true;
    bool eventCritical = true;
    bool eventAbnormal = true;

    // Thresholds
    int fullPercent = 100;
    int lowPercent = 20;
    int criticalPercent = 10;
    int abnormalWindowMinutes = 10;
    int abnormalDrop = 3;

    // Sound
    bool soundEnabled = true;
    bool soundEventPlug = true;
    bool soundEventUnplug = true;
    bool soundEventFull = false;
    bool soundEventLow = false;
    bool soundEventCritical = false;
    bool soundEventAbnormal = false;
    int masterVolume = 80;
    std::wstring soundPlug;
    std::wstring soundUnplug;
    std::wstring soundFull;
    std::wstring soundLow;
    std::wstring soundCritical;
    std::wstring soundAbnormal;
    int volumePlug = 100;
    int volumeUnplug = 100;
    int volumeFull = 100;
    int volumeLow = 100;
    int volumeCritical = 100;
    int volumeAbnormal = 100;

    // Windows notification
    bool notifyEnabled = false;
    std::wstring notifySoundMode = L"auto";  // auto | on | off
    bool notifyShowPercent = true;
    bool notifyShowThreshold = false;

    // Overlay (used by the settings/overlay implementation)
    bool overlayEnabled = true;
    std::wstring overlayPosition = L"bottom-center";
    int overlayDurationMs = 3000;
    int overlayOpacity = 95;
    int overlayCornerRadius = 12;
    std::wstring overlayBackground = L"system";  // system | black | white
    uint32_t overlayBackgroundColor = 0x202020;
    std::wstring overlayIconColor = L"system";
    bool overlayBlur = false;

    // Audio keep-alive
    bool instantMode = false;

    // Misc
    bool followDnd = true;
};

class ConfigStore {
public:
    bool Load();
    bool Save();

    Config& Get() { return config_; }
    const Config& Get() const { return config_; }
    const std::wstring& Path() const { return path_; }

private:
    Config config_;
    std::wstring path_;
};
