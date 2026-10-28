#pragma once

#include <windows.h>
#include "settings.h"
#include <optional>

namespace xfx {

inline constexpr wchar_t kUiFontFamily[] = L"Microsoft YaHei UI";
inline constexpr int kUiBodyPixels = 12;
inline constexpr int kUiStatusPixels = 10;
inline constexpr int kUiSlotNumberPixels = 8;
inline constexpr int kUiIconPixels = 18;

// COLORREF values are ready for GDI. Palettes own no GDI/AE resources.
struct ThemeColors {
    COLORREF background;
    COLORREF surface;
    COLORREF input;
    COLORREF text;
    COLORREF muted;
    COLORREF selected;
    COLORREF selected_text;
    COLORREF divider;
    COLORREF button;
    COLORREF button_pressed;
    COLORREF icon;
    COLORREF effect_badge;
    COLORREF preset_badge;
    COLORREF disabled;
    COLORREF accent;
    COLORREF success;
    COLORREF error;
    COLORREF search;
    COLORREF hint;
    COLORREF quiet;
};

// false preserves the existing dark theme; returned palettes have static lifetime.
const ThemeColors& theme_colors(bool light_theme) noexcept;
bool is_light_background(COLORREF background) noexcept;
ThemeColors resolve_theme(ThemeMode mode, std::optional<COLORREF> host_background = {}) noexcept;

} // namespace xfx
