#pragma once

static unsigned apply_observed_calls = 0;
static BOOL CALLBACK count_apply_loading_windows(HWND window, LPARAM count_pointer) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (process == GetCurrentProcessId() && wcscmp(name, L"XFXLoadingSurface") == 0 &&
        IsWindowVisible(window)) ++*reinterpret_cast<unsigned*>(count_pointer);
    return TRUE;
}

static void observe_apply_script_boundary() {
    ++apply_observed_calls;
    assert(g_state && IsWindow(g_state->window));
    assert(!IsWindowVisible(g_state->window));
    unsigned loading_windows = 0;
    EnumWindows(count_apply_loading_windows, reinterpret_cast<LPARAM>(&loading_windows));
    assert(loading_windows == 0);
    assert(g_state->modal_depth > 0);
}

static void check_apply_lifecycle() {
    const auto original_settings = g_state->settings;
    const auto original_reply = g_test_script_reply;
    const auto original_observer = g_test_before_script;
    // Action icons share the window's right edge drag band. They must capture
    // the click themselves, and changing the action before release must cancel
    // instead of applying or toggling a different icon.
    show_console();
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
    SetWindowTextW(g_state->search, L"gaussian");
    populate_results();
    RECT action_row{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0,
        reinterpret_cast<LPARAM>(&action_row)) != LB_ERR);
    const LPARAM action_point = MAKELPARAM(action_row.right - 4, action_row.top + 4);
    SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, action_point);
    assert(g_state->result_click_pending && GetCapture() == g_state->results);
    assert(!g_state->drag_active);
    SendMessageW(g_state->results, WM_LBUTTONUP, 0,
        MAKELPARAM(action_row.left + 12, action_row.top + 4));
    assert(!g_state->result_click_pending && GetCapture() != g_state->results);
    g_state->settings = original_settings;
    xfx::SettingsStore::save(g_test_settings_path, original_settings);
    populate_results();
    for (int entry = 0; entry < 3; ++entry) {
        show_console();
        SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_state->search, L"gaussian");
        populate_results();
        assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
        g_test_script_reply = xfx::ScriptReply{true, false, 1, ""};
        apply_observed_calls = 0;
        g_test_before_script = observe_apply_script_boundary;
        if (entry == 2) g_test_result_draw_count = 0;
        if (entry == 0) SendMessageW(g_state->search, WM_KEYDOWN, VK_RETURN, 0);
        else if (entry == 1) {
            g_state->settings.slots[0] = L"ADBE Gaussian Blur 2";
            execute_slot(0);
        } else {
            // Exercise the real mouse transaction: the list must not paint a
            // native pressed frame between button down and ApplyPresentation.
            SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 4));
            assert(GetFocus() == g_state->results);
            SendMessageW(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(12, 4));
            assert(g_test_result_draw_count == 0);
        }
        pump();
        assert(apply_observed_calls == 1);
        assert(!g_state->window);
        const auto closed_rows = g_state->visible_slot_numbers;
        populate_results(); // A returned popup command must not refill destroyed controls.
        assert(g_state->visible_slot_numbers == closed_rows);
        assert(g_state->modal_depth == 0);
        assert(!g_state->ime_session_active);
        g_test_before_script = nullptr;
    }
    for (const bool partial : {false, true}) {
        show_console();
        SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
        SetWindowTextW(g_state->search, L"gaussian");
        populate_results();
        const HWND original_window = g_state->window;
        g_test_script_reply = xfx::ScriptReply{false, partial, partial ? 1u : 0u, "test apply failure"};
        apply_observed_calls = 0;
        g_test_before_script = observe_apply_script_boundary;
        SendMessageW(g_state->search, WM_KEYDOWN, VK_RETURN, 0);
        pump();
        assert(apply_observed_calls == 1);
        assert(g_state->window == original_window && IsWindowVisible(original_window));
        assert(g_state->modal_depth == 0);
        wchar_t status[256]{};
        GetWindowTextW(g_state->status, status, 256);
        assert(std::wstring(status).find(partial ? L"部分完成" : L"操作失败") != std::wstring::npos);
        g_test_before_script = nullptr;
        SendMessageW(original_window, WM_CLOSE, 0, 0);
    }
    g_test_before_script = original_observer;
    g_test_script_reply = original_reply;
    g_state->settings = original_settings;
    xfx::SettingsStore::save(g_test_settings_path, original_settings);
    show_console();
    std::cout << "PASS effect apply hides before AE, has no loading flash, closes all success entries and restores errors\n";
}
