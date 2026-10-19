// Included by the owned standalone window harness, never by the AE plugin.
static void check_native_capture_unavailable() {
    const auto before = xfx::scan_captures(capture_directory());
    const auto modal_depth = g_state->modal_depth;
    fs::path saved = L"未保存.png";
    const auto reply = capture_frame(&saved);
    assert(!reply.succeeded && reply.count == 0 && reply.message == "AE 截图接口不可用");
    assert(saved == fs::path(L"未保存.png") && g_state->modal_depth == modal_depth);
    assert(xfx::scan_captures(capture_directory()) == before);
    wchar_t status[256]{}; GetWindowTextW(g_state->status, status, 256);
    assert(std::wstring(status) == L"快照失败：AE 截图接口不可用");
    std::cout << "PASS native capture failure preserves target and gallery\n";
}

static void check_full_resolution_setting() {
    const HWND checkbox = GetDlgItem(g_state->window, 1037);
    assert(!IsWindow(checkbox));
    assert(g_state->settings.full_resolution_screenshots);
    assert(xfx::SettingsStore::load(g_test_settings_path).full_resolution_screenshots);
    std::cout << "PASS full-resolution is enabled without a UI switch\n";
}

static void check_capture_location(const fs::path& artifacts) {
    const auto original = capture_directory();
    const auto original_captures = g_state->captures;
    const auto root = g_test_settings_path.parent_path();
    assert(original == root / L"Capture" && !original_captures.empty());
    const auto chosen = root / L"自定义快照🎬=测试";
    fs::create_directories(chosen);
    {
        Gdiplus::Bitmap image(9, 7, PixelFormat32bppARGB);
        image.SetPixel(0, 0, Gdiplus::Color(255, 203, 17, 61));
        CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        assert(image.Save((chosen / L"另一张快照.png").c_str(), &png) == Gdiplus::Ok);
    }
    set_capture_directory(chosen);
    assert(capture_directory() == chosen);
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == chosen.wstring());
    assert(g_state->captures == std::vector<fs::path>{chosen / L"另一张快照.png"});
    assert(selected_capture() == chosen / L"另一张快照.png");
    assert(g_state->gallery_images.size() == 1 && g_state->gallery_images[0]->GetWidth() == 9);
    Gdiplus::Color pixel;
    assert(g_state->gallery_images[0]->GetPixel(0, 0, &pixel) == Gdiplus::Ok && pixel.GetValue() == 0xFFCB113D);
    for (const auto& image : original_captures) assert(fs::exists(image));
    wchar_t displayed[1024]{}; GetWindowTextW(g_state->capture_location, displayed, 1024);
    assert(displayed == chosen.wstring());

    const auto empty = root / L"空图库";
    fs::create_directories(empty);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    bool failed = false;
    try { set_capture_directory(empty); } catch (const std::exception&) { failed = true; }
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(failed && capture_directory() == chosen && selected_capture() == chosen / L"另一张快照.png");
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == chosen.wstring());
    GetWindowTextW(g_state->capture_location, displayed, 1024);
    assert(displayed == chosen.wstring());
    for (const auto& bad : {root / L"missing", chosen / L"另一张快照.png", fs::path(L"relative")}) {
        failed = false;
        try { set_capture_directory(bad); } catch (const std::exception&) { failed = true; }
        assert(failed && capture_directory() == chosen);
    }

    const auto modal_depth = g_state->modal_depth;
    prepare_dialog(L"选择快照保存文件夹", L"", IDCANCEL, g_state->window, false);
    SendMessageW(g_state->window, WM_COMMAND, MAKEWPARAM(kChooseCaptureLocationId, BN_CLICKED),
                 reinterpret_cast<LPARAM>(g_state->choose_capture_location));
    assert(dialog_handled && capture_directory() == chosen && g_state->modal_depth == modal_depth);
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == chosen.wstring());
    set_capture_directory(empty);
    assert(g_state->captures.empty() && g_state->gallery_images.empty() && g_state->gallery_model.views().empty());
    assert(!IsWindowEnabled(GetDlgItem(g_state->gallery_window, kDeleteCaptureId)));
    assert(fs::exists(chosen / L"另一张快照.png"));
    set_capture_directory(chosen);

    // Exercise the real import commit path, not just the folder chooser setter.
    const auto import_file = root / L"导入目录.ini";
    auto imported = g_state->settings;
    xfx::SettingsStore::save(import_file, imported);
    HANDLE locked = CreateFileW(import_file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    failed = false;
    try { apply_imported_settings(xfx::SettingsStore::load(import_file, true)); } catch (const std::exception&) { failed = true; }
    CloseHandle(locked);
    assert(failed && capture_directory() == chosen && selected_capture() == chosen / L"另一张快照.png");
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == chosen.wstring());
    failed = false;
    try { apply_imported_settings(xfx::SettingsStore::load(root / L"disappeared-import.ini", true)); }
    catch (const std::exception&) { failed = true; }
    assert(failed && capture_directory() == chosen && selected_capture() == chosen / L"另一张快照.png");
    for (const auto& bad : {root / L"absent-import", chosen / L"另一张快照.png"}) {
        imported.capture_directory = bad.wstring();
        xfx::SettingsStore::save(import_file, imported);
        failed = false;
        try { apply_imported_settings(xfx::SettingsStore::load(import_file)); }
        catch (const std::exception&) { failed = true; }
        assert(failed && capture_directory() == chosen);
        assert(selected_capture() == chosen / L"另一张快照.png");
        assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == chosen.wstring());
    }
    imported.capture_directory = empty.wstring();
    imported.full_resolution_screenshots = false;
    xfx::SettingsStore::save(import_file, imported);
    apply_imported_settings(xfx::SettingsStore::load(import_file));
    assert(capture_directory() == empty && g_state->captures.empty() && g_state->gallery_images.empty());
    assert(g_state->settings.full_resolution_screenshots);
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_READONLY));
    imported.capture_directory = chosen.wstring();
    imported.full_resolution_screenshots = true;
    failed = false;
    try { apply_imported_settings(imported); } catch (const std::exception&) { failed = true; }
    assert(SetFileAttributesW(g_test_settings_path.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(failed && capture_directory() == empty && g_state->captures.empty());
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory == empty.wstring());
    assert(g_state->settings.full_resolution_screenshots);
    assert(xfx::SettingsStore::load(g_test_settings_path).full_resolution_screenshots);
    apply_imported_settings(imported);
    assert(capture_directory() == chosen && selected_capture() == chosen / L"另一张快照.png");
    assert(g_state->settings.full_resolution_screenshots);

    g_state->settings_visible = true; adjust_console_height(); layout_controls(g_state->window);
    assert(IsWindowVisible(g_state->capture_location) && IsWindowVisible(g_state->choose_capture_location));
    assert((GetWindowLongW(g_state->capture_location, GWL_STYLE) & ES_READONLY) != 0);
    RECT folder_rect{}, status_rect{};
    GetWindowRect(g_state->capture_location, &folder_rect);
    GetWindowRect(g_state->status, &status_rect);
    assert(folder_rect.bottom <= status_rect.top);
    save_test_preview(g_state->window, artifacts / L"capture-settings-light.png");
    set_light_theme(false);
    save_test_preview(g_state->window, artifacts / L"capture-settings-dark.png");
    set_light_theme(true);
    set_capture_directory({});
    assert(capture_directory() == original && g_state->captures == original_captures);
    assert(xfx::SettingsStore::load(g_test_settings_path).capture_directory.empty());
    std::cout << "PASS capture directory switch/cancel/rollback/gallery refresh and preserved original images\n";
}
