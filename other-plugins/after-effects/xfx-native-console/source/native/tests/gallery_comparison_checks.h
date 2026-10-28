#pragma once
#include <cmath>
#include <map>

static COLORREF comparison_pixel(HWND canvas, int x, int y) {
    RECT client{}; GetClientRect(canvas, &client);
    HDC window_dc = GetDC(canvas), memory = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
    const auto previous = SelectObject(memory, bitmap);
    SendMessageW(canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    const auto color = GetPixel(memory, x, y);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(canvas, window_dc);
    return color;
}

static void check_gallery_comparison(const fs::path& artifact_directory) {
    g_state->settings.auto_close = false; // Isolate these checks from unrelated desktop focus changes.
    std::map<fs::path, COLORREF> source_colors;
    CLSID png{}; CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
    for (int index = 0; index < 6; ++index) {
        const COLORREF color = RGB(30 + index * 30, 60 + index * 20, 180 - index * 20);
        const auto path = capture_directory() / (L"比较-" + std::to_wstring(index) + L".png");
        Gdiplus::Bitmap bitmap(1280, 720, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics draw(&bitmap);
            draw.Clear(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
            Gdiplus::Pen border(Gdiplus::Color(255, 255, 255, 255), 8);
            draw.DrawRectangle(&border, 28, 28, 1224, 664);
            Gdiplus::Font font(L"Segoe UI", 100, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush ink(Gdiplus::Color(255, 255, 255, 255));
            const auto label = std::to_wstring(index + 1);
            draw.DrawString(label.c_str(), -1, &font, Gdiplus::PointF(60, 50), &ink);
        }
        assert(bitmap.Save(path.c_str(), &png) == Gdiplus::Ok);
        source_colors[path] = color;
    }
    refresh_gallery();
    RECT list{}, canvas{};
    GetWindowRect(g_state->gallery_list, &list); GetWindowRect(g_state->gallery_canvas, &canvas);
    assert(list.right + 3 == canvas.left && list.top == canvas.top && list.bottom == canvas.bottom);
    assert((GetWindowLongPtrW(g_state->gallery_list, GWL_STYLE) & LBS_MULTICOLUMN) == 0);
    select_gallery_row(0, false, false, false);
    select_gallery_row(4, false, true, false);
    assert(selected_captures().size() == 5);
    select_gallery_row(2, true, true, false); // Ctrl wins over Shift, toggling only row 2.
    assert(selected_captures().size() == 4 && SendMessageW(g_state->gallery_list, LB_GETSEL, 2, 0) == 0);
    SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, -1); update_gallery_selection();
    const auto selected = selected_captures();
    assert(selected.size() > 4 && g_state->gallery_model.views().size() == 4);
    const auto& initial = g_state->gallery_model.views();
    assert(initial[1].cell.x == initial[0].cell.x && initial[1].cell.y > initial[0].cell.y);
    for (size_t index = 0; index < 4; ++index) {
        assert(initial[index].source.path == selected[index] && initial[index].scale < 1);
        const auto& cell = initial[index].cell;
        assert(comparison_pixel(g_state->gallery_canvas, static_cast<int>(cell.x + cell.width / 2),
            static_cast<int>(cell.y + cell.height / 2)) == source_colors.at(initial[index].source.path));
    }
    save_test_preview(g_state->gallery_window, artifact_directory / L"gallery-four-light.png");
    const int gap_x = static_cast<int>(std::floor(initial[0].cell.width)) + 1;
    assert(comparison_pixel(g_state->gallery_canvas, gap_x, 12) == colors().background);
    SendMessageW(g_state->gallery_list, WM_KEYDOWN, 'Z', 0);
    assert(g_state->gallery_model.actual_size());
    assert(comparison_pixel(g_state->gallery_canvas, gap_x, 12) == colors().background); // 100% is clipped per cell.
    save_test_preview(g_state->gallery_window, artifact_directory / L"gallery-actual-size.png");
    const auto first = g_state->gallery_model.views()[0].cell;
    const int point_x = static_cast<int>(first.x + first.width / 2), point_y = static_cast<int>(first.y + first.height / 2);
    SendMessageW(g_state->gallery_canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point_x, point_y));
    assert(GetCapture() == g_state->gallery_canvas && g_state->gallery_drag_view == 0);
    SendMessageW(g_state->gallery_canvas, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point_x + 30, point_y + 15));
    assert(g_state->gallery_model.views()[0].offset_x == 30);
    SendMessageW(g_state->gallery_canvas, WM_KEYDOWN, 'A', 0);
    assert(GetCapture() != g_state->gallery_canvas && g_state->gallery_drag_view == -1);
    assert(g_state->gallery_model.horizontal() && g_state->gallery_model.views()[0].offset_x == 0);
    assert(g_state->gallery_model.views()[1].cell.x > g_state->gallery_model.views()[0].cell.x);
    const auto before_wheel = g_state->gallery_model.views()[0].scale;
    POINT anchor{point_x, point_y}; ClientToScreen(g_state->gallery_canvas, &anchor);
    SendMessageW(g_state->gallery_list, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(anchor.x, anchor.y));
    assert(std::abs(g_state->gallery_model.views()[0].scale - before_wheel * 1.1) < 0.000001);
    set_light_theme(false);
    assert(comparison_pixel(g_state->gallery_canvas, gap_x, 12) == colors().background);
    save_test_preview(g_state->gallery_window, artifact_directory / L"gallery-four-dark.png");
    set_light_theme(true);
    // Divider width persists through resize, including a clamped narrow layout.
    MoveWindow(g_state->gallery_window, 40, 40, 1100, 700, TRUE);
    g_state->gallery_split = 600; layout_gallery(g_state->gallery_window);
    MoveWindow(g_state->gallery_window, 40, 40, 440, 420, TRUE);
    const int clamped_split = g_state->gallery_split;
    assert(clamped_split < 600 && clamped_split >= 100);
    MoveWindow(g_state->gallery_window, 40, 40, 1100, 700, TRUE);
    assert(g_state->gallery_split == clamped_split);
    // The wide thumbnail list must support the original divider geometry, beyond 255 pixels.
    g_state->gallery_split = 600; layout_gallery(g_state->gallery_window);
    assert(SendMessageW(g_state->gallery_list, LB_GETITEMHEIGHT, 0, 0) > 255);
    MoveWindow(g_state->gallery_window, 40, 40, 960, 620, TRUE);
    g_state->gallery_split = 230; layout_gallery(g_state->gallery_window);
    select_gallery_row(0, false, false, false); select_gallery_row(1, true, false, false);
    if (g_state->gallery_model.horizontal()) SendMessageW(g_state->gallery_canvas, WM_KEYDOWN, 'A', 0);
    assert(g_state->gallery_model.views().size() == 2);
    assert(g_state->gallery_model.views()[1].cell.y > g_state->gallery_model.views()[0].cell.y);
    save_test_preview(g_state->gallery_window, artifact_directory / L"gallery-two-vertical.png");
    const auto bad = capture_directory() / L"损坏的快照.png";
    { std::ofstream file(bad, std::ios::binary); file << "not a PNG"; }
    refresh_gallery();
    const auto bad_row = std::find(g_state->captures.begin(), g_state->captures.end(), bad) - g_state->captures.begin();
    select_gallery_row(static_cast<int>(bad_row), false, false, false);
    assert(g_state->gallery_model.views().size() == 1 && g_state->gallery_model.views()[0].source.width == 0);
    assert(g_state->gallery_images.size() == 1 && !g_state->gallery_images[0]);
    assert(comparison_pixel(g_state->gallery_canvas, 2, 2) == colors().background);
    SendMessageW(g_state->gallery_list, LB_SETSEL, FALSE, -1); update_gallery_selection();
    assert(g_state->gallery_model.views().empty() && g_state->gallery_images.empty());
    assert(comparison_pixel(g_state->gallery_canvas, 2, 2) == colors().background);
    fs::remove(bad);
    // Only this harness's temporary Capture files are removed.
    assert(capture_directory() == g_test_settings_path.parent_path() / L"Capture");
    for (const auto& file : xfx::scan_captures(capture_directory())) {
        assert(file.parent_path() == capture_directory());
        assert(fs::remove(file));
    }
    refresh_gallery();
    assert(g_state->captures.empty() && SendMessageW(g_state->gallery_list, LB_GETCOUNT, 0, 0) == 0);
    assert(selected_captures().empty() && g_state->gallery_model.views().empty() && g_state->gallery_images.empty());
    assert(comparison_pixel(g_state->gallery_canvas, 2, 2) == colors().background);
    // Leave a valid test capture for the existing standalone-preview lifecycle checks.
    {
        Gdiplus::Bitmap bitmap(64, 36, PixelFormat32bppARGB);
        { Gdiplus::Graphics fill(&bitmap); fill.Clear(Gdiplus::Color(255, 36, 120, 200)); }
        assert(bitmap.Save((capture_directory() / L"恢复测试快照.png").c_str(), &png) == Gdiplus::Ok);
    }
    refresh_gallery();
    g_state->settings.auto_close = true;
}
