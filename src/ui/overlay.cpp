#include "overlay.h"

#include "../util.h"
#include "overlay_style.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kOverlayClass[] = L"WindowsChargingReminder.Overlay";
constexpr UINT_PTR kTimerId = 1;
constexpr int kFadeInMs = 140;
constexpr int kFadeOutMs = 180;

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

}  // namespace

Overlay::~Overlay() {
    Hide();
    if (timer_ != 0 && window_ != nullptr)
        KillTimer(window_, kTimerId);

    if (memoryDc_ != nullptr) {
        if (oldBitmap_ != nullptr)
            SelectObject(memoryDc_, oldBitmap_);
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    target_.Reset();
    titleFormat_.Reset();
    bodyFormat_.Reset();
    write_.Reset();
    factory_.Reset();

    if (window_ != nullptr && IsWindow(window_))
        DestroyWindow(window_);
    window_ = nullptr;
}

bool Overlay::EnsureCreated(HINSTANCE instance) {
    if (window_ != nullptr && IsWindow(window_))
        return true;

    instance_ = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = &Overlay::WndProcStatic;
    windowClass.lpszClassName = kOverlayClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));

    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClass, L"", WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, instance, this);

    if (window_ == nullptr)
        return false;

    dpi_ = static_cast<float>(GetDpiForWindow(window_));
    if (dpi_ <= 1.0f)
        dpi_ = static_cast<float>(GetDpiForSystem());

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 factory_.GetAddressOf())))
        return false;

    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_.GetAddressOf()))))
        return false;

    const D2D1_RENDER_TARGET_PROPERTIES properties =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(factory_->CreateDCRenderTarget(
            &properties, target_.GetAddressOf())))
        return false;

    CreateFormats();
    ShowWindow(window_, SW_HIDE);
    return true;
}

void Overlay::CreateFormats() {
    if (write_ == nullptr)
        return;

    write_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                             DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"zh-CN",
                             titleFormat_.GetAddressOf());
    write_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                             DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN",
                             bodyFormat_.GetAddressOf());
    write_->CreateTextFormat(L"Segoe MDL2 Assets", nullptr,
                             DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-US",
                             iconFormat_.GetAddressOf());

    if (titleFormat_ != nullptr) {
        titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (bodyFormat_ != nullptr) {
        bodyFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (iconFormat_ != nullptr) {
        iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void Overlay::Show(const Config& config, const std::wstring& title,
                   const std::wstring& body, int percent, bool charging) {
    if (window_ == nullptr)
        return;

    config_ = config;
    title_ = title;
    body_ = body;
    percent_ = std::clamp(percent, 0, 100);
    charging_ = charging;
    durationMs_ = std::clamp(config.overlayDurationMs, 500, 10000);
    opacity_ = std::clamp(config.overlayOpacity, 0, 100);
    widthPx_ = MulDiv(270, static_cast<int>(dpi_), 96);
    heightPx_ = MulDiv(78, static_cast<int>(dpi_), 96);

    EnsureBitmap(widthPx_, heightPx_);
    Render();
    PositionWindow(widthPx_, heightPx_);

    fadeAlpha_ = 0.0;
    stateStart_ = GetTickCount64();
    visible_ = true;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    SetWindowPos(window_, HWND_TOPMOST, x_, y_, widthPx_, heightPx_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateLayered();

    if (timer_ == 0)
        timer_ = SetTimer(window_, kTimerId, 16, nullptr);
}

void Overlay::Hide() {
    if (window_ == nullptr || !visible_)
        return;

    visible_ = false;
    if (timer_ != 0) {
        KillTimer(window_, kTimerId);
        timer_ = 0;
    }
    ShowWindow(window_, SW_HIDE);
}

void Overlay::EnsureBitmap(int width, int height) {
    if (bitmap_ != nullptr && bitmapWidth_ == width && bitmapHeight_ == height)
        return;

    if (memoryDc_ != nullptr) {
        if (oldBitmap_ != nullptr)
            SelectObject(memoryDc_, oldBitmap_);
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
        oldBitmap_ = nullptr;
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bitmapBits_,
                               nullptr, 0);
    memoryDc_ = CreateCompatibleDC(nullptr);
    if (bitmap_ != nullptr && memoryDc_ != nullptr)
        oldBitmap_ = SelectObject(memoryDc_, bitmap_);

    bitmapWidth_ = width;
    bitmapHeight_ = height;
}

void Overlay::Render() {
    if (target_ == nullptr || memoryDc_ == nullptr)
        return;

    RECT rect{0, 0, bitmapWidth_, bitmapHeight_};
    target_->BindDC(memoryDc_, &rect);
    target_->SetDpi(dpi_, dpi_);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    const float width = 270.0f;
    const float height = 78.0f;
    const float radius = static_cast<float>(std::clamp(
        config_.overlayCornerRadius, 0, static_cast<int>(height / 2.0f)));
    const D2D1_RECT_F card = D2D1::RectF(0.0f, 0.0f, width, height);

    const bool themeDark = IsDarkTheme(config_);
    const uint32_t background = OverlayBackgroundRgb(config_, themeDark);
    const bool lightBackground = RgbLuminance(background) > 0.55f;
    const D2D1_COLOR_F textColor = Rgb(ContrastRgb(background));
    const uint32_t iconColor =
        OverlayIconRgb(config_, charging_, lightBackground);

    ComPtr<ID2D1SolidColorBrush> backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> iconBrush;
    ComPtr<ID2D1SolidColorBrush> iconGlyphBrush;

    target_->CreateSolidColorBrush(Rgb(background), backgroundBrush.GetAddressOf());
    target_->CreateSolidColorBrush(Rgb(0x808080, 0.35f),
                                   borderBrush.GetAddressOf());
    target_->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
    target_->CreateSolidColorBrush(Rgb(iconColor), iconBrush.GetAddressOf());
    target_->CreateSolidColorBrush(Rgb(ContrastRgb(iconColor)),
                                   iconGlyphBrush.GetAddressOf());

    target_->FillRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), backgroundBrush.Get());
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), borderBrush.Get(), 1.0f);

    const D2D1_RECT_F iconBox =
        D2D1::RectF(12.0f, 19.0f, 52.0f, 59.0f);
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(iconBox, 9.0f, 9.0f), iconBrush.Get());
    target_->DrawTextW(
        L"\uE945", 1, iconFormat_.Get(),
        D2D1::RectF(iconBox.left, iconBox.top, iconBox.right, iconBox.bottom),
        iconGlyphBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const float textLeft = iconBox.right + 10.0f;
    const float textWidth = width - textLeft - 12.0f;
    if (body_.empty()) {
        target_->DrawTextW(
            title_.c_str(), static_cast<UINT32>(title_.size()),
            titleFormat_.Get(),
            D2D1::RectF(textLeft, 18.0f, textLeft + textWidth, 60.0f),
            textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
        target_->DrawTextW(
            title_.c_str(), static_cast<UINT32>(title_.size()),
            titleFormat_.Get(),
            D2D1::RectF(textLeft, 12.0f, textLeft + textWidth, 38.0f),
            textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        target_->DrawTextW(
            body_.c_str(), static_cast<UINT32>(body_.size()),
            bodyFormat_.Get(),
            D2D1::RectF(textLeft, 38.0f, textLeft + textWidth, 62.0f),
            textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    target_->EndDraw();
}

void Overlay::PositionWindow(int width, int height) {
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor =
        MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        x_ = cursor.x;
        y_ = cursor.y;
        return;
    }

    const RECT work = info.rcWork;
    const int margin = MulDiv(24, static_cast<int>(dpi_), 96);
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;

    const std::wstring& position = config_.overlayPosition;
    if (position == L"top-left") {
        x_ = work.left + margin;
        y_ = work.top + margin;
    } else if (position == L"top-center") {
        x_ = work.left + (workWidth - width) / 2;
        y_ = work.top + margin;
    } else if (position == L"top-right") {
        x_ = work.right - width - margin;
        y_ = work.top + margin;
    } else if (position == L"middle-left") {
        x_ = work.left + margin;
        y_ = work.top + (workHeight - height) / 2;
    } else if (position == L"middle-right") {
        x_ = work.right - width - margin;
        y_ = work.top + (workHeight - height) / 2;
    } else if (position == L"bottom-left") {
        x_ = work.left + margin;
        y_ = work.bottom - height - margin;
    } else if (position == L"bottom-center") {
        x_ = work.left + (workWidth - width) / 2;
        y_ = work.bottom - height - margin;
    } else {
        x_ = work.right - width - margin;
        y_ = work.bottom - height - margin;
    }

    x_ = std::clamp(x_, static_cast<int>(work.left),
                    std::max(static_cast<int>(work.left),
                             static_cast<int>(work.right) - width));
    y_ = std::clamp(y_, static_cast<int>(work.top),
                    std::max(static_cast<int>(work.top),
                             static_cast<int>(work.bottom) - height));
}

void Overlay::UpdateLayered() {
    if (window_ == nullptr || memoryDc_ == nullptr)
        return;

    const BYTE alpha = static_cast<BYTE>(std::clamp(
        static_cast<int>(std::lround(fadeAlpha_ * opacity_)), 0, 100) *
        255 / 100);
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screen = GetDC(nullptr);
    POINT destination{x_, y_};
    SIZE size{widthPx_, heightPx_};
    POINT source{0, 0};
    UpdateLayeredWindow(window_, screen, &destination, &size, memoryDc_,
                        &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void Overlay::OnTimer() {
    if (!visible_)
        return;

    const ULONGLONG now = GetTickCount64();
    const int elapsed = static_cast<int>(now - stateStart_);

    if (elapsed < kFadeInMs) {
        fadeAlpha_ = static_cast<double>(elapsed) / kFadeInMs;
    } else if (elapsed < kFadeInMs + durationMs_) {
        fadeAlpha_ = 1.0;
    } else if (elapsed < kFadeInMs + durationMs_ + kFadeOutMs) {
        fadeAlpha_ = 1.0 - static_cast<double>(elapsed - kFadeInMs -
                                               durationMs_) /
                                kFadeOutMs;
    } else {
        Hide();
        return;
    }

    UpdateLayered();
}

LRESULT CALLBACK Overlay::WndProcStatic(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    Overlay* self = reinterpret_cast<Overlay*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<Overlay*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (self == nullptr)
        return DefWindowProcW(window, message, wParam, lParam);

    return self->WndProc(window, message, wParam, lParam);
}

LRESULT Overlay::WndProc(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kTimerId) {
                OnTimer();
                return 0;
            }
            return 0;

        case WM_LBUTTONUP:
            Hide();
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_DESTROY:
            if (timer_ != 0) {
                KillTimer(window, kTimerId);
                timer_ = 0;
            }
            window_ = nullptr;
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}
