#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <conio.h>
#include <powersetting.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
#endif

namespace {

constexpr double kTransitionWindowMs = 2500.0;

struct DeviceNotifySubscribeParameters {
    ULONG(CALLBACK* Callback)(PVOID, ULONG, PVOID);
    PVOID Context;
};

struct Transition {
    std::string State;
    SYSTEMTIME LocalTime{};
    double StartMs = 0.0;
    std::map<std::string, double> Offsets;
};

std::mutex g_logMutex;
std::mutex g_stateMutex;

FILE* g_csv = nullptr;
long long g_qpcStart = 0;
long long g_qpcFreq = 1;
int g_seq = 0;

std::string g_lastState;
std::string g_lastPollState;
BYTE g_lastPollPercent = 255;
int g_currentTransition = -1;
std::vector<Transition> g_transitions;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_windowReady{false};
HWND g_hwnd = nullptr;
HANDLE g_windowThread = nullptr;

DeviceNotifySubscribeParameters g_subscribe{};
HPOWERNOTIFY g_callbackAcDc = nullptr;
HPOWERNOTIFY g_callbackBattery = nullptr;
HPOWERNOTIFY g_windowAcDc = nullptr;

double MsNow() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart - g_qpcStart) * 1000.0 /
           static_cast<double>(g_qpcFreq);
}

SYSTEMTIME NowLocal() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st;
}

std::string AcState(BYTE value) {
    switch (value) {
        case 0: return "DC";
        case 1: return "AC";
        case 255: return "UNKNOWN";
        default: return "AC?" + std::to_string(value);
    }
}

std::string PowerSourceState(DWORD value) {
    switch (value) {
        case 0: return "AC";
        case 1: return "DC";
        case 2: return "HOT";
        default: return "UNKNOWN(" + std::to_string(value) + ")";
    }
}

std::string CsvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;

    std::string escaped = "\"";
    for (char c : value) {
        if (c == '"')
            escaped += '"';
        escaped += c;
    }
    escaped += '"';
    return escaped;
}

void LogAt(const char* source, const char* event, const std::string& detail,
           double ms, const SYSTEMTIME& st) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    char clock[16];
    std::snprintf(clock, sizeof(clock), "%02u:%02u:%02u.%03u",
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::printf("%s %9.1fms [%-13s] %-12s %s\n",
                clock, ms, source, event, detail.c_str());
    std::fflush(stdout);

    if (g_csv) {
        char iso[32];
        std::snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
                      st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        std::fprintf(g_csv, "%d,%s,%.3f,%s,%s,%s,\n",
                     ++g_seq, iso, ms,
                     CsvEscape(source).c_str(),
                     CsvEscape(event).c_str(),
                     CsvEscape(detail).c_str());
        std::fflush(g_csv);
    }
}

void Log(const char* source, const char* event, const std::string& detail) {
    LogAt(source, event, detail, MsNow(), NowLocal());
}

void Observe(const char* source, const std::string& state,
             double ms, const SYSTEMTIME& st) {
    if (state == "UNKNOWN" || state.rfind("AC?", 0) == 0)
        return;

    std::lock_guard<std::mutex> lock(g_stateMutex);

    if (state == g_lastState) {
        if (g_currentTransition < 0)
            return;

        Transition& transition = g_transitions[static_cast<size_t>(g_currentTransition)];
        if (transition.State != state)
            return;

        const double offset = ms - transition.StartMs;
        if (offset <= kTransitionWindowMs &&
            transition.Offsets.find(source) == transition.Offsets.end()) {
            transition.Offsets[source] = offset;
        }
        return;
    }

    g_lastState = state;

    Transition transition;
    transition.State = state;
    transition.LocalTime = st;
    transition.StartMs = ms;
    transition.Offsets[source] = 0.0;

    g_transitions.push_back(std::move(transition));
    g_currentTransition = static_cast<int>(g_transitions.size()) - 1;
}

std::string HexByte(BYTE value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return buffer;
}

void HandlePowerBroadcastSetting(const char* source, const POWERBROADCAST_SETTING* setting,
                                 double ms, const SYSTEMTIME& st) {
    DWORD data = 0;
    if (setting->DataLength >= sizeof(DWORD))
        std::memcpy(&data, setting->Data, sizeof(DWORD));

    if (IsEqualGUID(setting->PowerSetting, GUID_ACDC_POWER_SOURCE)) {
        const std::string state = PowerSourceState(data);
        LogAt(source, "ACDC", state + " data=" + std::to_string(data), ms, st);
        Observe(source, state, ms, st);
    } else if (IsEqualGUID(setting->PowerSetting, GUID_BATTERY_PERCENTAGE_REMAINING)) {
        LogAt(source, "BATT", "pct=" + std::to_string(data), ms, st);
    } else {
        LogAt(source, "OTHER",
              "len=" + std::to_string(setting->DataLength) + " data=" + std::to_string(data),
              ms, st);
    }
}

ULONG CALLBACK OnPowerSetting(PVOID /*context*/, ULONG type, PVOID setting) {
    const double ms = MsNow();
    const SYSTEMTIME st = NowLocal();

    if (type == PBT_POWERSETTINGCHANGE && setting != nullptr)
        HandlePowerBroadcastSetting("CALLBACK", static_cast<const POWERBROADCAST_SETTING*>(setting),
                                    ms, st);

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const double ms = MsNow();
    const SYSTEMTIME st = NowLocal();

    switch (msg) {
        case WM_POWERBROADCAST:
            if (static_cast<UINT>(wParam) == PBT_POWERSETTINGCHANGE && lParam != 0) {
                HandlePowerBroadcastSetting(
                    "WINDOW", reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam), ms, st);
            } else if (static_cast<UINT>(wParam) == PBT_APMPOWERSTATUSCHANGE) {
                SYSTEM_POWER_STATUS status{};
                if (GetSystemPowerStatus(&status)) {
                    const std::string state = AcState(status.ACLineStatus);
                    LogAt("WINDOW-STATUS", "statuschg",
                          state + " flag=" + HexByte(status.BatteryFlag) +
                              " pct=" + std::to_string(status.BatteryLifePercent),
                          ms, st);
                    Observe("WINDOW-STATUS", state, ms, st);
                }
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

DWORD WINAPI WindowThread(LPVOID /*param*/) {
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "ChargerProbeWindow";

    if (RegisterClassExA(&wc) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            Log("WINDOW", "error", "RegisterClassEx failed: " + std::to_string(error));
            g_windowReady = true;
            return 1;
        }
    }

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPED,
                             0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (g_hwnd == nullptr) {
        Log("WINDOW", "error", "CreateWindowEx failed: " + std::to_string(GetLastError()));
        g_windowReady = true;
        return 1;
    }

    g_windowAcDc = RegisterPowerSettingNotification(
        g_hwnd, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);

    if (g_windowAcDc != nullptr)
        Log("WINDOW", "register", "GUID_ACDC ok");
    else
        Log("WINDOW", "error",
            "RegisterPowerSettingNotification failed: " + std::to_string(GetLastError()));

    g_windowReady = true;
    Log("WINDOW", "ready", "hidden window created");

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_windowAcDc != nullptr) {
        UnregisterPowerSettingNotification(g_windowAcDc);
        g_windowAcDc = nullptr;
    }

    Log("WINDOW", "stop", "");
    return 0;
}

void PollOnce() {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) {
        Log("POLL", "error", "GetSystemPowerStatus failed: " + std::to_string(GetLastError()));
        return;
    }

    const double ms = MsNow();
    const SYSTEMTIME st = NowLocal();
    const std::string state = AcState(status.ACLineStatus);
    const std::string suffix = " flag=" + HexByte(status.BatteryFlag) +
                               " pct=" + std::to_string(status.BatteryLifePercent);

    if (state != g_lastPollState) {
        g_lastPollState = state;
        LogAt("POLL", "ACDC", state + suffix, ms, st);
        Observe("POLL", state, ms, st);
    }

    if (status.BatteryLifePercent != g_lastPollPercent) {
        g_lastPollPercent = status.BatteryLifePercent;
        LogAt("POLL", "BATT", "pct=" + std::to_string(status.BatteryLifePercent) + suffix,
              ms, st);
    }
}

BOOL WINAPI CtrlHandler(DWORD /*type*/) {
    g_stop = true;
    return TRUE;
}

void PrintUsage() {
    std::puts("ChargerProbe - records AC/DC events from three paths.");
    std::puts("Usage: probe.exe [--poll 250] [--seconds 30] [--no-window] [--no-battery]");
    std::puts("  --poll N       poll GetSystemPowerStatus every N ms (100-2000, default 250)");
    std::puts("  --seconds N    stop after N seconds (default: run until Ctrl+C)");
    std::puts("  --no-window    skip the WM_POWERBROADCAST window path");
    std::puts("  --no-battery   skip the battery percentage callback");
}

std::string OffsetText(const Transition& transition, const char* source) {
    auto it = transition.Offsets.find(source);
    if (it == transition.Offsets.end())
        return "-";

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%+.1f ms", it->second);
    return buffer;
}

void PrintSummary() {
    std::lock_guard<std::mutex> lock(g_logMutex);

    std::puts("");
    std::puts("Transitions (offset from the first observation):");

    if (g_transitions.empty()) {
        std::puts("  (none observed)");
    } else {
        std::printf("%-3s %-6s %-13s %-10s %-10s %-14s %-10s\n",
                    "#", "state", "time", "CALLBACK", "WINDOW", "WINDOW-STATUS", "POLL");

        int index = 1;
        int missed = 0;
        for (const Transition& transition : g_transitions) {
            char clock[16];
            std::snprintf(clock, sizeof(clock), "%02u:%02u:%02u.%03u",
                          transition.LocalTime.wHour, transition.LocalTime.wMinute,
                          transition.LocalTime.wSecond, transition.LocalTime.wMilliseconds);

            const std::string callback = OffsetText(transition, "CALLBACK");
            if (callback == "-")
                ++missed;

            std::printf("%-3d %-6s %-13s %-10s %-10s %-14s %-10s\n",
                        index++, transition.State.c_str(), clock,
                        callback.c_str(),
                        OffsetText(transition, "WINDOW").c_str(),
                        OffsetText(transition, "WINDOW-STATUS").c_str(),
                        OffsetText(transition, "POLL").c_str());
        }

        std::printf("Callback missed %d of %zu transitions.\n", missed, g_transitions.size());
    }

    std::puts("");
    std::puts("Reminder: a transition is only counted when the state actually changes.");
    std::puts("The ACDC line right after start is the system reporting the current state.");
}

}  // namespace

int main(int argc, char** argv) {
    int pollMs = 250;
    int seconds = 0;
    bool useWindow = true;
    bool useBattery = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--poll" && i + 1 < argc) {
            pollMs = std::atoi(argv[++i]);
            if (pollMs < 100) pollMs = 100;
            if (pollMs > 2000) pollMs = 2000;
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
            if (seconds < 1) seconds = 1;
        } else if (arg == "--no-window") {
            useWindow = false;
        } else if (arg == "--no-battery") {
            useBattery = false;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        } else {
            std::printf("Unknown argument: %s\n", arg.c_str());
            PrintUsage();
            return 1;
        }
    }

    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    g_qpcFreq = frequency.QuadPart;
    g_qpcStart = counter.QuadPart;

    const SYSTEMTIME started = NowLocal();
    char csvName[64];
    std::snprintf(csvName, sizeof(csvName), "probe-%04u%02u%02u-%02u%02u%02u.csv",
                  started.wYear, started.wMonth, started.wDay,
                  started.wHour, started.wMinute, started.wSecond);

    g_csv = std::fopen(csvName, "wb");
    if (g_csv != nullptr) {
        std::fputs("seq,local_time,monotonic_ms,source,event,detail,data\n", g_csv);
        std::fflush(g_csv);
    } else {
        std::printf("Warning: could not create %s, logging to console only.\n", csvName);
    }

    std::puts("ChargerProbe - AC/DC event probe");
    std::printf("Poll: %d ms | Window: %s | Battery callback: %s\n",
                pollMs, useWindow ? "on" : "off", useBattery ? "on" : "off");
    std::printf("CSV: %s\n", g_csv != nullptr ? csvName : "(none)");
    std::puts("Plug/unplug the charger 5 times, about 2 s apart.");
    std::puts("Press any key to stop (Ctrl+C also works).");
    std::puts("");

    SYSTEM_POWER_STATUS initial{};
    if (GetSystemPowerStatus(&initial)) {
        g_lastState = AcState(initial.ACLineStatus);
        g_lastPollState = g_lastState;
        g_lastPollPercent = initial.BatteryLifePercent;
        Log("INIT", "state",
            g_lastState + " flag=" + HexByte(initial.BatteryFlag) +
                " pct=" + std::to_string(initial.BatteryLifePercent));
    } else {
        Log("INIT", "error", "GetSystemPowerStatus failed: " + std::to_string(GetLastError()));
    }

    g_subscribe.Callback = &OnPowerSetting;
    g_subscribe.Context = nullptr;

    DWORD error = PowerSettingRegisterNotification(
        &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_CALLBACK,
        reinterpret_cast<HANDLE>(&g_subscribe), &g_callbackAcDc);

    if (error == ERROR_SUCCESS)
        Log("CALLBACK", "register", "ACDC ok");
    else
        Log("CALLBACK", "error", "GUID_ACDC registration failed: " + std::to_string(error));

    if (useBattery) {
        error = PowerSettingRegisterNotification(
            &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_CALLBACK,
            reinterpret_cast<HANDLE>(&g_subscribe), &g_callbackBattery);

        if (error == ERROR_SUCCESS)
            Log("CALLBACK", "register", "BATT ok");
        else
            Log("CALLBACK", "error", "GUID_BATTERY registration failed: " + std::to_string(error));
    }

    if (useWindow) {
        g_windowThread = CreateThread(nullptr, 0, WindowThread, nullptr, 0, nullptr);
        if (g_windowThread == nullptr)
            Log("WINDOW", "error", "CreateThread failed: " + std::to_string(GetLastError()));
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    const auto begin = std::chrono::steady_clock::now();
    auto next = begin;

    while (!g_stop.load()) {
        next += std::chrono::milliseconds(pollMs);
        PollOnce();

        if (_kbhit()) {
            _getch();
            break;
        }

        if (seconds > 0 &&
            std::chrono::steady_clock::now() - begin >= std::chrono::seconds(seconds)) {
            break;
        }

        std::this_thread::sleep_until(next);
    }

    if (g_callbackAcDc != nullptr) {
        PowerSettingUnregisterNotification(g_callbackAcDc);
        g_callbackAcDc = nullptr;
    }

    if (g_callbackBattery != nullptr) {
        PowerSettingUnregisterNotification(g_callbackBattery);
        g_callbackBattery = nullptr;
    }

    if (g_windowThread != nullptr) {
        for (int i = 0; i < 200 && !g_windowReady.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (g_hwnd != nullptr)
            PostMessageA(g_hwnd, WM_CLOSE, 0, 0);

        WaitForSingleObject(g_windowThread, 2000);
        CloseHandle(g_windowThread);
        g_windowThread = nullptr;
    }

    PrintSummary();

    if (g_csv != nullptr) {
        std::fclose(g_csv);
        g_csv = nullptr;
    }

    return 0;
}
