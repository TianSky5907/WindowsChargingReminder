#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct UiTheme {
    bool dark = false;
    D2D1_COLOR_F window{};
    D2D1_COLOR_F sidebar{};
    D2D1_COLOR_F card{};
    D2D1_COLOR_F cardHover{};
    D2D1_COLOR_F border{};
    D2D1_COLOR_F text{};
    D2D1_COLOR_F textSecondary{};
    D2D1_COLOR_F accent{};
    D2D1_COLOR_F accentHover{};
    D2D1_COLOR_F onAccent{};
    D2D1_COLOR_F track{};
    D2D1_COLOR_F danger{};
    D2D1_COLOR_F overlay{};
};

UiTheme MakeLightTheme();
UiTheme MakeDarkTheme();

enum class UiTextStyle {
    Title,
    Heading,
    Body,
    Small,
    Value,
    Icon,
    IconSmall,
};

class Ui {
public:
    Ui() = default;
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool Create(HWND window);
    void Destroy();

    bool Resize(UINT pixelWidth, UINT pixelHeight, float dpi);
    bool Begin(const UiTheme& theme);
    bool End();

    float Width() const { return width_; }
    float Height() const { return height_; }
    float Dpi() const { return dpi_; }
    const UiTheme& Theme() const { return theme_; }

    void MouseMove(float x, float y);
    void MouseLeave();
    void MouseDown(float x, float y);
    void MouseUp(float x, float y);
    void MouseWheel(int delta);

    bool MouseDownNow() const { return mouseDown_; }
    bool MousePressed() const { return mousePressed_; }
    bool MouseReleased() const { return mouseReleased_; }
    float MouseX() const { return mouse_.x; }
    float MouseY() const { return mouse_.y; }
    bool MouseInside(float left, float top, float right, float bottom) const;

    void FillRect(D2D1_RECT_F rect, D2D1_COLOR_F color);
    void FillRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color);
    void StrokeRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color,
                     float width = 1.0f);
    void Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color,
              float width = 1.0f);

    void Text(const std::wstring& text, D2D1_RECT_F rect, UiTextStyle style,
              D2D1_COLOR_F color,
              DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    void TextWrapped(const std::wstring& text, D2D1_RECT_F rect,
                     UiTextStyle style, D2D1_COLOR_F color);
    void Icon(const std::wstring& glyph, D2D1_RECT_F rect, float size,
              D2D1_COLOR_F color);

    void PushClip(D2D1_RECT_F rect);
    void PopClip();

    bool Toggle(int id, D2D1_RECT_F rect, bool& value);
    bool Button(int id, D2D1_RECT_F rect, const std::wstring& text,
                bool primary = false);
    bool SidebarItem(int id, D2D1_RECT_F rect, const std::wstring& glyph,
                     const std::wstring& text, bool selected);
    bool Stepper(int id, D2D1_RECT_F rect, int& value, int step, int low,
                 int high, const std::wstring& suffix, int displayDivisor = 1);
    bool Slider(int id, D2D1_RECT_F rect, int& value, int low, int high,
                const std::wstring& suffix);
    bool Dropdown(int id, D2D1_RECT_F rect,
                  const std::vector<std::wstring>& items, int& index);
    bool Segmented(int id, D2D1_RECT_F rect,
                   const std::vector<std::wstring>& items, int& index);
    bool Swatch(int id, D2D1_RECT_F rect, uint32_t rgb, bool selected);

    void DrawOverlays();
    bool HasOpenOverlay() const { return openDropdown_ != nullptr; }
    void CloseDropdown();

private:
    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F color);
    IDWriteTextFormat* Format(UiTextStyle style, bool wrap) const;
    bool Click(int id, D2D1_RECT_F rect);
    void CreateFormats();
    void RecreateTarget();
    void ClearBrushes();

    struct DropdownState {
        int id = 0;
        D2D1_RECT_F anchor{};
        std::vector<std::wstring> items;
        int* index = nullptr;
        int hover = -1;
        bool changed = false;
        bool openedThisFrame = false;
    };

    HWND window_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> headingFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyWrapFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallWrapFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> valueFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconSmallFormat_;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis_;

    std::map<uint32_t, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> brushes_;
    UiTheme theme_{};

    UINT pixelWidth_ = 0;
    UINT pixelHeight_ = 0;
    float dpi_ = 96.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;

    D2D1_POINT_2F mouse_{};
    bool mouseDown_ = false;
    bool mousePressed_ = false;
    bool mouseReleased_ = false;
    int activeId_ = 0;

    int clipDepth_ = 0;
    DropdownState* openDropdown_ = nullptr;
    DropdownState dropdown_;
};
