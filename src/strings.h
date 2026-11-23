#pragma once

#include <cstdint>
#include <string>

namespace strings {

void SetLanguage(const std::wstring& setting);  // system | zh-CN | en-US
std::wstring CurrentLanguage();                  // zh-CN | en-US
bool IsEnglish();

std::wstring Tr(const std::wstring& chinese);
std::wstring Duration(int64_t seconds);
std::wstring Ago(int64_t seconds);

}  // namespace strings
