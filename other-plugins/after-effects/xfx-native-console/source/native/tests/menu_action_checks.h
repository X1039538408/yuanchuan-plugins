#pragma once
#include <fstream>

// These tests call the real thread WH_MSGFILTER via CallMsgFilter while a real
// TrackPopupMenuEx loop is visible. They prove hook decisions/cleanup, not UIA
// per-cell exposure or physical-device delivery on the After Effects host.

static void check_action_geometry() {
    using namespace xfx;
    using namespace xfx::menu_action_detail;
    for (int dpi : {96, 144, 192}) {
        RECT row{17, 29, 17 + MulDiv(400, dpi, 96), 29 + MulDiv(32, dpi, 96)};
        const auto geometry = layout(row, dpi);
        require(geometry.label.left >= row.left && geometry.label.right <= geometry.cells[0].left,
                "action label overlaps right strip");
        for (int cell = 0; cell < 3; ++cell) {
            const auto rect = geometry.cells[cell];
            require(rect.left < rect.right && rect.left >= row.left && rect.right <= row.right &&
                    rect.top >= row.top && rect.bottom <= row.bottom, "action geometry escaped row");
            require(hit_test(geometry, {rect.left, rect.top}) == cell, "cell origin not hittable");
            require(hit_test(geometry, {rect.right - 1, rect.bottom - 1}) == cell, "cell far edge not hittable");
            if (cell < 2) require(rect.right <= geometry.cells[cell + 1].left, "action cells overlap");
        }
        require(hit_test(geometry, {geometry.label.left, row.top}) == -1, "label acts as action");
        require(hit_test(geometry, {row.right, row.bottom - 1}) == -1, "row right edge is not half-open");
        require(hit_test(geometry, {row.left, row.bottom}) == -1, "row bottom edge is not half-open");
    }
    require(hit_test(layout({0, 0, 20, 8}), {1, 1}) == -1, "tiny row has actionable cells");
    for (UINT slot = 0; slot < 9; ++slot) {
        std::array<MenuAction, 3> actions{{{100 + slot, MenuActionIcon::Bind, true, L"绑定"},
            {200 + slot, MenuActionIcon::Edit, true, L"备注"}, {300 + slot, MenuActionIcon::Clear, true, L"清除"}}};
        for (int cell = 0; cell < 3; ++cell) {
            require(released_command(actions, cell, cell) == (cell + 1) * 100u + slot, "27 action mapping changed");
            require(released_command(actions, cell, (cell + 1) % 3) == 0, "cross-cell drag activated");
            actions[cell].enabled = false;
            require(released_command(actions, cell, cell) == 0, "disabled action activated");
            actions[cell].enabled = true;
        }
        require(released_command(actions, -1, 0) == 0 && released_command(actions, 0, -1) == 0,
                "label/outside press or release activated");
        require(next_enabled(actions, -1, 1) == 0 && next_enabled(actions, 2, 1) == 0 &&
                next_enabled(actions, 0, -1) == 2, "keyboard entry/wrap incorrect");
        actions[1].enabled = false;
        require(next_enabled(actions, 0, 1) == 2 && next_enabled(actions, 2, -1) == 0, "keyboard did not skip disabled cell");
        actions[0].enabled = actions[2].enabled = false;
        require(next_enabled(actions, -1, 1) == -1, "keyboard entered disabled-only strip");
    }
    std::cout << "PASS action geometry DPI/half-open/27 mappings/disabled/press-release/keyboard helpers\n";
}

namespace action_tests {
constexpr UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_NOANIMATION;
enum class Flow { Mouse, Label, Drag, CrossRow, CrossButton, Disabled, Keyboard, KeyboardCancel, KeyboardExitFirstDisabled,
                  Destroy, NestedPlain, NestedAction, Overlap, Hover, HoverNextRow, Capture, ParentMouse, ParentKeyboard, Cancel };
struct State {
    HMENU menu{}, wrapper{}, popup_root{};
    xfx::MenuActionRows rows;
    xfx::ThemeColors colors{};
    Flow flow = Flow::Mouse;
    UINT slot = 0;
    int cell = 0, ticks = 0, commands = 0, parent_attempts = 0;
    std::string failure, parent_unverified;
    bool nested = false, nested_seen = false, light = false;
    POINT outer_point{};
    UINT nested_result = 0;
    std::array<WPARAM, 16> native_keys{};
    size_t native_key_count = 0;
};
static thread_local State* native_input_state = nullptr;
static LRESULT CALLBACK native_input_observer(int code, WPARAM wparam, LPARAM lparam) {
    if (code == MSGF_MENU && lparam && native_input_state) {
        const auto* message = reinterpret_cast<const MSG*>(lparam);
        if (message->message == WM_KEYDOWN && native_input_state->native_key_count < native_input_state->native_keys.size())
            native_input_state->native_keys[native_input_state->native_key_count++] = message->wParam;
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
static bool filter(HWND owner, UINT id, WPARAM key, POINT point, int code = MSGF_MENU) {
    MSG message{}; message.hwnd = owner; message.message = id; message.wParam = key; message.pt = point;
    return CallMsgFilterW(&message, code) != FALSE;
}
static POINT centre(RECT rect) { return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}; }
static int dpi(HWND owner) {
    const HDC dc = GetDC(owner); const int value = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(owner, dc); return value;
}
static int tooltip_count(bool visible_only) {
    struct Count { int value; bool visible; } count{0, visible_only};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        auto* counter = reinterpret_cast<Count*>(data);
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (lstrcmpW(name, TOOLTIPS_CLASSW) == 0 && (!counter->visible || IsWindowVisible(window))) ++counter->value;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count.value;
}
static void check_tooltip_hit_passthrough() {
    bool saw = false, valid = true;
    struct Check { bool* saw; bool* valid; } check{&saw, &valid};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        auto* result = reinterpret_cast<Check*>(data);
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (lstrcmpW(name, TOOLTIPS_CLASSW) == 0 && IsWindowVisible(window)) {
            *result->saw = true;
            RECT rect{}; GetWindowRect(window, &rect);
            *result->valid = *result->valid && !IsWindowEnabled(window) &&
                (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) && WindowFromPoint(centre(rect)) != window;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&check));
    require(saw && valid, "tracking tooltip intercepted WindowFromPoint/menu hit testing");
}
static xfx::MenuActionRows make_rows(HMENU menu) {
    xfx::MenuActionRows rows;
    for (UINT slot = 0; slot < 9; ++slot) rows.push_back({menu, slot, {{{100 + slot, xfx::MenuActionIcon::Bind, true, L"绑定当前效果"},
        {200 + slot, xfx::MenuActionIcon::Edit, true, L"编辑备注"}, {300 + slot, xfx::MenuActionIcon::Clear, true, L"清除绑定"}}}});
    return rows;
}
static HMENU make_action_menu() {
    HMENU menu = CreatePopupMenu();
    for (UINT slot = 0; slot < 9; ++slot) {
        const std::wstring label = slot ? L"快捷槽 " + std::to_wstring(slot + 1) : L"快捷槽 1：中文长备注(&Z) " + std::wstring(160, L'长');
        require(AppendMenuW(menu, MF_STRING, 1000 + slot, label.c_str()) != FALSE, "cannot append action row");
    }
    return menu;
}
static HWND visible_popup(HMENU menu) {
    struct Probe { HMENU menu; HWND window; } probe{menu, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        auto* result = reinterpret_cast<Probe*>(data);
        wchar_t name[32]{}; GetClassNameW(window, name, 32);
        MENUBARINFO bar{sizeof(bar)};
        if (IsWindowVisible(window) && lstrcmpW(name, L"#32768") == 0 &&
            GetMenuBarInfo(window, OBJID_CLIENT, 0, &bar) && bar.hMenu == result->menu) {
            result->window = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&probe));
    return probe.window;
}
static void capture_popup(const State& state, bool parent_child = false) {
    const HWND popup = visible_popup(state.menu);
    require(popup != nullptr, "capture target is not the real visible menu");
    RECT rect{}; require(GetWindowRect(popup, &rect) != FALSE, "capture window rect unavailable");
    if (parent_child) {
        const HWND parent = visible_popup(state.popup_root);
        RECT parent_rect{};
        require(parent && GetWindowRect(parent, &parent_rect), "parent and child menus are not simultaneously visible");
        RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        UnionRect(&rect, &rect, &parent_rect);
    }
    const LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
    require(width > 0 && height > 0 && width < 4096 && height < 4096, "capture dimensions invalid");
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biSizeImage = static_cast<DWORD>(width * height * 4);
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!screen || !memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); if (screen) ReleaseDC(nullptr, screen);
        require(false, "capture resources unavailable");
    }
    const auto previous = SelectObject(memory, bitmap);
    RedrawWindow(popup, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const bool captured = BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT) != FALSE;
    GdiFlush();
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader); file.bfSize = file.bfOffBits + info.bmiHeader.biSizeImage;
    bool written = false;
    if (captured) {
        const char* path = parent_child ?
            (state.light ? "artifacts/themed-menu-tests/parent-child-light.bmp" : "artifacts/themed-menu-tests/parent-child-dark.bmp") :
            (state.light ? "artifacts/themed-menu-tests/actual-menu-light.bmp" : "artifacts/themed-menu-tests/actual-menu-dark.bmp");
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
        output.write(static_cast<const char*>(bits), info.bmiHeader.biSizeImage); output.flush();
        written = output.good();
    }
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    require(captured && written, "actual native popup screenshot failed");
}
static void check_bitmap(HWND owner, State& state) {
    const auto item = item_info(state.menu, 0);
    MEASUREITEMSTRUCT measure{}; measure.CtlType = ODT_MENU; measure.itemData = item.dwItemData;
    require(SendMessageW(owner, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)) == TRUE, "action row not measured");
    require(measure.itemWidth <= static_cast<UINT>(MulDiv(448, dpi(owner), 96)), "long action label is not bounded");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(measure.itemWidth) + 16;
    info.bmiHeader.biHeight = -static_cast<LONG>(measure.itemHeight) - 16;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    require(dc && bitmap, "action test bitmap allocation failed");
    const auto old = SelectObject(dc, bitmap);
    try {
        RECT whole{0, 0, info.bmiHeader.biWidth, -info.bmiHeader.biHeight};
        HBRUSH guard = CreateSolidBrush(RGB(3, 5, 7)); FillRect(dc, &whole, guard); DeleteObject(guard);
        SetTextColor(dc, RGB(11, 22, 33)); SetBkMode(dc, OPAQUE);
        DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_MENU; draw.itemData = item.dwItemData; draw.hDC = dc;
        draw.rcItem = {8, 8, 8 + static_cast<LONG>(measure.itemWidth), 8 + static_cast<LONG>(measure.itemHeight)};
        require(SendMessageW(owner, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)) == TRUE, "action draw not handled");
        require(GetPixel(dc, 1, 1) == RGB(3, 5, 7) && GetPixel(dc, draw.rcItem.right, 9) == RGB(3, 5, 7), "action draw escaped row bounds");
        require(GetTextColor(dc) == RGB(11, 22, 33) && GetBkMode(dc) == OPAQUE, "action draw leaked DC state");
        const auto geometry = xfx::menu_action_detail::layout(draw.rcItem, dpi(owner));
        for (int cell = 0; cell < 3; ++cell) {
            int pixels = 0;
            const auto rect = geometry.cells[cell];
            const COLORREF expected = state.rows[0].actions[cell].enabled ? state.colors.icon : state.colors.disabled;
            for (int y = rect.top; y < rect.bottom; ++y) for (int x = rect.left; x < rect.right; ++x)
                if (GetPixel(dc, x, y) == expected) ++pixels;
            require(pixels > 5, "vector icon missing or does not use per-cell enabled/disabled color");
        }
    } catch (...) { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); throw; }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
}
static void native_mouse(HWND owner, POINT point, bool click) {
    DWORD foreground_process = 0, target_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    GetWindowThreadProcessId(WindowFromPoint(point), &target_process);
    require(foreground_process == GetCurrentProcessId() && target_process == GetCurrentProcessId() && IsWindow(owner),
        "fixture lost foreground/target ownership; refuse mouse input to another app");
    require(!(GetAsyncKeyState(VK_MENU) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        !(GetAsyncKeyState(VK_SHIFT) & 0x8000), "fixture input modifiers are held");
    std::array<INPUT, 3> input{};
    for (auto& event : input) event.type = INPUT_MOUSE;
    input[0].mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
    input[0].mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const UINT count = click ? 3u : 1u;
    require(SendInput(count, input.data(), sizeof(INPUT)) == count, "native submenu mouse input rejected");
}
static void open_native_submenu(HWND owner, HMENU root) {
    RECT parent_row{}; require(GetMenuItemRect(nullptr, root, 1, &parent_row), "ordinary parent row rect unavailable");
    const POINT point = centre(parent_row);
    require(visible_popup(root) == WindowFromPoint(point), "ordinary parent row is obscured");
    native_mouse(owner, point, true);
}
static LRESULT CALLBACK window_proc(HWND owner, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(owner, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(owner, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state && message == WM_COMMAND) ++state->commands;
    if (state && message == WM_TIMER) {
        try {
            if (state->flow == Flow::ParentMouse || state->flow == Flow::ParentKeyboard) {
                ++state->ticks;
                if (state->parent_attempts++ == 0) { open_native_submenu(owner, state->popup_root); return 0; }
                const HWND child_window = visible_popup(state->menu);
                if (!child_window) {
                    if (state->parent_attempts >= 40) std::cerr << "PARENT DIAGNOSTIC flow=" << static_cast<int>(state->flow) << " root=" << state->popup_root << " child=" << state->menu << " visible_root=" << visible_popup(state->popup_root) << " state0=" << GetMenuState(state->popup_root, 0, MF_BYPOSITION) << " state1=" << GetMenuState(state->popup_root, 1, MF_BYPOSITION) << " foreground=" << GetForegroundWindow() << " owner=" << owner << '\n';
                    if (state->parent_attempts >= 40) {
                        state->parent_unverified = "native input did not open ordinary child popup";
                        KillTimer(owner, 11); EndMenu();
                    }
                    return 0;
                }
                require(visible_popup(state->popup_root) != nullptr, "child popup appeared without its ordinary parent");
                // No manual WM_INITMENUPOPUP/TPM_RECURSE/new session: one native
                // parent loop really opened its attached child via mouse hover.
                if (state->flow == Flow::ParentKeyboard && !(GetMenuState(state->menu, 0, MF_BYPOSITION) & MF_HILITE)) {
                    // Mouse-opened popups initially have no selected child row.
                    // Real pointer motion (not HiliteMenuItem/WM_INIT injection)
                    // gives the child native row focus before testing its keys.
                    RECT first{}; require(GetMenuItemRect(nullptr, state->menu, 0, &first) != FALSE, "native child focus rect unavailable");
                    const POINT point = centre(first);
                    require(visible_popup(state->menu) == WindowFromPoint(point), "child row obscured; refuse pointer motion");
                    DWORD process = 0; GetWindowThreadProcessId(GetForegroundWindow(), &process);
                    require(process == GetCurrentProcessId(), "fixture lost foreground; refuse pointer motion");
                    if (state->parent_attempts >= 40) {
                        state->parent_unverified = "native pointer motion did not focus child row";
                        KillTimer(owner, 11); EndMenu(); return 0;
                    }
                    native_mouse(owner, point, false);
                    return 0;
                }
                if (state->flow == Flow::ParentMouse) {
                    capture_popup(*state, true);
                    RECT row{}; require(GetMenuItemRect(nullptr, state->menu, 0, &row) != FALSE, "native child row rect missing");
                    const auto geometry = xfx::menu_action_detail::layout(row, dpi(owner));
                    const POINT point = centre(geometry.cells[0]);
                    require(filter(owner, WM_LBUTTONDOWN, 0, point) && filter(owner, WM_LBUTTONUP, 0, point), "ordinary child action mouse click not intercepted");
                } else {
                    require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "child Right stolen by highlighted root parent");
                    require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}) && filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "child keyboard cells did not cycle");
                    require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "child focused Enter did not select action");
                }
                return 0;
            }
            if (state->nested) {
                state->nested_seen = true;
                // A message at an outer action's real screen position must not
                // be captured by the outer hook while this actionless loop runs.
                if (state->flow == Flow::NestedPlain) {
                    require(!filter(owner, WM_LBUTTONDOWN, 0, state->outer_point), "outer hook captured actionless nested menu");
                    require(!filter(owner, WM_LBUTTONUP, 0, state->outer_point), "outer hook executed from actionless nested menu");
                    EndMenu();
                } else {
                    HMENU inner = reinterpret_cast<HMENU>(GetPropW(owner, L"inner"));
                    HiliteMenuItem(owner, inner, 0, MF_BYPOSITION | MF_HILITE);
                    require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "nested action did not enter keyboard cell");
                    require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "nested action did not select keyboard cell");
                }
                return 0;
            }
            if (state->ticks++ > 0) { KillTimer(owner, 11); EndMenu(); return 0; }
            RECT row{};
            require(GetMenuItemRect(nullptr, state->menu, state->slot, &row) != FALSE, "visible action row has no screen rect");
            if (state->flow == Flow::Disabled || state->flow == Flow::Cancel) check_bitmap(owner, *state);
            const auto geometry = xfx::menu_action_detail::layout(row, dpi(owner));
            const POINT point = centre(geometry.cells[state->cell]);
            HiliteMenuItem(owner, state->menu, state->slot, MF_BYPOSITION | MF_HILITE);
            switch (state->flow) {
            case Flow::Mouse: {
                const bool captured = filter(owner, WM_LBUTTONDOWN, 0, point);
                if (!captured) {
                    const HWND under = WindowFromPoint(point); wchar_t name[64]{}; GetClassNameW(under, name, 64);
                    MENUBARINFO bar{sizeof(bar)}; const BOOL ok = GetMenuBarInfo(under, OBJID_CLIENT, 0, &bar);
                    std::wcerr << L"HIT DIAGNOSTIC class=" << name << L" point=" << point.x << L"," << point.y << L" getbar=" << ok << L" hmenu=" << bar.hMenu << L" expected=" << state->menu << L"\n";
                }
                require(captured, "action mouse press was not captured");
                require(filter(owner, WM_LBUTTONUP, 0, point), "action mouse release was not captured"); break;
            }
            case Flow::Label: {
                const POINT label = centre(geometry.label);
                require(filter(owner, WM_LBUTTONDOWN, 0, label) && filter(owner, WM_LBUTTONUP, 0, label), "label click not inert");
                require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "unfocused label Enter not inert");
                const LRESULT mnemonic = SendMessageW(owner, WM_MENUCHAR, MAKEWPARAM(L'z', MF_POPUP), reinterpret_cast<LPARAM>(state->menu));
                require(HIWORD(mnemonic) == MNC_SELECT, "action-row mnemonic executes label"); break;
            }
            case Flow::Drag:
                require(filter(owner, WM_LBUTTONDOWN, 0, centre(geometry.cells[0])), "drag press not captured");
                require(filter(owner, WM_LBUTTONUP, 0, centre(geometry.cells[1])), "cross-cell release not captured"); break;
            case Flow::CrossRow: {
                RECT other{}; require(GetMenuItemRect(nullptr, state->menu, 1, &other) != FALSE, "second row rect missing");
                const auto target = xfx::menu_action_detail::layout(other, dpi(owner));
                require(filter(owner, WM_LBUTTONDOWN, 0, point) && filter(owner, WM_LBUTTONUP, 0, centre(target.cells[0])),
                    "cross-row press/release not captured"); break;
            }
            case Flow::CrossButton:
                require(filter(owner, WM_LBUTTONDOWN, 0, point) && filter(owner, WM_RBUTTONUP, 0, point), "cross-button release not captured"); break;
            case Flow::Disabled:
                require(filter(owner, WM_LBUTTONDOWN, 0, centre(geometry.cells[1])) &&
                        filter(owner, WM_LBUTTONUP, 0, centre(geometry.cells[1])), "disabled cell mouse not captured");
                break;
            case Flow::Keyboard:
                require(!filter(owner, WM_KEYDOWN, VK_LEFT, {}), "unfocused Left must remain native");
                require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "Right did not enter cells");
                require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "Right did not skip disabled cell");
                require(filter(owner, WM_KEYDOWN, VK_LEFT, {}), "Left did not cycle cells");
                require(filter(owner, WM_KEYDOWN, VK_LEFT, {}), "Left at first enabled cell did not exit strip");
                require(tooltip_count(true) == 0, "exiting cell strip did not hide tooltip");
                require(!filter(owner, WM_KEYDOWN, VK_LEFT, {}), "Left after cell exit did not return to native parent navigation");
                require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "Enter after cell exit executed inert label");
                require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}) && filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "could not re-enter/cycle after cell exit");
                require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "focused Enter did not select"); break;
            case Flow::KeyboardExitFirstDisabled:
                require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "Right did not enter first enabled cell when Bind disabled");
                require(filter(owner, WM_KEYDOWN, VK_LEFT, {}), "Left did not exit first enabled cell when Bind disabled");
                require(!filter(owner, WM_KEYDOWN, VK_LEFT, {}), "Left after disabled-first exit did not return to native");
                require(filter(owner, WM_KEYDOWN, VK_RETURN, {}) && tooltip_count(true) == 0, "disabled-first exit did not clear focus/tooltip");
                break;
            case Flow::KeyboardCancel:
                require(filter(owner, WM_KEYDOWN, VK_RIGHT, {}), "Right did not enter cells before native navigation");
                require(!filter(owner, WM_KEYDOWN, VK_UP, {}) && !filter(owner, WM_KEYDOWN, VK_DOWN, {}), "Up/Down no longer native");
                require(filter(owner, WM_KEYDOWN, VK_RETURN, {}), "Enter after native navigation executed label");
                require(!filter(owner, WM_KEYDOWN, VK_ESCAPE, {}), "Esc no longer native"); break;
            case Flow::Destroy:
                DestroyWindow(owner); break;
            case Flow::NestedPlain:
            case Flow::NestedAction: {
                HMENU inner = CreatePopupMenu(); require(inner != nullptr, "inner menu creation failed");
                AppendMenuW(inner, MF_STRING, 999, L"嵌套菜单");
                SetPropW(owner, L"inner", reinterpret_cast<HANDLE>(inner));
                state->nested = true; state->outer_point = point;
                if (state->flow == Flow::NestedPlain)
                    state->nested_result = xfx::track_themed_popup(inner, flags | TPM_RECURSE, 700, 100, owner, state->colors);
                else {
                    const xfx::MenuActionRows actions{{inner, 0, {{{777, xfx::MenuActionIcon::Bind, true, L"绑定"},
                        {778, xfx::MenuActionIcon::Edit, true, L"备注"}, {779, xfx::MenuActionIcon::Clear, true, L"清除"}}}}};
                    state->nested_result = xfx::track_themed_action_popup(inner, flags | TPM_RECURSE, 700, 100, owner, state->colors, nullptr, actions);
                }
                state->nested = false; RemovePropW(owner, L"inner"); DestroyMenu(inner);
                require(state->nested_seen, "nested loop never ran");
                EndMenu(); break;
            }
            case Flow::Overlap: {
                Snapshot active; snapshot_menu(state->menu, active);
                for (bool action_api : {false, true}) {
                    SetLastError(ERROR_SUCCESS);
                    const UINT nested_result = action_api ?
                        xfx::track_themed_action_popup(state->menu, flags | TPM_RECURSE, 700, 100, owner, state->colors, nullptr, state->rows) :
                        xfx::track_themed_popup(state->menu, flags | TPM_RECURSE, 700, 100, owner, state->colors);
                    require(nested_result == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "nested same HMENU was not rejected");
                    assert_restored(active);
                    // A fresh parent can still share a currently tracked child.
                    // Constructed before tracking: do not mutate active menus.
                    const HMENU wrapper = state->wrapper;
                    SetLastError(ERROR_SUCCESS);
                    const UINT wrapped_result = action_api ?
                        xfx::track_themed_action_popup(wrapper, flags | TPM_RECURSE, 700, 100, owner, state->colors, nullptr, state->rows) :
                        xfx::track_themed_popup(wrapper, flags | TPM_RECURSE, 700, 100, owner, state->colors);
                    const DWORD error = GetLastError();
                    require(wrapped_result == 0 && error == ERROR_INVALID_PARAMETER, "wrapper sharing active child was not rejected");
                    assert_restored(active);
                }
                // Rejected sessions must not unhook or replace the outer session.
                require(filter(owner, WM_LBUTTONDOWN, 0, point) && filter(owner, WM_LBUTTONUP, 0, point), "outer session did not survive overlap rejection");
                break;
            }
            case Flow::Hover:
            case Flow::HoverNextRow:
                require(!filter(owner, WM_MOUSEMOVE, 0, point, MSGF_DIALOGBOX), "menu hook handled a different filter code");
                require(!filter(owner, WM_MOUSEMOVE, 0, point), "hover must preserve native mouse movement");
                require(tooltip_count(true) > 0, "hover did not surface action tooltip");
                check_tooltip_hit_passthrough();
                if (state->flow == Flow::HoverNextRow) {
                    RECT other{}; require(GetMenuItemRect(nullptr, state->menu, 1, &other) != FALSE, "second row rect missing");
                    const auto target = xfx::menu_action_detail::layout(other, dpi(owner));
                    const POINT next_point = centre(target.cells[0]);
                    require(filter(owner, WM_LBUTTONDOWN, 0, next_point) && filter(owner, WM_LBUTTONUP, 0, next_point), "tooltip prevented next-row action click");
                }
                break;
            case Flow::ParentMouse:
            case Flow::ParentKeyboard:
                require(false, "ordinary submenu flow bypassed its state machine"); break;
            case Flow::Capture:
                capture_popup(*state); EndMenu(); break;
            case Flow::Cancel:
                EndMenu(); break;
            }
        } catch (const std::exception& error) { state->failure = error.what(); EndMenu(); }
        return 0;
    }
    return DefWindowProcW(owner, message, wparam, lparam);
}
static void check_sessions() {
    struct CursorRestore {
        POINT point{};
        CursorRestore() { GetCursorPos(&point); }
        ~CursorRestore() { SetCursorPos(point.x, point.y); }
    } restore_cursor;
    WNDCLASSW type{}; type.lpfnWndProc = window_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"XFXInlineActionTest";
    RegisterClassW(&type);
    for (bool light : {false, true}) {
        State state; state.menu = make_action_menu(); state.rows = make_rows(state.menu); state.light = light;
        state.colors.surface = light ? RGB(245, 245, 245) : RGB(38, 38, 38);
        state.colors.text = state.colors.icon = light ? RGB(24, 24, 24) : RGB(232, 232, 232);
        state.colors.selected = light ? RGB(202, 226, 245) : RGB(42, 98, 138);
        state.colors.selected_text = state.colors.text; state.colors.disabled = RGB(130, 130, 130); state.colors.divider = RGB(70, 70, 70);
        Snapshot before; snapshot_menu(state.menu, before);
        HWND owner = CreateWindowW(type.lpszClassName, L"Action fixture", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, &state);
        require(owner != nullptr, "action test owner creation failed");
        try {
            ShowWindow(owner, SW_SHOW); SetForegroundWindow(owner); SetActiveWindow(owner);
            const auto run = [&]() {
                state.ticks = 0; state.parent_attempts = 0; state.failure.clear(); state.parent_unverified.clear(); state.commands = 0;
                // A menu reopening under the previous test's cursor can treat
                // the next parent click as a toggle. Begin outside popup bounds.
                if (state.popup_root) SetCursorPos(2, 2);
                state.native_key_count = 0;
                native_input_state = state.popup_root ? &state : nullptr;
                HHOOK observer = state.popup_root ? SetWindowsHookExW(WH_MSGFILTER, native_input_observer, nullptr, GetCurrentThreadId()) : nullptr;
                SetTimer(owner, 11, 35, nullptr);
                const UINT result = xfx::track_themed_action_popup(state.popup_root ? state.popup_root : state.menu,
                    flags, 100, 100, owner, state.colors, nullptr, state.rows);
                if (observer) UnhookWindowsHookEx(observer);
                native_input_state = nullptr;
                if (state.popup_root) {
                    std::cout << "OBSERVED ordinary parent WM_KEYDOWN:";
                    for (size_t i = 0; i < state.native_key_count; ++i) std::cout << ' ' << state.native_keys[i];
                    std::cout << " result=" << result << '\n';
                }
                if (IsWindow(owner)) KillTimer(owner, 11);
                require(state.failure.empty(), state.failure.c_str());
                if (!state.ticks) std::cerr << "ACTION LOOP DIAGNOSTIC flow=" << static_cast<int>(state.flow) << " slot=" << state.slot << " cell=" << state.cell << " result=" << result << " last_error=" << GetLastError() << '\n';
                require(state.ticks > 0, "real action menu loop never dispatched timer");
                require(state.commands == 0, "hook sent WM_COMMAND instead of returning after cleanup");
                assert_restored(before);
                require(tooltip_count(false) == 0, "session leaked a tooltip window");
                MSG after{}; after.message = WM_KEYDOWN; after.wParam = VK_RIGHT;
                require(!CallMsgFilterW(&after, MSGF_MENU), "menu action hook survived session cleanup");
                return result;
            };
            for (UINT slot = 0; slot < 9; ++slot) for (int cell = 0; cell < 3; ++cell) {
                state.flow = Flow::Mouse; state.slot = slot; state.cell = cell;
                TestState observed;
                const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
                const DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
                {
                    OwnerMessageObserver observer(owner, observed);
                    require(run() == (cell + 1) * 100u + slot, "real-loop 27 command mapping failed");
                }
                require(observed.actual_draw_calls > 0, "real action loop did not draw native ownerdraw rows");
                // User32 may retire a prior menu/hook object during this loop;
                // a decrease is cleanup, not a leak. Neither count may grow.
                if (slot || cell) require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi &&
                    GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= user, "action session leaked GDI/User objects");
            }
            state.slot = 0; state.cell = 0;
            for (auto flow : {Flow::Label, Flow::Drag, Flow::CrossRow, Flow::CrossButton, Flow::KeyboardCancel, Flow::Hover, Flow::Cancel}) {
                state.flow = flow; require(run() == 0, "inert/cancelled interaction selected an action");
                if (flow != Flow::Cancel) require(state.ticks >= 2, "inert interaction closed menu before explicit cancellation");
            }
            state.rows[0].actions[1].enabled = false;
            state.flow = Flow::Disabled; require(run() == 0 && state.ticks >= 2, "disabled cell activated/closed menu");
            state.flow = Flow::Keyboard; require(run() == 300, "keyboard enabled-cycle did not return expected clear action");
            state.rows[0].actions[1].enabled = true;
            state.rows[0].actions[0].enabled = false;
            state.flow = Flow::KeyboardExitFirstDisabled; require(run() == 0 && state.ticks >= 2, "Left exit depends on physical cell zero instead of first enabled cell");
            state.rows[0].actions[0].enabled = true;
            state.flow = Flow::HoverNextRow; require(run() == 101, "hover tooltip blocked next-row activation");
            state.wrapper = CreatePopupMenu(); require(state.wrapper != nullptr, "wrapper menu creation failed");
            require(AppendMenuW(state.wrapper, MF_POPUP, reinterpret_cast<UINT_PTR>(state.menu), L"共享子菜单") != FALSE, "cannot append shared menu");
            state.flow = Flow::Overlap;
            const UINT overlap_result = run();
            // Detach so destroying this wrapper cannot destroy the caller tree.
            RemoveMenu(state.wrapper, 0, MF_BYPOSITION); DestroyMenu(state.wrapper); state.wrapper = nullptr;
            require(overlap_result == 100, "overlap rejection damaged outer menu action");
            for (auto flow : {Flow::NestedPlain, Flow::NestedAction}) {
                state.flow = flow; state.nested_seen = false;
                require(run() == 0 && state.nested_result == (flow == Flow::NestedAction ? 777u : 0u), "nested session isolation failed");
            }
            const auto reject = [&](const xfx::MenuActionRows& rows, UINT input_flags = flags) {
                SetLastError(ERROR_SUCCESS);
                require(xfx::track_themed_action_popup(state.menu, input_flags, 100, 100, owner, state.colors, nullptr, rows) == 0 &&
                    GetLastError() == ERROR_INVALID_PARAMETER, "invalid flags/metadata was not rejected");
                assert_restored(before);
                require(tooltip_count(false) == 0, "failed prepare leaked tooltip");
            };
            reject(state.rows, TPM_RETURNCMD); reject(state.rows, TPM_NONOTIFY);
            auto invalid = state.rows; invalid.push_back(invalid[0]); reject(invalid);
            invalid = state.rows; invalid[0].position = 90; reject(invalid);
            invalid = state.rows; invalid[0].actions[1].command = invalid[0].actions[0].command; reject(invalid);
            invalid = state.rows; invalid[0].actions[0].command = 0; reject(invalid);
            invalid = state.rows; invalid[0].actions[0].command = 1000; reject(invalid);
            invalid = state.rows; invalid[0].actions[0].label.clear(); reject(invalid);
            invalid = state.rows; invalid[0].actions[0].icon = static_cast<xfx::MenuActionIcon>(90); reject(invalid);
            state.flow = Flow::Cancel; require(run() == 0, "valid session after failed prepare did not recover");
            state.rows[4].actions[1].enabled = state.rows[4].actions[2].enabled = false;
            state.flow = Flow::Capture; require(run() == 0, "screenshot capture unexpectedly activated action");
            state.rows[4].actions[1].enabled = state.rows[4].actions[2].enabled = true;
            state.popup_root = CreatePopupMenu(); require(state.popup_root != nullptr, "ordinary parent creation failed");
            require(AppendMenuW(state.popup_root, MF_STRING, 9000, L"应用当前效果") != FALSE &&
                AppendMenuW(state.popup_root, MF_POPUP, reinterpret_cast<UINT_PTR>(state.menu), L"快捷槽") != FALSE,
                "ordinary parent menu construction failed");
            Snapshot parent_before; snapshot_menu(state.popup_root, parent_before);
            for (auto flow : {Flow::ParentMouse, Flow::ParentKeyboard}) {
                ShowWindow(owner, SW_SHOW); SetForegroundWindow(owner); SetActiveWindow(owner); SetFocus(owner);
                state.flow = flow;
                const UINT selected = run();
                if (!state.parent_unverified.empty()) {
                    require(selected == 0, "unverified native opening unexpectedly executed a command");
                    std::cout << "UNVERIFIED " << (light ? "light" : "dark") << " ordinary child " <<
                        (flow == Flow::ParentMouse ? "mouse" : "keyboard") << ": " << state.parent_unverified << '\n';
                } else {
                    require(selected == (flow == Flow::ParentMouse ? 100u : 300u), "ordinary parent/child action returned wrong command");
                    std::cout << "PASS " << (light ? "light" : "dark") << " ordinary HMENU parent->child " <<
                        (flow == Flow::ParentMouse ? "mouse" : "Right/Enter filter") << " action mapping/full-tree restore\n";
                }
                assert_restored(parent_before);
            }
            RemoveMenu(state.popup_root, 1, MF_BYPOSITION); DestroyMenu(state.popup_root); state.popup_root = nullptr;
            state.flow = Flow::Destroy; require(run() == 0 && !IsWindow(owner), "owner destruction did not safely cancel action loop");
            std::cout << "PASS " << (light ? "light" : "dark") << " actual action loops: 27 mappings, inert labels/drags, disabled, keyboard filter, hover, nesting, invalid metadata, owner destruction/resources\n";
        } catch (...) {
            if (state.wrapper) { RemoveMenu(state.wrapper, 0, MF_BYPOSITION); DestroyMenu(state.wrapper); }
            if (state.popup_root) { RemoveMenu(state.popup_root, 1, MF_BYPOSITION); DestroyMenu(state.popup_root); }
            if (IsWindow(owner)) DestroyWindow(owner); DestroyMenu(state.menu); throw;
        }
        if (IsWindow(owner)) DestroyWindow(owner); DestroyMenu(state.menu);
    }
    std::cout << "UNVERIFIED action cells: UIA per-cell accessibility boundaries, physical mouse/keyboard delivery and AE-host rendering require manual host validation\n";
}
} // namespace action_tests
