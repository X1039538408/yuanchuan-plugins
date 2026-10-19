#pragma once

// These tests cover the two ways a native owner-draw ComboBox paints its
// closed face: WM_PAINT and immediate ODS_COMBOBOXEDIT updates. Checking only
// a WM_PRINTCLIENT preview misses the latter after focus/selection changes.
static void check_combo_owner_draw_coordinates(HWND combo) {
    COMBOBOXINFO info{sizeof(info)};
    assert(GetComboBoxInfo(combo, &info));
    RECT client{}; assert(GetClientRect(combo, &client));
    const int margin = 5;
    const COLORREF guard = RGB(237, 13, 191);
    const RECT canvas{0, 0, client.right + margin * 2, client.bottom + margin * 2};
    HDC window_dc = GetDC(combo), memory = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, canvas.right, canvas.bottom);
    assert(window_dc && memory && bitmap);
    const auto old_bitmap = SelectObject(memory, bitmap);
    SetDCBrushColor(memory, guard);
    FillRect(memory, &canvas, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetViewportOrgEx(memory, margin, margin, nullptr);
    SendMessageW(combo, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    std::vector<COLORREF> expected;
    for (int y = info.rcItem.top; y < info.rcItem.bottom; ++y)
        for (int x = info.rcItem.left; x < info.rcItem.right; ++x)
            expected.push_back(GetPixel(memory, x, y));

    // Reuse an offset, clipped caller DC, as real owner drawing is allowed to
    // do. Only rcItem may change, while coordinates come from the full face.
    SetViewportOrgEx(memory, 0, 0, nullptr);
    SetDCBrushColor(memory, guard);
    FillRect(memory, &canvas, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetViewportOrgEx(memory, margin, margin, nullptr);
    HFONT ambient = static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
    const auto old_font = SelectObject(memory, ambient);
    IntersectClipRect(memory, 0, 0, client.right, client.bottom);
    const COLORREF old_text = SetTextColor(memory, RGB(17, 83, 149));
    const int old_mode = SetBkMode(memory, OPAQUE);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_COMBOBOX; draw.CtlID = GetDlgCtrlID(combo);
    draw.itemID = static_cast<UINT>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    draw.itemAction = ODA_DRAWENTIRE;
    draw.itemState = ODS_COMBOBOXEDIT | (GetFocus() == combo ? ODS_FOCUS : 0);
    draw.hwndItem = combo; draw.hDC = memory; draw.rcItem = info.rcItem;
    SendMessageW(g_state->window, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    assert(GetCurrentObject(memory, OBJ_FONT) == ambient);
    assert(GetTextColor(memory) == RGB(17, 83, 149));
    assert(GetBkMode(memory) == OPAQUE);
    POINT origin{}; assert(GetViewportOrgEx(memory, &origin));
    assert(origin.x == margin && origin.y == margin);
    size_t index = 0;
    for (int y = 0; y < client.bottom; ++y) {
        for (int x = 0; x < client.right; ++x) {
            const POINT point{x, y};
            const bool item = PtInRect(&info.rcItem, point) != FALSE;
            const COLORREF actual = GetPixel(memory, x, y);
            if (item) {
                if (actual != expected[index]) {
                    std::wcerr << L"Combo closed-face coordinate mismatch id=" << draw.CtlID <<
                        L" at " << x << L"," << y << L" actual=" << actual <<
                        L" expected=" << expected[index] << L"\n";
                }
                assert(actual == expected[index++]);
            } else assert(actual == guard);
        }
    }
    SelectObject(memory, old_font);
    SetTextColor(memory, old_text); SetBkMode(memory, old_mode);
    SelectObject(memory, old_bitmap); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(combo, window_dc);
}

static fs::path combo_geometry_diagnostic_directory;

static bool combo_visible_pixel_matches(COLORREF actual, COLORREF expected) {
    // Captured screen and WM_PRINTCLIENT ClearType edge pixels have a verified
    // RGB difference of one in dark mode and two in light mode. Three channel
    // levels cover that quantization; independent ink bounds below must still
    // match exactly, and the offscreen ODS coordinate check remains exact.
    return std::abs(static_cast<int>(GetRValue(actual)) - GetRValue(expected)) <= 3 &&
        std::abs(static_cast<int>(GetGValue(actual)) - GetGValue(expected)) <= 3 &&
        std::abs(static_cast<int>(GetBValue(actual)) - GetBValue(expected)) <= 3;
}

static RECT combo_text_ink_bounds(HWND combo, const InputPaintSample& sample) {
    RECT client{}; GetClientRect(combo, &client);
    RECT ink{client.right, client.bottom, 0, 0};
    const COLORREF text = IsWindowEnabled(combo) ? colors().text : colors().disabled;
    const int inset = MulDiv(5, GetDpiForWindow(combo), 96);
    const int arrow_width = MulDiv(20, GetDpiForWindow(combo), 96);
    // Use a broad solid-core band instead of requiring a particular ClearType
    // channel to land within three levels. Some font/host combinations never
    // produce an exact core pixel, while the surrounding surface remains far
    // from the text role; the independent ODS coordinate check stays exact.
    for (int y = inset; y < client.bottom - inset; ++y) {
        for (int x = inset; x < client.right - arrow_width; ++x) {
            const COLORREF pixel = sample.pixels[static_cast<size_t>(y) * client.right + x];
            const int nearest_channel = (std::min)({
                std::abs(static_cast<int>(GetRValue(pixel)) - GetRValue(text)),
                std::abs(static_cast<int>(GetGValue(pixel)) - GetGValue(text)),
                std::abs(static_cast<int>(GetBValue(pixel)) - GetBValue(text))});
            if (nearest_channel > 48) continue;
            ink.left = (std::min)(ink.left, static_cast<LONG>(x));
            ink.top = (std::min)(ink.top, static_cast<LONG>(y));
            ink.right = (std::max)(ink.right, static_cast<LONG>(x + 1));
            ink.bottom = (std::max)(ink.bottom, static_cast<LONG>(y + 1));
        }
    }
    assert(ink.right > ink.left && ink.bottom > ink.top);
    return ink;
}

static void save_combo_pixel_diagnostic(HWND combo, const InputPaintSample& sample, const wchar_t* kind) {
    RECT client{}; GetClientRect(combo, &client);
    Gdiplus::Bitmap image(client.right, client.bottom, PixelFormat32bppARGB);
    for (int y = 0; y < client.bottom; ++y) for (int x = 0; x < client.right; ++x) {
        const auto color = sample.pixels[static_cast<size_t>(y) * client.right + x];
        image.SetPixel(x, y, Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
    const auto directory = combo_geometry_diagnostic_directory.empty() ? fs::temp_directory_path() : combo_geometry_diagnostic_directory;
    const auto destination = directory / (L"combo-" + std::wstring(kind) + L"-" + std::to_wstring(GetDlgCtrlID(combo)) + L".png");
    assert(image.Save(destination.c_str(), &png) == Gdiplus::Ok);
    std::wcerr << L"Combo diagnostic image=" << destination << L"\n";
}

static void check_combo_visible_matches_print(HWND combo, const char* action) {
    GdiFlush();
    const auto visible = sample_input_paint(combo, colors().background, false);
    const auto printed = sample_input_paint(combo, colors().background, true);
    assert(visible.pixels.size() == printed.pixels.size());
    const auto visible_text = combo_text_ink_bounds(combo, visible);
    const auto printed_text = combo_text_ink_bounds(combo, printed);
    const bool bounds_match = EqualRect(&visible.ink, &printed.ink) && EqualRect(&visible_text, &printed_text);
    const bool matches = std::equal(visible.pixels.begin(), visible.pixels.end(), printed.pixels.begin(), combo_visible_pixel_matches);
    if (!matches || !bounds_match) {
        std::cerr << "Combo immediate visible paint differs after " << action <<
            " id=" << GetDlgCtrlID(combo) << '\n';
        RECT client{}, window{}; GetClientRect(combo, &client); GetWindowRect(combo, &window);
        const HWND foreground = GetForegroundWindow();
        DWORD foreground_pid = 0; GetWindowThreadProcessId(foreground, &foreground_pid);
        wchar_t foreground_class[128]{}; GetClassNameW(foreground, foreground_class, 128);
        std::wcerr << L"Combo HWND=" << combo << L" visible=" << IsWindowVisible(combo) <<
            L" window=" << window.left << L"," << window.top << L"," << window.right << L"," << window.bottom <<
            L" client=" << client.right << L"x" << client.bottom << L" dpi=" << GetDpiForWindow(combo) <<
            L" foreground-pid=" << foreground_pid << L" foreground-class=" << foreground_class << L"\n";
        std::wcerr << L"Combo text bounds actual=" << visible_text.left << L"," << visible_text.top << L"," << visible_text.right << L"," << visible_text.bottom <<
            L" expected=" << printed_text.left << L"," << printed_text.top << L"," << printed_text.right << L"," << printed_text.bottom << L"\n";
        size_t difference_count = 0;
        for (size_t index = 0; index < visible.pixels.size(); ++index) {
            if (combo_visible_pixel_matches(visible.pixels[index], printed.pixels[index])) continue;
            if (difference_count < 12) {
                const auto actual = visible.pixels[index], expected = printed.pixels[index];
                std::wcerr << L"Combo pixel difference at " << index % client.right << L"," << index / client.right <<
                    L" actual-rgb=" << static_cast<int>(GetRValue(actual)) << L"," << static_cast<int>(GetGValue(actual)) << L"," << static_cast<int>(GetBValue(actual)) <<
                    L" expected-rgb=" << static_cast<int>(GetRValue(expected)) << L"," << static_cast<int>(GetGValue(expected)) << L"," << static_cast<int>(GetBValue(expected)) << L"\n";
            }
            ++difference_count;
        }
        std::wcerr << L"Combo differing pixel count=" << difference_count << L" of " << visible.pixels.size() << L"\n";
        save_combo_pixel_diagnostic(combo, visible, L"visible");
        save_combo_pixel_diagnostic(combo, printed, L"print");
    }
    assert(matches);
    assert(bounds_match);
}

static void check_combo_target_sizes() {
    const auto filter = child_rect_in_parent(g_state->window, g_state->filter);
    const int dpi = GetDpiForWindow(g_state->window);
    assert(filter.top == MulDiv(10, dpi, 96));
    assert(filter.bottom - filter.top == MulDiv(36, dpi, 96));
    const auto capture = child_rect_in_parent(g_state->window, g_state->capture);
    assert(filter.top == capture.top && filter.bottom == capture.bottom);
    const auto theme = child_rect_in_parent(g_state->window, g_state->light_theme);
    const auto checkbox = child_rect_in_parent(g_state->window, g_state->mode_checkbox);
    if (theme.bottom - theme.top != MulDiv(28, dpi, 96))
        std::wcerr << L"Theme ComboBox actual height=" << theme.bottom - theme.top <<
            L" target=" << MulDiv(28, dpi, 96) << L" dpi=" << dpi << L"\n";
    assert(theme.bottom - theme.top == MulDiv(28, dpi, 96));
    RECT client{}; GetClientRect(g_state->window, &client);
    const bool narrow = MulDiv(client.right, 96, dpi) < 440;
    // In a wide settings row the checkbox and theme selector share a center.
    if (!narrow)
        assert(std::abs(theme.top + theme.bottom - checkbox.top - checkbox.bottom) <= 1);
}

static void check_combo_geometry(const fs::path& artifact_directory = {}) {
    combo_geometry_diagnostic_directory = artifact_directory;
    const HWND original_focus = GetFocus();
    const bool originally_expanded = g_state->settings_visible;
    const LRESULT selection = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    SetFocus(g_state->search);
    check_combo_owner_draw_coordinates(g_state->filter);
    repaint_input(g_state->filter);
    check_combo_visible_matches_print(g_state->filter, "initial paint");
    for (int repeat = 0; repeat < 3; ++repeat) {
        SetFocus(g_state->filter);
        check_combo_visible_matches_print(g_state->filter, "focus");
        SendMessageW(g_state->filter, CB_SETCURSEL, (selection + repeat + 1) % 5, 0);
        check_combo_visible_matches_print(g_state->filter, "CB_SETCURSEL");
        SendMessageW(g_state->filter, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(g_state->filter, WM_KEYUP, VK_DOWN, 0);
        check_combo_visible_matches_print(g_state->filter, "keyboard selection");
        SendMessageW(g_state->filter, CB_SHOWDROPDOWN, TRUE, 0); pump();
        SendMessageW(g_state->filter, CB_SHOWDROPDOWN, FALSE, 0);
        check_combo_visible_matches_print(g_state->filter, "dropdown close");
        // Native ComboBox may run a tracking loop inside mouse-down. Queue the
        // matching up/close first so that loop can always consume its release.
        assert(PostMessageW(g_state->filter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 12)));
        assert(PostMessageW(g_state->filter, WM_LBUTTONUP, 0, MAKELPARAM(10, 12)));
        assert(PostMessageW(g_state->filter, CB_SHOWDROPDOWN, FALSE, 0));
        pump();
        check_combo_visible_matches_print(g_state->filter, "mouse click");
        SetFocus(g_state->search);
        check_combo_visible_matches_print(g_state->filter, "focus loss");
    }
    SendMessageW(g_state->filter, CB_SETCURSEL, selection, 0);
    populate_results();
    if (!originally_expanded) { SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump(); }
    check_combo_owner_draw_coordinates(g_state->light_theme);
    check_combo_target_sizes();
    RECT window{}; GetWindowRect(g_state->window, &window);
    const int dpi = GetDpiForWindow(g_state->window);
    SendMessageW(g_state->window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&window));
    pump();
    check_combo_target_sizes();
    check_combo_owner_draw_coordinates(g_state->filter);
    check_combo_owner_draw_coordinates(g_state->light_theme);
    if (!artifact_directory.empty())
        save_test_preview(g_state->window, artifact_directory / L"combo-alignment.png");
    if (!originally_expanded) { SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump(); }
    if (IsWindow(original_focus)) SetFocus(original_focus);
    std::cout << "PASS combo immediate native/print coordinates, clipping, target sizes and DPI reflow\n";
}
