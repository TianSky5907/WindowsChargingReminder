#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace util {

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);

bool EnsureDirectory(const std::wstring& path);
bool FileExists(const std::wstring& path);

std::wstring GetModulePath();
std::wstring GetModuleDir();
std::wstring GetRoamingAppDataDir();
std::wstring GetLocalAppDataDir();
std::wstring GetWindowsDir();
std::wstring GetSystemMediaPath(const wchar_t* fileName);

std::wstring AppDataDir();
std::wstring LocalAppDataDir();
std::wstring ConfigPath();
std::wstring LogDir();

std::wstring FormatSystemTime(const SYSTEMTIME& time);
std::wstring FormatTimeNow();
std::wstring FormatDuration(int64_t seconds);
int64_t UnixNow();

bool IsSystemLightTheme();
void EnablePerMonitorDpiAwareness();

int ClampInt(int value, int low, int high);
std::wstring Trim(const std::wstring& text);
std::wstring ToLower(std::wstring text);
std::wstring HexDword(uint32_t value);

}  // namespace util
