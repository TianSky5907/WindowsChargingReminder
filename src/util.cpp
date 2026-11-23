#include "util.h"

#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace util {
namespace {

std::wstring KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)))
        return std::wstring();

    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

}  // namespace

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty())
        return std::wstring();

    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return std::wstring();

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), length);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty())
        return std::string();

    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return std::string();

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty())
        return right;
    if (right.empty())
        return left;

    std::wstring result = left;
    const wchar_t last = result.back();
    if (last != L'\\' && last != L'/')
        result += L'\\';
    result += right;
    return result;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty())
        return false;

    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring GetModulePath() {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return std::wstring();
        if (length < buffer.size() - 1)
            return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetModuleDir() {
    return DirectoryOf(GetModulePath());
}

std::wstring GetRoamingAppDataDir() {
    return KnownFolderPath(FOLDERID_RoamingAppData);
}

std::wstring GetLocalAppDataDir() {
    return KnownFolderPath(FOLDERID_LocalAppData);
}

std::wstring GetWindowsDir() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const UINT length = GetWindowsDirectoryW(buffer.data(),
                                                 static_cast<UINT>(buffer.size()));
        if (length == 0)
            return std::wstring();
        if (length < buffer.size())
            return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetSystemMediaPath(const wchar_t* fileName) {
    return JoinPath(JoinPath(GetWindowsDir(), L"Media"), fileName ? fileName : L"");
}

std::wstring AppDataDir() {
    return JoinPath(GetRoamingAppDataDir(), L"WindowsChargingReminder");
}

std::wstring LocalAppDataDir() {
    return JoinPath(GetLocalAppDataDir(), L"WindowsChargingReminder");
}

std::wstring ConfigPath() {
    return JoinPath(AppDataDir(), L"config.ini");
}

std::wstring LogDir() {
    return JoinPath(LocalAppDataDir(), L"logs");
}

std::wstring FormatSystemTime(const SYSTEMTIME& time) {
    wchar_t buffer[40];
    swprintf(buffer, 40, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
             time.wYear, time.wMonth, time.wDay,
             time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

std::wstring FormatTimeNow() {
    SYSTEMTIME time;
    GetLocalTime(&time);
    return FormatSystemTime(time);
}

std::wstring FormatDuration(int64_t seconds) {
    if (seconds <= 0)
        return L"0 分钟";
    if (seconds < 60)
        return std::to_wstring(seconds) + L" 秒";
    if (seconds < 3600)
        return std::to_wstring(seconds / 60) + L" 分钟";
    return std::to_wstring(seconds / 3600) + L" 小时";
}

int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool IsSystemLightTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status != ERROR_SUCCESS || value != 0;
}

void EnablePerMonitorDpiAwareness() {
    using SetDpiContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr)
        return;

    auto setContext = reinterpret_cast<SetDpiContext>(
        reinterpret_cast<void*>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (setContext != nullptr) {
        setContext(reinterpret_cast<DPI_AWARENESS_CONTEXT>(-4));
        return;
    }

    using SetDpiAware = BOOL(WINAPI*)();
    auto setAware = reinterpret_cast<SetDpiAware>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDPIAware")));
    if (setAware != nullptr)
        setAware();
}

int ClampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return std::wstring();
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

std::wstring HexDword(uint32_t value) {
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%08X", value);
    return buffer;
}

}  // namespace util
