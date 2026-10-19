#pragma once
#include <windows.h>
#include <string>

namespace xfx {
struct ThemeColors;
// A standard owned modal dialog. Returns false on Cancel; value changes only on OK.
bool prompt_text(HWND owner, const std::wstring& title, const std::wstring& prompt,
                 bool multiline, std::wstring& value, HFONT font = nullptr, bool light_theme = false);
bool prompt_text_palette(HWND owner, const std::wstring& title, const std::wstring& prompt,
                 bool multiline, std::wstring& value, HFONT font, const ThemeColors& palette);
constexpr int kTextInputEdit = 2001;
}
