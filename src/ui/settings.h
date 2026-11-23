#pragma once

#include <windows.h>

#include <string>

#include "ui.h"

class App;

class SettingsWindow {
public:
    explicit SettingsWindow(App* app);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool EnsureCreated(HINSTANCE instance, HWND owner);
    void Show();
    void Hide();
    bool IsVisible() const;
    void Invalidate();
    void OnConfigReloaded();
    void ShowPage(int page);
    void ShowMuteSection();

private:
    static LRESULT CALLBACK WndProcStatic(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void UpdateWindowTheme();
    void CenterOnOwner();
    void Render();
    void RenderSidebar();
    void RenderHeader();
    void RenderPage(float& y);
    void RenderGeneral(float& y);
    void RenderSound(float& y);
    void RenderNotification(float& y);
    void RenderOverlay(float& y);
    void RenderSources(float& y);
    void RenderAbout(float& y);

    D2D1_RECT_F Card(float& y, float height);
    void RowTitle(D2D1_RECT_F card, float top, const std::wstring& title,
                  const std::wstring& description);
    int NextId();

    bool MouseInContent(float x, float y) const;
    bool PickSoundFile(std::wstring& path);
    std::wstring ThemeName() const;
    void CommitMuteEdit();
    void CancelMuteEdit();

    App* app_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    Ui ui_;
    float dpi_ = 96.0f;
    float scrollY_ = 0.0f;
    float contentHeight_ = 0.0f;
    float scrollTrackTop_ = 0.0f;
    float scrollTrackHeight_ = 0.0f;
    float scrollThumbTop_ = 0.0f;
    float scrollThumbHeight_ = 0.0f;
    float scrollMax_ = 0.0f;
    float scrollDragOffset_ = 0.0f;
    bool scrollDragging_ = false;
    int page_ = 0;
    int nextId_ = 0;
    int muteHours_ = 1;
    int muteMinutesPart_ = 0;
    int muteEditingField_ = 0;
    std::wstring muteEditText_;
    D2D1_RECT_F muteFieldRects_[3]{};
    int overlayPositionIndex_ = 6;
    int windowButtonPressed_ = 0;
    bool trackingLeave_ = false;
    bool touchActive_ = false;
    bool touchCanScroll_ = false;
    bool touchMoved_ = false;
    bool touchScrollbar_ = false;
    float touchThumbOffset_ = 0.0f;
    bool gestureActive_ = false;
    bool gestureOnScrollbar_ = false;
    float gestureStartY_ = 0.0f;
    float gestureStartScroll_ = 0.0f;
    float gestureThumbOffset_ = 0.0f;
    DWORD touchId_ = 0;
    float touchStartY_ = 0.0f;
    float touchStartScroll_ = 0.0f;
    int touchLastX_ = 0;
    int touchLastY_ = 0;
    bool dirty_ = false;
    bool powerDirty_ = false;
    bool initialPosition_ = false;
};
