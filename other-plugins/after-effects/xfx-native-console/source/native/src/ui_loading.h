#pragma once
#include "ui_theme.h"
#include <gdiplus.h>
#include <thread>
#include <atomic>
#include <string>
#include <cmath>
#include <algorithm>

namespace xfx {
inline Gdiplus::Color loading_color(COLORREF c) { return {255, GetRValue(c), GetGValue(c), GetBValue(c)}; }

// Rendering has no AE dependencies. An unknown task never acquires a percentage.
inline void paint_loading(HDC dc, int width, int height, const ThemeColors& c,
    const std::wstring& text, ULONGLONG elapsed, float scale = 1, std::optional<double> progress = {}, bool reduced = false) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(loading_color(c.surface));
    g.ScaleTransform(scale, scale);
    const float w = width / scale, h = height / scale;
    Gdiplus::Pen track(loading_color(c.divider), 2), active(loading_color(c.accent), 2);
    active.SetStartCap(Gdiplus::LineCapRound); active.SetEndCap(Gdiplus::LineCapRound);
    g.DrawEllipse(&track, 9.0f, 8.0f, 12.0f, 12.0f);
    g.DrawArc(&active, 9.0f, 8.0f, 12.0f, 12.0f, reduced ? -90.0f : static_cast<float>(elapsed % 750) * 360 / 750 - 90, 100.0f);
    Gdiplus::Font font(kUiFontFamily, static_cast<Gdiplus::REAL>(kUiBodyPixels), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush ink(loading_color(c.text)), rail(loading_color(c.divider)), fill(loading_color(c.accent));
    const auto label = progress ? text + L" " + std::to_wstring(static_cast<int>((std::clamp)(*progress, 0.0, 1.0) * 100)) + L"%" : text;
    Gdiplus::RectF label_rect(29, 5, w-36, h-10); g.DrawString(label.c_str(), -1, &font, label_rect, nullptr, &ink);
    g.FillRectangle(&rail, 8.0f, h-5, w-16, 3.0f);
    g.SetClip(Gdiplus::RectF(8, h-5, w-16, 3));
    if (progress) g.FillRectangle(&fill, 8.0f, h-5, static_cast<float>((w-16) * (std::clamp)(*progress, 0.0, 1.0)), 3.0f);
    else {
        const float bar = (w-16) * .38f;
        const float x = reduced ? 8 : 8 - bar + static_cast<float>(elapsed % 1100) / 1100 * (w-16+bar);
        g.FillRectangle(&fill, x, h-5, bar, 3.0f);
    }
}

// Host render/script calls remain on AE's thread. This separate, noninteractive
// Win32 surface only animates a copied palette and label while that thread is busy.
class LoadingSurface {
    std::thread thread_;
    HANDLE ready_ = nullptr;
    std::atomic<HWND> window_{nullptr};
    ThemeColors colors_;
    std::wstring text_;
    RECT bounds_{};
    float scale_ = 1;
    bool reduced_ = false;
    ULONGLONG started_ = 0;
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<LoadingSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<LoadingSurface*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        if (msg == WM_TIMER) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
            PAINTSTRUCT ps{}; HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wp);
            RECT rect{}; GetClientRect(hwnd, &rect);
            HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
            const auto old = SelectObject(buffer, bitmap);
            paint_loading(buffer, rect.right, rect.bottom, self->colors_, self->text_, GetTickCount64()-self->started_, self->scale_, {}, self->reduced_);
            BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer);
            if (msg == WM_PAINT) EndPaint(hwnd, &ps); return 0;
        }
        if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        if (msg == WM_DESTROY) { KillTimer(hwnd, 1); PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
public:
    LoadingSurface(RECT bounds, const ThemeColors& colors, std::wstring text, float scale, bool reduced)
        : colors_(colors), text_(std::move(text)), bounds_(bounds), scale_(scale), reduced_(reduced) {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_) return;
        try {
            thread_ = std::thread([this] {
                WNDCLASSW klass{}; klass.lpfnWndProc = proc; klass.hInstance = GetModuleHandleW(nullptr);
                klass.lpszClassName = L"XFXLoadingSurface"; RegisterClassW(&klass);
                started_ = GetTickCount64();
                HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                    klass.lpszClassName, text_.c_str(), WS_POPUP, bounds_.left, bounds_.top,
                    bounds_.right-bounds_.left, bounds_.bottom-bounds_.top, nullptr, nullptr, klass.hInstance, this);
                window_.store(hwnd);
                if (hwnd) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); UpdateWindow(hwnd); SetTimer(hwnd, 1, 16, nullptr); }
                SetEvent(ready_);
                if (!hwnd) return;
                MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                window_.store(nullptr);
            });
            WaitForSingleObject(ready_, INFINITE); // Wait for creation only; never for the task or animation.
        } catch (...) { CloseHandle(ready_); ready_ = nullptr; throw; }
    }
    ~LoadingSurface() {
        if (auto hwnd = window_.load()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (thread_.joinable()) thread_.join();
        if (ready_) CloseHandle(ready_);
    }
    LoadingSurface(const LoadingSurface&) = delete;
    LoadingSurface& operator=(const LoadingSurface&) = delete;
    HWND window() const { return window_.load(); }
};
}
