#pragma once

#include <cstdint>

#include "../config.h"

uint32_t SystemAccentRgb();
bool IsDarkTheme(const Config& config);
uint32_t OverlayBackgroundRgb(const Config& config, bool dark);
uint32_t OverlayIconRgb(const Config& config, bool charging,
                        bool lightBackground);
uint32_t ContrastRgb(uint32_t background);
float RgbLuminance(uint32_t rgb);
