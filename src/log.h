#pragma once

#include <string>

namespace logging {

enum class Level {
    Info,
    Warn,
    Error,
};

bool Init(const std::wstring& directory);
void Write(Level level, const std::wstring& message);
void Shutdown();

std::wstring CurrentFile();

}  // namespace logging

#define LOG_INFO(message) ::logging::Write(::logging::Level::Info, (message))
#define LOG_WARN(message) ::logging::Write(::logging::Level::Warn, (message))
#define LOG_ERROR(message) ::logging::Write(::logging::Level::Error, (message))
