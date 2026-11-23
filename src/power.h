#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "config.h"

enum class PowerEventType {
    Plugged,
    Unplugged,
    Full,
    Low,
    Critical,
    Abnormal,
};

struct PowerEvent {
    PowerEventType type;
    int percent;
    int threshold;
    std::wstring source;
};

struct PowerStats {
    uint64_t callbackEvents = 0;
    uint64_t windowEvents = 0;
    uint64_t broadcastEvents = 0;
    uint64_t pollEvents = 0;
    uint64_t batteryEvents = 0;
    std::wstring lastEvent;
    std::wstring lastSource;
    ULONGLONG lastTick = 0;

    uint64_t totalEvents() const {
        return callbackEvents + windowEvents + broadcastEvents + pollEvents +
               batteryEvents;
    }
};

const wchar_t* PowerEventName(PowerEventType type);

inline constexpr UINT kMsgAcDcCallback = WM_APP + 1;
inline constexpr UINT kMsgBatteryCallback = WM_APP + 2;
inline constexpr UINT_PTR kPollTimerId = 0x51;

class PowerMonitor {
public:
    using Handler = std::function<void(const PowerEvent&)>;

    PowerMonitor() = default;
    ~PowerMonitor();

    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    bool Start(HWND window, const Config& config, Handler handler, std::wstring* error);
    void Stop();

    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool IsOnAc() const { return ac_; }
    int Percent() const { return percent_; }

    PowerStats GetStats() const { return stats_; }
    void ResetStats() { stats_ = PowerStats{}; }

private:
    struct DeviceNotifySubscribeParameters {
        ULONG(CALLBACK* Callback)(PVOID, ULONG, PVOID);
        PVOID Context;
    };

    static ULONG CALLBACK NotificationCallback(PVOID context, ULONG type, PVOID setting);

    void Seed(const SYSTEM_POWER_STATUS& status);
    void InitializeLatchesForState();
    void Refresh(const std::wstring& source);
    void ApplyAc(bool ac, const std::wstring& source);
    void ApplyPercent(int percent, const std::wstring& source);
    void Emit(PowerEventType type, int threshold, const std::wstring& source);
    void HandleSetting(const POWERBROADCAST_SETTING* setting, const std::wstring& source);

    HWND window_ = nullptr;
    UINT_PTR pollTimer_ = 0;
    Config config_;
    Handler handler_;

    bool started_ = false;
    bool ac_ = false;
    bool acKnown_ = false;
    int percent_ = -1;

    bool fullNotified_ = false;
    bool lowNotified_ = false;
    bool criticalNotified_ = false;
    bool lowSuppressed_ = false;
    bool criticalSuppressed_ = false;
    bool abnormalNotified_ = false;

    int plugPercent_ = -1;
    ULONGLONG plugTick_ = 0;
    PowerStats stats_{};

    HPOWERNOTIFY acdcCallback_ = nullptr;
    HPOWERNOTIFY batteryCallback_ = nullptr;
    HPOWERNOTIFY acdcWindow_ = nullptr;
    DeviceNotifySubscribeParameters subscribe_{};
};
