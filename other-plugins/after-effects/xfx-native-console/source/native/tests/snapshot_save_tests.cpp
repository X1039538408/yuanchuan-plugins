#include <windows.h>
#include <commdlg.h>
#include "../src/ae_capture.h"
#include "../src/themed_menu.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace xfx {
CaptureCompName snapshot_test_name(SPBasicSuite*, AEGP_PluginID);
ScriptReply snapshot_test_render(SPBasicSuite*, AEGP_PluginID, const std::filesystem::path&, bool);
UINT snapshot_test_menu(HMENU, UINT, int, int, HWND, const ThemeColors&, HFONT);
}
static BOOL WINAPI snapshot_test_dialog(LPOPENFILENAMEW dialog);
#define XFX_CONSOLE_TEST
#define GetSaveFileNameW snapshot_test_dialog
#define current_capture_comp_name snapshot_test_name
#define capture_comp_png snapshot_test_render
#define track_themed_popup snapshot_test_menu
#include "../plugin/XFXNativeConsole.cpp"
#undef GetSaveFileNameW
#undef current_capture_comp_name
#undef capture_comp_png
#undef track_themed_popup

static unsigned dialog_calls = 0, render_calls = 0;
static bool accept_dialog = false, valid_comp = true, render_succeeds = true;
static std::wstring default_filename, dialog_title;
static DWORD dialog_flags;
static fs::path chosen_path, rendered_path;
static bool rendered_full_resolution;
static void require_save(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
namespace xfx {
UINT snapshot_test_menu(HMENU, UINT, int, int, HWND, const ThemeColors&, HFONT) { return 1; }
CaptureCompName snapshot_test_name(SPBasicSuite*, AEGP_PluginID) {
    return {{valid_comp, false, valid_comp ? 1U : 0U, valid_comp ? "" : "没有活动合成"}, L"当前 合成🎬"};
}
ScriptReply snapshot_test_render(SPBasicSuite*, AEGP_PluginID, const fs::path& target, bool full_resolution) {
    ++render_calls; rendered_path = target; rendered_full_resolution = full_resolution;
    if (!render_succeeds) return {false, false, 0, "测试渲染失败"};
    std::ofstream(target, std::ios::binary) << "snapshot bytes";
    return {true, false, 1, ""};
}
}
static BOOL WINAPI snapshot_test_dialog(LPOPENFILENAMEW dialog) {
    ++dialog_calls;
    require_save(render_calls == 0, "snapshot was rendered before the user confirmed Save As");
    default_filename = dialog->lpstrFile;
    dialog_title = dialog->lpstrTitle ? dialog->lpstrTitle : L"";
    dialog_flags = dialog->Flags;
    require_save(g_state->modal_depth > 0, "save dialog was not protected against console focus loss");
    if (!accept_dialog) return FALSE;
    wcscpy_s(dialog->lpstrFile, dialog->nMaxFile, chosen_path.c_str());
    return TRUE;
}
static void reset_save() {
    dialog_calls = render_calls = 0;
    accept_dialog = false; valid_comp = render_succeeds = true;
    default_filename.clear(); dialog_title.clear(); rendered_path.clear();
}
int wmain() {
    const auto directory = fs::temp_directory_path() / (L"xfx-snapshot-save-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    g_test_settings_path = directory / L"profile" / L"settings.ini";
    g_state = new PluginState();
    g_state->window = CreateWindowExW(0, L"STATIC", L"snapshot test", 0, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0,
        g_state->window, nullptr, GetModuleHandleW(nullptr), nullptr);
    chosen_path = directory / L"手工重命名.png";
    int exit_code = 0;
    try {
        reset_save();
        const auto cancelled = dispatch_bridge_request({1, xfx::BridgeCommand::Capture, -1});
        require_save(dialog_calls == 1, "capture command did not open Save As");
        require_save(!cancelled.succeeded && cancelled.code == "cancelled" && render_calls == 0,
            "cancelled Save As rendered a snapshot or returned an error instead of cancellation");
        require_save(default_filename == L"当前 合成🎬.png", "Save As default is not the current composition name");
        require_save(dialog_title == L"保存当前合成快照", "wrong snapshot save dialog");
        require_save((dialog_flags & (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR)) ==
            (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR), "Save As lacks file protections");
        require_save(!fs::exists(directory / L"profile" / L"Capture") && !fs::exists(chosen_path),
            "cancelled Save As created files or a gallery folder");
        std::cout << "PASS cancelled Save As has the comp default and creates no snapshot\n";

        reset_save(); accept_dialog = true; g_state->settings.full_resolution_screenshots = false;
        const auto saved = dispatch_bridge_request({2, xfx::BridgeCommand::Capture, -1});
        require_save(saved.succeeded && saved.count == 1 && dialog_calls == 1 && render_calls == 1,
            "confirmed Save As did not save exactly one snapshot");
        require_save(rendered_path == chosen_path && rendered_full_resolution,
            "snapshot ignored the user's filename/path or resolution setting");
        require_save(fs::exists(chosen_path) && !fs::exists(directory / L"profile" / L"Capture"),
            "snapshot was routed through a hidden gallery instead of the chosen file");
        require_save(g_state->modal_depth == 0, "snapshot leaked its modal guard");
        std::cout << "PASS snapshot saves directly to the chosen Unicode filename\n";

        reset_save(); valid_comp = false;
        const auto unavailable = dispatch_bridge_request({3, xfx::BridgeCommand::Capture, -1});
        require_save(!unavailable.succeeded && dialog_calls == 0 && render_calls == 0,
            "missing composition opened Save As or rendered");
        std::cout << "PASS missing composition is reported before Save As\n";

        reset_save(); accept_dialog = true; render_succeeds = false;
        const auto failed = dispatch_bridge_request({4, xfx::BridgeCommand::Capture, -1});
        require_save(!failed.succeeded && failed.count == 0 && render_calls == 1 &&
            failed.message.find("测试渲染失败") != std::string::npos, "render failure became a success");
        std::ifstream previous(chosen_path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(previous)), {});
        require_save(bytes == "snapshot bytes", "failed render damaged the previous file");
        previous.close();
        std::cout << "PASS render failure preserves the previous file and reports failure\n";

        reset_save();
        window_proc(g_state->window, WM_COMMAND, kCaptureId, 0);
        require_save(dialog_calls == 1 && render_calls == 0, "native capture button bypassed Save As");
        const auto removed = dispatch_bridge_request({5, xfx::BridgeCommand::Gallery, -1});
        require_save(!removed.succeeded && !IsWindow(g_state->gallery_window), "removed gallery can still open");
        std::cout << "PASS native capture button shares Save As and gallery is unavailable\n";

        reset_save(); accept_dialog = true;
        export_current_frame(); // The renderer boundary writes a non-image, so export fails safely.
        require_save(render_calls == 1 && rendered_path.parent_path() != capture_directory(),
            "export still writes to the removed gallery");
        require_save(!fs::exists(rendered_path), "export failure retained a temporary snapshot");
        std::cout << "PASS export uses a disposable frame instead of a hidden gallery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; exit_code = 1;
    }
    DestroyWindow(g_state->window); delete g_state; g_state = nullptr;
    if (!fs::equivalent(directory.parent_path(), fs::temp_directory_path())) {
        std::cerr << "FAIL test cleanup escaped temp\n";
        return 1;
    }
    fs::remove_all(directory);
    return exit_code;
}
