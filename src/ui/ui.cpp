#include "ui.h"

#include "../strings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {

D2D1_COLOR_F Color(uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

uint32_t ColorKey(D2D1_COLOR_F color) {
    const auto channel = [](float value) {
        return static_cast<uint32_t>(std::lround(
                   std::clamp(value, 0.0f, 1.0f) * 255.0f)) &
               0xFFu;
    };
    return (channel(color.a) << 24) | (channel(color.r) << 16) |
           (channel(color.g) << 8) | channel(color.b);
}

bool Hit(D2D1_RECT_F rect, D2D1_POINT_2F point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

D2D1_RECT_F Inset(D2D1_RECT_F rect, float x, float y) {
    return D2D1::RectF(rect.left + x, rect.top + y, rect.right - x,
                       rect.bottom - y);
}

float CenterY(D2D1_RECT_F rect) {
    return (rect.top + rect.bottom) * 0.5f;
}

}  // namespace

UiTheme MakeLightTheme() {
    UiTheme theme;
    theme.dark = false;
    theme.window = Color(0xF3F3F3);
    theme.sidebar = Color(0xEBEBEB);
    theme.card = Color(0xFFFFFF);
    theme.cardHover = Color(0xF7F7F7);
    theme.border = Color(0xDCDCDC);
    theme.text = Color(0x1A1A1A);
    theme.textSecondary = Color(0x6B6B6B);
    theme.accent = Color(0x0067C0);
    theme.accentHover = Color(0x005A9E);
    theme.onAccent = Color(0xFFFFFF);
    theme.track = Color(0xD5D5D5);
    theme.danger = Color(0xC42B1C);
    theme.overlay = Color(0x000000, 0.35f);
    return theme;
}

UiTheme MakeDarkTheme() {
    UiTheme theme;
    theme.dark = true;
    theme.window = Color(0x202020);
    theme.sidebar = Color(0x1A1A1A);
    theme.card = Color(0x2B2B2B);
    theme.cardHover = Color(0x323232);
    theme.border = Color(0x3A3A3A);
    theme.text = Color(0xF5F5F5);
    theme.textSecondary = Color(0xA0A0A0);
    theme.accent = Color(0x4CC2FF);
    theme.accentHover = Color(0x3AAADF);
    theme.onAccent = Color(0x062B3A);
    theme.track = Color(0x4A4A4A);
    theme.danger = Color(0xFF6B5E);
    theme.overlay = Color(0x000000, 0.45f);
    return theme;
}

Ui::~Ui() {
    Destroy();
}

bool Ui::Create(HWND window) {
    window_ = window;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   factory_.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(write_.GetAddressOf()));
    if (FAILED(hr))
        return false;

    CreateFormats();
    return titleFormat_ != nullptr && bodyFormat_ != nullptr;
}

void Ui::Destroy() {
    ClearBrushes();
    target_.Reset();
    ellipsis_.Reset();
    titleFormat_.Reset();
    headingFormat_.Reset();
    bodyFormat_.Reset();
    bodyWrapFormat_.Reset();
    smallFormat_.Reset();
    smallWrapFormat_.Reset();
    valueFormat_.Reset();
    iconFormat_.Reset();
    iconSmallFormat_.Reset();
    write_.Reset();
    factory_.Reset();
    window_ = nullptr;
}

void Ui::CreateFormats() {
    auto create = [this](const wchar_t* family, DWRITE_FONT_WEIGHT weight,
                         float size, ComPtr<IDWriteTextFormat>& format,
                         bool wrap) {
        if (write_ == nullptr)
            return;
        if (FAILED(write_->CreateTextFormat(
                family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN",
                format.GetAddressOf()))) {
            return;
        }
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                                     : DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    const wchar_t* family = L"Microsoft YaHei UI";
    create(family, DWRITE_FONT_WEIGHT_SEMI_BOLD, 20.0f, titleFormat_, false);
    create(family, DWRITE_FONT_WEIGHT_SEMI_BOLD, 15.0f, headingFormat_, false);
    create(family, DWRITE_FONT_WEIGHT_NORMAL, 13.0f, bodyFormat_, false);
    create(family, DWRITE_FONT_WEIGHT_NORMAL, 13.0f, bodyWrapFormat_, true);
    create(family, DWRITE_FONT_WEIGHT_NORMAL, 12.0f, smallFormat_, false);
    create(family, DWRITE_FONT_WEIGHT_NORMAL, 12.0f, smallWrapFormat_, true);
    create(family, DWRITE_FONT_WEIGHT_NORMAL, 13.0f, valueFormat_, false);
    create(L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 18.0f,
           iconFormat_, false);
    create(L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 14.0f,
           iconSmallFormat_, false);

    if (bodyFormat_ != nullptr && write_ != nullptr) {
        write_->CreateEllipsisTrimmingSign(bodyFormat_.Get(),
                                            ellipsis_.GetAddressOf());
        if (ellipsis_ != nullptr) {
            const DWRITE_TRIMMING trimming{
                DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            bodyFormat_->SetTrimming(&trimming, ellipsis_.Get());
            smallFormat_->SetTrimming(&trimming, ellipsis_.Get());
            valueFormat_->SetTrimming(&trimming, ellipsis_.Get());
        }
    }
}

bool Ui::Resize(UINT pixelWidth, UINT pixelHeight, float dpi) {
    pixelWidth_ = std::max<UINT>(pixelWidth, 1);
    pixelHeight_ = std::max<UINT>(pixelHeight, 1);
    dpi_ = dpi <= 1.0f ? 96.0f : dpi;
    width_ = static_cast<float>(pixelWidth_) * 96.0f / dpi_;
    height_ = static_cast<float>(pixelHeight_) * 96.0f / dpi_;

    if (target_ == nullptr) {
        RecreateTarget();
    } else {
        target_->Resize(D2D1::SizeU(pixelWidth_, pixelHeight_));
        target_->SetDpi(dpi_, dpi_);
    }

    return target_ != nullptr;
}

void Ui::RecreateTarget() {
    ClearBrushes();
    target_.Reset();

    if (factory_ == nullptr || window_ == nullptr)
        return;

    const D2D1_RENDER_TARGET_PROPERTIES properties =
        D2D1::RenderTargetProperties();
    const D2D1_HWND_RENDER_TARGET_PROPERTIES windowProperties =
        D2D1::HwndRenderTargetProperties(
            window_, D2D1::SizeU(pixelWidth_, pixelHeight_),
            D2D1_PRESENT_OPTIONS_NONE);

    if (SUCCEEDED(factory_->CreateHwndRenderTarget(
            properties, windowProperties, target_.GetAddressOf()))) {
        target_->SetDpi(dpi_, dpi_);
    }
}

void Ui::ClearBrushes() {
    brushes_.clear();
}

ID2D1SolidColorBrush* Ui::Brush(D2D1_COLOR_F color) {
    if (target_ == nullptr)
        return nullptr;

    const uint32_t key = ColorKey(color);
    auto existing = brushes_.find(key);
    if (existing != brushes_.end())
        return existing->second.Get();

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target_->CreateSolidColorBrush(color, brush.GetAddressOf())))
        return nullptr;

    ID2D1SolidColorBrush* raw = brush.Get();
    brushes_.emplace(key, std::move(brush));
    return raw;
}

bool Ui::Begin(const UiTheme& theme) {
    theme_ = theme;
    if (target_ == nullptr)
        return false;

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(theme_.window);
    clipDepth_ = 0;
    return true;
}

bool Ui::End() {
    if (target_ == nullptr)
        return false;

    const HRESULT result = target_->EndDraw();
    mousePressed_ = false;
    mouseReleased_ = false;

    if (result == D2DERR_RECREATE_TARGET) {
        RecreateTarget();
        return false;
    }

    return SUCCEEDED(result);
}

void Ui::MouseMove(float x, float y) {
    mouse_ = D2D1::Point2F(x, y);
}

void Ui::MouseLeave() {
    mouse_ = D2D1::Point2F(-10000.0f, -10000.0f);
}

void Ui::MouseDown(float x, float y) {
    mouse_ = D2D1::Point2F(x, y);
    mouseDown_ = true;
    mousePressed_ = true;
}

void Ui::MouseUp(float x, float y) {
    mouse_ = D2D1::Point2F(x, y);
    mouseDown_ = false;
    mouseReleased_ = true;
}

void Ui::MouseWheel(int /*delta*/) {
}

bool Ui::MouseInside(float left, float top, float right, float bottom) const {
    return Hit(D2D1::RectF(left, top, right, bottom), mouse_);
}

bool Ui::Click(int id, D2D1_RECT_F rect) {
    if (openDropdown_ != nullptr)
        return false;

    const bool hover = Hit(rect, mouse_);
    if (mousePressed_ && hover && activeId_ == 0)
        activeId_ = id;

    const bool clicked = mouseReleased_ && activeId_ == id && hover;
    if (mouseReleased_ && activeId_ == id)
        activeId_ = 0;

    return clicked;
}

void Ui::FillRect(D2D1_RECT_F rect, D2D1_COLOR_F color) {
    if (target_ != nullptr)
        target_->FillRectangle(rect, Brush(color));
}

void Ui::FillRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) {
    if (target_ == nullptr)
        return;
    radius = std::max(0.0f, std::min(radius,
                                     std::min(rect.right - rect.left,
                                              rect.bottom - rect.top) *
                                         0.5f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius),
                                  Brush(color));
}

void Ui::StrokeRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color,
                     float width) {
    if (target_ == nullptr)
        return;
    radius = std::max(0.0f, std::min(radius,
                                     std::min(rect.right - rect.left,
                                              rect.bottom - rect.top) *
                                         0.5f));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius),
                                  Brush(color), width);
}

void Ui::Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color,
              float width) {
    if (target_ != nullptr)
        target_->DrawLine(from, to, Brush(color), width);
}

IDWriteTextFormat* Ui::Format(UiTextStyle style, bool wrap) const {
    switch (style) {
        case UiTextStyle::Title: return titleFormat_.Get();
        case UiTextStyle::Heading: return headingFormat_.Get();
        case UiTextStyle::Body: return wrap ? bodyWrapFormat_.Get() : bodyFormat_.Get();
        case UiTextStyle::Small: return wrap ? smallWrapFormat_.Get() : smallFormat_.Get();
        case UiTextStyle::Value: return valueFormat_.Get();
        case UiTextStyle::Icon: return iconFormat_.Get();
        case UiTextStyle::IconSmall: return iconSmallFormat_.Get();
        default: return bodyFormat_.Get();
    }
}

void Ui::Text(const std::wstring& text, D2D1_RECT_F rect, UiTextStyle style,
              D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT alignment) {
    if (target_ == nullptr || text.empty())
        return;

    const std::wstring localized = strings::Tr(text);
    IDWriteTextFormat* format = Format(style, false);
    if (format == nullptr)
        return;

    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    target_->DrawTextW(localized.c_str(), static_cast<UINT32>(localized.size()),
                       format, rect, Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Ui::TextWrapped(const std::wstring& text, D2D1_RECT_F rect,
                     UiTextStyle style, D2D1_COLOR_F color) {
    if (target_ == nullptr || text.empty())
        return;

    const std::wstring localized = strings::Tr(text);
    IDWriteTextFormat* format = Format(style, true);
    if (format == nullptr)
        return;

    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    target_->DrawTextW(localized.c_str(), static_cast<UINT32>(localized.size()),
                       format, rect, Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Ui::Icon(const std::wstring& glyph, D2D1_RECT_F rect, float size,
              D2D1_COLOR_F color) {
    if (target_ == nullptr || glyph.empty())
        return;

    IDWriteTextFormat* format = size <= 15.0f ? iconSmallFormat_.Get()
                                              : iconFormat_.Get();
    if (format == nullptr)
        return;

    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    target_->DrawTextW(glyph.c_str(), static_cast<UINT32>(glyph.size()), format,
                       rect, Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Ui::PushClip(D2D1_RECT_F rect) {
    if (target_ != nullptr) {
        target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++clipDepth_;
    }
}

void Ui::PopClip() {
    if (target_ != nullptr && clipDepth_ > 0) {
        target_->PopAxisAlignedClip();
        --clipDepth_;
    }
}

bool Ui::Toggle(int id, D2D1_RECT_F rect, bool& value) {
    const bool changed = Click(id, rect);
    if (changed)
        value = !value;

    const float trackWidth = 42.0f;
    const float trackHeight = 24.0f;
    const D2D1_RECT_F track = D2D1::RectF(
        rect.right - trackWidth, CenterY(rect) - trackHeight * 0.5f,
        rect.right, CenterY(rect) + trackHeight * 0.5f);
    const bool hover = Hit(track, mouse_);

    FillRound(track, trackHeight * 0.5f,
              value ? theme_.accent
                    : (hover ? theme_.cardHover : theme_.track));
    if (!value)
        StrokeRound(track, trackHeight * 0.5f, theme_.border);

    const float knobRadius = (trackHeight - 6.0f) * 0.5f;
    const float knobX = value ? track.right - 3.0f - knobRadius
                              : track.left + 3.0f + knobRadius;
    target_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(knobX, CenterY(track)), knobRadius,
                      knobRadius),
        Brush(value ? theme_.onAccent : theme_.textSecondary));

    return changed;
}

bool Ui::Button(int id, D2D1_RECT_F rect, const std::wstring& text,
                bool primary) {
    const bool clicked = Click(id, rect);
    const bool hover = Hit(rect, mouse_);
    const bool pressed = activeId_ == id && mouseDown_;

    D2D1_COLOR_F background = theme_.cardHover;
    D2D1_COLOR_F foreground = theme_.text;
    if (primary) {
        background = hover ? theme_.accentHover : theme_.accent;
        foreground = theme_.onAccent;
    } else if (pressed) {
        background = theme_.track;
    } else if (!hover) {
        background = theme_.card;
    }

    FillRound(rect, 7.0f, background);
    if (!primary)
        StrokeRound(rect, 7.0f, theme_.border);

    Text(text, Inset(rect, 10.0f, 0.0f), UiTextStyle::Body, foreground,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    return clicked;
}

bool Ui::SidebarItem(int id, D2D1_RECT_F rect, const std::wstring& glyph,
                     const std::wstring& text, bool selected) {
    const bool clicked = Click(id, rect);
    const bool hover = Hit(rect, mouse_);

    if (selected) {
        const D2D1_COLOR_F background =
            D2D1::ColorF(theme_.accent.r, theme_.accent.g, theme_.accent.b, 0.14f);
        FillRound(rect, 8.0f, background);
    } else if (hover) {
        FillRound(rect, 8.0f, theme_.cardHover);
    }

    Icon(glyph,
         D2D1::RectF(rect.left + 10.0f, rect.top, rect.left + 40.0f,
                     rect.bottom),
         16.0f, selected ? theme_.accent : theme_.textSecondary);
    Text(text,
         D2D1::RectF(rect.left + 46.0f, rect.top, rect.right - 10.0f,
                     rect.bottom),
         UiTextStyle::Body, selected ? theme_.accent : theme_.text);
    return clicked;
}

bool Ui::Stepper(int id, D2D1_RECT_F rect, int& value, int step, int low,
                 int high, const std::wstring& suffix, int displayDivisor) {
    bool changed = false;
    rect.bottom = rect.top + 32.0f;

    const float buttonWidth = 32.0f;
    const D2D1_RECT_F minusRect =
        D2D1::RectF(rect.left, rect.top, rect.left + buttonWidth, rect.bottom);
    const D2D1_RECT_F plusRect =
        D2D1::RectF(rect.right - buttonWidth, rect.top, rect.right, rect.bottom);
    const D2D1_RECT_F valueRect = D2D1::RectF(
        minusRect.right, rect.top, plusRect.left, rect.bottom);

    FillRound(rect, 7.0f, theme_.card);
    StrokeRound(rect, 7.0f, theme_.border);

    if (Click(id, minusRect) && value > low) {
        value = std::max(low, value - step);
        changed = true;
    }
    if (Click(id + 1, plusRect) && value < high) {
        value = std::min(high, value + step);
        changed = true;
    }

    Text(L"−", minusRect, UiTextStyle::Body,
         value > low ? theme_.text : theme_.textSecondary,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"+", plusRect, UiTextStyle::Body,
         value < high ? theme_.text : theme_.textSecondary,
         DWRITE_TEXT_ALIGNMENT_CENTER);

    std::wstring valueText;
    if (displayDivisor > 1) {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%.1f", static_cast<double>(value) /
                                         static_cast<double>(displayDivisor));
        valueText = buffer;
    } else {
        valueText = std::to_wstring(value);
    }
    valueText += suffix;
    Text(valueText, valueRect, UiTextStyle::Value, theme_.text,
         DWRITE_TEXT_ALIGNMENT_CENTER);

    return changed;
}

bool Ui::Slider(int id, D2D1_RECT_F rect, int& value, int low, int high,
                const std::wstring& suffix) {
    if (high <= low)
        return false;

    const float valueWidth = 48.0f;
    const D2D1_RECT_F trackRect =
        D2D1::RectF(rect.left, CenterY(rect) - 2.0f,
                    rect.right - valueWidth, CenterY(rect) + 2.0f);

    bool changed = false;
    const bool hover = Hit(D2D1::RectF(rect.left, rect.top, rect.right,
                                       rect.bottom),
                           mouse_);

    if (mousePressed_ && hover && openDropdown_ == nullptr && activeId_ == 0)
        activeId_ = id;

    if (mouseDown_ && activeId_ == id) {
        const float t = std::clamp(
            (mouse_.x - trackRect.left) / (trackRect.right - trackRect.left),
            0.0f, 1.0f);
        const int next =
            std::clamp(low + static_cast<int>(std::lround(t * (high - low))),
                       low, high);
        if (next != value) {
            value = next;
            changed = true;
        }
    }

    if (mouseReleased_ && activeId_ == id)
        activeId_ = 0;

    FillRound(trackRect, 2.0f, theme_.track);
    const float t = static_cast<float>(value - low) / static_cast<float>(high - low);
    const D2D1_RECT_F filled = D2D1::RectF(
        trackRect.left, trackRect.top,
        trackRect.left + (trackRect.right - trackRect.left) * t,
        trackRect.bottom);
    FillRound(filled, 2.0f, theme_.accent);

    const float knobX = filled.right;
    target_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(knobX, CenterY(trackRect)), 7.0f, 7.0f),
        Brush(theme_.accent));

    Text(std::to_wstring(value) + suffix,
         D2D1::RectF(rect.right - valueWidth + 8.0f, rect.top, rect.right,
                     rect.bottom),
         UiTextStyle::Value, theme_.textSecondary,
         DWRITE_TEXT_ALIGNMENT_TRAILING);

    return changed;
}

bool Ui::Dropdown(int id, D2D1_RECT_F rect,
                  const std::vector<std::wstring>& items, int& index) {
    if (items.empty())
        return false;

    if (openDropdown_ != nullptr && openDropdown_->id != id) {
        // Another dropdown is open; keep the page blocked.
        return false;
    }

    const bool isOpen = openDropdown_ != nullptr && openDropdown_->id == id;
    if (!isOpen && Click(id, rect)) {
        dropdown_.id = id;
        dropdown_.anchor = rect;
        dropdown_.items = items;
        dropdown_.index = &index;
        dropdown_.hover = -1;
        dropdown_.changed = false;
        dropdown_.openedThisFrame = true;
        openDropdown_ = &dropdown_;
    }

    if (isOpen) {
        dropdown_.anchor = rect;
        dropdown_.items = items;
        dropdown_.index = &index;
    }

    const bool hover = Hit(rect, mouse_);
    FillRound(rect, 7.0f, hover && !isOpen ? theme_.cardHover : theme_.card);
    StrokeRound(rect, 7.0f, isOpen ? theme_.accent : theme_.border);
    Text(items[std::clamp(index, 0, static_cast<int>(items.size()) - 1)],
         D2D1::RectF(rect.left + 12.0f, rect.top, rect.right - 32.0f,
                     rect.bottom),
         UiTextStyle::Body, theme_.text);
    Icon(L"\uE70D",
         D2D1::RectF(rect.right - 28.0f, rect.top, rect.right - 8.0f,
                     rect.bottom),
         12.0f, theme_.textSecondary);
    return false;
}

bool Ui::Segmented(int id, D2D1_RECT_F rect,
                   const std::vector<std::wstring>& items, int& index) {
    if (items.empty())
        return false;

    FillRound(rect, 7.0f, theme_.card);
    StrokeRound(rect, 7.0f, theme_.border);

    const float itemWidth =
        (rect.right - rect.left) / static_cast<float>(items.size());
    bool changed = false;

    for (size_t i = 0; i < items.size(); ++i) {
        const D2D1_RECT_F itemRect =
            D2D1::RectF(rect.left + itemWidth * static_cast<float>(i) + 2.0f,
                        rect.top + 2.0f,
                        rect.left + itemWidth * static_cast<float>(i + 1) - 2.0f,
                        rect.bottom - 2.0f);
        if (Click(id + static_cast<int>(i), itemRect) &&
            index != static_cast<int>(i)) {
            index = static_cast<int>(i);
            changed = true;
        }

        if (index == static_cast<int>(i))
            FillRound(itemRect, 5.0f, theme_.accent);

        Text(items[i], itemRect, UiTextStyle::Body,
             index == static_cast<int>(i) ? theme_.onAccent : theme_.textSecondary,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    return changed;
}

bool Ui::Swatch(int id, D2D1_RECT_F rect, uint32_t rgb, bool selected) {
    const bool clicked = Click(id, rect);
    const float radius =
        std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.5f;

    if (selected) {
        target_->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F((rect.left + rect.right) * 0.5f,
                                        (rect.top + rect.bottom) * 0.5f),
                          radius + 3.0f, radius + 3.0f),
            Brush(theme_.accent), 2.0f);
    }

    target_->FillEllipse(
        D2D1::Ellipse(
            D2D1::Point2F((rect.left + rect.right) * 0.5f,
                          (rect.top + rect.bottom) * 0.5f),
            radius, radius),
        Brush(Color(rgb)));

    return clicked;
}

void Ui::DrawOverlays() {
    if (openDropdown_ == nullptr || target_ == nullptr)
        return;

    DropdownState& dropdown = *openDropdown_;
    const float itemHeight = 34.0f;
    const float padding = 6.0f;
    const float width = std::max(160.0f,
                                 dropdown.anchor.right - dropdown.anchor.left);
    const float height =
        itemHeight * static_cast<float>(dropdown.items.size()) + padding * 2.0f;
    float left = dropdown.anchor.left;
    float top = dropdown.anchor.bottom + 4.0f;
    if (top + height > height_ - 8.0f)
        top = std::max(8.0f, dropdown.anchor.top - height - 4.0f);
    left = std::min(left, width_ - width - 8.0f);
    left = std::max(8.0f, left);

    const D2D1_RECT_F popup =
        D2D1::RectF(left, top, left + width, top + height);

    FillRound(D2D1::RectF(popup.left + 2.0f, popup.top + 4.0f,
                          popup.right + 2.0f, popup.bottom + 4.0f),
              9.0f, Color(0x000000, 0.18f));
    FillRound(popup, 9.0f, theme_.card);
    StrokeRound(popup, 9.0f, theme_.border);

    dropdown.hover = -1;
    for (size_t i = 0; i < dropdown.items.size(); ++i) {
        const D2D1_RECT_F itemRect =
            D2D1::RectF(popup.left + padding, popup.top + padding +
                                                    itemHeight * static_cast<float>(i),
                        popup.right - padding,
                        popup.top + padding +
                            itemHeight * static_cast<float>(i + 1));
        if (Hit(itemRect, mouse_))
            dropdown.hover = static_cast<int>(i);

        const bool selected = dropdown.index != nullptr &&
                              *dropdown.index == static_cast<int>(i);
        if (dropdown.hover == static_cast<int>(i))
            FillRound(itemRect, 6.0f, theme_.cardHover);

        Text(dropdown.items[i],
             D2D1::RectF(itemRect.left + 8.0f, itemRect.top,
                         itemRect.right - 28.0f, itemRect.bottom),
             UiTextStyle::Body, selected ? theme_.accent : theme_.text);
        if (selected)
            Icon(L"\uE73E", D2D1::RectF(itemRect.right - 24.0f, itemRect.top,
                                        itemRect.right, itemRect.bottom),
                 12.0f, theme_.accent);
    }

    if (mouseReleased_) {
        if (dropdown.openedThisFrame) {
            dropdown.openedThisFrame = false;
        } else if (Hit(dropdown.anchor, mouse_)) {
            CloseDropdown();
        } else if (Hit(popup, mouse_)) {
            const int index = static_cast<int>(
                (mouse_.y - popup.top - padding) / itemHeight);
            if (index >= 0 && index < static_cast<int>(dropdown.items.size()) &&
                dropdown.index != nullptr) {
                *dropdown.index = index;
                dropdown.changed = true;
            }
            CloseDropdown();
        } else {
            CloseDropdown();
        }
    }
}

void Ui::CloseDropdown() {
    openDropdown_ = nullptr;
    dropdown_ = DropdownState{};
}
