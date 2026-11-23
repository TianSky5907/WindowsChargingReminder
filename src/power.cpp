#include "power.h"

#include <powersetting.h>

#include <cstring>

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
#endif

namespace {
constexpr ULONGLONG kAbnormalGraceMs = 60 * 1000;
}

const wchar_t* PowerEventName(PowerEventType type) {
    switch (type) {
        case PowerEventType::Plugged: return L"plug";
        case PowerEventType::Unplugged: return L"unplug";
        case PowerEventType::Full: return L"full";
        case PowerEventType::Low: return L"low";
        case PowerEventType::Critical: return L"critical";
        case PowerEventType::Abnormal: return L"abnormal";
        default: return L"unknown";
    }
}

PowerMonitor::~PowerMonitor() {
    Stop();
}

bool PowerMonitor::Start(HWND window, const Config& config, Handler handler,
                         std::wstring* error) {
    Stop();

    window_ = window;
    config_ = config;
    handler_ = std::move(handler);
    started_ = false;

    ac_ = false;
    acKnown_ = false;
    percent_ = -1;
    fullNotified_ = false;
    lowNotified_ = false;
    criticalNotified_ = false;
    lowSuppressed_ = false;
    criticalSuppressed_ = false;
    abnormalNotified_ = false;
    plugPercent_ = -1;
    plugTick_ = 0;
    stats_ = PowerStats{};

    auto append = [error](const std::wstring& text) {
        if (error == nullptr)
            return;
        if (!error->empty())
            *error += L"; ";
        *error += text;
    };

    if (window_ == nullptr) {
        append(L"no window");
        return false;
    }

    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status))
        Seed(status);
    else
        append(L"GetSystemPowerStatus failed: " + std::to_wstring(GetLastError()));

    subscribe_.Callback = &PowerMonitor::NotificationCallback;
    subscribe_.Context = this;

    if (config_.useCallback) {
        const DWORD result = PowerSettingRegisterNotification(
            &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_CALLBACK,
            reinterpret_cast<HANDLE>(&subscribe_), &acdcCallback_);

        if (result != ERROR_SUCCESS) {
            append(L"ACDC callback registration failed: " + std::to_wstring(result));
            acdcCallback_ = nullptr;
        }
    }

    if (acdcCallback_ == nullptr && config_.useWindowFallback) {
        acdcWindow_ = RegisterPowerSettingNotification(
            window_, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);

        if (acdcWindow_ == nullptr) {
            append(L"ACDC window registration failed: " +
                   std::to_wstring(GetLastError()));
        }
    }

    if (config_.useBatteryPercent) {
        const DWORD result = PowerSettingRegisterNotification(
            &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_CALLBACK,
            reinterpret_cast<HANDLE>(&subscribe_), &batteryCallback_);

        if (result != ERROR_SUCCESS) {
            append(L"battery callback registration failed: " + std::to_wstring(result));
            batteryCallback_ = nullptr;
        }
    }

    if (config_.usePoll) {
        pollTimer_ = SetTimer(window_, kPollTimerId,
                              static_cast<UINT>(config_.pollMs), nullptr);
        if (pollTimer_ == 0)
            append(L"SetTimer failed: " + std::to_wstring(GetLastError()));
    }

    started_ = true;
    return true;
}

void PowerMonitor::Stop() {
    if (pollTimer_ != 0) {
        if (window_ != nullptr && IsWindow(window_))
            KillTimer(window_, kPollTimerId);
        pollTimer_ = 0;
    }

    if (acdcCallback_ != nullptr) {
        PowerSettingUnregisterNotification(acdcCallback_);
        acdcCallback_ = nullptr;
    }

    if (batteryCallback_ != nullptr) {
        PowerSettingUnregisterNotification(batteryCallback_);
        batteryCallback_ = nullptr;
    }

    if (acdcWindow_ != nullptr) {
        UnregisterPowerSettingNotification(acdcWindow_);
        acdcWindow_ = nullptr;
    }

    started_ = false;
    handler_ = nullptr;
}

bool PowerMonitor::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (!started_)
        return false;

    if (message == WM_TIMER && wParam == kPollTimerId) {
        Refresh(L"poll");
        return true;
    }

    if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMPOWERSTATUSCHANGE && config_.useBroadcast) {
            Refresh(L"broadcast");
            return true;
        }

        if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0) {
            HandleSetting(reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam),
                          L"window");
            return true;
        }
    }

    if (message == kMsgAcDcCallback) {
        ApplyAc(wParam == 0 || wParam == 2, L"callback");
        return true;
    }

    if (message == kMsgBatteryCallback) {
        ApplyPercent(static_cast<int>(wParam), L"battery");
        return true;
    }

    return false;
}

ULONG CALLBACK PowerMonitor::NotificationCallback(PVOID context, ULONG type,
                                                  PVOID setting) {
    auto* self = static_cast<PowerMonitor*>(context);
    if (self == nullptr || type != PBT_POWERSETTINGCHANGE || setting == nullptr)
        return 0;

    const auto* broadcast = static_cast<const POWERBROADCAST_SETTING*>(setting);
    if (broadcast->DataLength < sizeof(DWORD))
        return 0;

    DWORD data = 0;
    std::memcpy(&data, broadcast->Data, sizeof(data));

    if (IsEqualGUID(broadcast->PowerSetting, GUID_ACDC_POWER_SOURCE)) {
        PostMessageW(self->window_, kMsgAcDcCallback, data, 0);
    } else if (IsEqualGUID(broadcast->PowerSetting,
                           GUID_BATTERY_PERCENTAGE_REMAINING)) {
        PostMessageW(self->window_, kMsgBatteryCallback, data, 0);
    }

    return 0;
}

void PowerMonitor::Seed(const SYSTEM_POWER_STATUS& status) {
    if (status.BatteryLifePercent != 255)
        percent_ = status.BatteryLifePercent;

    if (status.ACLineStatus == 255)
        return;

    ac_ = status.ACLineStatus == 1;
    acKnown_ = true;
    InitializeLatchesForState();
}

void PowerMonitor::InitializeLatchesForState() {
    fullNotified_ = ac_ && percent_ >= 0 && percent_ >= config_.fullPercent;
    lowSuppressed_ = !ac_ && percent_ >= 0 && percent_ <= config_.lowPercent;
    criticalSuppressed_ =
        !ac_ && percent_ >= 0 && percent_ <= config_.criticalPercent;
    lowNotified_ = lowSuppressed_;
    criticalNotified_ = criticalSuppressed_;
    abnormalNotified_ = false;
    plugPercent_ = ac_ ? percent_ : -1;
    plugTick_ = GetTickCount64();
}

void PowerMonitor::Refresh(const std::wstring& source) {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status))
        return;

    if (status.ACLineStatus != 255)
        ApplyAc(status.ACLineStatus == 1, source);

    if (status.BatteryLifePercent != 255)
        ApplyPercent(status.BatteryLifePercent, source);
}

void PowerMonitor::ApplyAc(bool ac, const std::wstring& source) {
    if (!acKnown_) {
        acKnown_ = true;
        ac_ = ac;
        InitializeLatchesForState();
        return;
    }

    if (ac == ac_)
        return;

    ac_ = ac;

    if (ac) {
        fullNotified_ = percent_ >= 0 && percent_ >= config_.fullPercent;
        lowNotified_ = false;
        criticalNotified_ = false;
        lowSuppressed_ = false;
        criticalSuppressed_ = false;
        abnormalNotified_ = false;
        plugPercent_ = percent_;
        plugTick_ = GetTickCount64();
        Emit(PowerEventType::Plugged, 0, source);
    } else {
        fullNotified_ = false;
        lowSuppressed_ = percent_ >= 0 && percent_ <= config_.lowPercent;
        criticalSuppressed_ =
            percent_ >= 0 && percent_ <= config_.criticalPercent;
        lowNotified_ = lowSuppressed_;
        criticalNotified_ = criticalSuppressed_;
        abnormalNotified_ = false;
        plugPercent_ = -1;
        Emit(PowerEventType::Unplugged, 0, source);
    }
}

void PowerMonitor::ApplyPercent(int percent, const std::wstring& source) {
    if (percent < 0 || percent > 100)
        return;

    if (!acKnown_) {
        percent_ = percent;
        return;
    }

    if (percent == percent_)
        return;

    percent_ = percent;

    if (ac_) {
        if (!fullNotified_ && percent_ >= config_.fullPercent) {
            fullNotified_ = true;
            Emit(PowerEventType::Full, config_.fullPercent, source);
        }

        if (!abnormalNotified_ && plugPercent_ >= 0) {
            const ULONGLONG elapsed = GetTickCount64() - plugTick_;
            const ULONGLONG windowMs =
                static_cast<ULONGLONG>(config_.abnormalWindowMinutes) * 60 * 1000;

            if (elapsed >= kAbnormalGraceMs && elapsed <= windowMs &&
                percent_ <= plugPercent_ - config_.abnormalDrop) {
                abnormalNotified_ = true;
                Emit(PowerEventType::Abnormal, config_.abnormalDrop, source);
            }
        }
        return;
    }

    if (!criticalNotified_ && !criticalSuppressed_ &&
        percent_ <= config_.criticalPercent) {
        criticalNotified_ = true;
        lowNotified_ = true;
        Emit(PowerEventType::Critical, config_.criticalPercent, source);
        return;
    }

    if (!lowNotified_ && !lowSuppressed_ && percent_ <= config_.lowPercent) {
        lowNotified_ = true;
        Emit(PowerEventType::Low, config_.lowPercent, source);
    }
}

void PowerMonitor::Emit(PowerEventType type, int threshold,
                        const std::wstring& source) {
    if (source == L"callback")
        ++stats_.callbackEvents;
    else if (source == L"window")
        ++stats_.windowEvents;
    else if (source == L"broadcast")
        ++stats_.broadcastEvents;
    else if (source == L"poll")
        ++stats_.pollEvents;
    else if (source == L"battery")
        ++stats_.batteryEvents;

    stats_.lastEvent = PowerEventName(type);
    stats_.lastSource = source;
    stats_.lastTick = GetTickCount64();

    if (handler_) {
        handler_(PowerEvent{type, percent_, threshold, source});
    }
}

void PowerMonitor::HandleSetting(const POWERBROADCAST_SETTING* setting,
                                 const std::wstring& source) {
    if (setting == nullptr || setting->DataLength < sizeof(DWORD))
        return;

    DWORD data = 0;
    std::memcpy(&data, setting->Data, sizeof(data));

    if (IsEqualGUID(setting->PowerSetting, GUID_ACDC_POWER_SOURCE)) {
        ApplyAc(data == 0 || data == 2, source);
    } else if (IsEqualGUID(setting->PowerSetting,
                           GUID_BATTERY_PERCENTAGE_REMAINING)) {
        ApplyPercent(static_cast<int>(data), source);
    }
}
