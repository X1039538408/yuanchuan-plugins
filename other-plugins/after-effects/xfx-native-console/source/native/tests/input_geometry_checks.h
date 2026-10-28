#pragma once
#include <vector>

// Included after the standalone native UI harness helpers. These checks use
// the real control paint surface as well as WM_PRINTCLIENT so a preview-only
// correction cannot hide text/caret placement defects in the visible window.
struct InputPaintSample {
    RECT ink{};
    std::vector<COLORREF> pixels;
};

static TEXTMETRICW input_font_metrics(HWND control) {
    HDC dc = GetDC(control);
    const auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
    TEXTMETRICW metrics{};
    assert(GetTextMetricsW(dc, &metrics));
    SelectObject(dc, previous);
    ReleaseDC(control, dc);
    return metrics;
}

static InputPaintSample sample_input_paint(HWND control, COLORREF background, bool print_client) {
    RECT client{}; GetClientRect(control, &client);
    HDC source = GetDC(control);
    HDC memory = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    if (print_client) {
        SetDCBrushColor(memory, RGB(255, 0, 255));
        FillRect(memory, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SendMessageW(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    } else {
        // Keep this independent of WM_PRINTCLIENT: a hidden/redraw-disabled
        // native EDIT can still print correctly while its actual surface is
        // blank. The test palette must be visible and unobscured for this
        // real-paint assertion; do not turn it into a second print assertion.
        assert(IsWindowVisible(control));
        assert(BitBlt(memory, 0, 0, client.right, client.bottom, source, 0, 0, SRCCOPY));
    }
    const auto scan = [&]() {
        InputPaintSample result;
        result.ink = {client.right, client.bottom, 0, 0};
        for (int y = 0; y < client.bottom; ++y) {
            for (int x = 0; x < client.right; ++x) {
                const auto pixel = GetPixel(memory, x, y);
                result.pixels.push_back(pixel);
                if (pixel == background) continue;
                result.ink.left = (std::min)(result.ink.left, static_cast<LONG>(x));
                result.ink.top = (std::min)(result.ink.top, static_cast<LONG>(y));
                result.ink.right = (std::max)(result.ink.right, static_cast<LONG>(x + 1));
                result.ink.bottom = (std::max)(result.ink.bottom, static_cast<LONG>(y + 1));
            }
        }
        return result;
    };
    const auto sample = scan();
    SelectObject(memory, previous);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(control, source);
    return sample;
}

static void repaint_input(HWND control) {
    InvalidateRect(control, nullptr, FALSE);
    UpdateWindow(control);
    GdiFlush();
}

// Call after show_console() has shown and painted the console. The function
// restores the query and focus; it does not modify settings or shortcuts.
static void check_search_input_geometry(const fs::path& artifact_directory = {}) {
    const HWND focus = GetFocus();
    wchar_t previous_query[512]{};
    GetWindowTextW(g_state->search, previous_query, 512);
    const auto metrics = input_font_metrics(g_state->search);
    const auto edit = child_rect_in_parent(g_state->window, g_state->search);
    const auto surface = toolbar_layout(g_state->window).search_surface;
    assert(edit.bottom - edit.top == metrics.tmHeight);
    assert(std::abs((edit.top + edit.bottom) - (surface.top + surface.bottom)) <= 1);

    SetWindowTextW(g_state->search, L"Sample 中文");
    SetFocus(g_state->search);
    SendMessageW(g_state->search, EM_SETSEL, 0, 0);
    POINT caret{}; assert(GetCaretPos(&caret));
    const auto first_char = SendMessageW(g_state->search, EM_POSFROMCHAR, 0, 0);
    assert(caret.x == GET_X_LPARAM(first_char));
    assert(caret.y == GET_Y_LPARAM(first_char));
    assert(std::abs((edit.top + caret.y) * 2 + metrics.tmHeight -
        (surface.top + surface.bottom)) <= 2);

    // Verify both populated and empty controls use one geometry for display
    // and print. Focus changes must never add a second horizontal padding.
    HideCaret(g_state->search);
    for (const auto* query : {L"Sample 中文", L""}) {
        SetWindowTextW(g_state->search, query);
        RECT focused_ink{};
        for (bool focused : {true, false}) {
            SetFocus(focused ? g_state->search : g_state->results);
            if (focused) HideCaret(g_state->search);
            repaint_input(g_state->search);
            const auto actual = sample_input_paint(g_state->search, colors().search, false);
            const auto printed = sample_input_paint(g_state->search, colors().search, true);
            assert(actual.ink.right > actual.ink.left);
            assert(EqualRect(&actual.ink, &printed.ink));
            if (focused) focused_ink = actual.ink;
            else assert(actual.ink.left == focused_ink.left && actual.ink.top == focused_ink.top);
        }
    }

    SetFocus(g_state->results);
    const POINT blank{surface.left + (edit.left - surface.left) / 2,
        (surface.top + surface.bottom) / 2};
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(blank.x, blank.y));
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(blank.x, blank.y));
    assert(GetFocus() == g_state->search);
    SetWindowTextW(g_state->search, previous_query);
    if (IsWindow(focus)) SetFocus(focus);
    repaint_input(g_state->search);
    if (!artifact_directory.empty())
        save_test_preview(g_state->window, artifact_directory / L"input-search-alignment.png");
    std::cout << "PASS search text/caret geometry, real/print paint and full-field focus\n";
}

// Call with settings expanded and the hotkey capture control visible. It
// deliberately checks immediately after mouse input, before WM_PAINT can
// conceal native EDIT selection or horizontal-scroll drawing.
static void check_hotkey_input_geometry(const fs::path& artifact_directory = {}) {
    assert(IsWindowVisible(g_state->hotkey_edit));
    const HWND focus = GetFocus();
    wchar_t previous[128]{}; GetWindowTextW(g_state->hotkey_edit, previous, 128);
    SetWindowTextW(g_state->hotkey_edit, L"Ctrl+Alt+Shift+F23");
    SetFocus(g_state->hotkey_edit);
    repaint_input(g_state->hotkey_edit);
    const auto initial = sample_input_paint(g_state->hotkey_edit, colors().input, false);
    const auto printed = sample_input_paint(g_state->hotkey_edit, colors().input, true);
    assert(initial.ink.right > initial.ink.left);
    assert(EqualRect(&initial.ink, &printed.ink));
    RECT client{}; GetClientRect(g_state->hotkey_edit, &client);
    for (int click = 0; click < 30; ++click) {
        const int x = click % 2 ? client.right - 3 : 3;
        SendMessageW(g_state->hotkey_edit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, client.bottom / 2));
        SendMessageW(g_state->hotkey_edit, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(client.right - x, client.bottom / 2));
        SendMessageW(g_state->hotkey_edit, WM_LBUTTONUP, 0, MAKELPARAM(client.right - x, client.bottom / 2));
        GdiFlush();
        assert(GetFocus() == g_state->hotkey_edit);
        const auto clicked = sample_input_paint(g_state->hotkey_edit, colors().input, false);
        assert(clicked.pixels == initial.pixels);
    }
    SetWindowTextW(g_state->hotkey_edit, previous);
    if (IsWindow(focus)) SetFocus(focus);
    repaint_input(g_state->hotkey_edit);
    if (!artifact_directory.empty())
        save_test_preview(g_state->window, artifact_directory / L"input-hotkey-alignment.png");
    std::cout << "PASS hotkey paint and stable repeated click/drag geometry\n";
}
