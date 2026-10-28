#include "hotkey.h"

#include <cwctype>
#include <sstream>

namespace {

std::wstring upper_trim(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    for (auto& ch : value) ch = static_cast<wchar_t>(std::towupper(ch));
    return value;
}

} // namespace

namespace xfx {

bool parse_hotkey(const std::wstring& text, HotkeySpec* spec) {
    if (!spec) return false;
    HotkeySpec parsed;
    std::wstring token;
    std::wistringstream input(text);
    while (std::getline(input, token, L'+')) {
        token = upper_trim(token);
        if (token.empty()) continue;

        if (token == L"CTRL" || token == L"CONTROL") {
            parsed.modifiers |= MOD_CONTROL;
        } else if (token == L"ALT") {
            parsed.modifiers |= MOD_ALT;
        } else if (token == L"SHIFT") {
            parsed.modifiers |= MOD_SHIFT;
        } else if (token == L"WIN" || token == L"WINDOWS") {
            parsed.modifiers |= MOD_WIN;
        } else if (token == L"SPACE") {
            parsed.virtual_key = VK_SPACE;
        } else if (token == L"ENTER" || token == L"RETURN") {
            parsed.virtual_key = VK_RETURN;
        } else if (token == L"TAB") {
            parsed.virtual_key = VK_TAB;
        } else if (token == L"ESC" || token == L"ESCAPE") {
            parsed.virtual_key = VK_ESCAPE;
        } else if (token.size() >= 2 && token[0] == L'F') {
            int function_number = 0;
            try {
                size_t parsed_length = 0;
                function_number = std::stoi(token.substr(1), &parsed_length);
                if (parsed_length != token.size() - 1) return false;
            } catch (...) {
                return false;
            }
            if (function_number < 1 || function_number > 24) return false;
            parsed.virtual_key = VK_F1 + function_number - 1;
        } else if (token.size() == 1 && std::iswalnum(token[0])) {
            parsed.virtual_key = static_cast<UINT>(std::towupper(token[0]));
        } else {
            return false;
        }
    }

    if (parsed.virtual_key == 0) return false;
    *spec = parsed;
    return true;
}

std::wstring format_hotkey(const HotkeySpec& spec) {
    std::wstring result;
    const auto append = [&result](const wchar_t* part) {
        if (!result.empty()) result += L"+";
        result += part;
    };
    if ((spec.modifiers & MOD_CONTROL) != 0) append(L"Ctrl");
    if ((spec.modifiers & MOD_ALT) != 0) append(L"Alt");
    if ((spec.modifiers & MOD_SHIFT) != 0) append(L"Shift");
    if ((spec.modifiers & MOD_WIN) != 0) append(L"Win");

    if (spec.virtual_key == VK_SPACE) append(L"Space");
    else if (spec.virtual_key == VK_RETURN) append(L"Enter");
    else if (spec.virtual_key == VK_TAB) append(L"Tab");
    else if (spec.virtual_key == VK_ESCAPE) append(L"Esc");
    else if (spec.virtual_key >= VK_F1 && spec.virtual_key <= VK_F24) {
        const std::wstring key = L"F" + std::to_wstring(spec.virtual_key - VK_F1 + 1);
        append(key.c_str());
    } else if ((spec.virtual_key >= 'A' && spec.virtual_key <= 'Z') ||
               (spec.virtual_key >= '0' && spec.virtual_key <= '9')) {
        wchar_t key[] = {static_cast<wchar_t>(spec.virtual_key), L'\0'};
        append(key);
    } else {
        const UINT scan_code = MapVirtualKeyW(spec.virtual_key, MAPVK_VK_TO_VSC);
        wchar_t key[64] = {};
        if (GetKeyNameTextW(static_cast<LONG>(scan_code << 16), key, 64) > 0) append(key);
    }
    return result;
}

bool GlobalHotkey::register_ctrl_space(HWND window, int id) {
    return register_hotkey(window, id, L"Ctrl+Space");
}

bool GlobalHotkey::register_hotkey(HWND window, int id, const std::wstring& text) {
    HotkeySpec spec;
    if (!parse_hotkey(text, &spec)) return false;

    const HWND previous_window = window_;
    const int previous_id = id_;
    const UINT previous_modifiers = modifiers_;
    const UINT previous_virtual_key = virtual_key_;
    if (previous_window && previous_id) UnregisterHotKey(previous_window, previous_id);

    if (!RegisterHotKey(window, id, spec.modifiers, spec.virtual_key)) {
        if (previous_window && previous_id && RegisterHotKey(
                previous_window, previous_id, previous_modifiers, previous_virtual_key)) {
            window_ = previous_window;
            id_ = previous_id;
            modifiers_ = previous_modifiers;
            virtual_key_ = previous_virtual_key;
        } else {
            window_ = nullptr;
            id_ = 0;
            modifiers_ = 0;
            virtual_key_ = 0;
        }
        return false;
    }
    window_ = window;
    id_ = id;
    modifiers_ = spec.modifiers;
    virtual_key_ = spec.virtual_key;
    return true;
}

void GlobalHotkey::unregister() {
    if (window_ && id_) UnregisterHotKey(window_, id_);
    window_ = nullptr;
    id_ = 0;
    modifiers_ = 0;
    virtual_key_ = 0;
}

} // namespace xfx
