#include "config.h"

#include "util.h"

#include <windows.h>

#include <vector>

namespace {

std::wstring ReadString(const std::wstring& path, const wchar_t* section,
                        const wchar_t* key, const std::wstring& fallback) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        section, key, fallback.c_str(), buffer.data(),
        static_cast<DWORD>(buffer.size()), path.c_str());
    return std::wstring(buffer.data(), length);
}

int ReadInt(const std::wstring& path, const wchar_t* section,
            const wchar_t* key, int fallback) {
    return _wtoi(ReadString(path, section, key, std::to_wstring(fallback)).c_str());
}

int64_t ReadInt64(const std::wstring& path, const wchar_t* section,
                  const wchar_t* key, int64_t fallback) {
    return _wtoi64(ReadString(path, section, key, std::to_wstring(fallback)).c_str());
}

uint32_t ReadHex(const std::wstring& path, const wchar_t* section,
                 const wchar_t* key, uint32_t fallback) {
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%08X", fallback);
    const std::wstring text = ReadString(path, section, key, buffer);
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(text.c_str(), &end, 16);
    return end == text.c_str() ? fallback : static_cast<uint32_t>(value);
}

bool ReadBool(const std::wstring& path, const wchar_t* section,
              const wchar_t* key, bool fallback) {
    return ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

bool WriteString(const std::wstring& path, const wchar_t* section,
                 const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
}

bool WriteInt(const std::wstring& path, const wchar_t* section,
              const wchar_t* key, int value) {
    return WriteString(path, section, key, std::to_wstring(value));
}

bool WriteBool(const std::wstring& path, const wchar_t* section,
               const wchar_t* key, bool value) {
    return WriteInt(path, section, key, value ? 1 : 0);
}

void EnsureUnicodeIni(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    CloseHandle(file);
}

}  // namespace

bool ConfigStore::Load() {
    path_ = util::ConfigPath();
    util::EnsureDirectory(util::AppDataDir());

    if (!util::FileExists(path_)) {
        Save();
    }

    const std::wstring& path = path_;
    Config& c = config_;

    c.startWithWindows = ReadBool(path, L"General", L"StartWithWindows", c.startWithWindows);
    c.startMinimized = ReadBool(path, L"General", L"StartMinimized", c.startMinimized);
    c.theme = ReadString(path, L"General", L"Theme", c.theme);
    c.language = ReadString(path, L"General", L"Language", c.language);
    c.muteUntil = ReadInt64(path, L"General", L"MuteUntil", c.muteUntil);
    c.followDnd = ReadBool(path, L"General", L"FollowDoNotDisturb", c.followDnd);

    c.pollMs = ReadInt(path, L"Detect", L"PollMs", c.pollMs);
    c.useCallback = ReadBool(path, L"Detect", L"UseCallback", c.useCallback);
    c.useBroadcast = ReadBool(path, L"Detect", L"UseBroadcast", c.useBroadcast);
    c.useWindowFallback = ReadBool(path, L"Detect", L"UseWindowFallback", c.useWindowFallback);
    c.useBatteryPercent = ReadBool(path, L"Detect", L"UseBatteryPercent", c.useBatteryPercent);
    c.usePoll = ReadBool(path, L"Detect", L"UsePoll", c.usePoll);

    c.eventPlug = ReadBool(path, L"Events", L"Plug", c.eventPlug);
    c.eventUnplug = ReadBool(path, L"Events", L"Unplug", c.eventUnplug);
    c.eventFull = ReadBool(path, L"Events", L"Full", c.eventFull);
    c.eventLow = ReadBool(path, L"Events", L"Low", c.eventLow);
    c.eventCritical = ReadBool(path, L"Events", L"Critical", c.eventCritical);
    c.eventAbnormal = ReadBool(path, L"Events", L"Abnormal", c.eventAbnormal);

    c.fullPercent = ReadInt(path, L"Thresholds", L"Full", c.fullPercent);
    c.lowPercent = ReadInt(path, L"Thresholds", L"Low", c.lowPercent);
    c.criticalPercent = ReadInt(path, L"Thresholds", L"Critical", c.criticalPercent);
    c.abnormalWindowMinutes =
        ReadInt(path, L"Thresholds", L"AbnormalWindowMinutes", c.abnormalWindowMinutes);
    c.abnormalDrop = ReadInt(path, L"Thresholds", L"AbnormalDrop", c.abnormalDrop);

    c.soundEnabled = ReadBool(path, L"Sound", L"Enabled", c.soundEnabled);
    c.soundEventPlug = ReadBool(path, L"Sound", L"PlugEnabled", c.soundEventPlug);
    c.soundEventUnplug = ReadBool(path, L"Sound", L"UnplugEnabled", c.soundEventUnplug);
    c.soundEventFull = ReadBool(path, L"Sound", L"FullEnabled", c.soundEventFull);
    c.soundEventLow = ReadBool(path, L"Sound", L"LowEnabled", c.soundEventLow);
    c.soundEventCritical = ReadBool(path, L"Sound", L"CriticalEnabled", c.soundEventCritical);
    c.soundEventAbnormal = ReadBool(path, L"Sound", L"AbnormalEnabled", c.soundEventAbnormal);
    c.masterVolume = ReadInt(path, L"Sound", L"MasterVolume", c.masterVolume);
    c.instantMode = ReadBool(path, L"Sound", L"InstantMode", c.instantMode);

    c.soundPlug = ReadString(path, L"Sound", L"PlugFile", c.soundPlug);
    c.soundUnplug = ReadString(path, L"Sound", L"UnplugFile", c.soundUnplug);
    c.soundFull = ReadString(path, L"Sound", L"FullFile", c.soundFull);
    c.soundLow = ReadString(path, L"Sound", L"LowFile", c.soundLow);
    c.soundCritical = ReadString(path, L"Sound", L"CriticalFile", c.soundCritical);
    c.soundAbnormal = ReadString(path, L"Sound", L"AbnormalFile", c.soundAbnormal);
    c.volumePlug = ReadInt(path, L"Sound", L"PlugVolume", c.volumePlug);
    c.volumeUnplug = ReadInt(path, L"Sound", L"UnplugVolume", c.volumeUnplug);
    c.volumeFull = ReadInt(path, L"Sound", L"FullVolume", c.volumeFull);
    c.volumeLow = ReadInt(path, L"Sound", L"LowVolume", c.volumeLow);
    c.volumeCritical = ReadInt(path, L"Sound", L"CriticalVolume", c.volumeCritical);
    c.volumeAbnormal = ReadInt(path, L"Sound", L"AbnormalVolume", c.volumeAbnormal);

    c.notifyEnabled = ReadBool(path, L"Notify", L"Enabled", c.notifyEnabled);
    c.notifySoundMode = ReadString(path, L"Notify", L"SoundMode", c.notifySoundMode);
    c.notifyShowPercent = ReadBool(path, L"Notify", L"ShowPercent", c.notifyShowPercent);
    c.notifyShowThreshold = ReadBool(path, L"Notify", L"ShowThreshold", c.notifyShowThreshold);

    c.overlayEnabled = ReadBool(path, L"Overlay", L"Enabled", c.overlayEnabled);
    c.overlayPosition = ReadString(path, L"Overlay", L"Position", c.overlayPosition);
    c.overlayDurationMs = ReadInt(path, L"Overlay", L"DurationMs", c.overlayDurationMs);
    c.overlayOpacity = ReadInt(path, L"Overlay", L"Opacity", c.overlayOpacity);
    c.overlayCornerRadius = ReadInt(path, L"Overlay", L"CornerRadius", c.overlayCornerRadius);
    c.overlayBackground = ReadString(path, L"Overlay", L"Background", c.overlayBackground);
    c.overlayBackgroundColor =
        ReadHex(path, L"Overlay", L"BackgroundColor", c.overlayBackgroundColor);
    c.overlayIconColor = ReadString(path, L"Overlay", L"IconColor", c.overlayIconColor);
    c.overlayBlur = ReadBool(path, L"Overlay", L"Blur", c.overlayBlur);

    c.pollMs = util::ClampInt(c.pollMs, 100, 5000);
    c.fullPercent = util::ClampInt(c.fullPercent, 1, 100);
    c.criticalPercent = util::ClampInt(c.criticalPercent, 1, 99);
    c.lowPercent = util::ClampInt(c.lowPercent, c.criticalPercent + 1, 100);
    c.abnormalWindowMinutes = util::ClampInt(c.abnormalWindowMinutes, 1, 1440);
    c.abnormalDrop = util::ClampInt(c.abnormalDrop, 1, 50);
    c.masterVolume = util::ClampInt(c.masterVolume, 0, 100);
    c.volumePlug = util::ClampInt(c.volumePlug, 0, 100);
    c.volumeUnplug = util::ClampInt(c.volumeUnplug, 0, 100);
    c.volumeFull = util::ClampInt(c.volumeFull, 0, 100);
    c.volumeLow = util::ClampInt(c.volumeLow, 0, 100);
    c.volumeCritical = util::ClampInt(c.volumeCritical, 0, 100);
    c.volumeAbnormal = util::ClampInt(c.volumeAbnormal, 0, 100);
    c.overlayOpacity = util::ClampInt(c.overlayOpacity, 0, 100);
    c.overlayCornerRadius = util::ClampInt(c.overlayCornerRadius, 0, 40);
    c.overlayDurationMs = util::ClampInt(c.overlayDurationMs, 500, 10000);

    c.notifySoundMode = util::ToLower(util::Trim(c.notifySoundMode));
    if (c.notifySoundMode != L"auto" && c.notifySoundMode != L"on" &&
        c.notifySoundMode != L"off") {
        c.notifySoundMode = L"auto";
    }

    c.language = util::Trim(c.language);
    if (c.language != L"system" && c.language != L"zh-CN" &&
        c.language != L"en-US") {
        c.language = L"system";
    }

    c.overlayIconColor = util::ToLower(util::Trim(c.overlayIconColor));
    if (c.overlayIconColor.empty())
        c.overlayIconColor = L"system";

    return true;
}

bool ConfigStore::Save() {
    if (path_.empty())
        path_ = util::ConfigPath();
    if (!util::EnsureDirectory(util::AppDataDir()))
        return false;

    EnsureUnicodeIni(path_);

    const std::wstring& path = path_;
    const Config& c = config_;
    bool ok = true;

    ok &= WriteBool(path, L"General", L"StartWithWindows", c.startWithWindows);
    ok &= WriteBool(path, L"General", L"StartMinimized", c.startMinimized);
    ok &= WriteString(path, L"General", L"Theme", c.theme);
    ok &= WriteString(path, L"General", L"Language", c.language);
    ok &= WriteString(path, L"General", L"MuteUntil", std::to_wstring(c.muteUntil));
    ok &= WriteBool(path, L"General", L"FollowDoNotDisturb", c.followDnd);

    ok &= WriteInt(path, L"Detect", L"PollMs", c.pollMs);
    ok &= WriteBool(path, L"Detect", L"UseCallback", c.useCallback);
    ok &= WriteBool(path, L"Detect", L"UseBroadcast", c.useBroadcast);
    ok &= WriteBool(path, L"Detect", L"UseWindowFallback", c.useWindowFallback);
    ok &= WriteBool(path, L"Detect", L"UseBatteryPercent", c.useBatteryPercent);
    ok &= WriteBool(path, L"Detect", L"UsePoll", c.usePoll);

    ok &= WriteBool(path, L"Events", L"Plug", c.eventPlug);
    ok &= WriteBool(path, L"Events", L"Unplug", c.eventUnplug);
    ok &= WriteBool(path, L"Events", L"Full", c.eventFull);
    ok &= WriteBool(path, L"Events", L"Low", c.eventLow);
    ok &= WriteBool(path, L"Events", L"Critical", c.eventCritical);
    ok &= WriteBool(path, L"Events", L"Abnormal", c.eventAbnormal);

    ok &= WriteInt(path, L"Thresholds", L"Full", c.fullPercent);
    ok &= WriteInt(path, L"Thresholds", L"Low", c.lowPercent);
    ok &= WriteInt(path, L"Thresholds", L"Critical", c.criticalPercent);
    ok &= WriteInt(path, L"Thresholds", L"AbnormalWindowMinutes", c.abnormalWindowMinutes);
    ok &= WriteInt(path, L"Thresholds", L"AbnormalDrop", c.abnormalDrop);

    ok &= WriteBool(path, L"Sound", L"Enabled", c.soundEnabled);
    ok &= WriteBool(path, L"Sound", L"PlugEnabled", c.soundEventPlug);
    ok &= WriteBool(path, L"Sound", L"UnplugEnabled", c.soundEventUnplug);
    ok &= WriteBool(path, L"Sound", L"FullEnabled", c.soundEventFull);
    ok &= WriteBool(path, L"Sound", L"LowEnabled", c.soundEventLow);
    ok &= WriteBool(path, L"Sound", L"CriticalEnabled", c.soundEventCritical);
    ok &= WriteBool(path, L"Sound", L"AbnormalEnabled", c.soundEventAbnormal);
    ok &= WriteInt(path, L"Sound", L"MasterVolume", c.masterVolume);
    ok &= WriteBool(path, L"Sound", L"InstantMode", c.instantMode);
    ok &= WriteString(path, L"Sound", L"PlugFile", c.soundPlug);
    ok &= WriteString(path, L"Sound", L"UnplugFile", c.soundUnplug);
    ok &= WriteString(path, L"Sound", L"FullFile", c.soundFull);
    ok &= WriteString(path, L"Sound", L"LowFile", c.soundLow);
    ok &= WriteString(path, L"Sound", L"CriticalFile", c.soundCritical);
    ok &= WriteString(path, L"Sound", L"AbnormalFile", c.soundAbnormal);
    ok &= WriteInt(path, L"Sound", L"PlugVolume", c.volumePlug);
    ok &= WriteInt(path, L"Sound", L"UnplugVolume", c.volumeUnplug);
    ok &= WriteInt(path, L"Sound", L"FullVolume", c.volumeFull);
    ok &= WriteInt(path, L"Sound", L"LowVolume", c.volumeLow);
    ok &= WriteInt(path, L"Sound", L"CriticalVolume", c.volumeCritical);
    ok &= WriteInt(path, L"Sound", L"AbnormalVolume", c.volumeAbnormal);

    ok &= WriteBool(path, L"Notify", L"Enabled", c.notifyEnabled);
    ok &= WriteString(path, L"Notify", L"SoundMode", c.notifySoundMode);
    WritePrivateProfileStringW(L"Notify", L"Sound", nullptr, path.c_str());
    ok &= WriteBool(path, L"Notify", L"ShowPercent", c.notifyShowPercent);
    ok &= WriteBool(path, L"Notify", L"ShowThreshold", c.notifyShowThreshold);

    ok &= WriteBool(path, L"Overlay", L"Enabled", c.overlayEnabled);
    ok &= WriteString(path, L"Overlay", L"Position", c.overlayPosition);
    ok &= WriteInt(path, L"Overlay", L"DurationMs", c.overlayDurationMs);
    ok &= WriteInt(path, L"Overlay", L"Opacity", c.overlayOpacity);
    ok &= WriteInt(path, L"Overlay", L"CornerRadius", c.overlayCornerRadius);
    ok &= WriteString(path, L"Overlay", L"Background", c.overlayBackground);
    ok &= WriteString(path, L"Overlay", L"BackgroundColor",
                      util::HexDword(c.overlayBackgroundColor));
    ok &= WriteString(path, L"Overlay", L"IconColor", c.overlayIconColor);
    ok &= WriteBool(path, L"Overlay", L"Blur", c.overlayBlur);

    return ok;
}
