#pragma once

#include <windows.h>
#include <string>

namespace xfx {

struct HotkeySpec {
    UINT modifiers = 0;
    UINT virtual_key = 0;
};

bool parse_hotkey(const std::wstring& text, HotkeySpec* spec);
std::wstring format_hotkey(const HotkeySpec& spec);

class GlobalHotkey {
public:
    bool register_ctrl_space(HWND window, int id);
    bool register_hotkey(HWND window, int id, const std::wstring& text);
    void unregister();

private:
    HWND window_ = nullptr;
    int id_ = 0;
    UINT modifiers_ = 0;
    UINT virtual_key_ = 0;
};

} // namespace xfx
