#include "themed_menu.h"
#include <commctrl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Row { HMENU menu; UINT position; UINT type, state, id; ULONG_PTR data; HMENU submenu; std::wstring text; };
struct Snapshot { std::vector<Row> rows; std::vector<std::pair<HMENU, HBRUSH>> brushes; };
static MENUITEMINFOW item_info(HMENU menu, UINT position) {
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_SUBMENU;
    require(GetMenuItemInfoW(menu, position, TRUE, &item) != FALSE, "GetMenuItemInfo failed");
    return item;
}
static std::wstring item_text(HMENU menu, UINT position) {
    MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_STRING;
    require(GetMenuItemInfoW(menu, position, TRUE, &item) != FALSE, "text length lookup failed");
    std::wstring text(static_cast<size_t>(item.cch) + 1, L'\0');
    item.dwTypeData = text.data(); ++item.cch;
    require(GetMenuItemInfoW(menu, position, TRUE, &item) != FALSE, "text lookup failed");
    text.resize(item.cch); return text;
}
static void snapshot_menu(HMENU menu, Snapshot& result) {
    MENUINFO info{sizeof(info)}; info.fMask = MIM_BACKGROUND;
    require(GetMenuInfo(menu, &info) != FALSE, "GetMenuInfo failed");
    result.brushes.push_back({menu, info.hbrBack});
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        const auto item = item_info(menu, static_cast<UINT>(i));
        result.rows.push_back({menu, static_cast<UINT>(i), item.fType, item.fState, item.wID, item.dwItemData, item.hSubMenu,
                               item_text(menu, static_cast<UINT>(i))});
        if (item.hSubMenu) snapshot_menu(item.hSubMenu, result);
    }
}
static void assert_restored(const Snapshot& before) {
    for (const auto& row : before.rows) {
        const auto now = item_info(row.menu, row.position);
        require(now.fType == row.type && now.fState == row.state && now.wID == row.id && now.dwItemData == row.data &&
                now.hSubMenu == row.submenu, "menu flags/state/data/submenus were not restored");
        require(item_text(row.menu, row.position) == row.text, "Chinese label/accelerator text was not restored");
    }
    for (const auto& saved : before.brushes) {
        MENUINFO now{sizeof(now)}; now.fMask = MIM_BACKGROUND;
        require(GetMenuInfo(saved.first, &now) && now.hbrBack == saved.second, "original menu background brush was not restored");
    }
}
struct TestState {
    HMENU menu{}; Snapshot before; xfx::ThemeColors colors{};
    std::string failure; int probes = 0; int original_measure_calls = 0;
    UINT max_row_height = 0;
    ULONG_PTR temporary_data = 0; bool destroy_owner = false;
    int keyboard_flow = 0; bool keyboard_sent = false;
    bool plain_baseline = false;
    int actual_measure_calls = 0, actual_draw_calls = 0;
    int probe_measure_calls = 0, probe_draw_calls = 0;
    bool probing = false;
};
static thread_local HWND observed_owner = nullptr;
static thread_local TestState* observed_state = nullptr;
static LRESULT CALLBACK observe_owner_messages(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && observed_state && lparam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
        if (message->hwnd == observed_owner && message->lParam) {
            if (message->message == WM_MEASUREITEM &&
                reinterpret_cast<const MEASUREITEMSTRUCT*>(message->lParam)->CtlType == ODT_MENU) {
                ++(observed_state->probing ? observed_state->probe_measure_calls : observed_state->actual_measure_calls);
            } else if (message->message == WM_DRAWITEM &&
                       reinterpret_cast<const DRAWITEMSTRUCT*>(message->lParam)->CtlType == ODT_MENU) {
                ++(observed_state->probing ? observed_state->probe_draw_calls : observed_state->actual_draw_calls);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
class OwnerMessageObserver {
public:
    OwnerMessageObserver(HWND owner, TestState& state) {
        require(!observed_state, "nested menu message observer");
        hook_ = SetWindowsHookExW(WH_CALLWNDPROC, observe_owner_messages, nullptr, GetCurrentThreadId());
        require(hook_ != nullptr, "cannot observe actual menu owner messages");
        state.actual_measure_calls = state.actual_draw_calls = 0;
        state.probe_measure_calls = state.probe_draw_calls = 0;
        observed_owner = owner; observed_state = &state;
    }
    ~OwnerMessageObserver() {
        UnhookWindowsHookEx(hook_);
        observed_state = nullptr; observed_owner = nullptr;
    }
    OwnerMessageObserver(const OwnerMessageObserver&) = delete;
    OwnerMessageObserver& operator=(const OwnerMessageObserver&) = delete;
private:
    HHOOK hook_ = nullptr;
};
struct ProbeMessageScope {
    TestState& state; bool previous;
    explicit ProbeMessageScope(TestState& value) : state(value), previous(value.probing) { state.probing = true; }
    ~ProbeMessageScope() { state.probing = previous; }
};
static UINT probe_draw(HWND owner, const Row& row, const MENUITEMINFOW& item, TestState& state) {
    ProbeMessageScope probe_scope(state);
    const auto& colors = state.colors;
    MEASUREITEMSTRUCT measure{}; measure.CtlType = ODT_MENU; measure.itemID = item.wID; measure.itemData = item.dwItemData;
    require(SendMessageW(owner, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)) == TRUE &&
            measure.itemWidth > 0 && measure.itemHeight > 0, "owner-drawn item did not provide dimensions");
    require(measure.itemWidth < 2048 && measure.itemHeight < 256, "menu item measurement is unreasonable");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = static_cast<LONG>(measure.itemWidth) + 16; bitmap.bmiHeader.biHeight = -static_cast<LONG>(measure.itemHeight) - 16;
    bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(dc && dib, "cannot allocate test bitmap");
    const auto old_bitmap = SelectObject(dc, dib);
    RECT whole{0, 0, bitmap.bmiHeader.biWidth, -bitmap.bmiHeader.biHeight};
    HBRUSH guard = CreateSolidBrush(RGB(3, 5, 7)); FillRect(dc, &whole, guard); DeleteObject(guard);
    const COLORREF original_text_color = SetTextColor(dc, RGB(11, 22, 33));
    const int original_mode = SetBkMode(dc, OPAQUE);
    DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_MENU; draw.itemID = item.wID; draw.itemData = item.dwItemData;
    draw.hwndItem = reinterpret_cast<HWND>(row.menu); draw.hDC = dc; draw.itemAction = ODA_DRAWENTIRE;
    draw.rcItem = {8, 8, 8 + static_cast<LONG>(measure.itemWidth), 8 + static_cast<LONG>(measure.itemHeight)};
    if (row.state & MFS_DISABLED) draw.itemState |= ODS_DISABLED;
    if (row.state & MFS_CHECKED) draw.itemState |= ODS_CHECKED;
    require(SendMessageW(owner, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)) == TRUE, "menu draw was not handled");
    require(GetPixel(dc, 9, 9) == colors.surface, "menu surface did not use the selected theme");
    require(GetPixel(dc, 1, 1) == RGB(3, 5, 7), "menu draw escaped its item rectangle");
    require(GetTextColor(dc) == RGB(11, 22, 33) && GetBkMode(dc) == OPAQUE, "menu draw leaked DC state");
    if (row.type & MFT_SEPARATOR) {
        require(GetPixel(dc, 8 + static_cast<int>(measure.itemWidth / 2), 8 + static_cast<int>(measure.itemHeight / 2)) == colors.divider,
                "separator did not use theme divider color");
    } else if (!(row.state & MFS_DISABLED)) {
        draw.itemState |= ODS_SELECTED;
        SendMessageW(owner, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
        require(GetPixel(dc, 9, 9) == colors.selected, "selected item did not use theme selection color");
    } else {
        bool disabled_text_found = false;
        for (int y = draw.rcItem.top; y < draw.rcItem.bottom; ++y)
            for (int x = draw.rcItem.left + 24; x < draw.rcItem.right - 12; ++x)
                disabled_text_found = disabled_text_found || GetPixel(dc, x, y) == colors.disabled;
        require(disabled_text_found, "disabled menu text did not use theme disabled color");
    }
    SetTextColor(dc, original_text_color); SetBkMode(dc, original_mode);
    SelectObject(dc, old_bitmap); DeleteObject(dib); DeleteDC(dc);
    return (row.type & MFT_SEPARATOR) ? 0 : measure.itemHeight;
}
static LRESULT CALLBACK test_window(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<TestState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<TestState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state && message == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(wparam) == state->menu) {
        if (state->plain_baseline) return 0;
        try {
            for (const auto& row : state->before.rows) {
                const auto item = item_info(row.menu, row.position);
                require((item.fType & MFT_OWNERDRAW) != 0 && item.dwItemData != row.data, "menu tree was not converted to scoped owner-draw");
                require((item.fType & ~MFT_OWNERDRAW) == row.type && item.fState == row.state && item.hSubMenu == row.submenu,
                        "theming changed disabled/check/separator/submenu behavior");
                require(item_text(row.menu, row.position) == row.text, "active owner-draw lost accessible Chinese label");
                state->temporary_data = item.dwItemData;
                state->max_row_height = (std::max)(state->max_row_height, probe_draw(hwnd, row, item, *state)); ++state->probes;
            }
            const LRESULT mnemonic = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(L'c', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
            require(HIWORD(mnemonic) == MNC_EXECUTE && LOWORD(mnemonic) == 0, "Chinese label mnemonic was lost");
            const LRESULT disabled = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(L'd', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
            require(HIWORD(disabled) == MNC_IGNORE, "disabled item accepted its mnemonic");
            const LRESULT repeated = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(L'a', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
            require(HIWORD(repeated) == MNC_SELECT, "duplicate mnemonics should select instead of execute");
            const LRESULT literal = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(L'&', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
            require(HIWORD(literal) == MNC_IGNORE, "escaped ampersand became a mnemonic");
            const LRESULT submenu = SendMessageW(hwnd, WM_MENUCHAR, MAKEWPARAM(L'm', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
            require(HIWORD(submenu) == MNC_EXECUTE && LOWORD(submenu) == 3, "submenu mnemonic lost its original position");
        } catch (const std::exception& error) { state->failure = error.what(); }
        return 0;
    }
    if (state && message == WM_TIMER) {
        KillTimer(hwnd, wparam);
        if (!state->probes && state->failure.empty())
            SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(state->menu), 0);
        if (state->keyboard_flow && !state->keyboard_sent) {
            state->keyboard_sent = true;
            PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, VK_HOME, 0);
            PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, VK_DOWN, 0); // Skips the disabled row and separator.
            PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, state->keyboard_flow == 1 ? VK_DOWN : VK_RIGHT, 0);
            if (state->keyboard_flow == 2) PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, VK_HOME, 0);
            PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, VK_RETURN, 0);
            SetTimer(hwnd, 9, 200, nullptr); // Bound the test if Windows rejects synthetic queued keys.
            return 0;
        }
        if (state->destroy_owner) DestroyWindow(hwnd); else EndMenu();
        return 0;
    }
    if (state && message == WM_MEASUREITEM) { ++state->original_measure_calls; return 0x1234; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
static HMENU make_menu() {
    HMENU menu = CreatePopupMenu(), child = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_CHECKED, 101, L"中文复制(&C)\tCtrl+C");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 102, L"不可用(&D)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(child, MF_STRING, 201, L"导出 PNG(&P)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child), L"更多(&M)");
    AppendMenuW(menu, MF_STRING, 103, L"选项(&A)");
    AppendMenuW(menu, MF_STRING, 104, L"另项(&A)");
    AppendMenuW(menu, MF_STRING, 105, L"Literal && amp");
    for (HMENU current : {menu, child}) {
        for (int i = 0; i < GetMenuItemCount(current); ++i) {
            MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_DATA; item.dwItemData = static_cast<ULONG_PTR>(0x1234 + i);
            SetMenuItemInfoW(current, static_cast<UINT>(i), TRUE, &item);
        }
    }
    return menu;
}
int main() {
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW type{}; type.lpfnWndProc = test_window; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"XFXThemedMenuTestOwner";
    RegisterClassW(&type);
    int failed = 0;
    for (bool light : {false, true}) {
        TestState state; state.menu = make_menu();
        state.colors.surface = light ? RGB(245, 245, 245) : RGB(38, 38, 38);
        state.colors.text = light ? RGB(24, 24, 24) : RGB(232, 232, 232);
        state.colors.selected = light ? RGB(202, 226, 245) : RGB(42, 98, 138);
        state.colors.selected_text = state.colors.text;
        state.colors.disabled = light ? RGB(130, 130, 130) : RGB(115, 115, 115);
        state.colors.divider = light ? RGB(200, 200, 200) : RGB(70, 70, 70);
        state.colors.icon = state.colors.text;
        HBRUSH original = CreateSolidBrush(RGB(1, 2, 3));
        MENUINFO background{sizeof(background)}; background.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS; background.hbrBack = original;
        SetMenuInfo(state.menu, &background); snapshot_menu(state.menu, state.before);
        HWND owner = CreateWindowW(type.lpszClassName, L"Owned menu test", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, &state);
        HFONT caller_font = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        try {
            require(owner != nullptr, "cannot create owned test window");
            ShowWindow(owner, SW_SHOWNOACTIVATE);
            UINT default_height = 0;
            for (int iteration = 0; iteration < 2; ++iteration) {
                state.failure.clear(); state.probes = 0; state.max_row_height = 0;
                const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
                SetTimer(owner, 1, 40, nullptr);
                UINT selected = 0;
                {
                    OwnerMessageObserver observer(owner, state);
                    selected = xfx::track_themed_popup(state.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION, 1, 1, owner, state.colors,
                        iteration ? caller_font : nullptr);
                }
                KillTimer(owner, 1);
                require(selected == 0, "auto-cancel should not execute a command");
                require(state.failure.empty(), state.failure.c_str());
                require(state.probes == static_cast<int>(state.before.rows.size()), "menu was never fully inspected");
                require(state.probe_measure_calls == state.probes && state.probe_draw_calls >= state.probes,
                        "manual drawing probes were not separately observed");
                std::cout << "OBSERVED " << (light ? "light" : "dark") << (iteration ? " caller" : " default")
                          << " font: actual measure=" << state.actual_measure_calls << " draw=" << state.actual_draw_calls
                          << "; manual probe measure=" << state.probe_measure_calls << " draw=" << state.probe_draw_calls << '\n';
                require(state.actual_draw_calls > 0, "TrackPopupMenuEx did not send actual owner-draw messages");
                // Windows can retain measurements when this HMENU is reopened; the
                // first display must independently prove actual system measurement.
                if (!iteration) require(state.actual_measure_calls > 0,
                                        "first TrackPopupMenuEx did not send actual menu measurement messages");
                assert_restored(state.before);
                if (!iteration) default_height = state.max_row_height;
                else {
                    LOGFONTW still_owned{};
                    require(caller_font && GetObjectW(caller_font, sizeof(still_owned), &still_owned), "caller font was deleted by the helper");
                    require(state.max_row_height > default_height, "explicit caller font did not affect menu measurement");
                }
                if (iteration) require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi_before, "menu tracking leaked GDI objects");
                MEASUREITEMSTRUCT unrelated{}; unrelated.CtlType = ODT_MENU; unrelated.itemData = state.temporary_data;
                require(SendMessageW(owner, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&unrelated)) == 0x1234,
                        "temporary subclass or dangling item data survived tracking");
            }
            std::cout << "PASS " << (light ? "light" : "dark") << " recursive drawing, Chinese labels, mnemonics and state restoration\n";
            SetLastError(ERROR_SUCCESS);
            require(xfx::track_themed_popup(state.menu, TPM_RETURNCMD, 0, 0, nullptr, state.colors) == 0 &&
                    GetLastError() == ERROR_INVALID_PARAMETER, "invalid owner should be rejected before menu mutation");
            assert_restored(state.before);
            bool keyboard_verified = true;
            for (int flow : {1, 2}) {
                ShowWindow(owner, SW_SHOW); SetForegroundWindow(owner); SetActiveWindow(owner); SetFocus(owner);
                state.plain_baseline = true;
                state.keyboard_flow = flow; state.keyboard_sent = false; state.probes = 1;
                SetTimer(owner, 1, 40, nullptr);
                const UINT baseline = TrackPopupMenuEx(state.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION,1,1,owner,nullptr);
                KillTimer(owner, 1); KillTimer(owner, 9);
                state.plain_baseline = false;
                state.keyboard_flow = flow; state.keyboard_sent = false; state.probes = 0; state.failure.clear();
                SetTimer(owner, 1, 40, nullptr);
                const UINT selected = xfx::track_themed_popup(state.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION,
                    1, 1, owner, state.colors);
                KillTimer(owner, 1); KillTimer(owner, 9);
                const UINT expected = flow == 1 ? 103u : 201u;
                if (baseline != expected) {
                    // Posted WM_KEYDOWN is not physical input. If the plain Windows menu
                    // cannot complete the requested sequence, this fixture cannot prove arrow/Enter behavior.
                    keyboard_verified = false;
                    require(selected == baseline, "themed menu changed the plain native baseline's observed selection");
                } else require(selected == baseline && selected == (flow == 1 ? 103u : 201u),
                    "native keyboard navigation/disabled skipping/submenu selection changed");
                assert_restored(state.before);
            }
            state.keyboard_flow = 0;
            if (keyboard_verified) std::cout << "PASS " << (light ? "light" : "dark") << " native arrow/Enter navigation skips disabled rows and opens submenus\n";
            else std::cout << "UNVERIFIED " << (light ? "light" : "dark") << " arrow/Enter: plain Windows baseline did not complete posted sequence; physical/host validation required\n";
            state.destroy_owner = true; state.failure.clear(); SetTimer(owner, 2, 40, nullptr);
            xfx::track_themed_popup(state.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION, 1, 1, owner, state.colors);
            require(!IsWindow(owner), "owned-window destruction did not end tracking");
            assert_restored(state.before);
            std::cout << "PASS " << (light ? "light" : "dark") << " owner destruction safely releases temporary menu state\n";
        } catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << error.what() << '\n'; }
        if (IsWindow(owner)) DestroyWindow(owner);
        if (caller_font) DeleteObject(caller_font);
        DestroyMenu(state.menu); DeleteObject(original);
    }
    return failed ? 1 : 0;
}
