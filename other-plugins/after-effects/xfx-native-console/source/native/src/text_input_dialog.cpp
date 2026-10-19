#include "text_input_dialog.h"
#include "ui_theme.h"
#include "ui_shapes.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

namespace xfx {
namespace {
struct InputState {
    const std::wstring& title;
    const std::wstring& prompt;
    std::wstring value;
    bool multiline;
    HFONT font;
    HWND edit = nullptr;
    ThemeColors palette;
    HBRUSH background = nullptr, input = nullptr;
    struct Wave { POINT origin; ULONGLONG started; };
    std::map<HWND,std::vector<Wave>> waves;
    ~InputState() { if (background) DeleteObject(background); if (input) DeleteObject(input); }
};

void paint_button(HWND button, HDC dc, const InputState& state) {
    const int saved = SaveDC(dc); if (!saved) return;
    const auto& palette = state.palette;
    RECT rect{}; GetClientRect(button, &rect);
    const bool primary=(GetWindowLongW(button,GWL_STYLE)&BS_TYPEMASK)==BS_DEFPUSHBUTTON;
    SetDCBrushColor(dc,palette.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const float radius=3.0f*GetDpiForWindow(button)/96.0f;
    paint_round_rect(dc,rect,SendMessageW(button,BM_GETSTATE,0,0)&BST_PUSHED?palette.button_pressed:primary?palette.selected:palette.button,
        radius,GetFocus()==button?palette.accent:palette.divider);
    const auto found=state.waves.find(button);
    if(found!=state.waves.end() && IsWindowEnabled(button)) {
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath clip;rounded_path(clip,{1,1,static_cast<float>(rect.right-2),static_cast<float>(rect.bottom-2)},radius);graphics.SetClip(&clip);
        for(const auto& wave:found->second) {
            const double t=(std::min)(1.0,(GetTickCount64()-wave.started)/700.0);
            const float radius=static_cast<float>(std::hypot((std::max)(wave.origin.x,rect.right-wave.origin.x),
                (std::max)(wave.origin.y,rect.bottom-wave.origin.y))*(1-std::pow(1-t,3)));
            const auto color=primary?palette.selected_text:palette.accent;
            Gdiplus::SolidBrush ink(Gdiplus::Color(static_cast<BYTE>(85*(1-t)),GetRValue(color),GetGValue(color),GetBValue(color)));
            graphics.FillEllipse(&ink,wave.origin.x-radius,wave.origin.y-radius,radius*2,radius*2);
        }
    }
    if (state.font) SelectObject(dc, state.font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(button) ? primary?palette.selected_text:palette.text : palette.disabled);
    wchar_t label[128]{}; GetWindowTextW(button, label, 128);
    DrawTextW(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (GetFocus() == button) { InflateRect(&rect, -3, -3); DrawFocusRect(dc, &rect); }
    RestoreDC(dc, saved);
}
LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    auto* state = reinterpret_cast<InputState*>(data);
    if (message == WM_NCDESTROY) { KillTimer(hwnd,0xB006);state->waves.erase(hwnd);RemoveWindowSubclass(hwnd, button_proc, id); return DefSubclassProc(hwnd, message, wparam, lparam); }
    if (message==WM_ENABLE && !wparam) {KillTimer(hwnd,0xB006);state->waves.erase(hwnd);}
    if (IsWindowEnabled(hwnd) && (message==WM_LBUTTONDOWN || message==BM_CLICK ||
        (message==WM_KEYDOWN && (wparam==VK_SPACE || wparam==VK_RETURN) && !(lparam&(1L<<30))))) {
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
        if(animations) {
            RECT rect{};GetClientRect(hwnd,&rect);
            const POINT point=message==WM_LBUTTONDOWN?POINT{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}:POINT{rect.right/2,rect.bottom/2};
            auto& waves=state->waves[hwnd];if(waves.size()>=12)waves.erase(waves.begin());
            waves.push_back({point,GetTickCount64()});SetTimer(hwnd,0xB006,15,nullptr);InvalidateRect(hwnd,nullptr,FALSE);
        }
    }
    if(message==WM_TIMER && wparam==0xB006) {
        auto& waves=state->waves[hwnd];waves.erase(std::remove_if(waves.begin(),waves.end(),[](const auto& wave){return GetTickCount64()-wave.started>=700;}),waves.end());
        if(waves.empty())KillTimer(hwnd,0xB006);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint); paint_button(hwnd, dc, *state); EndPaint(hwnd, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { paint_button(hwnd, reinterpret_cast<HDC>(wparam), *state); return 0; }
    const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == BM_SETSTATE || message == WM_ENABLE)
        InvalidateRect(hwnd, nullptr, FALSE);
    return result;
}

LRESULT CALLBACK edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                           UINT_PTR id, DWORD_PTR data) {
    auto* state = reinterpret_cast<InputState*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, edit_proc, id);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    // DialogBox's default-button routing is not reliable when a host sends a
    // key message directly to the edit control. Keep Enter/Escape deterministic
    // for single-line prompts while preserving Enter in multiline editors.
    if (message == WM_KEYDOWN && state) {
        if (wparam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, IDCANCEL, 0);
            return 0;
        }
        if (wparam == VK_RETURN && !state->multiline) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, IDOK, 0);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

INT_PTR CALLBACK input_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
    auto* state = reinterpret_cast<InputState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<InputState*>(lparam);
        SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
        SetWindowTextW(hwnd, state->title.c_str());
        RECT client{}; GetClientRect(hwnd, &client);
        const int width = client.right, height = client.bottom;
        const auto control = [&](const wchar_t* klass, const wchar_t* text, DWORD style,
                                 int x, int y, int w, int h, int id, DWORD ex = 0) {
            HWND child = CreateWindowExW(ex, klass, text, WS_CHILD | WS_VISIBLE | style,
                x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->font ? state->font :
                GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return child;
        };
        control(L"STATIC", state->prompt.c_str(), 0, 14, 12, width - 28, 44, -1);
        state->edit = control(L"EDIT", state->value.c_str(), WS_TABSTOP | ES_AUTOVSCROLL |
            (state->multiline ? ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL : ES_AUTOHSCROLL),
            14, 60, width - 28, height - 116, kTextInputEdit, WS_EX_CLIENTEDGE);
        SendMessageW(state->edit, EM_SETLIMITTEXT, state->multiline ? 16000 : 256, 0);
        SetWindowTheme(state->edit, L"", L"");
        if (!SetWindowSubclass(state->edit, edit_proc, 1, reinterpret_cast<DWORD_PTR>(state))) {
            EndDialog(hwnd, -1); return TRUE;
        }
        HWND save = control(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
            width - 182, height - 42, 78, 28, IDOK);
        HWND cancel = control(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
            width - 92, height - 42, 78, 28, IDCANCEL);
        // Preserve standard dialog/button keyboard semantics; only paint is customized.
        for (HWND button : {save, cancel}) {
            if (!SetWindowSubclass(button, button_proc, 1, reinterpret_cast<DWORD_PTR>(state))) {
                EndDialog(hwnd, -1); return TRUE;
            }
        }
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        SetFocus(state->edit);
        return FALSE;
    }
    if (state && (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORBTN)) {
        const auto& palette = state->palette;
        HDC dc = reinterpret_cast<HDC>(wparam);
        const bool edit = message == WM_CTLCOLOREDIT;
        SetTextColor(dc, palette.text); SetBkColor(dc, edit ? palette.input : palette.background);
        return reinterpret_cast<INT_PTR>(edit ? state->input : state->background);
    }
    if (state && message == WM_ERASEBKGND) {
        RECT client{}; GetClientRect(hwnd, &client); FillRect(reinterpret_cast<HDC>(wparam), &client, state->background); return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK && state) {
        const int length = GetWindowTextLengthW(state->edit);
        std::wstring text(static_cast<size_t>(length + 1), L'\0');
        GetWindowTextW(state->edit, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        state->value = std::move(text);
        EndDialog(hwnd, IDOK);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
        EndDialog(hwnd, IDCANCEL); return TRUE;
    }
    return FALSE;
    } catch (...) { EndDialog(hwnd, -1); return TRUE; }
}
}

bool prompt_text(HWND owner, const std::wstring& title, const std::wstring& prompt,
                 bool multiline, std::wstring& value, HFONT font, bool light_theme) {
    return prompt_text_palette(owner,title,prompt,multiline,value,font,theme_colors(light_theme));
}
bool prompt_text_palette(HWND owner, const std::wstring& title, const std::wstring& prompt,
                 bool multiline, std::wstring& value, HFONT font, const ThemeColors& palette) {
    // No child templates: WM_INITDIALOG creates Unicode controls with a shared font.
    struct Template { DLGTEMPLATE dialog; WORD menu, klass, title; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    layout.dialog.cx = 300;
    layout.dialog.cy = multiline ? 168 : 106;
    InputState state{title, prompt, value, multiline, font, nullptr, palette};
    state.background = CreateSolidBrush(palette.background);
    state.input = CreateSolidBrush(palette.input);
    if (!state.background || !state.input) return false;
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, input_proc,
        reinterpret_cast<LPARAM>(&state)) != IDOK) return false;
    value = std::move(state.value);
    return true;
}
}
