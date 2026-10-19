#pragma once

// Included after the native plugin and test pump helpers. These checks exercise
// observable Win32 geometry, drawing bounds, and visibility rather than flags
// used internally by the implementation.
static void check_console_frame_geometry() {
    RECT window{}, client{};
    assert(GetWindowRect(g_state->window, &window));
    assert(GetClientRect(g_state->window, &client));
    assert(window.right - window.left == client.right - client.left);
    assert(window.bottom - window.top == client.bottom - client.top);
}

static bool paint_colors_near(COLORREF first, COLORREF second, int tolerance = 12) {
    return std::abs(static_cast<int>(GetRValue(first)) - GetRValue(second)) <= tolerance &&
        std::abs(static_cast<int>(GetGValue(first)) - GetGValue(second)) <= tolerance &&
        std::abs(static_cast<int>(GetBValue(first)) - GetBValue(second)) <= tolerance;
}

static void check_button_round_geometry(HWND button) {
    RECT client{}; assert(GetClientRect(button, &client));
    const int width = client.right, height = client.bottom;
    const int margin = 3;
    HDC screen = GetDC(button), memory = CreateCompatibleDC(screen);
    HBITMAP image = CreateCompatibleBitmap(screen, width + margin * 2, height + margin * 2);
    assert(screen && memory && image);
    const auto previous = SelectObject(memory, image);
    const RECT canvas{0, 0, width + margin * 2, height + margin * 2};
    const COLORREF guard = RGB(237, 13, 191);
    HBRUSH guard_brush = CreateSolidBrush(guard);
    FillRect(memory, &canvas, guard_brush); DeleteObject(guard_brush);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_BUTTON; draw.CtlID = GetDlgCtrlID(button);
    draw.itemAction = ODA_DRAWENTIRE; draw.hwndItem = button; draw.hDC = memory;
    draw.rcItem = {margin, margin, width + margin, height + margin};
    SendMessageW(g_state->window, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    // An owner-draw control may receive an offset rectangle on a shared DC.
    // Clearing its entire drawing surface destroys adjacent controls.
    assert(GetPixel(memory, 0, 0) == guard);
    assert(GetPixel(memory, canvas.right - 1, canvas.bottom - 1) == guard);

    const int corner = (std::min)(MulDiv(6, GetDpiForWindow(button), 96), (std::min)(width, height) / 3);
    const auto pixel = [&](int x, int y) { return GetPixel(memory, margin + x, margin + y); };
    const auto fail_geometry = [&](const wchar_t* reason, int x, int y, int reflected_x, int reflected_y) {
        const COLORREF first = pixel(x, y), reflected = pixel(reflected_x, reflected_y);
        const auto destination = fs::temp_directory_path() / (L"xfx-button-geometry-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetDlgCtrlID(button)) + L".png");
        {
            Gdiplus::Bitmap diagnostic(image, nullptr);
            CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
            diagnostic.Save(destination.c_str(), &png);
        }
        std::wcerr << L"Button corner mismatch: " << reason << L" id=" << GetDlgCtrlID(button) << L" width=" << width <<
            L" height=" << height << L" x=" << x << L" y=" << y << L" reflected_x=" << reflected_x <<
            L" reflected_y=" << reflected_y << L" RGB=" << static_cast<int>(GetRValue(first)) << L"," <<
            static_cast<int>(GetGValue(first)) << L"," << static_cast<int>(GetBValue(first)) << L" reflected_RGB=" <<
            static_cast<int>(GetRValue(reflected)) << L"," << static_cast<int>(GetGValue(reflected)) << L"," <<
            static_cast<int>(GetBValue(reflected)) << L" image=" << destination << L"\n";
        assert(false && "Button corner contour differs beyond antialiasing error");
    };
    // Different GDI+ arc directions have different AA intensity, especially
    // over the blue primary fill. Compare the actual contour against the
    // background instead, allowing one boundary pixel but no sustained shift.
    const auto foreground = [&](int x, int y) {
        return !paint_colors_near(pixel(x, y), colors().background, 3);
    };
    const auto row_gaps = [&](int y) {
        int left = 0, right = 0;
        while (left < corner && !foreground(left, y)) ++left;
        while (right < corner && !foreground(width - 1 - right, y)) ++right;
        return std::pair<int, int>{left, right};
    };
    const auto column_gaps = [&](int x) {
        int top = 0, bottom = 0;
        while (top < corner && !foreground(x, top)) ++top;
        while (bottom < corner && !foreground(x, height - 1 - bottom)) ++bottom;
        return std::pair<int, int>{top, bottom};
    };
    int horizontal_bias = 0, vertical_bias = 0;
    for (int edge = 0; edge < corner; ++edge) {
        const auto top = row_gaps(edge), bottom = row_gaps(height - 1 - edge);
        const auto left = column_gaps(edge), right = column_gaps(width - 1 - edge);
        if (std::abs(top.first - top.second) > 1 || std::abs(bottom.first - bottom.second) > 1 ||
            std::abs(top.first - bottom.first) > 1 || std::abs(top.second - bottom.second) > 1)
            fail_geometry(L"row contour", top.first, edge, width - 1 - top.second, height - 1 - edge);
        if (std::abs(left.first - left.second) > 1 || std::abs(right.first - right.second) > 1 ||
            std::abs(left.first - right.first) > 1 || std::abs(left.second - right.second) > 1)
            fail_geometry(L"column contour", edge, left.first, width - 1 - edge, height - 1 - left.second);
        horizontal_bias += top.first - top.second + bottom.first - bottom.second;
        vertical_bias += left.first - left.second + right.first - right.second;
    }
    const double horizontal_mean = static_cast<double>(horizontal_bias) / (corner * 2);
    const double vertical_mean = static_cast<double>(vertical_bias) / (corner * 2);
    if (std::abs(horizontal_mean) > 0.25 || std::abs(vertical_mean) > 0.25) {
        std::wcerr << L"Button contour signed mean: horizontal=" << horizontal_mean <<
            L" vertical=" << vertical_mean << L"\n";
        fail_geometry(L"sustained contour offset", 0, 0, width - 1, height - 1);
    }
    SelectObject(memory, previous); DeleteObject(image); DeleteDC(memory); ReleaseDC(button, screen);

    HRGN region = CreateRectRgn(0, 0, 0, 0);
    const int kind = GetWindowRgn(button, region);
    if (kind != ERROR) {
        // A native clip must not remove an extra row/column from one corner.
        for (int y = 0; y < corner; ++y) for (int x = 0; x < corner; ++x) {
            const bool inside = PtInRegion(region, x, y) != FALSE;
            assert(inside == (PtInRegion(region, width - 1 - x, y) != FALSE));
            assert(inside == (PtInRegion(region, x, height - 1 - y) != FALSE));
        }
    }
    DeleteObject(region);
}

static void check_settings_redraw_visibility() {
    assert(!g_state->settings_visible);
    HWND hidden = CreateWindowExW(0, L"STATIC", L"hidden-redraw-regression", WS_CHILD,
        0, 0, 1, 1, g_state->window, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(hidden && !(GetWindowLongPtrW(hidden, GWL_STYLE) & WS_VISIBLE));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
    assert(g_state->settings_visible);
    assert(!(GetWindowLongPtrW(hidden, GWL_STYLE) & WS_VISIBLE));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
    assert(!g_state->settings_visible);
    assert(!(GetWindowLongPtrW(hidden, GWL_STYLE) & WS_VISIBLE));
    assert(!IsWindowVisible(g_state->hotkey_edit));
    DestroyWindow(hidden);
}

static void check_console_paint_geometry() {
    check_console_frame_geometry();
    for (HWND button : {g_state->capture, g_state->slot, g_state->preferences,
        g_state->refresh, g_state->dictionary, g_state->hotkey_save,
        g_state->import_settings, g_state->export_settings})
        check_button_round_geometry(button);
    check_settings_redraw_visibility();
}
