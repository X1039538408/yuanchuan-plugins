#define XFX_CONSOLE_TEST
#include "../plugin/XFXNativeConsole.cpp"
#include <cassert>
#include <iostream>
#include <cstring>

static PF_App_Color simulated_host_color{7196,7196,7196};
static PF_Err fake_background(PF_App_Color* color) { *color=simulated_host_color;return 0; }
static SPErr fake_acquire(const char* name,int32,const void** suite) {
    static PFAppSuite6 app{};app.PF_AppGetBgColor=fake_background;
    if(std::strcmp(name,kPFAppSuite)!=0)return -1;
    *suite=&app;return 0;
}
static SPErr fake_release(const char*,int32) {return 0;}

static unsigned long long loading_pixels(HWND window) {
    RECT rect{};GetClientRect(window,&rect);
    HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);const auto old=SelectObject(memory,bitmap);
    assert(PrintWindow(window,memory,PW_CLIENTONLY));
    unsigned long long hash=1469598103934665603ULL;
    // Sample only the spinner and progress track, excluding status text.
    for(int y=5;y<33;++y) for(int x=5;x<(y<24?25:310);++x) {hash^=GetPixel(memory,x,y);hash*=1099511628211ULL;}
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);return hash;
}
static unsigned long long result_pixels_with_font(HFONT ambient_font) {
    RECT rect{};GetClientRect(g_state->results,&rect);rect.bottom=28;
    HDC dc=GetDC(g_state->results),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);
    const auto old_bitmap=SelectObject(memory,bitmap),old_font=SelectObject(memory,ambient_font);
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_LISTBOX;draw.CtlID=kResultsId;draw.itemID=0;
    draw.hwndItem=g_state->results;draw.hDC=memory;draw.rcItem=rect;draw.itemAction=ODA_DRAWENTIRE;
    SendMessageW(g_state->window,WM_DRAWITEM,kResultsId,reinterpret_cast<LPARAM>(&draw));
    assert(GetCurrentObject(memory,OBJ_FONT)==ambient_font);
    unsigned long long hash=1469598103934665603ULL;
    for(int y=0;y<rect.bottom;++y)for(int x=0;x<rect.right;++x){hash^=GetPixel(memory,x,y);hash*=1099511628211ULL;}
    SelectObject(memory,old_font);SelectObject(memory,old_bitmap);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(g_state->results,dc);
    return hash;
}

static void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}

static std::wstring dialog_title, dialog_text;
static UINT dialog_command;
static bool dialog_handled;
static HWND dialog_owner;
static bool dialog_needs_edit;
static bool dialog_use_keyboard;
static unsigned dialog_attempts;
static void CALLBACK dialog_timeout(HWND dialog, UINT, UINT_PTR timer, DWORD) {
    KillTimer(dialog, timer); dialog_handled = false; EndDialog(dialog, -1);
}
static void CALLBACK answer_test_dialog(HWND, UINT, UINT_PTR timer, DWORD) {
    if (++dialog_attempts > 100) { std::wcerr << L"Test dialog did not appear: " << dialog_title << L"\n"; std::abort(); }
    HWND dialog = nullptr;
    for (HWND candidate : {GetLastActivePopup(dialog_owner), GetLastActivePopup(g_state->window), GetActiveWindow()}) {
        DWORD process = 0; GetWindowThreadProcessId(candidate, &process);
        wchar_t title[128]{}; GetWindowTextW(candidate, title, 128);
        if (process == GetCurrentProcessId() && title == dialog_title) { dialog = candidate; break; }
    }
    if (!dialog) return;
    HWND edit = GetDlgItem(dialog, xfx::kTextInputEdit);
    if (dialog_needs_edit && !edit) return;
    if (edit) {
        const auto& palette = colors();
        HDC dc = CreateCompatibleDC(nullptr);
        const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(dialog, WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit)));
        assert(GetTextColor(dc) == palette.text && GetBkColor(dc) == palette.input);
        LOGBRUSH description{}; assert(GetObjectW(brush, sizeof(description), &description));
        assert(description.lbColor == palette.input);
        DeleteDC(dc);
    }
    KillTimer(nullptr, timer);
    if (edit) SetWindowTextW(edit, dialog_text.c_str());
    dialog_handled = true;
    if (dialog_use_keyboard && edit) {
        std::cerr << "KEYBOARD dialog focus=" << GetDlgCtrlID(GetFocus()) << " default=" <<
            LOWORD(SendMessageW(dialog, DM_GETDEFID, 0, 0)) << " edit-code=" <<
            SendMessageW(edit, WM_GETDLGCODE, VK_RETURN, 0) << " active=" << (GetActiveWindow() == dialog) << '\n';
        SetTimer(dialog, 9000, 2000, dialog_timeout);
        const auto key = dialog_command == IDOK ? VK_RETURN : VK_ESCAPE;
        PostMessageW(edit, WM_KEYDOWN, key, 0); PostMessageW(edit, WM_KEYUP, key, 0);
    } else PostMessageW(dialog, WM_COMMAND, dialog_command, 0);
}

static void prepare_dialog(const std::wstring& title, const std::wstring& text, UINT command = IDOK,
    HWND owner = nullptr, bool needs_edit = true, bool keyboard = false) {
    dialog_title = title; dialog_text = text; dialog_command = command; dialog_handled = false;
    dialog_owner = owner ? owner : g_state->window; dialog_needs_edit = needs_edit; dialog_attempts = 0;
    dialog_use_keyboard = keyboard;
    assert(SetTimer(nullptr, 0, 50, answer_test_dialog));
}

static void save_test_preview(HWND window, const fs::path& destination) {
    UpdateWindow(window); pump();
    RECT rect{}; GetWindowRect(window, &rect);
    HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
    HBITMAP image = CreateCompatibleBitmap(dc, rect.right - rect.left, rect.bottom - rect.top);
    HGDIOBJ previous = SelectObject(memory, image);
    assert(PrintWindow(window, memory, 0));
    {
        Gdiplus::Bitmap bitmap(image, nullptr);
        CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        assert(bitmap.Save(destination.c_str(), &png) == Gdiplus::Ok);
    }
    SelectObject(memory, previous); DeleteObject(image); DeleteDC(memory); ReleaseDC(window, dc);
}

static void pump_for(DWORD milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do { pump(); Sleep(1); } while (GetTickCount64() < deadline);
}

static RECT child_rect_in_parent(HWND parent, HWND child) {
    RECT rect{}; GetWindowRect(child, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ScreenToClient(parent, &top_left); ScreenToClient(parent, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
}

static BOOL CALLBACK count_visible_child_update_regions(HWND child, LPARAM count_pointer) {
    if (!IsWindowVisible(child)) return TRUE;
    RECT update{};
    if (GetUpdateRect(child, &update, FALSE)) {
        std::wcerr << L"Pending visible child paint id=" << GetDlgCtrlID(child) << L" class=";
        wchar_t class_name[128]{}; GetClassNameW(child, class_name, 128); std::wcerr << class_name << L"\n";
        ++*reinterpret_cast<int*>(count_pointer);
    }
    return TRUE;
}

static BOOL CALLBACK invalidate_child_for_reopen(HWND child, LPARAM) {
    InvalidateRect(child, nullptr, TRUE);
    return TRUE;
}

static int visible_children_with_pending_paint(HWND parent) {
    int count = 0;
    EnumChildWindows(parent, count_visible_child_update_regions, reinterpret_cast<LPARAM>(&count));
    return count;
}

// Inspect owner drawing, rather than internal animation state, so a timer that
// runs without changing the visible button cannot satisfy this regression.
static COLORREF button_background(HWND button) {
    RECT rect{}; GetClientRect(button, &rect);
    HDC dc = GetDC(button), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto previous = SelectObject(memory, bitmap);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_BUTTON; draw.CtlID = GetDlgCtrlID(button);
    draw.itemAction = ODA_DRAWENTIRE; draw.hwndItem = button;
    draw.hDC = memory; draw.rcItem = rect;
    SendMessageW(g_state->window, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    // Sample the opaque interior, clear of rounded corners, icons, and text.
    const auto color = GetPixel(memory, 4, rect.bottom / 2);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(button, dc);
    return color;
}

static COLORREF result_corner_pixel(bool selected) {
    RECT rect{}; GetClientRect(g_state->results, &rect); rect.bottom = 28;
    HDC dc = GetDC(g_state->results), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto previous = SelectObject(memory, bitmap);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_LISTBOX; draw.CtlID = kResultsId; draw.itemID = 0;
    draw.hwndItem = g_state->results; draw.hDC = memory; draw.rcItem = rect;
    draw.itemState = selected ? ODS_SELECTED : 0;
    SendMessageW(g_state->window, WM_DRAWITEM, kResultsId, reinterpret_cast<LPARAM>(&draw));
    const auto color = GetPixel(memory, 0, 0);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_state->results, dc);
    return color;
}

static COLORREF result_surface_pixel(int row) {
    RECT client{};
    GetClientRect(g_state->results, &client);
    HDC dc = GetDC(g_state->results), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    SendMessageW(g_state->results, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    RECT item{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, row,
        reinterpret_cast<LPARAM>(&item)) != LB_ERR);
    COLORREF color = GetPixel(memory, (std::min)(item.right - 4, item.left + 30),
        (item.top + item.bottom) / 2);
    // The old sample landed on the right-side action glyph at some font/DPI
    // combinations. Find a real selected-surface pixel while staying inside
    // the row, so the assertion tests selection paint rather than icon ink.
    for (int y = item.top + 3; y < item.bottom - 3; ++y) {
        for (int x = item.left + 3; x < item.right - 3; ++x) {
            if (GetPixel(memory, x, y) == colors().selected) {
                color = colors().selected;
                y = item.bottom;
                break;
            }
        }
    }
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_state->results, dc);
    return color;
}

static int search_hint_pixel_count() {
    RECT client{};
    GetClientRect(g_state->search, &client);
    HDC source = GetDC(g_state->search), memory = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    SetBkColor(memory, RGB(255, 0, 255));
    ExtTextOutW(memory, 0, 0, ETO_OPAQUE, &client, nullptr, 0, nullptr);
    SendMessageW(g_state->search, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    int count = 0;
    for (int y = 0; y < client.bottom; ++y)
        for (int x = 0; x < client.right; ++x)
            if (GetPixel(memory, x, y) == colors().hint) ++count;
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(g_state->search, source);
    return count;
}

static void check_button_hover(HWND button) {
    assert(IsWindow(button));
    // Hovering must settle immediately. A stationary cursor must not keep
    // repainting the button or change its pixels over time.
    g_test_button_hover_time = GetTickCount64();
    const auto advance = [button](ULONGLONG milliseconds) {
        *g_test_button_hover_time += milliseconds;
        SendMessageW(button, WM_TIMER, kButtonHoverTimerId, 0);
    };
    SendMessageW(button, WM_MOUSELEAVE, 0, 0); advance(220);
    const auto resting = button_background(button);
    SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(8, 8));
    const auto hovered = button_background(button);
    advance(40);
    assert(button_background(button) == hovered);
    advance(220);
    assert(button_background(button) == hovered);
    SendMessageW(button, WM_MOUSELEAVE, 0, 0); advance(220);
    assert(button_background(button) == resting);
    // Captured mouse messages outside the client must not re-enter hover.
    SendMessageW(button, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(8, 8)); advance(220);
    SetCapture(button);
    SendMessageW(button, WM_MOUSELEAVE, 0, 0);
    SendMessageW(button, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-4, 8)); advance(220);
    assert(button_background(button) == resting);
    ReleaseCapture();
    g_test_button_hover_time.reset();
}

#include "gallery_comparison_checks.h"
#include "capture_location_checks.h"
#include "input_geometry_checks.h"
#include "paint_geometry_checks.h"
#include "hotkey_guard_checks.h"
#include "apply_lifecycle_checks.h"
#include "combo_geometry_checks.h"
#include "ime_integration_checks.h"
#include "apply_click_checks.h"

int wmain(int argc, wchar_t** argv) {
    std::set_terminate([] {
        try {
            if (const auto error = std::current_exception()) std::rethrow_exception(error);
        } catch (const std::exception& error) {
            std::cerr << "Unhandled native UI test exception: " << error.what() << '\n';
        } catch (...) { std::cerr << "Unhandled native UI test exception\n"; }
        std::abort();
    });
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto directory = fs::temp_directory_path() / (L"xfx-ui-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    g_test_settings_path = directory / L"settings.ini";
    SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", (directory / L"thumbnail-cache").c_str());
    {
        std::ofstream(directory / L"XFXNativeConsole.aliases.default.tsv") <<
            "ADBE Glo2\tGlow\t默认发光\t\t\t\n";
        std::ofstream(directory / L"aliases.user.tsv") <<
            "\xEF\xBB\xBF\tGlow\t自定义辉光\t\t\t自定义\n";
        const auto aliases = load_aliases();
        assert(aliases.by_english_name.at("Glow").chinese_name == L"自定义辉光");
        assert(aliases.by_english_name.at("Glow").priority > aliases.by_match_name.at("ADBE Glo2").priority);
    }
    g_test_catalog = {
        {L"Gaussian Blur", "gaosimohu", "gsmh", "Gaussian Blur", "ADBE Gaussian Blur 2", L"效果", "ae"},
        {L"Glow", "faguang", "fg", "Glow", "ADBE Glo2", L"效果", "ae"}
    };
    // Exercise built-in translation when no external/default translation file is present.
    g_test_catalog[1].translated_name = L"发光";
    assert(item_display_label(g_test_catalog[1]) == L"Glow（发光）");
    xfx::Settings settings; settings.hotkey = L"Ctrl+Alt+Shift+F23";
    settings.custom_aliases[L"ADBE Gaussian Blur 2"] = {L"柔化专用"};
    xfx::SettingsStore::save(g_test_settings_path, settings);
    g_state = new PluginState();
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&g_state->gdiplus_token, &startup, nullptr) == Gdiplus::Ok);
    g_state->hotkey_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    show_console();
    assert(g_state->ime.test_target_window() == g_state->search);
    assert(g_state->initial_background_primed && g_test_initial_background_fill_count > 0);
    assert(g_state->initial_frame_painted && g_test_window_paint_count > 0);
    assert(visible_children_with_pending_paint(g_state->window) == 0);
    assert(g_test_initial_background_color == colors().background);
    const auto regression_artifacts = argc > 1 ? fs::path(argv[1]).parent_path() : fs::path{};
    if (argc > 2) {
        const std::wstring check = argv[2];
        if (check == L"--search-geometry") check_search_input_geometry(regression_artifacts);
        else if (check == L"--hotkey-geometry") {
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
            check_hotkey_input_geometry(regression_artifacts);
        } else if (check == L"--frame-geometry") check_console_frame_geometry();
        else if (check == L"--button-geometry") check_button_round_geometry(g_state->capture);
        else if (check == L"--redraw-visibility") check_settings_redraw_visibility();
        else if (check == L"--hotkey-guards") check_hotkey_dispatch_guards();
        else if (check == L"--hover-leave") check_result_hover_leave();
        else if (check == L"--apply-lifecycle") check_apply_lifecycle();
        else if (check == L"--combo-geometry") check_combo_geometry(regression_artifacts);
        else if (check == L"--combo-light") {
            set_theme_mode(xfx::ThemeMode::Light);
            SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump();
            SetWindowTextW(g_state->search, L"gaussian");
            check_combo_geometry(regression_artifacts);
        }
        else if (check == L"--ime-retry") check_ime_failure_retry();
        else if (check == L"--ime-pending-restore") check_ime_pending_restore();
        else if (check == L"--apply-click") check_result_click_is_deferred();
        else return 2;
        death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
        return 0;
    }
    check_search_input_geometry(regression_artifacts);
    check_console_paint_geometry();
    check_hotkey_dispatch_guards();
    check_result_hover_leave();
    {
        const RECT search = child_rect_in_parent(g_state->window, g_state->search);
        const RECT filter = child_rect_in_parent(g_state->window, g_state->filter);
        const RECT capture = child_rect_in_parent(g_state->window, g_state->capture);
        const RECT slot = child_rect_in_parent(g_state->window, g_state->slot);
        const RECT preferences = child_rect_in_parent(g_state->window, g_state->preferences);
        RECT client{}; GetClientRect(g_state->window, &client);
        assert(filter.top == 10 && filter.bottom - filter.top == 36);
        assert(capture.top == filter.top && capture.bottom == filter.bottom);
        assert(slot.top == filter.top && slot.bottom == filter.bottom);
        assert(preferences.top == filter.top && preferences.bottom == filter.bottom);
        assert(filter.left - search.right == 8);
        assert(capture.left - filter.right == 4);
        assert(slot.left - capture.right == 4 && preferences.left - slot.right == 4);
        const auto metrics = input_font_metrics(g_state->search);
        assert(search.bottom - search.top == metrics.tmHeight);
        assert(std::abs((search.top + search.bottom) - (filter.top + filter.bottom)) <= 1);
        assert(preferences.right == client.right - 8);
    }
    {
        g_test_search_hint_paint_count = 0;
        g_test_search_default_paint_count = 0;
        SetFocus(g_state->search);
        InvalidateRect(g_state->search, nullptr, FALSE); UpdateWindow(g_state->search);
        assert(g_test_search_default_paint_count == 0);
        assert(search_hint_pixel_count() > 0);
        assert(g_test_search_hint_paint_count > 0);
        SetFocus(g_state->results);
        InvalidateRect(g_state->search, nullptr, FALSE); UpdateWindow(g_state->search);
        assert(g_test_search_hint_paint_count > 0);
        SetFocus(g_state->search);
    }
    {
        HIMC context = ImmGetContext(g_state->search);
        DWORD original_conversion = 0, original_sentence = 0;
        if (context && ImmGetConversionStatus(context, &original_conversion, &original_sentence)) {
            ImmReleaseContext(g_state->search, context);
            SendMessageW(g_state->mode_checkbox, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(kModeId, BN_CLICKED), 0);
            assert(g_state->ime_session_active);
            assert(g_state->settings.use_english_mode);
            context = ImmGetContext(g_state->search);
            DWORD conversion = 0, sentence = 0;
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            SetFocus(g_state->results);
            context = ImmGetContext(g_state->search);
            assert(context && ImmSetConversionStatus(context, original_conversion, original_sentence));
            ImmReleaseContext(g_state->search, context);
            SetFocus(g_state->search);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            save_settings();
            assert(g_state->settings.use_english_mode &&
                xfx::SettingsStore::load(g_test_settings_path).use_english_mode);
            SendMessageW(g_state->mode_checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(kModeId, BN_CLICKED), 0);
            save_settings();
            assert(!g_state->settings.use_english_mode &&
                !xfx::SettingsStore::load(g_test_settings_path).use_english_mode);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(conversion == original_conversion && sentence == original_sentence);
            ImmReleaseContext(g_state->search, context);
            SendMessageW(g_state->mode_checkbox, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(kModeId, BN_CLICKED), 0);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(!ImmGetOpenStatus(context));
            ImmReleaseContext(g_state->search, context);
            save_and_restore();
            assert(!g_state->ime_session_active);
            context = ImmGetContext(g_state->search);
            assert(context && ImmGetConversionStatus(context, &conversion, &sentence));
            assert(conversion == original_conversion && sentence == original_sentence);
            ImmReleaseContext(g_state->search, context);
            SendMessageW(g_state->mode_checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(kModeId, BN_CLICKED), 0);
            std::cout << "PASS English input mode really closes the IME and restores its original conversion state\n";
        } else {
            if (context) ImmReleaseContext(g_state->search, context);
            std::cout << "SKIP IME conversion round trip: active test input context does not expose conversion status\n";
        }
    }
    WNDCLASSEXW console_class{sizeof(console_class)};
    assert(GetClassInfoExW(GetModuleHandleW(nullptr), L"XFXNativeConsoleWindow", &console_class));
    assert(console_class.hbrBackground == nullptr);
    {
        SetWindowTextW(g_state->search,L"g");
        HFONT sans=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Arial");
        HFONT serif=CreateFontW(-20,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Cambria");
        assert(result_pixels_with_font(sans)==result_pixels_with_font(serif));
        DeleteObject(sans);DeleteObject(serif);SetWindowTextW(g_state->search,L"");
        std::cout<<"PASS result owner drawing uses its UI font and restores caller font\n";
    }
    {
        assert(g_state->settings.theme_mode==xfx::ThemeMode::Follow);
        SPBasicSuite basic{};basic.AcquireSuite=fake_acquire;basic.ReleaseSuite=fake_release;
        g_basic=&basic;refresh_host_theme(true);assert(colors().background==RGB(28,28,28));
        simulated_host_color={59110,59110,59110};g_state->last_theme_check=0;refresh_host_theme();
        assert(colors().background==RGB(230,230,230) && effective_light_theme());
        set_theme_mode(xfx::ThemeMode::Dark);simulated_host_color={65535,65535,65535};refresh_host_theme(true);
        assert(colors().background==RGB(19,19,19));
        set_theme_mode(xfx::ThemeMode::Follow);g_basic=nullptr;refresh_host_theme(true);
        assert(colors().background==RGB(32,32,32));
        HWND loading_window=nullptr;
        {
            xfx::LoadingSurface loading({40,40,360,74},colors(),L"正在测试…",1,false);
            loading_window=loading.window();assert(IsWindow(loading_window));
            const auto first=loading_pixels(loading_window);
            Sleep(250); // AE/UI thread does not pump: the loading surface must still animate.
            assert(loading_pixels(loading_window)!=first);
            if(argc>1) save_test_preview(loading_window,fs::path(argv[1]).parent_path()/L"loading-preview.png");
        }
        assert(!IsWindow(loading_window));
        const bool enabled=IsWindowEnabled(g_state->capture)!=FALSE;
        try {BusyFeedback feedback(L"测试异常清理…");assert(!IsWindowEnabled(g_state->capture));throw 1;}catch(int){}
        assert((IsWindowEnabled(g_state->capture)!=FALSE)==enabled);
        std::cout<<"PASS host RGB changes, manual override, fallback, independent loading pixels and exception cleanup\n";
    }
    // Owner drawing must change after a real pointer press, then return to rest.
    {
        const auto before = button_background(g_state->capture);
        g_test_button_hover_time = GetTickCount64();
        SendMessageW(g_state->capture, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 3));
        *g_test_button_hover_time += 180;
        SendMessageW(g_state->capture, WM_TIMER, kButtonHoverTimerId, 0);
        assert(button_background(g_state->capture) != before);
        SendMessageW(g_state->capture, WM_CANCELMODE, 0, 0);
        *g_test_button_hover_time += 800;
        SendMessageW(g_state->capture, WM_TIMER, kButtonHoverTimerId, 0);
        assert(button_background(g_state->capture) == before);
        g_test_button_hover_time.reset();
    }
    {
        RECT before{},after{};GetWindowRect(g_state->capture,&before);
        g_test_button_hover_time=GetTickCount64();
        for(int click=0;click<20;++click) {
            SendMessageW(g_state->capture,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,9));
            SendMessageW(g_state->capture,WM_CANCELMODE,0,0);
        }
        assert(g_state->button_hovers.at(g_state->capture).ripples.size()==12);
        assert(g_state->button_hovers.at(g_state->capture).ripples.back().origin.x==4);
        *g_test_button_hover_time+=701;SendMessageW(g_state->capture,WM_TIMER,kButtonHoverTimerId,0);
        assert(g_state->button_hovers.at(g_state->capture).ripples.empty());
        SendMessageW(g_state->capture,WM_KEYDOWN,VK_RETURN,0);
        RECT client{};GetClientRect(g_state->capture,&client);
        assert(g_state->button_hovers.at(g_state->capture).ripples.back().origin.x==client.right/2);
        EnableWindow(g_state->capture,FALSE);SendMessageW(g_state->capture,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,9));
        assert(g_state->button_hovers.at(g_state->capture).ripples.empty());EnableWindow(g_state->capture,TRUE);
        GetWindowRect(g_state->capture,&after);assert(EqualRect(&before,&after));g_test_button_hover_time.reset();
        set_status(L"配置已导入",true);pump_for(2100);
        wchar_t status[128]{};GetWindowTextW(g_state->status,status,128);assert(std::wstring(status)==L"就绪");
        set_status(L"操作失败：测试原因");pump_for(2100);GetWindowTextW(g_state->status,status,128);
        assert(std::wstring(status)==L"操作失败：测试原因");
        std::cout<<"PASS rapid/off-center/keyboard/disabled ripples, stable bounds, success expiry and persistent errors\n";
    }
    {
        auto& hover = g_state->button_hovers.at(g_state->preferences);
        g_test_button_hover_time = GetTickCount64();
        SendMessageW(g_state->preferences, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(18, 18));
        assert(hover.ripples.empty());
        SendMessageW(g_state->preferences, WM_CANCELMODE, 0, 0);
        g_test_button_hover_time.reset();
    }
    assert(!IsWindow(GetDlgItem(g_state->window, kApplyId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kFavoriteId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kGalleryId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kCaptureLocationId)));
    assert(!IsWindow(GetDlgItem(g_state->window, kThumbnailCacheClearId)));
    check_native_capture_unavailable();
    assert(SendMessageW(g_state->tooltip, TTM_GETTOOLCOUNT, 0, 0) >= 14);
    {
        TOOLINFOW capture_tool{TTTOOLINFOW_V1_SIZE};
        capture_tool.uFlags = TTF_IDISHWND;
        capture_tool.hwnd = g_state->window;
        capture_tool.uId = reinterpret_cast<UINT_PTR>(g_state->capture);
        assert(SendMessageW(g_state->tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&capture_tool)));
        assert((capture_tool.uFlags & (TTF_IDISHWND | TTF_SUBCLASS)) == (TTF_IDISHWND | TTF_SUBCLASS));
        POINT original_cursor{}; GetCursorPos(&original_cursor);
        SetWindowPos(g_state->window, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetForegroundWindow(g_state->window);
        SetCursorPos(60, 60); pump();
        POINT hover{14, 14}; ClientToScreen(g_state->capture, &hover);
        SetCursorPos(hover.x, hover.y);
        SendMessageW(g_state->capture, WM_MOUSEMOVE, 0, MAKELPARAM(14, 14));
        const auto deadline = GetTickCount64() + 5000;
        while (!IsWindowVisible(g_state->tooltip) && GetTickCount64() < deadline) {
            POINT actual{}; GetCursorPos(&actual);
            if (actual.x != hover.x || actual.y != hover.y) {
                SetCursorPos(hover.x, hover.y);
                SendMessageW(g_state->capture, WM_MOUSEMOVE, 0, MAKELPARAM(14, 14));
            }
            pump(); Sleep(10);
        }
        if (!IsWindowVisible(g_state->tooltip)) {
            POINT actual{}; GetCursorPos(&actual);
            std::cerr << "hover parent=" << g_state->window << "/" << IsWindowVisible(g_state->window) << " hit=" << WindowFromPoint(hover) << " button=" << g_state->capture << " cursor=" << actual.x << "," << actual.y << " expected=" << hover.x << "," << hover.y << " foreground=" << GetForegroundWindow() << '\n';
        }
        assert(IsWindowVisible(g_state->tooltip));
        assert(button_background(g_state->capture) == colors().button_pressed);
        TOOLINFOW current{TTTOOLINFOW_V1_SIZE};
        assert(SendMessageW(g_state->tooltip, TTM_GETCURRENTTOOLW, 0, reinterpret_cast<LPARAM>(&current)));
        assert(current.uId == reinterpret_cast<UINT_PTR>(g_state->capture));
        if (argc > 1) save_test_preview(g_state->tooltip, fs::path(argv[1]).parent_path() / L"hover-snapshot-tooltip.png");
        SetCursorPos(original_cursor.x, original_cursor.y);
        SendMessageW(g_state->tooltip, TTM_POP, 0, 0);
    }
    SetWindowTextW(g_state->search, L"g");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    {
        std::vector<xfx::CatalogItem> many;
        for (int i = 0; i < 12; ++i) {
            auto item = g_test_catalog[0]; item.display_name = L"fixture " + std::to_wstring(i);
            item.match_name = "fixture-" + std::to_string(i); many.push_back(std::move(item));
        }
        g_state->engine = xfx::SearchEngine(many);
        SetWindowTextW(g_state->search, L"fixture");
        assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 12);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
        SendMessageW(g_state->results, LB_SETTOPINDEX, 4, 0);
        assert(SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0) == 4);
        g_state->engine = xfx::SearchEngine(g_state->catalog);
        SetWindowTextW(g_state->search, L"g");
    }
    {
        RECT row{};
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        const int right = row.right;
        assert(result_action_at_point({right - 70, row.top + 8}) == ResultAction::Pin);
        assert(result_action_at_point({right - 46, row.top + 8}) == ResultAction::Favorite);
        assert(result_action_at_point({right - 22, row.top + 8}) == ResultAction::Shortcut);
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0, MAKELPARAM(right - 70, row.top + 8));
        assert(g_state->result_tooltip == L"置顶排序 / 取消置顶排序");
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(right - 70, row.top + 8));
        assert(g_state->settings.pinned.size() == 1);
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(row.right - 46, row.top + 8));
        assert(g_state->settings.favorites.size() == 1);
        assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row)) != LB_ERR);
        results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(row.right - 22, row.top + 8));
        assert(g_state->settings.slots[0] == L"ADBE Gaussian Blur 2");
        g_state->settings.pinned.clear();
        g_state->settings.favorites.clear();
        g_state->settings.slots.fill(L"");
        xfx::SettingsStore::save(g_test_settings_path, g_state->settings);
        populate_results();
    }
    SetWindowTextW(g_state->search, L"");
    assert(!IsWindow(GetDlgItem(g_state->window, 1034)));
    assert(!IsWindow(GetDlgItem(g_state->window, 1035)));
    assert(!IsWindow(GetDlgItem(g_state->window, 1037))); // Full resolution is always on; no switch.
    assert(!IsWindow(GetDlgItem(g_state->window, 1038)));
    check_full_resolution_setting();
    {
        HMENU shortcut_menu = CreatePopupMenu();
        append_slot_menu(shortcut_menu, false);
        assert(GetMenuItemCount(shortcut_menu) == 9);
        MENUITEMINFO first_column_break{sizeof(first_column_break)};
        first_column_break.fMask = MIIM_FTYPE;
        MENUITEMINFO second_column_break{sizeof(second_column_break)};
        second_column_break.fMask = MIIM_FTYPE;
        assert(GetMenuItemInfoW(shortcut_menu, 3, TRUE, &first_column_break));
        assert(GetMenuItemInfoW(shortcut_menu, 6, TRUE, &second_column_break));
        assert((first_column_break.fType & MFT_MENUBREAK) == 0);
        assert((second_column_break.fType & MFT_MENUBREAK) == 0);
        DestroyMenu(shortcut_menu);
    }
    {
        const auto original = capture_directory();
        const auto chosen = directory / L"中文快照🎬";
        g_state->settings.capture_directory = chosen.wstring();
        assert(capture_directory() == chosen);
        g_state->settings.capture_directory.clear();
        assert(capture_directory() == original);
    }
    assert(IsWindow(g_state->window));
    RECT drag_window_rect{}; GetWindowRect(g_state->window, &drag_window_rect);
    // The window edge should use the native caption drag path.
    assert(SendMessageW(g_state->window, WM_NCHITTEST, 0,
        MAKELPARAM(drag_window_rect.left + 1, drag_window_rect.top + 1)) == HTCAPTION);
    SendMessageW(g_state->results, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(250, 42));
    assert(!g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    assert(g_state->drag_active && !g_state->drag_pending);
    ReleaseCapture();
    assert(!g_state->drag_active && !g_state->drag_pending);
    {
        RECT original{}; GetWindowRect(g_state->window, &original);
        SetWindowPos(g_state->window, nullptr, 180, 180, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetCursorPos(182, 182);
        g_test_drag_button_held = true;
        SendMessageW(g_state->window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
        assert(g_state->drag_active && !g_state->drag_pending);
        SendMessageW(g_state->window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(62, 62));
        RECT moved{}; GetWindowRect(g_state->window, &moved);
        // Removing the system border also removes its one-pixel coordinate offset.
        assert(moved.left == 240 && moved.top == 240);
        SendMessageW(g_state->window, WM_LBUTTONUP, 0, MAKELPARAM(62, 62));
        assert(!g_state->drag_active && !g_state->drag_pending);
        g_test_drag_button_held.reset();
        SetWindowPos(g_state->window, nullptr, original.left, original.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
    }
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    const LRESULT top_before_wheel = SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0);
    for (int wheel = 0; wheel < 8; ++wheel)
        results_list_proc(g_state->results, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    assert(SendMessageW(g_state->results, LB_GETTOPINDEX, 0, 0) >= top_before_wheel);
    RECT status_client{}; GetClientRect(g_state->status, &status_client);
    SendMessageW(g_state->status, WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(2, static_cast<LONG>((std::max)(0L, status_client.bottom - 1L))));
    assert(g_state->drag_active && !g_state->drag_pending);
    SendMessageW(g_state->window, WM_LBUTTONUP, 0, 0);
    assert(!g_state->drag_active && !g_state->drag_pending);
    SetWindowTextW(g_state->search, L"g");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    g_state->settings.pinned = {L"ADBE Glo2"};
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    {
        const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, 0, 0);
        assert(data != LB_ERR && data != 0);
        assert(item_key(*reinterpret_cast<const xfx::CatalogItem*>(data)) == L"ADBE Glo2");
    }
    toggle_pinned(L"ADBE Glo2");
    assert(g_state->settings.pinned.empty());
    toggle_pinned(L"ADBE Glo2");
    assert(g_state->settings.pinned == std::vector<std::wstring>{L"ADBE Glo2"});
    {
        const LPARAM second_row = MAKELPARAM(12, kResultHeight + 4);
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0, second_row);
        assert(SendMessageW(g_state->results, LB_GETCURSEL, 0, 0) == 1);
        pump();
        assert(result_surface_pixel(1) == colors().selected);
    }
    g_test_result_draw_count = 0;
    g_test_result_erase_count = 0;
    for (int move = 0; move < 8; ++move) {
        const int row = move % 2;
        results_list_proc(g_state->results, WM_MOUSEMOVE, 0,
            MAKELPARAM(12, row * kResultHeight + 4));
        pump();
    }
    std::cout << "result hover draws=" << g_test_result_draw_count
              << " erases=" << g_test_result_erase_count
              << " buffered-paints=" << g_test_result_buffered_paint_count << '\n';
    assert(g_test_result_erase_count == 0);
    assert(g_test_result_buffered_paint_count > 0);
    g_state->settings.pinned.clear();
    populate_results();
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    RECT compact{}; GetWindowRect(g_state->window, &compact);
    assert(compact.bottom - compact.top < 200);
    SetWindowTextW(g_state->search, L"rhzy");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    save_item_aliases(L"ADBE Gaussian Blur 2", {});
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 0);
    prepare_dialog(L"编辑搜索别名", L"  软化\r\n柔焦\r\n柔焦 \r\n");
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->modal_depth == 0);
    assert(g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2") ==
        (std::vector<std::wstring>{L"软化", L"柔焦"}));
    SetWindowTextW(g_state->search, L"rj");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    prepare_dialog(L"编辑搜索别名", L"取消的别名", IDCANCEL);
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2").size() == 2);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    bool alias_save_failed = false;
    try { save_item_aliases(L"ADBE Gaussian Blur 2", {L"写入失败"}); }
    catch (...) { alias_save_failed = true; }
    assert(alias_save_failed && g_state->settings.custom_aliases.at(L"ADBE Gaussian Blur 2")[0] == L"软化");
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    save_item_aliases(L"ADBE Gaussian Blur 2", {});
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 0);
    SetWindowTextW(g_state->search, L"gsmh");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    assign_slot(0);
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9 && g_state->visible_slot_numbers[0] == 0);
    for (int slot = 0; slot < 9; ++slot) assert(g_state->visible_slot_numbers[static_cast<size_t>(slot)] == slot);
    g_state->settings.slots[1] = L"ADBE Glo2";
    SetWindowTextW(g_state->search, L"");
    populate_results();
    const LONG_PTR shortcut_style = GetWindowLongPtrW(g_state->results, GWL_STYLE);
    assert((shortcut_style & LBS_MULTICOLUMN) != 0 && (shortcut_style & WS_HSCROLL) == 0);
    RECT shortcut_bounds{}; GetWindowRect(g_state->results, &shortcut_bounds);
    assert(shortcut_bounds.bottom - shortcut_bounds.top == kShortcutHeight);
    RECT first_slot_rect{}, second_slot_rect{};
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&first_slot_rect)) != LB_ERR);
    assert(SendMessageW(g_state->results, LB_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&second_slot_rect)) != LB_ERR);
    assert(first_slot_rect.right - first_slot_rect.left >= 50);
    {
        RECT normal{};GetWindowRect(g_state->window,&normal);
        SetWindowPos(g_state->window,nullptr,0,0,360,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);pump();
        RECT last{},client{};GetClientRect(g_state->results,&client);
        assert(SendMessageW(g_state->results,LB_GETITEMRECT,8,reinterpret_cast<LPARAM>(&last))!=LB_ERR);
        assert(last.right<=client.right && last.top==0);
        g_test_window_erase_count = 0;
        SendMessageW(g_state->window,WM_COMMAND,kPreferencesId,0);pump_for(240);
        assert(g_test_window_erase_count == 0);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
        if(argc>1)save_test_preview(g_state->window,fs::path(argv[1]).parent_path()/L"narrow-preview.png");
        g_test_window_erase_count = 0;
        SendMessageW(g_state->window,WM_COMMAND,kPreferencesId,0);pump_for(240);
        assert(g_test_window_erase_count == 0);
        assert((GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0);
        SetWindowPos(g_state->window,nullptr,0,0,normal.right-normal.left,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);pump();
    }
    assert(second_slot_rect.left > first_slot_rect.left);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"shortcut-preview.png");
    clear_slot(1);
    assert(slot_display_label(0) == L"Gaussian Blur（高斯模糊）");
    RECT slot_screen{}; GetWindowRect(g_state->results, &slot_screen);
    g_test_popup_command = 30;
    prepare_dialog(L"编辑快捷槽备注", L"柔化一键");
    show_result_menu(g_state->window, MAKELPARAM(slot_screen.left + 10, slot_screen.top + 10));
    assert(dialog_handled && slot_display_label(0) == L"柔化一键");
    assert(xfx::SettingsStore::load(g_test_settings_path).slot_labels[0] == L"柔化一键");
    prepare_dialog(L"编辑快捷槽备注", L"忽略改名", IDCANCEL);
    edit_slot_label(0);
    assert(dialog_handled && slot_display_label(0) == L"柔化一键");
    SetWindowTextW(g_state->search, L"");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 9);
    assert(g_state->visible_slot_numbers[0] == 0);
    BYTE saved_keys[256]{}, digit_keys[256]{};
    GetKeyboardState(saved_keys); SetKeyboardState(digit_keys);
    search_edit_proc(g_state->search, WM_KEYDOWN, '1', 0);
    search_edit_proc(g_state->search, WM_CHAR, '1', 0);
    assert(GetWindowTextLengthW(g_state->search) == 0);
    wchar_t digit_status[256]{}; GetWindowTextW(g_state->status, digit_status, 256);
    assert(std::wstring(digit_status).find(L"AE host unavailable") != std::wstring::npos);
    SetKeyboardState(saved_keys);
    clear_slot(0);
    assert(slot_display_label(0) == L"空" && g_state->settings.slot_labels[0].empty());
    SetWindowTextW(g_state->search, L"");
    SetWindowTextW(g_state->search, L"gsmh");
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SetWindowTextW(g_state->search, L"");
    SetWindowTextW(g_state->search, L"g");
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 2);
    g_state->settings.favorites = {L"ADBE Glo2"};
    SendMessageW(g_state->filter, CB_SETCURSEL, 3, 0); populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) == 1);
    SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0); populate_results();
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    bool escaped = false;
    BYTE original_keys[256]{}, keys[256]{};
    GetKeyboardState(original_keys); memcpy(keys, original_keys, sizeof(keys)); keys[VK_CONTROL] = 0x80;
    SetKeyboardState(keys);
    const auto previous_hotkey = g_state->settings.hotkey;
    const auto previous_hotkey_spec = g_state->hotkey_spec;
    try { hotkey_edit_proc(g_state->hotkey_edit, WM_KEYDOWN, 'K', 0); }
    catch (...) { escaped = true; }
    SetKeyboardState(original_keys);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(!escaped); // Win32 callbacks must contain file write exceptions.
    assert(g_state->settings.hotkey == previous_hotkey);
    assert(g_state->hotkey_spec.virtual_key == previous_hotkey_spec.virtual_key &&
        g_state->hotkey_spec.modifiers == previous_hotkey_spec.modifiers);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    assert(xfx::should_close_console(true, false, 0));
    assert(!xfx::should_close_console(true, false, 1));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    // Owned modal dialogs must not queue a destructive WM_CLOSE.
    {
        ModalGuard modal;
        SendMessageW(g_state->window, WM_ACTIVATE, WA_INACTIVE, 0); pump();
        assert(IsWindow(g_state->window));
    }
    RECT before{}, after{};
    GetWindowRect(g_state->results, &before);
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    GetWindowRect(g_state->results, &after);
    assert(before.bottom - before.top == after.bottom - after.top);
    assert(GetWindowLongW(g_state->hotkey_edit, GWL_STYLE) & WS_VISIBLE);
    check_hotkey_input_geometry(regression_artifacts);
    RECT hotkey_label_rect{}, hotkey_edit_rect{}, status_rect{};
    GetWindowRect(g_state->hotkey_label, &hotkey_label_rect);
    GetWindowRect(g_state->hotkey_edit, &hotkey_edit_rect);
    GetWindowRect(g_state->status, &status_rect);
    assert(std::abs((hotkey_label_rect.top + hotkey_label_rect.bottom) -
        (hotkey_edit_rect.top + hotkey_edit_rect.bottom)) <= 2);
    RECT refresh_rect{}, dictionary_rect{};
    GetWindowRect(g_state->refresh, &refresh_rect);
    GetWindowRect(g_state->dictionary, &dictionary_rect);
    assert(refresh_rect.left == hotkey_label_rect.left);
    assert(dictionary_rect.left - refresh_rect.right == 4);
    POINT saved_cursor{}; GetCursorPos(&saved_cursor);
    SetCursorPos(60, 60); pump();
    for (HWND button : {g_state->capture, g_state->slot, g_state->preferences,
        g_state->refresh, g_state->dictionary, g_state->hotkey_save,
        g_state->import_settings, g_state->export_settings})
        check_button_hover(button);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"settings-preview.png");
    POINT refresh_hover{8, 8}; ClientToScreen(g_state->refresh, &refresh_hover);
    SetCursorPos(refresh_hover.x, refresh_hover.y);
    SendMessageW(g_state->refresh, WM_MOUSEMOVE, 0, MAKELPARAM(8, 8)); pump_for(220);
    if (argc > 1) save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"button-hover-preview.png");
    SetCursorPos(60, 60);
    SendMessageW(g_state->refresh, WM_MOUSELEAVE, 0, 0); pump_for(220);
    SetCursorPos(saved_cursor.x, saved_cursor.y);
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    assert(!IsWindowVisible(g_state->hotkey_edit));
    if (argc > 1) {
        SetWindowPos(g_state->window, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        save_test_preview(g_state->window, argv[1]);
        save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"ui-preview-dark.png");
    }
    const HWND theme_choice = GetDlgItem(g_state->window, 1032);
    assert(theme_choice); // The appearance option must be reachable in native settings.
    SendMessageW(theme_choice, CB_SETCURSEL, static_cast<int>(xfx::ThemeMode::Light), 0);
    SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(1032, CBN_SELCHANGE), 0);
    assert(g_state->settings.light_theme && xfx::SettingsStore::load(g_test_settings_path).light_theme);
    SetCursorPos(60, 60); pump();
    check_button_hover(g_state->capture);
    LOGBRUSH theme_brush{};
    assert(GetObjectW(g_state->background_brush, sizeof(theme_brush), &theme_brush));
    assert(theme_brush.lbColor == RGB(237,237,237));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    SendMessageW(theme_choice, CB_SETCURSEL, static_cast<int>(xfx::ThemeMode::Dark), 0);
    SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(1032, CBN_SELCHANGE), 0);
    assert(g_state->settings.light_theme && SendMessageW(theme_choice, CB_GETCURSEL, 0, 0) == static_cast<int>(xfx::ThemeMode::Light));
    assert(GetObjectW(g_state->background_brush, sizeof(theme_brush), &theme_brush));
    assert(theme_brush.lbColor == RGB(237,237,237));
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    prepare_dialog(L"编辑搜索别名", L"取消的浅色别名", IDCANCEL);
    edit_item_aliases(g_test_catalog[0]);
    assert(dialog_handled && g_state->settings.light_theme);
    const DWORD gdi_before_theme_cycles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int cycle = 0; cycle < 24; ++cycle) set_light_theme((cycle % 2) != 0);
    assert(g_state->settings.light_theme);
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before_theme_cycles + 2);
    {
    ModalGuard modal; // Match the plugin caller: activating an owned editor must not close its owner.
    std::wstring single_line = L"原值";
    prepare_dialog(L"单行保存测试", L"回车确认", IDOK, nullptr, true, true);
    assert(xfx::prompt_text(g_state->window, L"单行保存测试", L"Enter 应保存。", false,
        single_line, g_state->font, true));
    assert(dialog_handled && single_line == L"回车确认");
    prepare_dialog(L"单行取消测试", L"取消内容", IDCANCEL, nullptr, true, true);
    assert(!xfx::prompt_text(g_state->window, L"单行取消测试", L"Escape 应取消。", false,
        single_line, g_state->font, true));
    assert(dialog_handled && single_line == L"回车确认");
    }
    if (argc > 1) {
        save_test_preview(g_state->window, fs::path(argv[1]).parent_path() / L"ui-preview-light.png");
    }
    fs::create_directories(capture_directory());
    {
        Gdiplus::Bitmap bitmap(3, 2, PixelFormat32bppARGB);
        bitmap.SetPixel(0, 0, Gdiplus::Color(255, 12, 34, 56));
        CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        assert(bitmap.Save((capture_directory() / L"测试一.png").c_str(), &png) == Gdiplus::Ok);
        assert(bitmap.Save((capture_directory() / L"测试二.png").c_str(), &png) == Gdiplus::Ok);
    }
    show_gallery(); pump();
    UpdateWindow(g_state->gallery_window);
    const auto cached_thumbnail = xfx::thumbnail_cache_file(capture_directory() / L"测试一.png");
    assert(fs::exists(cached_thumbnail));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    clear_thumbnail_cache(); // Retain legacy cache utility coverage without a product UI entry.
    assert(!fs::exists(cached_thumbnail));
    SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0); pump_for(240);
    assert((GetWindowLongPtrW(g_state->gallery_window, GWL_STYLE) & WS_THICKFRAME) != 0);
    assert(IsWindow(GetDlgItem(g_state->gallery_window, 1033))); // Integrated comparison preview.
    assert(g_state->settings.light_theme);
    HDC gallery_dc = CreateCompatibleDC(nullptr);
    SendMessageW(g_state->gallery_window, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(gallery_dc),
        reinterpret_cast<LPARAM>(g_state->gallery_status));
    assert(GetTextColor(gallery_dc) == RGB(37,37,37) && GetBkColor(gallery_dc) == RGB(237,237,237));
    DeleteDC(gallery_dc);
    assert(IsWindow(g_state->window) && IsWindow(g_state->gallery_window));
    assert(g_state->modal_depth == 1);
    assert(selected_captures().size() == 1 && !selected_capture().empty());
    SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, -1); update_gallery_selection();
    assert(selected_captures().size() == 2 && selected_capture().empty());
    assert(!IsWindowEnabled(GetDlgItem(g_state->gallery_window, kCopyCaptureId)));
    prepare_dialog(L"删除快照", L"", IDNO, g_state->gallery_window, false);
    delete_selected_captures();
    assert(dialog_handled && xfx::scan_captures(capture_directory()).size() == 2);
    SendMessageW(g_state->gallery_list, LB_SETSEL, FALSE, -1); update_gallery_selection();
    assert(!IsWindowEnabled(GetDlgItem(g_state->gallery_window, kDeleteCaptureId)));
    delete_selected_captures(); // No selection must not open a confirmation dialog.
    assert(xfx::scan_captures(capture_directory()).size() == 2);
    SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, 0); update_gallery_selection();
    const auto cached_capture = selected_capture();
    HANDLE cached_file = CreateFileW(cached_capture.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(cached_file != INVALID_HANDLE_VALUE);
    CloseHandle(cached_file);
    prepare_dialog(L"删除快照", L"", IDYES, g_state->gallery_window, false);
    delete_selected_captures();
    assert(dialog_handled && xfx::scan_captures(capture_directory()).size() == 1);
    assert(g_state->modal_depth == 1 && selected_captures().size() == 1);
    check_gallery_comparison(argc > 1 ? fs::path(argv[1]).parent_path() : directory);
    DestroyWindow(g_state->gallery_window);
    assert(g_state->modal_depth == 0);
    SetWindowTextW(g_state->search, L"gsmh");
    populate_results();
    g_test_script_reply = xfx::ScriptReply{true, false, 1, ""};
    results_list_proc(g_state->results, WM_LBUTTONUP, 0, MAKELPARAM(12, 4));
    pump();
    g_test_script_reply.reset();
    assert(!g_state->window);
    g_test_initial_background_fill_count = 0;
    show_console();
    assert(IsWindow(g_state->window));
    assert(g_state->initial_background_primed && g_test_initial_background_fill_count > 0);
    // The configured shortcut is already registered by the message-only window.
    // Use a different key to confirm unrelated global registrations remain available.
    assert(RegisterHotKey(nullptr, 0x5847, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24));
    UnregisterHotKey(nullptr, 0x5847);
    xfx::HotkeySpec low_level_spec{MOD_SHIFT, VK_SPACE};
    assert(hotkey_event_matches(WM_KEYDOWN, VK_SPACE, MOD_SHIFT, low_level_spec));
    assert(!hotkey_event_matches(WM_KEYUP, VK_SPACE, MOD_SHIFT, low_level_spec));
    assert(!hotkey_event_matches(WM_KEYDOWN, VK_SPACE, MOD_CONTROL, low_level_spec));
    show_console();
    assert(IsWindow(g_state->window));
    assert(g_state->settings.full_resolution_screenshots);
    SendMessageW(g_state->window, WM_ACTIVATE, WA_INACTIVE, 0); pump();
    assert(!g_state->window);
    auto& bridge = xfx::script_bridge();
    if (IsWindow(g_state->gallery_window)) DestroyWindow(g_state->gallery_window);
    bridge.set_available(true); // Standalone harness substitutes for AE idle registration.
    assert(bridge.appearance().find("\tlight\tEDEDED\t") != std::string::npos);
    auto queued = bridge.submit(xfx::BridgeCommand::Gallery);
    auto request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    A_long max_sleep = 60;
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).find("\tfeature_removed\t") != std::string::npos);
    assert(!IsWindow(g_state->gallery_window));
    assert(!g_state->window);
    queued = bridge.submit(xfx::BridgeCommand::Capture);
    request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).find("\thost_error\t") != std::string::npos);
    assert(bridge.status(request_id).starts_with("XFX1\tERROR\t"));
    assert(!IsWindow(g_state->window)); // Missing comp fails before opening a dialog/console.
    queued = bridge.submit(xfx::BridgeCommand::Preferences);
    request_id = std::stoull(queued.substr(queued.find_last_of('\t') + 1));
    bridge_idle_hook(nullptr, nullptr, &max_sleep);
    assert(bridge.status(request_id).starts_with("XFX1\tOK\t"));
    assert(g_state->settings_visible && IsWindowVisible(g_state->window));
    pump_for(350);
    assert(g_state->settings_extent == g_state->expand_to && !g_state->settings_animation_active);
    SetWindowTextW(g_state->search, L"gaussian");
    populate_results();
    assert(SendMessageW(g_state->results, LB_GETCOUNT, 0, 0) > 0);
    assert(result_corner_pixel(true) != colors().selected);
    ShowWindow(g_state->window, SW_HIDE);
    EnumChildWindows(g_state->window, invalidate_child_for_reopen, 0);
    show_console();
    assert(visible_children_with_pending_paint(g_state->window) == 0);
    check_combo_geometry(regression_artifacts);
    check_ime_failure_retry();
    check_ime_pending_restore();
    check_apply_lifecycle();
    death_hook(nullptr, reinterpret_cast<AEGP_DeathRefcon>(g_state));
    assert(!g_state);
    assert(bridge.status().find("\tunavailable\t") != std::string::npos);
    SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", nullptr);
    fs::remove_all(directory);
    std::cout << "native UI tests passed (search, settings, shortcut layout, mouse selection, drag, modal, gallery, close/reopen, hotkey)\n";
}
