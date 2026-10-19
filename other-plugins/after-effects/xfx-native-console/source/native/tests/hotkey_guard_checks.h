#pragma once

// Included after the native plugin and standalone UI pump helpers. Observe the
// actual close message while preventing a regression from destroying the test
// console before the second dispatch case can run.
static LRESULT CALLBACK count_hotkey_guard_closes(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR count_pointer) {
    if (message == WM_CLOSE) {
        ++*reinterpret_cast<unsigned int*>(count_pointer);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void check_hotkey_dispatch_guards() {
    assert(g_state && IsWindow(g_state->window) && IsWindow(g_state->hotkey_window));
    const HWND console = g_state->window;
    const HWND previous_focus = GetFocus();
    const int previous_focus_id = GetParent(previous_focus) == console ? GetDlgCtrlID(previous_focus) : 0;
    const bool previous_settings_visible = g_state->settings_visible;
    const auto previous_modal_depth = g_state->modal_depth;
    const auto previous_foreground_override = g_test_hotkey_foreground_process;
    const LRESULT previous_selection = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    unsigned int close_count = 0;
    constexpr UINT_PTR observer_id = 0x484B4755;
    assert(SetWindowSubclass(console, count_hotkey_guard_closes, observer_id,
        reinterpret_cast<DWORD_PTR>(&close_count)));

    // The harness creates a message-only STATIC window. Install the real entry
    // point temporarily so SendMessage exercises production hotkey dispatch.
    const auto previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_state->hotkey_window,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hotkey_window_proc)));
    assert(previous_proc);
    g_state->modal_depth = 0;
    if (!g_state->settings_visible)
        SendMessageW(console, WM_COMMAND, kPreferencesId, 0);
    ShowWindow(console, SW_SHOWNORMAL);
    SetActiveWindow(console);
    SetFocus(g_state->search);
    pump();
    // Windows may reject foreground activation in an unattended test run. Only
    // replace that external query; focus, dispatch, and close messages stay real.
    g_test_hotkey_foreground_process = GetCurrentProcessId();

    g_state->modal_depth = 1;
    SendMessageW(g_state->hotkey_window, WM_HOTKEY, kHotkeyId, 0);
    pump();
    const bool modal_dispatch_preserved_console = close_count == 0 && IsWindow(console);

    close_count = 0;
    g_state->modal_depth = 0;
    SetFocus(g_state->hotkey_edit);
    assert(GetFocus() == g_state->hotkey_edit);
    SendMessageW(g_state->hotkey_window, WM_HOTKEY, kHotkeyId, 0);
    pump();
    const bool capture_dispatch_preserved_console = close_count == 0 && IsWindow(console);

    close_count = 0;
    SetFocus(g_state->search);
    SendMessageW(g_state->hotkey_window, WM_HOTKEY, kHotkeyId, 0);
    pump();
    const bool ordinary_dispatch_requested_close = close_count == 1;

    close_count = 0;
    g_test_hotkey_foreground_process = GetCurrentProcessId() + 1;
    SendMessageW(g_state->hotkey_window, WM_HOTKEY, kHotkeyId, 0);
    pump();
    const bool foreign_process_dispatch_preserved_console = close_count == 0 && IsWindow(console);

    g_test_hotkey_foreground_process = previous_foreground_override;
    SetWindowLongPtrW(g_state->hotkey_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_proc));
    if (g_state->settings_visible != previous_settings_visible)
        SendMessageW(console, WM_COMMAND, kPreferencesId, 0);
    SendMessageW(g_state->results, LB_SETCURSEL, previous_selection, 0);
    g_state->modal_depth = previous_modal_depth;
    RemoveWindowSubclass(console, count_hotkey_guard_closes, observer_id);
    const HWND restore_focus = previous_focus_id ? GetDlgItem(console, previous_focus_id) : previous_focus;
    if (IsWindow(restore_focus)) SetFocus(restore_focus);
    pump();

    assert(modal_dispatch_preserved_console);
    assert(capture_dispatch_preserved_console);
    assert(ordinary_dispatch_requested_close);
    assert(foreign_process_dispatch_preserved_console);
    std::cout << "PASS hotkey dispatch guards modal dialogs, shortcut capture and foreground process\n";
}

static void check_result_hover_leave() {
    assert(g_state && IsWindow(g_state->window));
    const HWND previous_focus = GetFocus();
    const int previous_focus_id = GetParent(previous_focus) == g_state->window ? GetDlgCtrlID(previous_focus) : 0;
    wchar_t previous_query[512]{};
    GetWindowTextW(g_state->search, previous_query, 512);
    const LRESULT previous_filter = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    const LRESULT previous_selection = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    const auto previous_hover_action = g_state->result_hover_action;
    const int previous_hover_row = g_state->result_hover_row;
    POINT previous_cursor{};
    assert(GetCursorPos(&previous_cursor));
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);
    SetWindowTextW(g_state->search, L"g");
    populate_results();
    pump();
    assert(!shortcut_list_mode() && SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
    RECT row{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
    const POINT action{row.right - ui_px(kResultActionWidth) * 3 / 2, (row.top + row.bottom) / 2};
    POINT action_screen = action;
    ClientToScreen(g_state->results, &action_screen);
    assert(SetCursorPos(action_screen.x, action_screen.y));
    pump();
    SendMessageW(g_state->results, WM_MOUSEMOVE, 0, MAKELPARAM(action.x, action.y));
    UpdateWindow(g_state->results);
    assert(g_state->result_hover_action == ResultAction::Favorite && g_state->result_hover_row == 0);
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_QUERY, g_state->results, 0};
    assert(TrackMouseEvent(&tracking));
    // If Windows considers another window to own the cursor, TME_LEAVE posts
    // an immediate leave notification instead of retaining active tracking.
    // Inspect that notification before pumping; either outcome proves the
    // production mouse-move path requested real system leave tracking.
    MSG leave_message{};
    const bool leave_notification_queued = PeekMessageW(&leave_message, g_state->results,
        WM_MOUSELEAVE, WM_MOUSELEAVE, PM_NOREMOVE) != FALSE;
    const bool leave_tracking_requested = (tracking.dwFlags & TME_LEAVE) != 0 || leave_notification_queued;

    ValidateRect(g_state->results, nullptr);
    SendMessageW(g_state->results, WM_MOUSELEAVE, 0, 0);
    RECT update{};
    const bool old_hover_row_invalidated = GetUpdateRect(g_state->results, &update, FALSE) &&
        update.left < row.right && update.right > row.left && update.top < row.bottom && update.bottom > row.top;
    const bool hover_cleared = g_state->result_hover_action == ResultAction::None && g_state->result_hover_row == -1;

    TRACKMOUSEEVENT cancel{sizeof(cancel), TME_CANCEL | TME_LEAVE, g_state->results, 0};
    TrackMouseEvent(&cancel);
    SendMessageW(g_state->filter, CB_SETCURSEL, previous_filter, 0);
    SetWindowTextW(g_state->search, previous_query);
    populate_results();
    SendMessageW(g_state->results, LB_SETCURSEL, previous_selection, 0);
    g_state->result_hover_action = previous_hover_action;
    g_state->result_hover_row = previous_hover_row;
    assert(SetCursorPos(previous_cursor.x, previous_cursor.y));
    const HWND restore_focus = previous_focus_id ? GetDlgItem(g_state->window, previous_focus_id) : previous_focus;
    if (IsWindow(restore_focus)) SetFocus(restore_focus);
    pump();

    assert(leave_tracking_requested);
    assert(hover_cleared);
    assert(old_hover_row_invalidated);
    std::cout << "PASS result hover requests leave tracking and repaints the exited action\n";
}
