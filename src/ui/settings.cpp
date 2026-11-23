#include "settings.h"

#include "../app.h"
#include "../log.h"
#include "../strings.h"
#include "../util.h"
#include "overlay_style.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kSettingsClass[] =
    L"WindowsChargingReminder.SettingsWindow";
constexpr float kSidebarWidth = 220.0f;
constexpr float kContentTop = 96.0f;
constexpr float kCardRadius = 10.0f;
constexpr float kMargin = 28.0f;

const wchar_t* const kPageTitles[] = {
    L"常规", L"声音", L"通知", L"悬浮窗", L"事件源", L"关于",
};
const wchar_t* const kPageDescriptions[] = {
    L"外观、启动、提示与静音",
    L"每个事件的触发阈值、声音文件和音量",
    L"Windows 通知的内容和通知声音",
    L"悬浮窗的样式、位置和预览",
    L"监听方式和运行诊断",
    L"版本、日志和重置",
};
const wchar_t* const kNavLabels[] = {
    L"常规", L"声音", L"通知", L"悬浮窗", L"事件源", L"关于",
};
const wchar_t* const kNavGlyphs[] = {
    L"\uE713", L"\uE767", L"\uE7E7", L"\uE8B9", L"\uE895", L"\uE946",
};

bool PointIn(D2D1_RECT_F rect, D2D1_POINT_2F point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

int IndexOf(const std::wstring& value,
            std::initializer_list<const wchar_t*> options) {
    int index = 0;
    for (const wchar_t* option : options) {
        if (value == option)
            return index;
        ++index;
    }
    return 0;
}

std::wstring OptionAt(int index, std::initializer_list<const wchar_t*> options) {
    int current = 0;
    for (const wchar_t* option : options) {
        if (current == index)
            return option;
        ++current;
    }
    return std::wstring();
}

std::wstring SourceLabel(const std::wstring& source) {
    if (source == L"callback") return L"回调";
    if (source == L"window") return L"窗口消息";
    if (source == L"broadcast") return L"状态广播";
    if (source == L"poll") return L"轮询";
    if (source == L"battery") return L"电池回调";
    return source;
}

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

uint32_t BlendRgb(uint32_t rgb, float factor) {
    auto channel = [factor](uint32_t value) {
        const float scaled = static_cast<float>(value) * factor;
        return static_cast<uint32_t>(
            std::clamp(scaled, 0.0f, 255.0f));
    };
    return (channel((rgb >> 16) & 0xFF) << 16) |
           (channel((rgb >> 8) & 0xFF) << 8) | channel(rgb & 0xFF);
}

}  // namespace

SettingsWindow::SettingsWindow(App* app) : app_(app) {
    if (app_ != nullptr) {
        const std::wstring& position = app_->GetConfig().overlayPosition;
        const wchar_t* keys[] = {
            L"top-left", L"top-center", L"top-right", L"middle-left",
            L"middle-right", L"bottom-left", L"bottom-center", L"bottom-right"};
        for (int i = 0; i < 8; ++i) {
            if (position == keys[i]) {
                overlayPositionIndex_ = i;
                break;
            }
        }
    }
}

SettingsWindow::~SettingsWindow() {
    ui_.Destroy();
    if (window_ != nullptr && IsWindow(window_))
        DestroyWindow(window_);
}

bool SettingsWindow::EnsureCreated(HINSTANCE instance, HWND owner) {
    if (window_ != nullptr && IsWindow(window_))
        return true;

    instance_ = instance;
    owner_ = owner;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = &SettingsWindow::WndProcStatic;
    windowClass.lpszClassName = kSettingsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = windowClass.hIcon;

    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = static_cast<float>(GetDpiForSystem());
    int width = MulDiv(1000, static_cast<int>(dpi_), 96);
    int height = MulDiv(700, static_cast<int>(dpi_), 96);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR handle = MonitorFromWindow(
        owner != nullptr ? owner : GetDesktopWindow(),
        MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(handle, &monitor)) {
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        width = std::min(width, workWidth - 80);
        height = std::min(height, workHeight - 80);
    }

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (GetMonitorInfoW(handle, &monitor)) {
        x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
    }

    window_ = CreateWindowExW(
        WS_EX_APPWINDOW, kSettingsClass,
        strings::Tr(L"Windows 充电提示 - 设置").c_str(),
        WS_POPUP | WS_MINIMIZEBOX, x, y, width, height,
        owner_, nullptr, instance_, this);

    if (window_ == nullptr)
        return false;

    const BOOL touchRegistered = RegisterTouchWindow(window_, TWF_WANTPALM);
    LOG_INFO(L"RegisterTouchWindow=" +
             std::to_wstring(touchRegistered ? 1 : 0));

    GESTURECONFIG gesture{};
    gesture.dwID = GID_PAN;
    gesture.dwWant = GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
                    GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
    gesture.dwBlock = 0;
    SetGestureConfig(window_, 0, 1, &gesture, sizeof(gesture));

    dpi_ = static_cast<float>(GetDpiForWindow(window_));

    // The first size was computed with the system DPI. On mixed-DPI or
    // high-DPI displays the monitor DPI can be larger, so resize in physical
    // pixels to keep roughly 1000 x 700 DIPs.
    int desiredWidth = MulDiv(1000, static_cast<int>(dpi_), 96);
    int desiredHeight = MulDiv(700, static_cast<int>(dpi_), 96);
    if (GetMonitorInfoW(handle, &monitor)) {
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        desiredWidth = std::min(desiredWidth, workWidth - 40);
        desiredHeight = std::min(desiredHeight, workHeight - 40);
        x = monitor.rcWork.left + (workWidth - desiredWidth) / 2;
        y = monitor.rcWork.top + (workHeight - desiredHeight) / 2;
    }

    RECT current{};
    GetWindowRect(window_, &current);
    if (current.right - current.left < desiredWidth ||
        current.bottom - current.top < desiredHeight) {
        SetWindowPos(window_, nullptr, x, y, desiredWidth, desiredHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!ui_.Create(window_)) {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    RECT client{};
    GetClientRect(window_, &client);
    ui_.Resize(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom),
               dpi_);
    UpdateWindowTheme();
    ShowWindow(window_, SW_HIDE);
    return true;
}

void SettingsWindow::Show() {
    if (window_ == nullptr)
        return;

    if (IsIconic(window_))
        ShowWindow(window_, SW_RESTORE);
    else
        ShowWindow(window_, SW_SHOW);

    UpdateWindowTheme();
    SetForegroundWindow(window_);
    SetFocus(window_);
    Invalidate();
}

void SettingsWindow::Hide() {
    if (window_ == nullptr)
        return;

    app_->SaveConfigNow();
    ShowWindow(window_, SW_HIDE);
}

bool SettingsWindow::IsVisible() const {
    return window_ != nullptr && IsWindowVisible(window_);
}

void SettingsWindow::Invalidate() {
    if (window_ != nullptr)
        InvalidateRect(window_, nullptr, FALSE);
}

void SettingsWindow::OnConfigReloaded() {
    scrollY_ = 0.0f;
    muteHours_ = 1;
    muteMinutesPart_ = 0;
    muteEditingField_ = 0;
    dirty_ = false;
    powerDirty_ = false;
    ui_.CloseDropdown();

    const std::wstring& position = app_->GetConfig().overlayPosition;
    const wchar_t* keys[] = {
        L"top-left", L"top-center", L"top-right", L"middle-left",
        L"middle-right", L"bottom-left", L"bottom-center", L"bottom-right"};
    overlayPositionIndex_ = 6;
    for (int i = 0; i < 8; ++i) {
        if (position == keys[i]) {
            overlayPositionIndex_ = i;
            break;
        }
    }

    UpdateWindowTheme();
    if (window_ != nullptr)
        SetWindowTextW(window_,
                       strings::Tr(L"Windows 充电提示 - 设置").c_str());
    Invalidate();
}

void SettingsWindow::ShowPage(int page) {
    page_ = std::clamp(page, 0, 5);
    scrollY_ = 0.0f;
    ui_.CloseDropdown();
    Invalidate();
}

void SettingsWindow::ShowMuteSection() {
    page_ = 0;
    scrollY_ = 600.0f;
    ui_.CloseDropdown();
    Invalidate();
}

void SettingsWindow::UpdateWindowTheme() {
    if (window_ == nullptr)
        return;

    const Config& config = app_->GetConfig();
    const bool dark = config.theme == L"dark" ||
                      (config.theme == L"system" && !util::IsSystemLightTheme());
    const BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark,
                          sizeof(useDark));

    const int corner = 2;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                          sizeof(corner));

    // Windows 11 paints a 1px border around top-level windows. Match it to the
    // window background so the borderless frame does not show a white line.
    const COLORREF border = dark ? RGB(0x20, 0x20, 0x20)
                                 : RGB(0xF3, 0xF3, 0xF3);
    HRESULT borderResult = DwmSetWindowAttribute(
        window_, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));

    const DWORD ncPolicy = 1;  // DWMNCRP_DISABLED
    DwmSetWindowAttribute(window_, 2 /* DWMWA_NCRENDERING_POLICY */,
                          &ncPolicy, sizeof(ncPolicy));
    static bool loggedBorder = false;
    if (!loggedBorder) {
        loggedBorder = true;
        LOG_INFO(L"border attribute hr=" +
                 util::HexDword(static_cast<uint32_t>(borderResult)));
    }
}

int SettingsWindow::NextId() {
    nextId_ += 10;
    return nextId_;
}

D2D1_RECT_F SettingsWindow::Card(float& y, float height) {
    const float left = kSidebarWidth + kMargin;
    const float right = ui_.Width() - 16.0f;
    const D2D1_RECT_F rect = D2D1::RectF(
        left, kContentTop + y - scrollY_, right,
        kContentTop + y + height - scrollY_);

    if (rect.bottom >= kContentTop && rect.top <= ui_.Height()) {
        ui_.FillRound(rect, kCardRadius, ui_.Theme().card);
        ui_.StrokeRound(rect, kCardRadius, ui_.Theme().border);
    }

    y += height + 14.0f;
    return rect;
}

void SettingsWindow::RowTitle(D2D1_RECT_F card, float top,
                              const std::wstring& title,
                              const std::wstring& description) {
    const UiTheme& theme = ui_.Theme();
    const float left = card.left + 20.0f;
    const float right = card.right - 300.0f;
    ui_.Text(title, D2D1::RectF(left, card.top + top, right,
                                card.top + top + 22.0f),
             UiTextStyle::Body, theme.text);
    if (!description.empty()) {
        ui_.TextWrapped(description,
                        D2D1::RectF(left, card.top + top + 21.0f, right,
                                    card.top + top + 42.0f),
                        UiTextStyle::Small, theme.textSecondary);
    }
}

LRESULT CALLBACK SettingsWindow::WndProcStatic(HWND window, UINT message,
                                               WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (self == nullptr)
        return DefWindowProcW(window, message, wParam, lParam);

    return self->WndProc(window, message, wParam, lParam);
}

LRESULT SettingsWindow::WndProc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
    switch (message) {
        case WM_NCCALCSIZE:
            if (wParam != 0)
                return 0;
            return DefWindowProcW(window, message, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            Render();
            EndPaint(window, &paint);
            return 0;
        }

        case WM_SIZE: {
            RECT client{};
            GetClientRect(window, &client);
            ui_.Resize(static_cast<UINT>(client.right),
                       static_cast<UINT>(client.bottom), dpi_);
            Invalidate();
            return 0;
        }

        case WM_DPICHANGED: {
            dpi_ = static_cast<float>(HIWORD(wParam));
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RECT client{};
            GetClientRect(window, &client);
            ui_.Resize(static_cast<UINT>(client.right),
                       static_cast<UINT>(client.bottom), dpi_);
            UpdateWindowTheme();
            Invalidate();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x =
                MulDiv(880, static_cast<int>(dpi_), 96);
            info->ptMinTrackSize.y =
                MulDiv(600, static_cast<int>(dpi_), 96);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT client = screen;
            ScreenToClient(window, &client);
            const float x = static_cast<float>(client.x) * 96.0f / dpi_;
            const float y = static_cast<float>(client.y) * 96.0f / dpi_;

            const float width = ui_.Width();
            const D2D1_RECT_F close =
                D2D1::RectF(width - 52.0f, 8.0f, width - 12.0f, 40.0f);
            const D2D1_RECT_F minimize =
                D2D1::RectF(width - 100.0f, 8.0f, width - 56.0f, 40.0f);

            if (PointIn(close, D2D1::Point2F(x, y)) ||
                PointIn(minimize, D2D1::Point2F(x, y))) {
                return HTCLIENT;
            }

            if (y < 44.0f)
                return HTCAPTION;

            return HTCLIENT;
        }

        case WM_GESTURE: {
            GESTUREINFO info{};
            info.cbSize = sizeof(info);
            if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lParam), &info) &&
                info.dwID == GID_PAN) {
                static int gestureLogCount = 0;
                if (gestureLogCount++ < 20) {
                    LOG_INFO(L"WM_GESTURE pan flags=0x" +
                             util::HexDword(info.dwFlags) + L" x=" +
                             std::to_wstring(info.ptsLocation.x) + L" y=" +
                             std::to_wstring(info.ptsLocation.y));
                }
                POINT point{info.ptsLocation.x, info.ptsLocation.y};
                ScreenToClient(window, &point);
                const float x = static_cast<float>(point.x) * 96.0f / dpi_;
                const float y = static_cast<float>(point.y) * 96.0f / dpi_;

                if (info.dwFlags & GF_BEGIN) {
                    gestureActive_ = true;
                    gestureStartY_ = y;
                    gestureStartScroll_ = scrollY_;
                    gestureOnScrollbar_ =
                        scrollMax_ > 0.0f && x >= ui_.Width() - 20.0f;
                    if (gestureOnScrollbar_) {
                        const float thumbBottom =
                            scrollThumbTop_ + scrollThumbHeight_;
                        if (y >= scrollThumbTop_ && y <= thumbBottom)
                            gestureThumbOffset_ = y - scrollThumbTop_;
                        else
                            gestureThumbOffset_ = scrollThumbHeight_ * 0.5f;
                    }
                } else if (gestureActive_) {
                    if (gestureOnScrollbar_ && scrollMax_ > 0.0f) {
                        const float range =
                            scrollTrackHeight_ - scrollThumbHeight_;
                        if (range > 0.0f) {
                            const float thumbTop = std::clamp(
                                y - gestureThumbOffset_, scrollTrackTop_,
                                scrollTrackTop_ + range);
                            scrollY_ =
                                (thumbTop - scrollTrackTop_) / range * scrollMax_;
                            Invalidate();
                        }
                    } else {
                        const float delta = y - gestureStartY_;
                        if (std::fabs(delta) > 4.0f && scrollMax_ > 0.0f) {
                            scrollY_ = std::clamp(
                                gestureStartScroll_ - delta, 0.0f, scrollMax_);
                            Invalidate();
                        }
                    }
                }

                if (info.dwFlags & GF_END)
                    gestureActive_ = false;

                CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lParam));
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        case WM_TOUCH: {
            const UINT count = std::min<UINT>(static_cast<UINT>(wParam), 10);
            TOUCHINPUT inputs[10]{};
            if (count > 0 &&
                GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), count,
                                  inputs, sizeof(TOUCHINPUT))) {
                static int touchLogCount = 0;
                if (touchLogCount++ < 30) {
                    LOG_INFO(L"WM_TOUCH count=" + std::to_wstring(count) +
                             L" flags=0x" +
                             util::HexDword(inputs[0].dwFlags) + L" x=" +
                             std::to_wstring(inputs[0].x) + L" y=" +
                             std::to_wstring(inputs[0].y));
                }
                for (UINT i = 0; i < count; ++i) {
                    const TOUCHINPUT& input = inputs[i];
                    POINT point{};
                    point.x = TOUCH_COORD_TO_PIXEL(input.x);
                    point.y = TOUCH_COORD_TO_PIXEL(input.y);
                    ScreenToClient(window, &point);

                    if ((input.dwFlags & TOUCHEVENTF_DOWN) && !touchActive_) {
                        touchActive_ = true;
                        touchMoved_ = false;
                        touchId_ = input.dwID;
                        touchLastX_ = point.x;
                        touchLastY_ = point.y;
                        touchStartY_ =
                            static_cast<float>(point.y) * 96.0f / dpi_;
                        touchStartScroll_ = scrollY_;
                        const float xDip =
                            static_cast<float>(point.x) * 96.0f / dpi_;
                        touchScrollbar_ =
                            scrollMax_ > 0.0f && xDip >= ui_.Width() - 20.0f;
                        if (touchScrollbar_) {
                            const float yDip =
                                static_cast<float>(point.y) * 96.0f / dpi_;
                            const float thumbBottom =
                                scrollThumbTop_ + scrollThumbHeight_;
                            if (yDip >= scrollThumbTop_ && yDip <= thumbBottom)
                                touchThumbOffset_ = yDip - scrollThumbTop_;
                            else
                                touchThumbOffset_ = scrollThumbHeight_ * 0.5f;
                            touchCanScroll_ = false;
                        } else {
                            touchCanScroll_ = MouseInContent(
                                xDip, static_cast<float>(point.y) * 96.0f / dpi_);
                        }
                    } else if ((input.dwFlags & TOUCHEVENTF_MOVE) &&
                               touchActive_ && input.dwID == touchId_) {
                        touchLastX_ = point.x;
                        touchLastY_ = point.y;
                        const float y = static_cast<float>(point.y) * 96.0f / dpi_;
                        if (std::fabs(y - touchStartY_) > 10.0f)
                            touchMoved_ = true;
                        if (touchMoved_ && touchScrollbar_ &&
                            scrollMax_ > 0.0f) {
                            const float range =
                                scrollTrackHeight_ - scrollThumbHeight_;
                            if (range > 0.0f) {
                                const float thumbTop = std::clamp(
                                    y - touchThumbOffset_, scrollTrackTop_,
                                    scrollTrackTop_ + range);
                                scrollY_ = (thumbTop - scrollTrackTop_) /
                                           range * scrollMax_;
                                Invalidate();
                            }
                        } else if (touchMoved_ && touchCanScroll_ &&
                                   scrollMax_ > 0.0f) {
                            scrollY_ = std::clamp(
                                touchStartScroll_ - (y - touchStartY_), 0.0f,
                                scrollMax_);
                            Invalidate();
                        }
                    } else if ((input.dwFlags & TOUCHEVENTF_UP) &&
                               touchActive_ && input.dwID == touchId_) {
                        const bool wasTap = !touchMoved_;
                        touchActive_ = false;
                        touchScrollbar_ = false;

                        if (wasTap) {
                            const LPARAM click = MAKELPARAM(touchLastX_,
                                                            touchLastY_);
                            SendMessageW(window, WM_MOUSEMOVE, 0, click);
                            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                                         click);
                            SendMessageW(window, WM_LBUTTONUP, 0, click);
                        }
                    }
                }
                CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0f / dpi_;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0f / dpi_;

            if (scrollDragging_ && scrollMax_ > 0.0f) {
                const float range = scrollTrackHeight_ - scrollThumbHeight_;
                if (range > 0.0f) {
                    const float thumbTop = std::clamp(
                        y - scrollDragOffset_, scrollTrackTop_,
                        scrollTrackTop_ + range);
                    scrollY_ = (thumbTop - scrollTrackTop_) / range * scrollMax_;
                }
                Invalidate();
                return 0;
            }

            ui_.MouseMove(x, y);

            if (!trackingLeave_) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);
                trackingLeave_ = true;
            }

            Invalidate();
            return 0;
        }

        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            ui_.MouseLeave();
            Invalidate();
            return 0;

        case WM_LBUTTONDOWN: {
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0f / dpi_;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0f / dpi_;
            const float width = ui_.Width();
            const D2D1_POINT_2F point = D2D1::Point2F(x, y);
            if (muteEditingField_ != 0 &&
                !PointIn(muteFieldRects_[1], point) &&
                !PointIn(muteFieldRects_[2], point)) {
                CommitMuteEdit();
            }
            if (PointIn(D2D1::RectF(width - 52.0f, 8.0f, width - 12.0f, 40.0f),
                        point)) {
                windowButtonPressed_ = 2;
                return 0;
            }
            if (PointIn(D2D1::RectF(width - 100.0f, 8.0f, width - 56.0f, 40.0f),
                        point)) {
                windowButtonPressed_ = 1;
                return 0;
            }

            if (scrollMax_ > 0.0f && x >= width - 18.0f &&
                y >= scrollTrackTop_ &&
                y <= scrollTrackTop_ + scrollTrackHeight_) {
                const float thumbBottom = scrollThumbTop_ + scrollThumbHeight_;
                scrollDragging_ = true;
                if (y >= scrollThumbTop_ && y <= thumbBottom) {
                    scrollDragOffset_ = y - scrollThumbTop_;
                } else {
                    scrollDragOffset_ = scrollThumbHeight_ * 0.5f;
                    const float range =
                        scrollTrackHeight_ - scrollThumbHeight_;
                    if (range > 0.0f) {
                        const float thumbTop = std::clamp(
                            y - scrollDragOffset_, scrollTrackTop_,
                            scrollTrackTop_ + range);
                        scrollY_ =
                            (thumbTop - scrollTrackTop_) / range * scrollMax_;
                    }
                }
                SetCapture(window);
                Invalidate();
                return 0;
            }

            SetCapture(window);
            ui_.MouseDown(x, y);
            Invalidate();
            return 0;
        }

        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0f / dpi_;
            const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0f / dpi_;
            const float width = ui_.Width();
            const D2D1_POINT_2F point = D2D1::Point2F(x, y);

            if (scrollDragging_) {
                scrollDragging_ = false;
                if (GetCapture() == window)
                    ReleaseCapture();
                Invalidate();
                return 0;
            }

            if (windowButtonPressed_ != 0) {
                const int pressed = windowButtonPressed_;
                windowButtonPressed_ = 0;
                if (pressed == 1 &&
                    PointIn(D2D1::RectF(width - 100.0f, 8.0f, width - 56.0f,
                                       40.0f),
                            point)) {
                    ShowWindow(window, SW_MINIMIZE);
                } else if (pressed == 2 &&
                           PointIn(D2D1::RectF(width - 52.0f, 8.0f, width - 12.0f,
                                              40.0f),
                                   point)) {
                    Hide();
                }
                return 0;
            }

            ui_.MouseUp(x, y);
            if (GetCapture() == window)
                ReleaseCapture();
            Invalidate();
            return 0;
        }

        case WM_CAPTURECHANGED:
            scrollDragging_ = false;
            if (lParam != 0)
                ui_.MouseUp(-10000.0f, -10000.0f);
            return 0;

        case WM_MOUSEWHEEL: {
            if (ui_.HasOpenOverlay())
                return 0;

            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &screen);
            const float x = static_cast<float>(screen.x) * 96.0f / dpi_;
            const float y = static_cast<float>(screen.y) * 96.0f / dpi_;
            if (!MouseInContent(x, y))
                return 0;

            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float viewHeight = ui_.Height() - kContentTop - 16.0f;
            const float maxScroll =
                std::max(0.0f, contentHeight_ - viewHeight);
            scrollY_ = std::clamp(
                scrollY_ - static_cast<float>(delta) / 120.0f * 64.0f,
                0.0f, maxScroll);
            Invalidate();
            return 0;
        }

        case WM_CHAR:
            if (muteEditingField_ != 0) {
                const wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch >= L'0' && ch <= L'9' && muteEditText_.size() < 2) {
                    muteEditText_.push_back(ch);
                    Invalidate();
                } else if (ch == 8) {
                    if (!muteEditText_.empty())
                        muteEditText_.pop_back();
                    Invalidate();
                } else if (ch == 13) {
                    CommitMuteEdit();
                } else if (ch == 27) {
                    CancelMuteEdit();
                }
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);

        case WM_KEYDOWN:
            if (muteEditingField_ != 0 && wParam == VK_ESCAPE) {
                CancelMuteEdit();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                if (ui_.HasOpenOverlay()) {
                    ui_.CloseDropdown();
                    Invalidate();
                } else {
                    Hide();
                }
                return 0;
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (muteEditingField_ != 0)
                    CommitMuteEdit();
                ui_.CloseDropdown();
                Invalidate();
            }
            UpdateWindowTheme();
            return 0;

        case WM_NCACTIVATE:
            UpdateWindowTheme();
            return DefWindowProcW(window, message, wParam, lParam);

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            UpdateWindowTheme();
            Invalidate();
            return 0;

        case WM_CLOSE:
            Hide();
            return 0;

        case WM_DESTROY:
            ui_.Destroy();
            window_ = nullptr;
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void SettingsWindow::Render() {
    Config& config = app_->MutableConfig();
    const bool dark = config.theme == L"dark" ||
                      (config.theme == L"system" && !util::IsSystemLightTheme());
    UiTheme theme = dark ? MakeDarkTheme() : MakeLightTheme();
    if (config.theme == L"system") {
        const uint32_t accent = SystemAccentRgb();
        theme.accent = Rgb(accent);
        theme.accentHover =
            Rgb(BlendRgb(accent, dark ? 1.15f : 0.85f));
        theme.onAccent = Rgb(ContrastRgb(accent));
    }
    const bool themeChanged = ui_.Theme().dark != theme.dark;

    RECT client{};
    GetClientRect(window_, &client);
    ui_.Resize(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom),
               dpi_);

    static int lastLoggedWidth = -1;
    static int lastLoggedHeight = -1;
    if (client.right != lastLoggedWidth || client.bottom != lastLoggedHeight) {
        lastLoggedWidth = client.right;
        lastLoggedHeight = client.bottom;
        LOG_INFO(L"settings client=" + std::to_wstring(client.right) + L"x" +
                 std::to_wstring(client.bottom) + L" dpi=" +
                 std::to_wstring(static_cast<int>(dpi_)) + L" ui=" +
                 std::to_wstring(static_cast<int>(ui_.Width())) + L"x" +
                 std::to_wstring(static_cast<int>(ui_.Height())));
    }

    if (!ui_.Begin(theme))
        return;

    RenderSidebar();
    RenderHeader();

    const D2D1_RECT_F contentClip = D2D1::RectF(
        kSidebarWidth, kContentTop, ui_.Width() - 14.0f, ui_.Height());
    ui_.PushClip(contentClip);

    nextId_ = page_ * 10000;
    float y = 8.0f;
    RenderPage(y);
    contentHeight_ = y + 8.0f;

    ui_.PopClip();

    const float viewHeight = ui_.Height() - kContentTop - 16.0f;
    if (contentHeight_ > viewHeight && contentHeight_ > 0.0f) {
        const float maxScroll = contentHeight_ - viewHeight;
        if (scrollY_ > maxScroll)
            scrollY_ = maxScroll;
        if (scrollY_ < 0.0f)
            scrollY_ = 0.0f;

        const float trackTop = kContentTop + 8.0f;
        const float trackHeight = viewHeight - 16.0f;
        const float thumbHeight =
            std::max(40.0f, trackHeight * viewHeight / contentHeight_);
        const float thumbTop =
            trackTop + (trackHeight - thumbHeight) * scrollY_ / maxScroll;
        const float right = ui_.Width() - 6.0f;

        scrollTrackTop_ = trackTop;
        scrollTrackHeight_ = trackHeight;
        scrollThumbTop_ = thumbTop;
        scrollThumbHeight_ = thumbHeight;
        scrollMax_ = maxScroll;

        const bool scrollHover = ui_.MouseInside(
            ui_.Width() - 18.0f, trackTop, ui_.Width(), trackTop + trackHeight);
        const D2D1_COLOR_F thumbColor =
            scrollDragging_ || scrollHover ? ui_.Theme().accent
                                            : ui_.Theme().textSecondary;

        ui_.FillRound(D2D1::RectF(right - 3.0f, trackTop, right, trackTop + trackHeight),
                      3.0f, ui_.Theme().track);
        ui_.FillRound(D2D1::RectF(right - 3.0f, thumbTop, right,
                                  thumbTop + thumbHeight),
                      3.0f, thumbColor);
    } else {
        scrollMax_ = 0.0f;
    }

    ui_.DrawOverlays();
    ui_.End();

    if (themeChanged)
        UpdateWindowTheme();

    if (dirty_) {
        app_->SaveConfigSoon();
        dirty_ = false;
    }

    if (powerDirty_) {
        app_->ApplyPowerConfig();
        powerDirty_ = false;
    }
}

void SettingsWindow::RenderSidebar() {
    const UiTheme& theme = ui_.Theme();
    const float height = ui_.Height();

    ui_.FillRect(D2D1::RectF(0.0f, 0.0f, kSidebarWidth, height),
                 theme.sidebar);
    ui_.Line(D2D1::Point2F(kSidebarWidth, 0.0f),
             D2D1::Point2F(kSidebarWidth, height), theme.border);

    const D2D1_RECT_F badge =
        D2D1::RectF(20.0f, 18.0f, 56.0f, 54.0f);
    const uint32_t accent = SystemAccentRgb();
    ui_.FillRound(badge, 9.0f, Rgb(accent));
    ui_.Icon(L"\uE945",
             D2D1::RectF(badge.left, badge.top, badge.right, badge.bottom),
             20.0f, Rgb(ContrastRgb(accent)));

    ui_.Text(L"Windows",
             D2D1::RectF(66.0f, 14.0f, kSidebarWidth - 12.0f, 38.0f),
             UiTextStyle::Heading, theme.text);
    ui_.Text(L"充电提示",
             D2D1::RectF(66.0f, 38.0f, kSidebarWidth - 12.0f, 62.0f),
             UiTextStyle::Heading, theme.text);

    float y = 96.0f;
    for (int i = 0; i < 6; ++i) {
        if (ui_.SidebarItem(1000 + i * 10,
                            D2D1::RectF(12.0f, y, kSidebarWidth - 12.0f,
                                        y + 42.0f),
                            kNavGlyphs[i], kNavLabels[i], page_ == i)) {
            page_ = i;
            scrollY_ = 0.0f;
            ui_.CloseDropdown();
            Invalidate();
        }
        y += 48.0f;
    }

    const std::wstring status =
        app_->IsOnAc() ? L"AC" : L"DC";
    const int percent = app_->Percent();
    std::wstring state = status;
    if (percent >= 0)
        state += L" · " + std::to_wstring(percent) + L"%";

    ui_.Text(state, D2D1::RectF(22.0f, height - 62.0f, kSidebarWidth - 16.0f,
                               height - 38.0f),
             UiTextStyle::Small, theme.textSecondary);
    ui_.Text(L"v0.1.0", D2D1::RectF(22.0f, height - 38.0f,
                                    kSidebarWidth - 16.0f, height - 16.0f),
             UiTextStyle::Small, theme.textSecondary);
}

void SettingsWindow::RenderHeader() {
    const UiTheme& theme = ui_.Theme();
    const float left = kSidebarWidth + kMargin;

    ui_.Text(kPageTitles[page_],
             D2D1::RectF(left, 22.0f, ui_.Width() - 140.0f, 50.0f),
             UiTextStyle::Title, theme.text);
    ui_.Text(kPageDescriptions[page_],
             D2D1::RectF(left, 54.0f, ui_.Width() - 140.0f, 74.0f),
             UiTextStyle::Small, theme.textSecondary);
    ui_.Line(D2D1::Point2F(kSidebarWidth, 88.0f),
             D2D1::Point2F(ui_.Width(), 88.0f), theme.border);

    const float width = ui_.Width();
    const bool closeHover = ui_.MouseInside(width - 52.0f, 8.0f, width - 12.0f,
                                            40.0f);
    const bool minHover = ui_.MouseInside(width - 100.0f, 8.0f, width - 56.0f,
                                          40.0f);

    if (minHover)
        ui_.FillRound(D2D1::RectF(width - 100.0f, 8.0f, width - 56.0f, 40.0f),
                      7.0f, theme.cardHover);
    if (closeHover)
        ui_.FillRound(D2D1::RectF(width - 52.0f, 8.0f, width - 12.0f, 40.0f),
                      7.0f, theme.danger);

    ui_.Icon(L"\uE921",
             D2D1::RectF(width - 100.0f, 8.0f, width - 56.0f, 40.0f),
             12.0f, theme.textSecondary);
    ui_.Icon(L"\uE8BB",
             D2D1::RectF(width - 52.0f, 8.0f, width - 12.0f, 40.0f),
             12.0f, closeHover ? theme.onAccent : theme.textSecondary);
}

void SettingsWindow::RenderPage(float& y) {
    switch (page_) {
        case 0:
            RenderGeneral(y);
            break;
        case 1:
            RenderSound(y);
            break;
        case 2:
            RenderNotification(y);
            break;
        case 3:
            RenderOverlay(y);
            break;
        case 4:
            RenderSources(y);
            break;
        default:
            RenderAbout(y);
            break;
    }
}

void SettingsWindow::RenderGeneral(float& y) {
    Config& config = app_->MutableConfig();
    const UiTheme& theme = ui_.Theme();

    {
        D2D1_RECT_F card = Card(y, 184.0f);
        ui_.Text(L"外观", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);
        RowTitle(card, 48.0f, L"主题", L"");
        const int id = NextId();
        int index = config.theme == L"light" ? 1 : (config.theme == L"dark" ? 2 : 0);
        if (ui_.Segmented(id,
                          D2D1::RectF(card.right - 300.0f, card.top + 52.0f,
                                      card.right - 20.0f, card.top + 84.0f),
                          {L"跟随系统", L"浅色", L"深色"}, index)) {
            config.theme = index == 1 ? L"light" : (index == 2 ? L"dark" : L"system");
            dirty_ = true;
        }

        RowTitle(card, 104.0f, L"语言", L"");
        int language = config.language == L"zh-CN"
                           ? 1
                           : (config.language == L"en-US" ? 2 : 0);
        if (ui_.Segmented(NextId(),
                          D2D1::RectF(card.right - 300.0f, card.top + 108.0f,
                                      card.right - 20.0f, card.top + 140.0f),
                          {L"跟随系统", L"简体中文", L"English"}, language)) {
            config.language = language == 1
                                  ? L"zh-CN"
                                  : (language == 2 ? L"en-US" : L"system");
            strings::SetLanguage(config.language);
            SetWindowTextW(window_,
                           strings::Tr(L"Windows 充电提示 - 设置").c_str());
            app_->OnLanguageChanged();
            dirty_ = true;
        }
    }

    {
        D2D1_RECT_F card = Card(y, 184.0f);
        ui_.Text(L"启动与行为", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                           card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        RowTitle(card, 44.0f, L"开机启动", L"");
        {
            const int id = NextId();
            if (ui_.Toggle(id,
                           D2D1::RectF(card.right - 80.0f, card.top + 52.0f,
                                       card.right - 20.0f, card.top + 84.0f),
                           config.startWithWindows)) {
                app_->SetAutoStartEnabled(config.startWithWindows);
                dirty_ = true;
            }
        }

        RowTitle(card, 100.0f, L"跟随系统勿扰", L"静音、全屏或演示时不提示");
        {
            const int id = NextId();
            if (ui_.Toggle(id,
                           D2D1::RectF(card.right - 80.0f, card.top + 108.0f,
                                       card.right - 20.0f, card.top + 140.0f),
                           config.followDnd))
                dirty_ = true;
        }
    }

    {
        D2D1_RECT_F card = Card(y, 238.0f);
        ui_.Text(L"提示方式", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                         card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        const wchar_t* labels[] = {L"播放声音", L"Windows 通知", L"悬浮窗"};
        bool* values[] = {&config.soundEnabled, &config.notifyEnabled,
                          &config.overlayEnabled};
        for (int i = 0; i < 3; ++i) {
            RowTitle(card, 44.0f + i * 56.0f, labels[i], L"");
            const int id = NextId();
            if (ui_.Toggle(id,
                           D2D1::RectF(card.right - 80.0f,
                                       card.top + 52.0f + i * 56.0f,
                                       card.right - 20.0f,
                                       card.top + 84.0f + i * 56.0f),
                           *values[i]))
                dirty_ = true;
        }
    }

    {
        D2D1_RECT_F card = Card(y, 220.0f);
        ui_.Text(L"暂时静音", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                         card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        const bool muted = config.muteUntil > util::UnixNow();
        const std::wstring state =
            muted ? strings::Tr(L"已静音，剩余 ") +
                        strings::Duration(config.muteUntil - util::UnixNow())
                  : strings::Tr(L"当前未静音");
        ui_.Text(state, D2D1::RectF(card.left + 20.0f, card.top + 50.0f,
                                    card.right - 220.0f, card.top + 78.0f),
                 UiTextStyle::Body,
                 muted ? theme.accent : theme.textSecondary);

        const int id = NextId();
        if (muted) {
            if (ui_.Button(id,
                           D2D1::RectF(card.right - 180.0f, card.top + 48.0f,
                                       card.right - 20.0f, card.top + 80.0f),
                           L"取消静音", false)) {
                config.muteUntil = 0;
                dirty_ = true;
            }
        }

        RowTitle(card, 104.0f, L"静音时长", L"最大 99 小时 59 分钟");

        auto durationField = [&](int field, float left, int& value, int maxValue,
                                 const wchar_t* unit) {
            const float top = card.top + 148.0f;
            const float width = 150.0f;
            const D2D1_RECT_F outer =
                D2D1::RectF(left, top, left + width, top + 34.0f);
            muteFieldRects_[field] = outer;

            const bool editing = muteEditingField_ == field;
            const D2D1_RECT_F textRect =
                D2D1::RectF(outer.left + 30.0f, outer.top,
                            outer.right - 30.0f, outer.bottom);
            const bool textHover = ui_.MouseInside(
                textRect.left, textRect.top, textRect.right, textRect.bottom);
            if (ui_.MousePressed() && textHover) {
                muteEditingField_ = field;
                muteEditText_.clear();
            }

            ui_.FillRound(outer, 7.0f,
                          textHover || editing ? theme.cardHover : theme.card);
            ui_.StrokeRound(outer, 7.0f,
                            editing ? theme.accent : theme.border);

            if (ui_.Button(NextId(),
                           D2D1::RectF(outer.left, outer.top,
                                       outer.left + 30.0f, outer.bottom),
                           L"−")) {
                if (field == 2 && value == 0 && muteHours_ > 0) {
                    --muteHours_;
                    value = 59;
                } else {
                    value = std::max(0, value - 1);
                }
            }
            if (ui_.Button(NextId(),
                           D2D1::RectF(outer.right - 30.0f, outer.top,
                                       outer.right, outer.bottom),
                           L"+")) {
                if (field == 2 && value == 59 && muteHours_ < 99) {
                    ++muteHours_;
                    value = 0;
                } else {
                    value = std::min(maxValue, value + 1);
                }
            }

            ui_.Text(editing ? muteEditText_ : std::to_wstring(value),
                     textRect, UiTextStyle::Value, theme.text,
                     DWRITE_TEXT_ALIGNMENT_CENTER);
            ui_.Text(unit,
                     D2D1::RectF(outer.right + 6.0f, outer.top,
                                 outer.right + 48.0f, outer.bottom),
                     UiTextStyle::Small, theme.textSecondary);
        };

        durationField(1, card.right - 560.0f, muteHours_, 99, L"小时");
        durationField(2, card.right - 360.0f, muteMinutesPart_, 59, L"分钟");

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 160.0f, card.top + 148.0f,
                                   card.right - 20.0f, card.top + 182.0f),
                       L"立即静音", true)) {
            CommitMuteEdit();
            const int totalMinutes =
                std::clamp(muteHours_ * 60 + muteMinutesPart_, 1, 99 * 60 + 59);
            config.muteUntil = util::UnixNow() + totalMinutes * 60;
            dirty_ = true;
        }
    }
}

void SettingsWindow::RenderSound(float& y) {
    Config& config = app_->MutableConfig();
    const UiTheme& theme = ui_.Theme();

    {
        D2D1_RECT_F card = Card(y, 166.0f);
        ui_.Text(L"总体", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        RowTitle(card, 44.0f, L"启用声音", L"关闭后仍可使用通知和悬浮窗");
        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 52.0f,
                                   card.right - 20.0f, card.top + 84.0f),
                       config.soundEnabled))
            dirty_ = true;

        RowTitle(card, 100.0f, L"主音量", L"会与每个事件的音量相乘");
        if (ui_.Slider(NextId(),
                       D2D1::RectF(card.right - 340.0f, card.top + 108.0f,
                                   card.right - 20.0f, card.top + 140.0f),
                       config.masterVolume, 0, 100, L"%"))
            dirty_ = true;

        ui_.Text(L"暂只支持 WAV。",
                 D2D1::RectF(card.left + 20.0f, card.top + 144.0f,
                             card.right - 20.0f, card.top + 162.0f),
                 UiTextStyle::Small, theme.textSecondary);
    }

    auto eventCard = [&](const std::wstring& title, const std::wstring& description,
                         bool& soundEnabled, std::wstring& file,
                         int& volume, PowerEventType type, bool hasThreshold,
                         int& threshold, int thresholdLow, int thresholdHigh,
                         const std::wstring& suffix, bool abnormal,
                         int* abnormalMinutes, int* abnormalDrop) {
        const float height = 210.0f + (hasThreshold ? 38.0f : 0.0f) +
                             (abnormal ? 70.0f : 0.0f);
        D2D1_RECT_F card = Card(y, height);
        if (card.bottom < kContentTop || card.top > ui_.Height())
            return;

        ui_.Text(title, D2D1::RectF(card.left + 20.0f, card.top + 16.0f,
                                    card.right - 110.0f, card.top + 40.0f),
                 UiTextStyle::Heading, theme.text);
        ui_.TextWrapped(description,
                        D2D1::RectF(card.left + 20.0f, card.top + 40.0f,
                                    card.right - 110.0f, card.top + 62.0f),
                        UiTextStyle::Small, theme.textSecondary);

        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 16.0f,
                                   card.right - 20.0f, card.top + 48.0f),
                       soundEnabled))
            dirty_ = true;

        float row = card.top + 68.0f;

        if (hasThreshold) {
            ui_.Text(L"触发阈值",
                     D2D1::RectF(card.left + 20.0f, row, card.right - 180.0f,
                                 row + 30.0f),
                     UiTextStyle::Small, theme.textSecondary);
            if (ui_.Stepper(NextId(),
                            D2D1::RectF(card.right - 160.0f, row - 1.0f,
                                        card.right - 20.0f, row + 31.0f),
                            threshold, 1, thresholdLow, thresholdHigh, suffix)) {
                dirty_ = true;
                powerDirty_ = true;
                if (type == PowerEventType::Low &&
                    config.criticalPercent >= config.lowPercent) {
                    config.criticalPercent =
                        std::max(1, config.lowPercent - 1);
                }
            }
            row += 38.0f;
        }

        ui_.Text(L"声音文件",
                 D2D1::RectF(card.left + 20.0f, row, card.right - 380.0f,
                             row + 18.0f),
                 UiTextStyle::Small, theme.textSecondary);
        const std::wstring display =
            file.empty() ? L"使用 Windows 默认声音" : util::FileNameOf(file);
        ui_.Text(display,
                 D2D1::RectF(card.left + 20.0f, row + 19.0f,
                             card.right - 360.0f, row + 42.0f),
                 UiTextStyle::Small, theme.textSecondary);

        const float buttonTop = row + 3.0f;
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 330.0f, buttonTop,
                                   card.right - 250.0f, buttonTop + 30.0f),
                       L"选择...")) {
            std::wstring picked;
            if (PickSoundFile(picked)) {
                file = picked;
                dirty_ = true;
            }
        }
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 244.0f, buttonTop,
                                   card.right - 168.0f, buttonTop + 30.0f),
                       L"默认")) {
            if (!file.empty()) {
                file.clear();
                dirty_ = true;
            }
        }
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 162.0f, buttonTop,
                                   card.right - 20.0f, buttonTop + 30.0f),
                       L"预览")) {
            app_->PlayPreview(type);
        }

        row += 48.0f;

        ui_.Text(L"音量", D2D1::RectF(card.left + 20.0f, row,
                                      card.right - 300.0f, row + 30.0f),
                 UiTextStyle::Small, theme.textSecondary);
        if (ui_.Slider(NextId(),
                       D2D1::RectF(card.right - 300.0f, row - 1.0f,
                                   card.right - 20.0f, row + 31.0f),
                       volume, 0, 100, L"%"))
            dirty_ = true;

        if (abnormal && abnormalMinutes != nullptr && abnormalDrop != nullptr) {
            row += 40.0f;
            ui_.Text(L"时间窗口",
                     D2D1::RectF(card.left + 20.0f, row, card.right - 360.0f,
                                 row + 30.0f),
                     UiTextStyle::Small, theme.textSecondary);
            if (ui_.Stepper(NextId(),
                            D2D1::RectF(card.right - 340.0f, row - 1.0f,
                                        card.right - 200.0f, row + 31.0f),
                            *abnormalMinutes, 1, 1, 1440, strings::Tr(L" 分钟"))) {
                dirty_ = true;
                powerDirty_ = true;
            }
            ui_.Text(L"下降",
                     D2D1::RectF(card.right - 190.0f, row, card.right - 160.0f,
                                 row + 30.0f),
                     UiTextStyle::Small, theme.textSecondary);
            if (ui_.Stepper(NextId(),
                            D2D1::RectF(card.right - 160.0f, row - 1.0f,
                                        card.right - 20.0f, row + 31.0f),
                            *abnormalDrop, 1, 1, 50, L"%")) {
                dirty_ = true;
                powerDirty_ = true;
            }
        }
    };

    eventCard(L"插入电源", L"",
              config.soundEventPlug, config.soundPlug, config.volumePlug,
              PowerEventType::Plugged, false, config.fullPercent, 1, 100, L"%",
              false, nullptr, nullptr);

    eventCard(L"拔出电源", L"",
              config.soundEventUnplug, config.soundUnplug, config.volumeUnplug,
              PowerEventType::Unplugged, false, config.fullPercent, 1, 100, L"%",
              false, nullptr, nullptr);

    eventCard(L"电量充满", L"达到阈值时提示。",
              config.soundEventFull, config.soundFull, config.volumeFull,
              PowerEventType::Full, true, config.fullPercent, 1, 100, L"%",
              false, nullptr, nullptr);

    eventCard(L"电量较低", L"低于阈值时提示一次。",
              config.soundEventLow, config.soundLow, config.volumeLow,
              PowerEventType::Low, true, config.lowPercent, 2, 99, L"%",
              false, nullptr, nullptr);

    eventCard(L"电量不足", L"低于第二阈值时提示一次。",
              config.soundEventCritical, config.soundCritical, config.volumeCritical,
              PowerEventType::Critical, true, config.criticalPercent, 1,
              std::max(1, config.lowPercent - 1), L"%", false, nullptr, nullptr);

    eventCard(L"充电异常", L"插电后电量异常下降时提示。",
              config.soundEventAbnormal, config.soundAbnormal, config.volumeAbnormal,
              PowerEventType::Abnormal, false, config.fullPercent, 1, 100, L"%",
              true, &config.abnormalWindowMinutes, &config.abnormalDrop);
}

void SettingsWindow::RenderNotification(float& y) {
    Config& config = app_->MutableConfig();
    const UiTheme& theme = ui_.Theme();

    {
        D2D1_RECT_F card = Card(y, 270.0f);
        ui_.Text(L"Windows 通知", D2D1::RectF(card.left + 20.0f,
                                             card.top + 14.0f,
                                             card.right - 20.0f,
                                             card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        RowTitle(card, 44.0f, L"启用通知", L"");
        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 52.0f,
                                   card.right - 20.0f, card.top + 84.0f),
                       config.notifyEnabled))
            dirty_ = true;

        RowTitle(card, 100.0f, L"显示当前电量", L"");
        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 108.0f,
                                   card.right - 20.0f, card.top + 140.0f),
                       config.notifyShowPercent))
            dirty_ = true;

        RowTitle(card, 156.0f, L"显示阈值文案", L"充满时显示“电量已充至 N%”");
        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 164.0f,
                                   card.right - 20.0f, card.top + 196.0f),
                       config.notifyShowThreshold))
            dirty_ = true;

        RowTitle(card, 212.0f, L"通知声音", L"自动：应用声音开启时不响系统通知声");
        int soundMode = config.notifySoundMode == L"on"
                            ? 1
                            : (config.notifySoundMode == L"off" ? 2 : 0);
        if (ui_.Segmented(NextId(),
                          D2D1::RectF(card.right - 300.0f, card.top + 220.0f,
                                      card.right - 20.0f, card.top + 252.0f),
                          {L"自动", L"始终开启", L"始终关闭"}, soundMode)) {
            config.notifySoundMode =
                soundMode == 1 ? L"on" : (soundMode == 2 ? L"off" : L"auto");
            dirty_ = true;
        }
    }

    {
        D2D1_RECT_F card = Card(y, 118.0f);
        ui_.Text(L"预览", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);
        ui_.TextWrapped(L"通知内容使用当前阈值和电量显示设置生成。",
                        D2D1::RectF(card.left + 20.0f, card.top + 44.0f,
                                    card.right - 220.0f, card.top + 72.0f),
                        UiTextStyle::Small, theme.textSecondary);
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 180.0f, card.top + 48.0f,
                                   card.right - 20.0f, card.top + 80.0f),
                       L"显示通知预览", true)) {
            app_->PreviewNotification();
        }
    }
}

void SettingsWindow::RenderOverlay(float& y) {
    Config& config = app_->MutableConfig();
    const UiTheme& theme = ui_.Theme();

    {
        D2D1_RECT_F card = Card(y, 120.0f);
        ui_.Text(L"悬浮窗", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                       card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);
        RowTitle(card, 44.0f, L"启用悬浮窗", L"");
        if (ui_.Toggle(NextId(),
                       D2D1::RectF(card.right - 80.0f, card.top + 52.0f,
                                   card.right - 20.0f, card.top + 84.0f),
                       config.overlayEnabled))
            dirty_ = true;
    }

    {
        D2D1_RECT_F card = Card(y, 360.0f);
        ui_.Text(L"外观与位置", D2D1::RectF(card.left + 20.0f,
                                            card.top + 14.0f,
                                            card.right - 20.0f,
                                            card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        RowTitle(card, 44.0f, L"位置", L"不会占用任务栏区域");
        {
            const std::vector<std::wstring> positions = {
                L"左上", L"上中", L"右上", L"左中",
                L"右中", L"左下", L"下中", L"右下"};
            const wchar_t* keys[] = {
                L"top-left", L"top-center", L"top-right", L"middle-left",
                L"middle-right", L"bottom-left", L"bottom-center",
                L"bottom-right"};

            ui_.Dropdown(NextId(),
                         D2D1::RectF(card.right - 260.0f, card.top + 42.0f,
                                     card.right - 20.0f, card.top + 74.0f),
                         positions, overlayPositionIndex_);

            if (overlayPositionIndex_ >= 0 && overlayPositionIndex_ < 8 &&
                config.overlayPosition != keys[overlayPositionIndex_]) {
                config.overlayPosition = keys[overlayPositionIndex_];
                dirty_ = true;
            }
        }

        RowTitle(card, 100.0f, L"显示时长", L"0.5–10.0 秒，按 0.1 秒调整");
        {
            int tenths = config.overlayDurationMs / 100;
            if (ui_.Stepper(NextId(),
                            D2D1::RectF(card.right - 160.0f, card.top + 99.0f,
                                        card.right - 20.0f, card.top + 131.0f),
                            tenths, 1, 5, 100, strings::Tr(L" 秒"), 10)) {
                config.overlayDurationMs = tenths * 100;
                dirty_ = true;
            }
        }

        RowTitle(card, 152.0f, L"不透明度", L"");
        if (ui_.Slider(NextId(),
                       D2D1::RectF(card.right - 260.0f, card.top + 151.0f,
                                   card.right - 20.0f, card.top + 183.0f),
                       config.overlayOpacity, 0, 100, L"%"))
            dirty_ = true;

        RowTitle(card, 196.0f, L"圆角", L"");
        if (ui_.Slider(NextId(),
                       D2D1::RectF(card.right - 260.0f, card.top + 195.0f,
                                   card.right - 20.0f, card.top + 227.0f),
                       config.overlayCornerRadius, 0, 40, L" px"))
            dirty_ = true;

        RowTitle(card, 240.0f, L"背景", L"");
        {
            int background = IndexOf(config.overlayBackground,
                                     {L"system", L"black", L"white"});
            if (ui_.Segmented(NextId(),
                              D2D1::RectF(card.right - 320.0f, card.top + 239.0f,
                                          card.right - 20.0f, card.top + 271.0f),
                              {L"跟随系统", L"黑色", L"白色"}, background)) {
                config.overlayBackground =
                    OptionAt(background, {L"system", L"black", L"white"});
                dirty_ = true;
            }
        }

        RowTitle(card, 284.0f, L"图标框颜色", L"第一个为跟随系统强调色");
        {
            const wchar_t* keys[] = {L"system", L"blue", L"green",
                                     L"orange", L"purple", L"red"};
            const uint32_t colors[] = {SystemAccentRgb(), 0x0078D4,
                                       0x107C10, 0xCA5010, 0x8764B8,
                                       0xC42B1C};
            float x = card.right - 220.0f;
            for (int i = 0; i < 6; ++i) {
                const D2D1_RECT_F swatchRect =
                    D2D1::RectF(x, card.top + 280.0f, x + 26.0f,
                                card.top + 306.0f);
                if (ui_.Swatch(NextId(), swatchRect, colors[i],
                               config.overlayIconColor == keys[i])) {
                    config.overlayIconColor = keys[i];
                    dirty_ = true;
                }
                if (i == 0) {
                    ui_.Icon(L"\uE713", swatchRect, 13.0f,
                             Rgb(ContrastRgb(colors[0])));
                }
                x += 34.0f;
            }
        }
    }

    {
        D2D1_RECT_F card = Card(y, 218.0f);
        ui_.Text(L"预览", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        const float sampleWidth = std::min(270.0f, card.right - card.left - 40.0f);
        const D2D1_RECT_F sample =
            D2D1::RectF(card.left + 20.0f, card.top + 50.0f,
                        card.left + 20.0f + sampleWidth, card.top + 128.0f);
        const bool themeDark = IsDarkTheme(config);
        const uint32_t background =
            OverlayBackgroundRgb(config, themeDark);
        const bool lightBackground = RgbLuminance(background) > 0.55f;
        const D2D1_COLOR_F textColor = Rgb(ContrastRgb(background));

        ui_.FillRound(sample, static_cast<float>(config.overlayCornerRadius),
                      Rgb(background));
        ui_.StrokeRound(sample, static_cast<float>(config.overlayCornerRadius),
                        Rgb(0x808080, 0.35f));

        const D2D1_RECT_F iconBox =
            D2D1::RectF(sample.left + 12.0f, sample.top + 19.0f,
                        sample.left + 52.0f, sample.top + 59.0f);
        const uint32_t iconColor =
            OverlayIconRgb(config, true, lightBackground);
        ui_.FillRound(iconBox, 9.0f, Rgb(iconColor));
        ui_.Icon(L"\uE945",
                 D2D1::RectF(iconBox.left, iconBox.top, iconBox.right,
                             iconBox.bottom),
                 20.0f, Rgb(ContrastRgb(iconColor)));

        ui_.Text(L"已插入电源",
                 D2D1::RectF(iconBox.right + 10.0f, sample.top + 12.0f,
                             sample.right - 12.0f, sample.top + 38.0f),
                 UiTextStyle::Heading, textColor);
        ui_.Text(L"当前电量 80%",
                 D2D1::RectF(iconBox.right + 10.0f, sample.top + 38.0f,
                             sample.right - 12.0f, sample.top + 62.0f),
                 UiTextStyle::Body, textColor);

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 200.0f, card.top + 140.0f,
                                   card.right - 20.0f, card.top + 172.0f),
                       L"显示预览", true)) {
            app_->PreviewOverlay();
        }
    }
}

void SettingsWindow::RenderSources(float& y) {
    Config& config = app_->MutableConfig();
    const UiTheme& theme = ui_.Theme();

    {
        D2D1_RECT_F card = Card(y, 424.0f);
        ui_.Text(L"监听方式", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                         card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);
        ui_.TextWrapped(
            L"默认使用 GUID_ACDC。修改后会立即重新注册监听。",
            D2D1::RectF(card.left + 20.0f, card.top + 40.0f,
                        card.right - 20.0f, card.top + 64.0f),
            UiTextStyle::Small, theme.textSecondary);

        struct ToggleRow {
            const wchar_t* title;
            bool* value;
        };
        ToggleRow rows[] = {
            {L"GUID_ACDC 回调", &config.useCallback},
            {L"PBT_APMPOWERSTATUSCHANGE 广播", &config.useBroadcast},
            {L"回调失败时使用窗口消息", &config.useWindowFallback},
            {L"电池百分比回调", &config.useBatteryPercent},
            {L"轮询（兜底）", &config.usePoll},
        };

        for (int i = 0; i < 5; ++i) {
            RowTitle(card, 76.0f + i * 48.0f, rows[i].title, L"");
            if (ui_.Toggle(NextId(),
                           D2D1::RectF(card.right - 80.0f,
                                       card.top + 84.0f + i * 48.0f,
                                       card.right - 20.0f,
                                       card.top + 116.0f + i * 48.0f),
                           *rows[i].value)) {
                dirty_ = true;
                powerDirty_ = true;
            }
        }

        ui_.Text(L"轮询间隔",
                 D2D1::RectF(card.left + 20.0f, card.top + 320.0f,
                             card.right - 200.0f, card.top + 352.0f),
                 UiTextStyle::Small, theme.textSecondary);
        if (ui_.Stepper(NextId(),
                        D2D1::RectF(card.right - 180.0f, card.top + 318.0f,
                                    card.right - 20.0f, card.top + 350.0f),
                        config.pollMs, 100, 100, 5000, L" ms")) {
            dirty_ = true;
            powerDirty_ = true;
        }

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.left + 20.0f, card.top + 366.0f,
                                   card.left + 170.0f, card.top + 398.0f),
                       L"全部启用（测试）")) {
            config.useCallback = true;
            config.useBroadcast = true;
            config.useWindowFallback = true;
            config.useBatteryPercent = true;
            config.usePoll = true;
            app_->ResetPowerStatistics();
            dirty_ = true;
            powerDirty_ = true;
        }

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.left + 180.0f, card.top + 366.0f,
                                   card.left + 300.0f, card.top + 398.0f),
                       L"采用最佳")) {
            const PowerStats stats = app_->PowerStatistics();
            int best = 0;
            uint64_t count = stats.callbackEvents;
            if (stats.windowEvents > count) {
                count = stats.windowEvents;
                best = 1;
            }
            if (stats.broadcastEvents > count) {
                count = stats.broadcastEvents;
                best = 2;
            }
            if (stats.pollEvents > count) {
                count = stats.pollEvents;
                best = 3;
            }

            config.useCallback = best == 0;
            config.useWindowFallback = best == 1;
            config.useBroadcast = best == 2;
            config.usePoll = best == 3;
            config.useBatteryPercent = true;
            app_->ResetPowerStatistics();
            dirty_ = true;
            powerDirty_ = true;
        }

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.left + 310.0f, card.top + 366.0f,
                                   card.left + 430.0f, card.top + 398.0f),
                       L"恢复默认")) {
            config.useCallback = true;
            config.useBroadcast = false;
            config.useWindowFallback = false;
            config.useBatteryPercent = true;
            config.usePoll = false;
            app_->ResetPowerStatistics();
            dirty_ = true;
            powerDirty_ = true;
        }
    }

    {
        D2D1_RECT_F card = Card(y, 250.0f);
        ui_.Text(L"诊断", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        const PowerStats stats = app_->PowerStatistics();
        std::wstring last = strings::Tr(L"暂无事件，插拔一次充电器试试");
        if (!stats.lastEvent.empty()) {
            const std::wstring eventName =
                stats.lastEvent == L"plug" ? strings::Tr(L"插入电源")
                : stats.lastEvent == L"unplug" ? strings::Tr(L"拔出电源")
                : stats.lastEvent == L"full" ? strings::Tr(L"电量充满")
                : stats.lastEvent == L"low" ? strings::Tr(L"电量较低")
                : stats.lastEvent == L"critical" ? strings::Tr(L"电量不足")
                : stats.lastEvent == L"abnormal" ? strings::Tr(L"充电异常")
                                                  : stats.lastEvent;
            last = eventName + L" · " +
                   strings::Tr(SourceLabel(stats.lastSource));
            if (stats.lastTick != 0) {
                const ULONGLONG seconds =
                    (GetTickCount64() - stats.lastTick) / 1000;
                last += L" · " + strings::Ago(static_cast<int64_t>(seconds));
            }
        }
        ui_.Text(L"最近事件", D2D1::RectF(card.left + 20.0f, card.top + 48.0f,
                                         card.right - 20.0f, card.top + 72.0f),
                 UiTextStyle::Small, theme.textSecondary);
        ui_.Text(last, D2D1::RectF(card.left + 20.0f, card.top + 72.0f,
                                   card.right - 20.0f, card.top + 98.0f),
                 UiTextStyle::Body, theme.text);

        ui_.Text(L"事件计数",
                 D2D1::RectF(card.left + 20.0f, card.top + 116.0f,
                             card.right - 20.0f, card.top + 140.0f),
                 UiTextStyle::Small, theme.textSecondary);
        const std::wstring counts =
            strings::Tr(L"回调 ") + std::to_wstring(stats.callbackEvents) +
            L"   " + strings::Tr(L"窗口 ") +
            std::to_wstring(stats.windowEvents) + L"   " +
            strings::Tr(L"广播 ") + std::to_wstring(stats.broadcastEvents) +
            L"   " + strings::Tr(L"轮询 ") +
            std::to_wstring(stats.pollEvents) + L"   " +
            strings::Tr(L"电池 ") + std::to_wstring(stats.batteryEvents);
        ui_.Text(counts,
                 D2D1::RectF(card.left + 20.0f, card.top + 140.0f,
                             card.right - 20.0f, card.top + 166.0f),
                 UiTextStyle::Body, theme.text);

        if (ui_.Button(NextId(),
                       D2D1::RectF(card.left + 20.0f, card.top + 188.0f,
                                   card.left + 140.0f, card.top + 220.0f),
                       L"重置统计")) {
            app_->ResetPowerStatistics();
            Invalidate();
        }
    }
}

void SettingsWindow::RenderAbout(float& y) {
    const UiTheme& theme = ui_.Theme();
    Config& config = app_->MutableConfig();

    {
        D2D1_RECT_F card = Card(y, 150.0f);
        ui_.Text(L"Windows 充电提示",
                 D2D1::RectF(card.left + 20.0f, card.top + 18.0f,
                             card.right - 20.0f, card.top + 46.0f),
                 UiTextStyle::Title, theme.text);
        ui_.Text(L"版本 0.1.0",
                 D2D1::RectF(card.left + 20.0f, card.top + 50.0f,
                             card.right - 20.0f, card.top + 74.0f),
                 UiTextStyle::Small, theme.textSecondary);

#if defined(_M_ARM64) || defined(__aarch64__)
        const std::wstring arch = L"ARM64 原生";
#else
        const std::wstring arch = L"x64";
#endif
        ui_.Text(strings::Tr(L"架构：") + strings::Tr(arch),
                 D2D1::RectF(card.left + 20.0f, card.top + 78.0f,
                             card.right - 20.0f, card.top + 102.0f),
                 UiTextStyle::Small, theme.textSecondary);
        ui_.Text(L"无管理员权限、无服务、无第三方运行时。",
                 D2D1::RectF(card.left + 20.0f, card.top + 104.0f,
                             card.right - 20.0f, card.top + 128.0f),
                 UiTextStyle::Small, theme.textSecondary);
    }

    {
        D2D1_RECT_F card = Card(y, 170.0f);
        ui_.Text(L"文件位置", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                         card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);

        ui_.Text(L"配置文件",
                 D2D1::RectF(card.left + 20.0f, card.top + 48.0f,
                             card.right - 220.0f, card.top + 72.0f),
                 UiTextStyle::Small, theme.textSecondary);
        ui_.Text(util::ConfigPath(),
                 D2D1::RectF(card.left + 20.0f, card.top + 70.0f,
                             card.right - 220.0f, card.top + 94.0f),
                 UiTextStyle::Small, theme.text);
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 180.0f, card.top + 52.0f,
                                   card.right - 20.0f, card.top + 84.0f),
                       L"打开配置文件")) {
            ShellExecuteW(nullptr, L"open", util::ConfigPath().c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
        }

        ui_.Text(L"日志文件",
                 D2D1::RectF(card.left + 20.0f, card.top + 100.0f,
                             card.right - 220.0f, card.top + 124.0f),
                 UiTextStyle::Small, theme.textSecondary);
        ui_.Text(logging::CurrentFile(),
                 D2D1::RectF(card.left + 20.0f, card.top + 122.0f,
                             card.right - 220.0f, card.top + 146.0f),
                 UiTextStyle::Small, theme.text);
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 180.0f, card.top + 104.0f,
                                   card.right - 20.0f, card.top + 136.0f),
                       L"打开日志文件夹")) {
            util::EnsureDirectory(util::LogDir());
            ShellExecuteW(nullptr, L"open", util::LogDir().c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
        }
    }

    {
        D2D1_RECT_F card = Card(y, 180.0f);
        ui_.Text(L"维护", D2D1::RectF(card.left + 20.0f, card.top + 14.0f,
                                     card.right - 20.0f, card.top + 38.0f),
                 UiTextStyle::Heading, theme.text);
        ui_.TextWrapped(L"导出会把当前日志和配置复制到桌面上的一个新文件夹。",
                        D2D1::RectF(card.left + 20.0f, card.top + 44.0f,
                                    card.right - 240.0f, card.top + 72.0f),
                        UiTextStyle::Small, theme.textSecondary);
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 220.0f, card.top + 44.0f,
                                   card.right - 20.0f, card.top + 76.0f),
                       L"导出日志", true)) {
            app_->ExportLogs();
        }

        ui_.TextWrapped(L"恢复默认会重置所有设置，包括阈值、声音和悬浮窗样式。",
                        D2D1::RectF(card.left + 20.0f, card.top + 96.0f,
                                    card.right - 240.0f, card.top + 124.0f),
                        UiTextStyle::Small, theme.textSecondary);
        if (ui_.Button(NextId(),
                       D2D1::RectF(card.right - 220.0f, card.top + 96.0f,
                                   card.right - 20.0f, card.top + 128.0f),
                       L"恢复默认设置")) {
            if (MessageBoxW(window_,
                            strings::Tr(L"确定要恢复所有默认设置吗？\n当前配置会被覆盖。").c_str(),
                            strings::Tr(L"Windows 充电提示").c_str(),
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                app_->ResetConfiguration();
            }
        }
    }

    (void)config;
}

bool SettingsWindow::MouseInContent(float x, float y) const {
    return x >= kSidebarWidth && x <= ui_.Width() - 14.0f &&
           y >= kContentTop && y <= ui_.Height() - 16.0f;
}

void SettingsWindow::CommitMuteEdit() {
    if (muteEditingField_ == 0)
        return;

    const int value = _wtoi(muteEditText_.c_str());
    const bool hasValue = !muteEditText_.empty();
    if (muteEditingField_ == 1 && hasValue)
        muteHours_ = std::clamp(value, 0, 99);
    else if (muteEditingField_ == 2 && hasValue)
        muteMinutesPart_ = std::clamp(value, 0, 59);

    muteEditingField_ = 0;
    Invalidate();
}

void SettingsWindow::CancelMuteEdit() {
    if (muteEditingField_ == 0)
        return;
    muteEditingField_ = 0;
    Invalidate();
}

bool SettingsWindow::PickSoundFile(std::wstring& path) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return false;
    }

    const std::wstring wavLabel = strings::Tr(L"WAV 音频");
    const std::wstring allLabel = strings::Tr(L"所有文件");
    const COMDLG_FILTERSPEC filters[] = {
        {wavLabel.c_str(), L"*.wav"},
        {allLabel.c_str(), L"*.*"},
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(strings::Tr(L"选择提示音").c_str());

    if (FAILED(dialog->Show(window_)))
        return false;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf())))
        return false;

    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)))
        return false;

    std::wstring selected = raw;
    CoTaskMemFree(raw);

    if (selected.size() < 4 ||
        _wcsicmp(selected.c_str() + selected.size() - 4, L".wav") != 0) {
        MessageBoxW(window_,
                    strings::Tr(L"当前版本只支持 WAV。\nMP3 / M4A / FLAC 的自动转换会在后续版本加入。").c_str(),
                    strings::Tr(L"Windows 充电提示").c_str(),
                    MB_OK | MB_ICONINFORMATION);
        return false;
    }

    path = selected;
    return true;
}

std::wstring SettingsWindow::ThemeName() const {
    return app_->GetConfig().theme;
}


