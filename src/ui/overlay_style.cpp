#include "overlay_style.h"

#include "../util.h"

#include <dwmapi.h>
#include <windows.h>

uint32_t SystemAccentRgb() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
        return color & 0x00FFFFFF;
    return 0x0078D4;
}

bool IsDarkTheme(const Config& config) {
    if (config.theme == L"dark")
        return true;
    if (config.theme == L"light")
        return false;
    return !util::IsSystemLightTheme();
}

uint32_t OverlayBackgroundRgb(const Config& config, bool dark) {
    if (config.overlayBackground == L"black")
        return 0x000000;
    if (config.overlayBackground == L"white")
        return 0xFFFFFF;
    if (config.overlayBackground == L"system")
        return dark ? 0x1B1B1B : 0xFFFFFF;
    return config.overlayBackgroundColor;
}

uint32_t OverlayIconRgb(const Config& config, bool charging,
                        bool lightBackground) {
    if (!charging)
        return lightBackground ? 0xE3E3E3 : 0x3A3A3A;

    const std::wstring& color = config.overlayIconColor;
    if (color == L"blue") return 0x0078D4;
    if (color == L"green") return 0x107C10;
    if (color == L"orange") return 0xCA5010;
    if (color == L"purple") return 0x8764B8;
    if (color == L"red") return 0xC42B1C;
    return SystemAccentRgb();
}

float RgbLuminance(uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

uint32_t ContrastRgb(uint32_t background) {
    return RgbLuminance(background) > 0.55f ? 0x1A1A1A : 0xFFFFFF;
}
