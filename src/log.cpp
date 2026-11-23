#include "log.h"

#include "util.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace logging {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_path;

const wchar_t* LevelName(Level level) {
    switch (level) {
        case Level::Warn: return L"WARN";
        case Level::Error: return L"ERROR";
        default: return L"INFO";
    }
}

void RotateIfNeeded(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return;

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (size.QuadPart < 2 * 1024 * 1024)
        return;

    const std::wstring oldPath = path + L".1";
    DeleteFileW(oldPath.c_str());
    MoveFileW(path.c_str(), oldPath.c_str());
}

}  // namespace

bool Init(const std::wstring& directory) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }

    if (!util::EnsureDirectory(directory))
        return false;

    g_path = util::JoinPath(directory, L"app.log");
    RotateIfNeeded(g_path);

    g_file = _wfopen(g_path.c_str(), L"ab");
    return g_file != nullptr;
}

void Write(Level level, const std::wstring& message) {
    const std::wstring line =
        util::FormatTimeNow() + L" [" + LevelName(level) + L"] [" +
        std::to_wstring(GetCurrentThreadId()) + L"] " + message + L"\r\n";

    OutputDebugStringW(line.c_str());

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file == nullptr)
        return;

    const std::string utf8 = util::WideToUtf8(line);
    fwrite(utf8.data(), 1, utf8.size(), g_file);
    fflush(g_file);
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }
}

std::wstring CurrentFile() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

}  // namespace logging
