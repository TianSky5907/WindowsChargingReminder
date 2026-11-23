#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

#include "../config.h"

class Overlay {
public:
    Overlay() = default;
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    bool EnsureCreated(HINSTANCE instance);
    void Show(const Config& config, const std::wstring& title,
              const std::wstring& body, int percent, bool charging);
    void Hide();

private:
    static LRESULT CALLBACK WndProcStatic(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void Render();
    void UpdateLayered();
    void EnsureBitmap(int width, int height);
    void CreateFormats();
    void PositionWindow(int width, int height);
    void OnTimer();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Config config_{};

    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;

    HBITMAP bitmap_ = nullptr;
    HDC memoryDc_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bitmapBits_ = nullptr;
    int bitmapWidth_ = 0;
    int bitmapHeight_ = 0;
    float dpi_ = 96.0f;

    std::wstring title_;
    std::wstring body_;
    int percent_ = 80;
    bool charging_ = true;

    UINT_PTR timer_ = 0;
    ULONGLONG stateStart_ = 0;
    double fadeAlpha_ = 0.0;
    int durationMs_ = 3000;
    int opacity_ = 95;
    int x_ = 0;
    int y_ = 0;
    int widthPx_ = 0;
    int heightPx_ = 0;
    bool visible_ = false;
};
