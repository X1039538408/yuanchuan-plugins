#include "AEConfig.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifdef AE_OS_WIN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <imm.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

#include "entry.h"
#include "AE_GeneralPlug.h"
#include "AE_Macros.h"
#include "AEGP_SuiteHandler.h"

#include "../src/hotkey.h"
#include "../src/ime_controller.h"
#include "../src/search_core.h"
#include "../src/settings.h"
#include "../src/ae_script.h"
#include "../src/ae_capture.h"
#include "../src/console_support.h"
#include "../src/catalog_customization.h"
#include "../src/text_input_dialog.h"
#include "../src/extendscript_bridge.h"
#include "../src/ui_theme.h"
#include "../src/ui_shapes.h"
#include "../src/ui_loading.h"
#include "../src/themed_menu.h"
#include "../src/gallery_model.h"
#include "../src/thumbnail_list.h"
#include "../src/thumbnail_cache.h"

namespace {

namespace fs = std::filesystem;

constexpr int kHotkeyId = 0x5846;
constexpr wchar_t kHotkeyWindowClassName[] = L"XFXNativeConsoleHotkeyWindow";
constexpr wchar_t kHotkeyCaptureClassName[] = L"XFXNativeConsoleHotkeyCapture";
constexpr int kSearchId = 1001;
constexpr int kFilterId = 1002;
constexpr int kResultsId = 1003;
constexpr int kApplyId = 1004;
constexpr int kRefreshId = 1005;
constexpr int kHotkeyEditId = 1006;
constexpr int kHotkeySaveId = 1007;
constexpr int kModeId = 1008;
constexpr int kStatusId = 1009;
constexpr int kCountId = 1010;
constexpr int kTitleId = 1011;
constexpr int kSubtitleId = 1012;
constexpr int kHotkeyLabelId = 1013;
constexpr int kFavoriteId = 1014;
constexpr int kSlotId = 1015;
constexpr int kImportId = 1016;
constexpr int kExportId = 1017;
constexpr int kCaptureId = 1020;
constexpr int kGalleryId = 1021;
constexpr int kDictionaryId = 1022;
constexpr int kPreferencesId = 1023;
constexpr int kGalleryListId = 1024;
constexpr int kPreviewId = 1025;
constexpr int kSaveCaptureId = 1026;
constexpr int kImportCaptureId = 1027;
constexpr int kCaptureFolderId = 1028;
constexpr int kCopyCaptureId = 1030;
constexpr int kDeleteCaptureId = 1031;
constexpr int kLightThemeId = 1032;
constexpr int kGalleryCanvasId = 1033;
constexpr int kCaptureLocationId = 1034;
constexpr int kChooseCaptureLocationId = 1035;
constexpr int kCaptureLocationLabelId = 1036;
constexpr int kFullResolutionId = 1037;
constexpr int kThumbnailCacheClearId = 1038;
constexpr int kSettingsHeight = 132;
constexpr int kResultHeight = 28;
constexpr int kResultActionWidth = 24;
constexpr int kShortcutColumnWidth = 55;
constexpr int kShortcutHeight = 40;
constexpr int kToolbarCenterY = 28;
constexpr int kToolbarButtonSize = 36;
constexpr int kToolbarGap = 4;
constexpr UINT_PTR kDragTimerId = 0xD001;
constexpr UINT kDragHoldMs = 280;
constexpr int kDragEdge = 14;
constexpr UINT_PTR kResultTooltipId = 0x7F01;

enum class ResultAction { None, Pin, Favorite, Shortcut };

AEGP_Command g_command = 0;
AEGP_PluginID g_plugin_id = 0;
SPBasicSuite* g_basic = nullptr;
#ifdef XFX_CONSOLE_TEST
fs::path g_test_settings_path;
std::vector<xfx::CatalogItem> g_test_catalog;
std::optional<xfx::ScriptReply> g_test_script_reply;
void (*g_test_before_script)() = nullptr;
std::optional<ULONGLONG> g_test_button_hover_time;
std::optional<bool> g_test_drag_button_held;
std::optional<UINT> g_test_popup_command;
std::optional<DWORD> g_test_hotkey_foreground_process;
int g_test_window_erase_count = 0;
int g_test_window_paint_count = 0;
int g_test_result_draw_count = 0;
int g_test_result_erase_count = 0;
int g_test_result_buffered_paint_count = 0;
int g_test_search_hint_paint_count = 0;
int g_test_search_default_paint_count = 0;
int g_test_initial_background_fill_count = 0;
COLORREF g_test_initial_background_color = CLR_INVALID;
#endif

std::wstring utf8_to_wide(const char* value) {
    if (!value || !*value) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (size > 0) {
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
        result.resize(static_cast<size_t>(size - 1));
        return result;
    }
    const int local_size = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (local_size <= 0) return {};
    std::wstring result(static_cast<size_t>(local_size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), local_size);
    result.resize(static_cast<size_t>(local_size - 1));
    return result;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::string wide_to_ansi(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::filesystem::path settings_path() {
#ifdef XFX_CONSOLE_TEST
    if (!g_test_settings_path.empty()) return g_test_settings_path;
#endif
    wchar_t profile[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"XFX_NATIVE_DATA_DIR", profile, 32768);
    if (length && length < 32768) return fs::path(profile) / L"settings.ini";
    wchar_t documents[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents);
    return fs::path(documents) / L"LoYoi" / L"XFXNative" / L"settings.ini";
}

std::wstring after_effects_root() {
    // The running host is authoritative, including custom installs and multiple AE versions.
    wchar_t host_path[32768] = {};
    if (GetModuleFileNameW(nullptr, host_path, 32768)) {
        return fs::path(host_path).parent_path().wstring();
    }
    HKEY key = nullptr;
    wchar_t plugin_path[1024] = {};
    DWORD size = sizeof(plugin_path);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Adobe\\After Effects\\25.0", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        const LONG result = RegQueryValueExW(key, L"PluginInstallPath", nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(plugin_path), &size);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS) {
            std::wstring path(plugin_path);
            while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
            const size_t marker = path.rfind(L"\\Plug-ins");
            if (marker != std::wstring::npos) return path.substr(0, marker);
        }
    }

    wchar_t program_files[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, program_files);
    return (fs::path(program_files) / L"Adobe" / L"Adobe After Effects 2025" / L"Support Files").wstring();
}

struct AliasEntry {
    int priority = 0;
    std::wstring chinese_name;
    std::string pinyin_full;
    std::string pinyin_initials;
    std::vector<std::wstring> aliases;
};

struct AliasCatalog {
    std::map<std::string, AliasEntry> by_match_name;
    std::map<std::string, AliasEntry> by_english_name;
};

std::vector<std::string> split_tsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream input(line);
    while (std::getline(input, field, '\t')) fields.push_back(field);
    while (fields.size() < 6) fields.emplace_back();
    return fields;
}

std::vector<std::wstring> split_aliases(const std::string& value) {
    std::vector<std::wstring> aliases;
    std::istringstream input(value);
    std::string alias;
    while (std::getline(input, alias, '|')) {
        const std::wstring wide = utf8_to_wide(alias.c_str());
        if (!wide.empty()) aliases.push_back(wide);
    }
    return aliases;
}

fs::path aliases_path() {
#ifdef XFX_CONSOLE_TEST
    if (!g_test_settings_path.empty()) return g_test_settings_path.parent_path() / L"XFXNativeConsole.aliases.tsv";
#endif
    HMODULE module = nullptr;
    wchar_t filename[32768] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&g_basic), &module) && GetModuleFileNameW(module, filename, 32768)) {
        return fs::path(filename).parent_path() / L"XFXNativeConsole.aliases.tsv";
    }
    return fs::path(after_effects_root()) / L"Plug-ins" / L"XFXNativeConsole.aliases.tsv";
}

AliasCatalog load_aliases() {
    AliasCatalog catalog;
    int priority = 0;
    const fs::path legacy = aliases_path();
    const fs::path sources[] = {legacy.parent_path() / L"XFXNativeConsole.aliases.default.tsv", legacy,
        settings_path().parent_path() / L"aliases.user.tsv"};
    for (const auto& source : sources) {
    std::ifstream input(source, std::ios::binary);
    if (!input) continue;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tsv(line);
        AliasEntry entry;
        entry.priority = ++priority;
        entry.chinese_name = utf8_to_wide(fields[2].c_str());
        entry.pinyin_full = fields[3];
        entry.pinyin_initials = fields[4];
        entry.aliases = split_aliases(fields[5]);
        if (!fields[0].empty()) catalog.by_match_name[fields[0]] = entry;
        if (!fields[1].empty()) catalog.by_english_name[fields[1]] = entry;
    }
    }
    return catalog;
}

void append_effects(std::vector<xfx::CatalogItem>& items) {
    if (!g_basic) return;
    try {
        AEGP_SuiteHandler suites(g_basic);
        A_long count = 0;
        if (suites.EffectSuite2()->AEGP_GetNumInstalledEffects(&count)) return;

        const AliasCatalog aliases = load_aliases();
        std::set<std::string> seen;
        AEGP_InstalledEffectKey key = AEGP_InstalledEffectKey_NONE;
        for (A_long index = 0; index < count; ++index) {
            if (suites.EffectSuite2()->AEGP_GetNextInstalledEffect(key, &key)) break;
            char name[AEGP_MAX_EFFECT_NAME_SIZE] = {};
            char match_name[AEGP_MAX_EFFECT_MATCH_NAME_SIZE] = {};
            char category[AEGP_MAX_EFFECT_CATEGORY_NAME_SIZE] = {};
            if (suites.EffectSuite2()->AEGP_GetEffectName(key, name)) continue;
            if (suites.EffectSuite2()->AEGP_GetEffectMatchName(key, match_name)) continue;
            suites.EffectSuite2()->AEGP_GetEffectCategory(key, category);
            if (!seen.insert(match_name).second) continue;

            const auto alias_match = aliases.by_match_name.find(match_name);
            const auto alias_english = aliases.by_english_name.find(name);
            const AliasEntry* alias = alias_match != aliases.by_match_name.end() ? &alias_match->second : nullptr;
            if (alias_english != aliases.by_english_name.end() && (!alias || alias_english->second.priority > alias->priority)) {
                alias = &alias_english->second;
            }
            xfx::CatalogItem item;
            item.display_name = utf8_to_wide(name);
            item.pinyin_full = alias && !alias->pinyin_full.empty()
                ? alias->pinyin_full : "";
            item.pinyin_initials = alias && !alias->pinyin_initials.empty()
                ? alias->pinyin_initials : "";
            item.english_name = name;
            item.match_name = match_name;
            if (item.display_name.empty()) item.display_name = xfx::catalog_display_name(item);
            item.translated_name = alias && !alias->chinese_name.empty()
                ? alias->chinese_name : L"";
            if (alias) item.aliases = alias->aliases;
            item.category = L"效果";
            item.source = "ae";
            item.item_type = L"effect";
            xfx::enrich_catalog_item(item);
            items.push_back(std::move(item));
        }
    } catch (...) {
    }
}

void append_presets_from(const fs::path& root, std::vector<xfx::CatalogItem>& items,
                         std::set<std::wstring>& seen) {
    if (!fs::exists(root)) return;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::wstring extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (extension != L".ffx") continue;
            const std::wstring path = entry.path().wstring();
            if (!seen.insert(path).second) continue;
            const std::wstring stem = entry.path().stem().wstring();

            xfx::CatalogItem item;
            item.display_name = stem;
            item.english_name = wide_to_utf8(stem);
            item.category = L"预设";
            item.source = "file";
            item.item_type = L"preset";
            item.file_path = path;
            xfx::enrich_catalog_item(item);
            items.push_back(std::move(item));
        }
    } catch (...) {
    }
}

std::vector<xfx::CatalogItem> scan_catalog() {
#ifdef XFX_CONSOLE_TEST
    return g_test_catalog;
#endif
    std::vector<xfx::CatalogItem> items;
    append_effects(items);

    std::set<std::wstring> seen_presets;
    const fs::path root(after_effects_root());
    wchar_t documents[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents);
    const fs::path documents_root(documents);
    append_presets_from(root / L"Presets", items, seen_presets);
    append_presets_from(root / L"Support Files" / L"Presets", items, seen_presets);

    const fs::path adobe_documents = documents_root / L"Adobe";
    if (fs::exists(adobe_documents)) {
        for (const auto& version : fs::directory_iterator(
                 adobe_documents, fs::directory_options::skip_permission_denied)) {
            if (!version.is_directory() || version.path().filename().wstring().rfind(L"After Effects", 0) != 0) continue;
            append_presets_from(version.path() / L"User Presets", items, seen_presets);
            append_presets_from(version.path() / L"Presets", items, seen_presets);
        }
    }

    const fs::path adobe_program_files = fs::path(L"C:\\Program Files\\Adobe");
    if (fs::exists(adobe_program_files)) {
        for (const auto& version : fs::directory_iterator(
                 adobe_program_files, fs::directory_options::skip_permission_denied)) {
            if (!version.is_directory() || version.path().filename().wstring().rfind(L"Adobe After Effects", 0) != 0) continue;
            append_presets_from(version.path() / L"Support Files" / L"Presets", items, seen_presets);
        }
    }
    return items;
}

struct ButtonHoverState {
    struct Ripple { POINT origin; ULONGLONG started; };
    std::vector<Ripple> ripples;
    double progress = 0;
    double start_progress = 0;
    ULONGLONG started = 0;
    bool hovered = false;
};

struct PluginState {
    HWND window = nullptr;
    HWND hotkey_window = nullptr;
    HWND search = nullptr;
    HWND filter = nullptr;
    HWND results = nullptr;
    HWND refresh = nullptr;
    HWND hotkey_edit = nullptr;
    HWND hotkey_save = nullptr;
    HWND mode_checkbox = nullptr;
    HWND status = nullptr;
    HWND count = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND hotkey_label = nullptr;
    HWND slot = nullptr;
    HWND import_settings = nullptr;
    HWND export_settings = nullptr;
    HWND light_theme = nullptr;
    HWND capture_location = nullptr;
    HWND choose_capture_location = nullptr;
    HWND capture_location_label = nullptr;
    HWND thumbnail_cache_clear = nullptr;
    HFONT font = nullptr;
    HFONT slot_number_font = nullptr;
    HFONT status_font = nullptr;
    HBRUSH background_brush = nullptr;
    HBRUSH input_brush = nullptr;
    HBRUSH search_brush = nullptr;
    xfx::SearchEngine engine;
    xfx::Settings settings;
    xfx::ImeController ime;
    xfx::GlobalHotkey hotkey;
    std::vector<xfx::CatalogItem> catalog;
    std::vector<xfx::CatalogItem> base_catalog;
    std::vector<int> visible_slot_numbers;
    HWND capture = nullptr;
    HWND gallery = nullptr;
    HWND dictionary = nullptr;
    HWND preferences = nullptr;
    HWND gallery_window = nullptr;
    HWND gallery_list = nullptr;
    HWND gallery_status = nullptr;
    HWND gallery_canvas = nullptr;
    xfx::GalleryModel gallery_model;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> gallery_images;
    int gallery_split = -1;
    RECT gallery_splitter{};
    bool gallery_dragging_splitter = false;
    int gallery_drag_view = -1;
    POINT gallery_drag_start{};
    double gallery_drag_offset_x = 0, gallery_drag_offset_y = 0;
    HWND tooltip = nullptr;
    std::wstring result_tooltip;
    std::map<UINT_PTR, std::wstring> tooltip_texts;
    std::map<HWND, ButtonHoverState> button_hovers;
    ResultAction result_hover_action = ResultAction::None;
    int result_hover_row = -1;
    HWND preview_window = nullptr;
    wchar_t suppressed_search_character = 0;
    std::vector<fs::path> captures;
    bool settings_visible = false;
    bool settings_animation_active = false;
    bool redraw_transaction = false;
    bool ime_session_active = false;
    HWND ime_source_window = nullptr;
    bool applying_item = false;
    bool result_click_pending = false;
    bool result_click_redraw_held = false;
    int result_click_row = -1;
    ResultAction result_click_action = ResultAction::None;
    bool initial_background_primed = false;
    bool initial_frame_painted = false;
    unsigned int modal_depth = 0;
    ULONG_PTR gdiplus_token = 0;
    HHOOK keyboard_hook = nullptr;
    xfx::HotkeySpec hotkey_spec;
    bool hotkey_key_down = false;
    bool drag_pending = false;
    bool drag_active = false;
    POINT drag_anchor_cursor{};
    POINT drag_anchor_window{};
    bool search_composing = false;
    SIZE corner_size{};
    bool status_important = false;
    std::optional<COLORREF> host_background;
    xfx::ThemeColors palette = xfx::resolve_theme(xfx::ThemeMode::Follow);
    ULONGLONG last_theme_check = 0;
    ULONGLONG status_expires = 0;
    ULONGLONG expand_started = 0;
    int expand_from = 0, expand_to = 0, settings_extent = 0;

    PluginState() : engine({}) {}
};

PluginState* g_state = nullptr;
const xfx::ThemeColors& colors() { return g_state ? g_state->palette : xfx::theme_colors(false); }
int ui_px(int value) {const UINT dpi=g_state && IsWindow(g_state->window)?GetDpiForWindow(g_state->window):96;return MulDiv(value,dpi,96);}
bool effective_light_theme() { return xfx::is_light_background(colors().background); }
bool reduced_motion() {
    BOOL enabled = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled;
}

void repaint_theme_windows() {
    xfx::script_bridge().set_theme(colors());
    for (HWND window : {g_state->window, g_state->gallery_window, g_state->preview_window}) {
        if (IsWindow(window)) RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
    }
    if (g_state->tooltip) {
        SendMessageW(g_state->tooltip, TTM_SETTIPBKCOLOR, colors().surface, 0);
        SendMessageW(g_state->tooltip, TTM_SETTIPTEXTCOLOR, colors().text, 0);
    }
}
struct ThemeBrushes {
    HBRUSH background=nullptr,input=nullptr,search=nullptr;
    explicit ThemeBrushes(bool light):ThemeBrushes(xfx::theme_colors(light)) {}
    explicit ThemeBrushes(const xfx::ThemeColors& palette) {
        background=CreateSolidBrush(palette.background);input=CreateSolidBrush(palette.input);search=CreateSolidBrush(palette.search);
        if(!background || !input || !search) {
            if(background)DeleteObject(background);if(input)DeleteObject(input);if(search)DeleteObject(search);
            throw std::bad_alloc();
        }
    }
    ~ThemeBrushes(){if(background)DeleteObject(background);if(input)DeleteObject(input);if(search)DeleteObject(search);}
    ThemeBrushes(const ThemeBrushes&)=delete;ThemeBrushes& operator=(const ThemeBrushes&)=delete;
    void install(){std::swap(background,g_state->background_brush);std::swap(input,g_state->input_brush);std::swap(search,g_state->search_brush);repaint_theme_windows();}
};

void refresh_host_theme(bool force = false) {
    if (!g_state) return;
    const auto now = GetTickCount64();
    if (!force && now - g_state->last_theme_check < 500) return;
    g_state->last_theme_check = now;
    std::optional<COLORREF> background;
    if (g_basic) {
        try {
            AEGP_SuiteHandler suites(g_basic);
            PF_App_Color color{};
            if (!suites.AppSuite6()->PF_AppGetBgColor(&color)) {
                // PF_App_Color uses 16-bit channels, unlike COLORREF.
                background = RGB((color.red + 128) / 257, (color.green + 128) / 257, (color.blue + 128) / 257);
            }
        } catch (...) { /* Missing host suite falls back to preview AE gray. */ }
    }
    const auto palette = xfx::resolve_theme(g_state->settings.theme_mode, background);
    g_state->host_background = background;
    if (!force && palette.background == colors().background && palette.button == colors().button) return;
    ThemeBrushes brushes(palette);
    g_state->palette = palette;
    brushes.install();
}
LRESULT theme_control_color(UINT message,HDC dc,HWND control=nullptr) {
    const bool search=control==g_state->search;
    const bool quiet=control==g_state->status && !g_state->status_important;
    SetTextColor(dc,quiet?colors().quiet:colors().text);
    const bool background=message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN;
    SetBkColor(dc,background?colors().background:search?colors().search:colors().input);
    return reinterpret_cast<LRESULT>(background?g_state->background_brush:search?g_state->search_brush:g_state->input_brush);
}

constexpr UINT_PTR kButtonHoverSubclassId = 0xB001;
constexpr UINT_PTR kButtonHoverTimerId = 0xB002;
constexpr UINT_PTR kSettingsAnimationTimerId = 0xB004;
constexpr double kButtonHoverDurationMs = 160.0;

ULONGLONG button_hover_time() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_button_hover_time) return *g_test_button_hover_time;
#endif
    return GetTickCount64();
}

void advance_button_hover(ButtonHoverState& state) {
    const double elapsed = static_cast<double>(button_hover_time() - state.started) / kButtonHoverDurationMs;
    state.progress = state.hovered ? (std::min)(1.0, state.start_progress + elapsed)
        : (std::max)(0.0, state.start_progress - elapsed);
}
void start_button_ripple(HWND hwnd, POINT point) {
    if (!g_state || !IsWindowEnabled(hwnd) || reduced_motion()) return;
    auto& waves = g_state->button_hovers[hwnd].ripples;
    if (waves.size() >= 12) waves.erase(waves.begin());
    waves.push_back({point, button_hover_time()});
    SetTimer(hwnd, kButtonHoverTimerId, 15, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool forward_edge_drag(HWND child, UINT message, WPARAM wparam, LPARAM lparam);

LRESULT CALLBACK button_hover_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR) {
    if (g_state) {
        const auto found = g_state->button_hovers.find(hwnd);
        if (found != g_state->button_hovers.end()) {
            auto& state = found->second;
            if (message == WM_LBUTTONDOWN && IsWindowEnabled(hwnd) && hwnd != g_state->preferences)
                start_button_ripple(hwnd, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (((message == WM_KEYDOWN && (wparam == VK_SPACE || wparam == VK_RETURN) && !(lparam & (1L << 30))) || message == BM_CLICK) &&
                hwnd != g_state->preferences) {
                RECT rect{}; GetClientRect(hwnd, &rect);
                start_button_ripple(hwnd, {rect.right / 2, rect.bottom / 2});
            }
            if ((message == WM_MOUSEMOVE && IsWindowEnabled(hwnd)) || message == WM_MOUSELEAVE) {
                RECT client{}; GetClientRect(hwnd, &client);
                const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                // Buttons retain mouse capture while pressed. Captured moves
                // outside their client area must continue the fade out.
                const bool hovered = message == WM_MOUSEMOVE && PtInRect(&client, point);
                if (hovered != state.hovered) {
                    advance_button_hover(state);
                    state.start_progress = state.progress;
                    state.started = button_hover_time();
                    state.hovered = hovered;
                    state.progress = hovered ? 1.0 : 0.0;
                    if (hovered) {
                        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
                        TrackMouseEvent(&tracking);
                    }
                    if (state.ripples.empty()) KillTimer(hwnd, kButtonHoverTimerId);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (message == WM_TIMER && wparam == kButtonHoverTimerId) {
                auto& waves = state.ripples;
                waves.erase(std::remove_if(waves.begin(), waves.end(), [](const auto& wave) {
                    return button_hover_time() - wave.started >= 700;
                }), waves.end());
                state.progress = state.hovered ? 1.0 : 0.0;
                if (waves.empty()) KillTimer(hwnd, kButtonHoverTimerId);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if ((message == WM_SHOWWINDOW || message == WM_ENABLE) && !wparam) {
                KillTimer(hwnd, kButtonHoverTimerId);
                state = {};
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (message == WM_NCDESTROY) {
                KillTimer(hwnd, kButtonHoverTimerId);
                g_state->button_hovers.erase(found);
                RemoveWindowSubclass(hwnd, button_hover_proc, subclass);
            }
        }
    }
    if (message == WM_ERASEBKGND) return TRUE;
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

COLORREF button_background_color(HWND button, COLORREF base) {
    const auto found = g_state->button_hovers.find(button);
    if (found == g_state->button_hovers.end()) return base;
    const double amount = found->second.progress;
    const auto target = colors().button_pressed;
    const auto blend = [amount](int first, int second) {
        return static_cast<int>(first + (second - first) * amount + 0.5);
    };
    return RGB(blend(GetRValue(base), GetRValue(target)),
        blend(GetGValue(base), GetGValue(target)), blend(GetBValue(base), GetBValue(target)));
}

Gdiplus::Color vector_color(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}
void paint_button_surface(const DRAWITEMSTRUCT& draw, bool primary = false) {
    SetDCBrushColor(draw.hDC, colors().background);
    FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    Gdiplus::Graphics graphics(draw.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const auto& c = colors();
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool selected = primary || (draw.CtlID == kPreferencesId && g_state->settings_visible);
    COLORREF base = selected ? c.selected : c.button;
    if (!disabled) base = draw.itemState & ODS_SELECTED ? c.button_pressed : button_background_color(draw.hwndItem, base);
    Gdiplus::RectF rect(static_cast<float>(draw.rcItem.left) + .5f, static_cast<float>(draw.rcItem.top) + .5f,
        static_cast<float>(draw.rcItem.right - draw.rcItem.left) - 1, static_cast<float>(draw.rcItem.bottom - draw.rcItem.top) - 1);
    const float radius = 4.0f * GetDpiForWindow(draw.hwndItem) / 96.0f;
    Gdiplus::GraphicsPath clip;
    xfx::rounded_path(clip, rect, radius);
    Gdiplus::SolidBrush brush(vector_color(base)); graphics.FillPath(&brush, &clip);
    graphics.SetClip(&clip);
    const auto found = g_state->button_hovers.find(draw.hwndItem);
    if (!disabled && !reduced_motion() && found != g_state->button_hovers.end()) for (const auto& wave : found->second.ripples) {
        const double t = (std::min)(1.0, (button_hover_time() - wave.started) / 700.0);
        const double dx = (std::max)(wave.origin.x - draw.rcItem.left, draw.rcItem.right - wave.origin.x);
        const double dy = (std::max)(wave.origin.y - draw.rcItem.top, draw.rcItem.bottom - wave.origin.y);
        const float r = static_cast<float>(std::hypot(dx, dy) * (1 - std::pow(1-t, 3)));
        Gdiplus::SolidBrush ink(vector_color(selected ? c.selected_text : c.accent, static_cast<BYTE>(85 * (1-t))));
        graphics.FillEllipse(&ink, wave.origin.x-r, wave.origin.y-r, 2*r, 2*r);
    }
    graphics.ResetClip();
    Gdiplus::Pen border(vector_color((draw.itemState & ODS_FOCUS) ? c.accent : c.divider), (draw.itemState & ODS_FOCUS) ? 2.0f : 1.0f);
    graphics.DrawPath(&border, &clip);
}

void paint_text_button(const DRAWITEMSTRUCT& draw) {
    const int saved = SaveDC(draw.hDC);
    if (!saved) return;
    if (g_state->font) SelectObject(draw.hDC, g_state->font);
    const auto& palette = colors();
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    paint_button_surface(draw, draw.CtlID == kHotkeySaveId);
    wchar_t text[256]{}; GetWindowTextW(draw.hwndItem, text, 256);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, draw.itemState & ODS_DISABLED ? palette.disabled : draw.CtlID == kHotkeySaveId ? palette.selected_text : palette.text);
    RECT text_rect = draw.rcItem;
    DrawTextW(draw.hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(draw.hDC, saved);
}
struct ModalGuard {
    ModalGuard() { if (g_state) ++g_state->modal_depth; }
    ~ModalGuard() { if (g_state) --g_state->modal_depth; }
};
struct BusyFeedback {
    std::vector<std::pair<HWND, bool>> controls;
    std::unique_ptr<xfx::LoadingSurface> surface;
    explicit BusyFeedback(const wchar_t* text) {
        if (!g_state) return;
        HWND anchor = IsWindowVisible(g_state->window) ? g_state->window : GetForegroundWindow();
        UINT dpi = anchor ? GetDpiForWindow(anchor) : 96; if (!dpi) dpi = 96;
        const float scale = dpi / 96.0f;
        RECT bounds{}; if (anchor) GetWindowRect(anchor,&bounds);
        const int width = static_cast<int>(320*scale), height = static_cast<int>(34*scale);
        bounds.left += 8; bounds.top = bounds.bottom+4; bounds.right = bounds.left+width; bounds.bottom = bounds.top+height;
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int x = (std::max)(monitor.rcWork.left,(std::min)(bounds.left,monitor.rcWork.right-width));
            const int y = (std::max)(monitor.rcWork.top,(std::min)(bounds.top,monitor.rcWork.bottom-height));
            bounds = {x,y,x+width,y+height};
        }
        surface = std::make_unique<xfx::LoadingSurface>(bounds,colors(),text,scale,reduced_motion());
        for (HWND control : {g_state->capture,g_state->refresh,g_state->hotkey_save,g_state->import_settings,g_state->export_settings}) {
            if (IsWindow(control)) { controls.emplace_back(control,IsWindowEnabled(control)!=FALSE); EnableWindow(control,FALSE); }
        }
    }
    ~BusyFeedback() {
        surface.reset();
        for (const auto& [control,enabled] : controls) if(IsWindow(control)) EnableWindow(control,enabled);
    }
};
WNDPROC g_search_proc = nullptr;
WNDPROC g_results_proc = nullptr;
LRESULT CALLBACK hotkey_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

bool hotkey_event_matches(UINT message, UINT virtual_key, UINT modifiers,
                          const xfx::HotkeySpec& spec) {
    return (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        virtual_key == spec.virtual_key && modifiers == spec.modifiers;
}

UINT current_hotkey_modifiers() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    return modifiers;
}

LRESULT CALLBACK console_keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && wparam == PM_REMOVE && g_state && g_state->modal_depth == 0) {
        auto* message = reinterpret_cast<MSG*>(lparam);
        if (GetFocus() != g_state->hotkey_edit &&
            hotkey_event_matches(message->message, static_cast<UINT>(message->wParam),
                                 current_hotkey_modifiers(), g_state->hotkey_spec)) {
            DWORD process = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            if (process == GetCurrentProcessId()) {
                if (!(message->lParam & (1L << 30))) PostMessageW(g_state->hotkey_window, WM_APP + 1, 0, 0);
                message->message = WM_NULL;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK console_low_level_keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && g_state && g_state->hotkey_window && lparam) {
        auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
        if (up && event->vkCode == g_state->hotkey_spec.virtual_key) {
            g_state->hotkey_key_down = false;
        } else if (down && g_state->modal_depth == 0 &&
                   GetFocus() != g_state->hotkey_edit &&
                   !g_state->hotkey_key_down &&
                   hotkey_event_matches(static_cast<UINT>(wparam), event->vkCode,
                                        current_hotkey_modifiers(),
                                         g_state->hotkey_spec)) {
            DWORD process = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            if (process == GetCurrentProcessId()) {
                g_state->hotkey_key_down = true;
                PostMessageW(g_state->hotkey_window, WM_APP + 1, 0, 0);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

HMODULE plugin_module_handle() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&console_low_level_keyboard_hook), &module);
    return module;
}

bool register_configured_hotkey() {
    if (!g_state || !g_state->hotkey_window) return false;
    xfx::HotkeySpec spec;
    if (!xfx::parse_hotkey(g_state->settings.hotkey, &spec)) return false;
    const auto record_registration = [&](HWND window, bool registered, DWORD error) {
        if (!window) return;
        SetPropW(window, L"XFXHotkeyRegistered",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(registered ? 1 : 0)));
        SetPropW(window, L"XFXHotkeyLastError",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(error)));
        SetPropW(window, L"XFXHotkeyModifiers",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(spec.modifiers)));
        SetPropW(window, L"XFXHotkeyVirtualKey",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(spec.virtual_key)));
    };
    // Register against the message-only window first. The previous thread
    // hook depended on plugin initialization happening on AE's UI thread and
    // missed real keyboard messages in some host sessions.
    SetLastError(ERROR_SUCCESS);
    if (g_state->hotkey.register_hotkey(g_state->hotkey_window, kHotkeyId,
                                        g_state->settings.hotkey)) {
        record_registration(g_state->hotkey_window, true, ERROR_SUCCESS);
        g_state->hotkey_spec = spec;
        g_state->hotkey_key_down = false;
        // RegisterHotKey is the primary path. Keeping the low-level fallback
        // active at the same time would deliver every press twice and make a
        // toggle shortcut open then close the console immediately.
        if (g_state->keyboard_hook) {
            UnhookWindowsHookEx(g_state->keyboard_hook);
            g_state->keyboard_hook = nullptr;
        }
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookInstalled",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(0)));
        return true;
    }
    const DWORD registration_error = GetLastError();
    g_state->hotkey.unregister();
    if (!g_state->keyboard_hook) {
        SetLastError(ERROR_SUCCESS);
        g_state->keyboard_hook = SetWindowsHookExW(
            WH_KEYBOARD_LL, console_low_level_keyboard_hook, plugin_module_handle(), 0);
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookInstalled",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(g_state->keyboard_hook ? 1 : 0)));
        SetPropW(g_state->hotkey_window, L"XFXHotkeyHookLastError",
                 reinterpret_cast<HANDLE>(static_cast<INT_PTR>(
                     g_state->keyboard_hook ? ERROR_SUCCESS : GetLastError())));
    }
    if (!g_state->keyboard_hook) {
        record_registration(g_state->hotkey_window, false, registration_error);
        return false;
    }
    record_registration(g_state->hotkey_window, false, registration_error);
    g_state->hotkey_spec = spec;
    return true;
}

bool ensure_hotkey_window() {
    if (!g_state) return false;
    if (g_state->hotkey_window && IsWindow(g_state->hotkey_window)) return true;
    WNDCLASSEXW hotkey_class{sizeof(hotkey_class)};
    hotkey_class.hInstance = GetModuleHandleW(nullptr);
    hotkey_class.lpfnWndProc = hotkey_window_proc;
    hotkey_class.lpszClassName = kHotkeyWindowClassName;
    RegisterClassExW(&hotkey_class);
    g_state->hotkey_window = CreateWindowExW(
        0, kHotkeyWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
        hotkey_class.hInstance, nullptr);
    if (!g_state->hotkey_window) return false;
    return register_configured_hotkey();
}

void set_status(const std::wstring& message, bool success = false) {
    if (g_state && g_state->status) {
        SetWindowTextW(g_state->status, message.c_str());
        g_state->status_important=message.find(L"失败")!=std::wstring::npos || message.find(L"不可用")!=std::wstring::npos || message.find(L"部分完成")!=std::wstring::npos;
        SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_important?g_state->font:g_state->status_font),TRUE);
        // Call sites classify actual outcomes; message wording never decides expiry.
        g_state->status_expires = success ? GetTickCount64() + 2000 : 0;
        if (g_state->window) { KillTimer(g_state->window,0xB003); if(success) SetTimer(g_state->window,0xB003,2000,nullptr); }
    }
}

void set_font(HWND control) {
    if (g_state && g_state->font && control) SendMessageW(control, WM_SETFONT,
                                                            reinterpret_cast<WPARAM>(g_state->font), TRUE);
    if (g_state && control == g_state->search)
        SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

HWND current_host_input_window() {
    const HWND foreground = GetForegroundWindow();
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(foreground, &process);
    if (!thread || process != GetCurrentProcessId()) return nullptr;
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(thread, &info)) return nullptr;
    const HWND focus = info.hwndFocus ? info.hwndFocus : foreground;
    if (g_state->window && (focus == g_state->window || IsChild(g_state->window, focus))) return nullptr;
    return focus;
}

bool ensure_search_english_mode() {
    if (!g_state->settings.use_english_mode) return true;
    if (!g_state->ime_session_active)
        g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
    if (g_state->ime_session_active && g_state->ime.enable_english_mode()) return true;
    // A rejected switch may have rolled its backend snapshot back already.
    // Let the next focus retry capture a fresh session instead of skipping it.
    g_state->ime_session_active = false;
    set_status(L"当前输入法未能切换为英文模式，保持原状态");
    return false;
}

void begin_window_drag(HWND hwnd) {
    if (!hwnd) return;
    POINT cursor{}; GetCursorPos(&cursor);
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
}

bool point_is_drag_edge(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (point.x < -1 || point.y < -1 || point.x > client.right || point.y > client.bottom) return false;
    return point.x <= kDragEdge || point.y <= kDragEdge ||
        point.x >= client.right - kDragEdge || point.y >= client.bottom - kDragEdge;
}

bool point_is_window_edge(HWND hwnd, POINT screen_point) {
    RECT window{};
    if (!GetWindowRect(hwnd, &window)) return false;
    return screen_point.x >= window.left && screen_point.x <= window.right &&
        screen_point.y >= window.top && screen_point.y <= window.bottom &&
        (screen_point.x <= window.left + kDragEdge || screen_point.y <= window.top + kDragEdge ||
         screen_point.x >= window.right - kDragEdge || screen_point.y >= window.bottom - kDragEdge);
}

bool forward_edge_drag(HWND child, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!g_state || !g_state->window || g_state->modal_depth || message != WM_LBUTTONDOWN) return false;
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ClientToScreen(child, &point);
    ScreenToClient(g_state->window, &point);
    if (!point_is_drag_edge(g_state->window, point)) return false;
    SendMessageW(g_state->window, WM_LBUTTONDOWN, wparam, MAKELPARAM(point.x, point.y));
    return true;
}

constexpr UINT_PTR kEdgeDragSubclassId = 0xB00A;
LRESULT CALLBACK edge_drag_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, edge_drag_proc, id);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    if (forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

bool drag_button_is_held() {
#ifdef XFX_CONSOLE_TEST
    if (g_test_drag_button_held) return *g_test_drag_button_held;
#endif
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void save_hotkey();
void toggle_console();
void save_settings();
bool execute_selected();
bool defer_result_click(UINT message, int row, bool modal) {
    return !modal && row >= 0 &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP);
}
void toggle_favorite();
void toggle_favorite_key(const std::wstring& key);
void toggle_pinned(const std::wstring& key);
void assign_next_slot();
ResultAction result_action_at_point(POINT point);
void update_result_tooltip(ResultAction action, int row);
bool shortcut_list_mode();
std::wstring slot_display_label(int slot);
std::wstring item_key(const xfx::CatalogItem& item);
void assign_slot(int slot);
xfx::ScriptReply execute_slot(int slot);
void populate_results();
void rebuild_customized_catalog();
void show_shortcut_menu();
void adjust_console_height();
xfx::BridgeCompletion save_current_snapshot();
void clear_result_data();
bool add_tooltip_for_control(HWND parent, HWND tooltip, HWND control, UINT_PTR id, const wchar_t* text);

bool try_digit_shortcut(WPARAM key, LPARAM flags, bool in_search) {
    int slot = key >= '1' && key <= '9' ? static_cast<int>(key - '1') :
        (key >= VK_NUMPAD1 && key <= VK_NUMPAD9 ? static_cast<int>(key - VK_NUMPAD1) : -1);
    if (slot < 0 || (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_SHIFT) & 0x8000) || ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ||
        GetWindowTextLengthW(g_state->search) != 0 ||
        g_state->settings.slots[static_cast<size_t>(slot)].empty()) return false;
    if (in_search) g_state->suppressed_search_character = static_cast<wchar_t>('1' + slot);
    if (!(flags & (1L << 30))) execute_slot(slot);
    return true;
}

template<typename Callback> LRESULT guarded_callback(Callback action) {
    try { return action(); }
    catch (...) { set_status(L"操作未保存，请检查文档目录的写入权限"); return 0; }
}

struct ToolbarLayout {
    RECT search_surface{}, search_edit{}, filter{};
    RECT buttons[3]{};
};

int search_line_height(HWND hwnd) {
    const HWND search = g_state ? g_state->search : nullptr;
    HDC dc = GetDC(search ? search : hwnd);
    if (!dc) return MulDiv(xfx::kUiBodyPixels, GetDpiForWindow(hwnd), 96);
    const auto font = search ? reinterpret_cast<HFONT>(SendMessageW(search, WM_GETFONT, 0, 0)) : g_state->font;
    const auto previous = SelectObject(dc, font ? font : g_state->font);
    TEXTMETRICW metrics{};
    const bool measured = GetTextMetricsW(dc, &metrics) != FALSE;
    SelectObject(dc, previous);
    ReleaseDC(search ? search : hwnd, dc);
    return measured ? metrics.tmHeight : MulDiv(xfx::kUiBodyPixels, GetDpiForWindow(hwnd), 96);
}

ToolbarLayout toolbar_layout(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT dpi = (std::max)(1u, GetDpiForWindow(hwnd));
    const int width = MulDiv(client.right, 96, static_cast<int>(dpi));
    const int button_size = kToolbarButtonSize;
    const int button_gap = kToolbarGap;
    const int right_margin = 8;
    const int tools_left = width - (button_size * 3 + button_gap * 2 + right_margin);
    const int filter_left = tools_left - 68;
    const int center_y = kToolbarCenterY;
    const auto px = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const auto rect = [&](int left, int top, int right, int bottom) {
        return RECT{px(left), px(top), px(right), px(bottom)};
    };
    ToolbarLayout result;
    result.search_surface = rect(8, center_y - 18, filter_left - 4, center_y + 18);
    const int line_height = search_line_height(hwnd);
    const int line_top = px(center_y) - line_height / 2;
    result.search_edit = {px(34), line_top, px(filter_left - 8), line_top + line_height};
    result.filter = rect(filter_left, center_y - 18, filter_left + 64, center_y + 18);
    for (int index = 0; index < 3; ++index) {
        const int left = tools_left + index * (button_size + button_gap);
        result.buttons[index] = rect(left, center_y - button_size / 2,
            left + button_size, center_y + button_size / 2);
    }
    return result;
}

LRESULT CALLBACK hotkey_edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_SETFONT) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(wparam));
        if (lparam) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_GETFONT) return GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_SETTEXT) {
        const auto result = DefWindowProcW(hwnd, message, wparam, lparam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wparam);
        const int saved = SaveDC(dc);
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        FillRect(dc, &bounds, g_state->input_brush);
        const auto font = reinterpret_cast<HFONT>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        SelectObject(dc, font ? font : g_state->font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(hwnd) ? colors().text : colors().disabled);
        wchar_t text[128]{}; GetWindowTextW(hwnd, text, 128);
        const int margin = MulDiv(4, GetDpiForWindow(hwnd), 96);
        bounds.left += margin; bounds.right -= margin;
        DrawTextW(dc, text, -1, &bounds, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (saved) RestoreDC(dc, saved);
        if (message == WM_PAINT) EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        SetFocus(hwnd);
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        const UINT key = static_cast<UINT>(wparam);
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU ||
            key == VK_LWIN || key == VK_RWIN) {
            set_status(L"继续按一个字母、数字或功能键");
            return 0;
        }

        xfx::HotkeySpec spec;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) spec.modifiers |= MOD_CONTROL;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) spec.modifiers |= MOD_ALT;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) spec.modifiers |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            spec.modifiers |= MOD_WIN;
        }
        if (spec.modifiers == 0) {
            set_status(L"请按组合键，例如 Ctrl+K 或 Ctrl+Space");
            return 0;
        }
        spec.virtual_key = key;
        const std::wstring text = xfx::format_hotkey(spec);
        if (!text.empty()) {
            SetWindowTextW(hwnd, text.c_str());
            save_hotkey();
        }
        return 0;
    }
    if (message == WM_CHAR || message == WM_SYSCHAR || message == WM_KEYUP || message == WM_SYSKEYUP) return 0;
    return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}

void cycle_filter() {
    const LRESULT selected = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    SendMessageW(g_state->filter, CB_SETCURSEL, (selected + 1) % 3, 0);
    populate_results();
}

LRESULT CALLBACK search_edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_ERASEBKGND) return TRUE;
    if (forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    if(message==WM_IME_STARTCOMPOSITION) {
        g_state->search_composing=true;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if(message==WM_IME_ENDCOMPOSITION || message==WM_NCDESTROY)g_state->search_composing=false;
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_IME_ENDCOMPOSITION) {
        InvalidateRect(hwnd,nullptr,FALSE);
        const HWND parent = GetParent(hwnd);
        if (IsWindow(parent)) {
            const auto layout = toolbar_layout(parent);
            InvalidateRect(parent, &layout.search_surface, FALSE);
        }
    }
    if((message==WM_PAINT || message==WM_PRINTCLIENT) && !GetWindowTextLengthW(hwnd) &&
       !g_state->search_composing) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_search_hint_paint_count;
#endif
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wparam);
        RECT rect{};GetClientRect(hwnd,&rect);FillRect(dc,&rect,g_state->search_brush);
        const int saved=SaveDC(dc);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        SelectObject(dc,font ? font : g_state->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,colors().hint);
        wchar_t cue[256]{};
        if(!SendMessageW(hwnd,EM_GETCUEBANNER,reinterpret_cast<WPARAM>(cue),256))
            wcscpy_s(cue,L"输入中文、拼音、英文或 matchName");
        DrawTextW(dc,cue,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if(saved)RestoreDC(dc,saved);if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;
    }
    if (message == WM_CHAR && g_state->suppressed_search_character) {
        const bool suppress = wparam == g_state->suppressed_search_character;
        g_state->suppressed_search_character = 0;
        if (suppress) return 0;
    }
    if (message == WM_KEYDOWN) {
        if (try_digit_shortcut(wparam, lparam, true)) return 0;
        if (wparam == VK_RETURN) {
            execute_selected();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wparam == VK_DOWN) {
            SetFocus(g_state->results);
            return 0;
        }
        if (wparam == VK_TAB) {
            cycle_filter();
            return 0;
        }
    }
    if (message == WM_PAINT) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_search_default_paint_count;
#endif
    }
    const LRESULT result = CallWindowProcW(g_search_proc, hwnd, message, wparam, lparam);
    if (message == WM_SETFOCUS && IsWindowVisible(GetParent(hwnd))) ensure_search_english_mode();
    return result;
    });
}

LRESULT CALLBACK results_list_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_ERASEBKGND) {
#ifdef XFX_CONSOLE_TEST
        ++g_test_result_erase_count;
#endif
        return TRUE;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width > 0 && height > 0) {
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
            if (buffer && bitmap) {
                const HGDIOBJ previous = SelectObject(buffer, bitmap);
                FillRect(buffer, &client, g_state->input_brush);
                CallWindowProcW(g_results_proc, hwnd, WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
                BitBlt(target, paint.rcPaint.left, paint.rcPaint.top,
                    paint.rcPaint.right - paint.rcPaint.left,
                    paint.rcPaint.bottom - paint.rcPaint.top,
                    buffer, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
                SelectObject(buffer, previous);
                DeleteObject(bitmap);
                DeleteDC(buffer);
#ifdef XFX_CONSOLE_TEST
                ++g_test_result_buffered_paint_count;
#endif
            } else {
                if (bitmap) DeleteObject(bitmap);
                if (buffer) DeleteDC(buffer);
                CallWindowProcW(g_results_proc, hwnd, WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(target), PRF_CLIENT);
            }
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    bool result_action_click = false;
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        if (!HIWORD(hit)) {
            result_action_click = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) != ResultAction::None;
        }
    }
    if (!result_action_click && forward_edge_drag(hwnd, message, wparam, lparam)) return 0;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
        const auto action = row >= 0 ? result_action_at_point({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) : ResultAction::None;
        const auto previous_action = g_state->result_hover_action;
        const auto previous_row = g_state->result_hover_row;
        update_result_tooltip(action, row);
        bool selection_changed = false;
        RECT previous_selection_rect{};
        RECT next_selection_rect{};
        if (row >= 0) {
            const LRESULT current_selection = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (current_selection != row) {
                const bool has_previous_selection = current_selection >= 0 &&
                    SendMessageW(hwnd, LB_GETITEMRECT, current_selection,
                        reinterpret_cast<LPARAM>(&previous_selection_rect)) != LB_ERR;
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                SendMessageW(hwnd, LB_SETCURSEL, row, 0);
                if (!g_state->result_click_redraw_held)
                    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                const bool has_next_selection = SendMessageW(hwnd, LB_GETITEMRECT, row,
                    reinterpret_cast<LPARAM>(&next_selection_rect)) != LB_ERR;
                if (has_previous_selection) InvalidateRect(hwnd, &previous_selection_rect, FALSE);
                if (has_next_selection) InvalidateRect(hwnd, &next_selection_rect, FALSE);
                selection_changed = true;
            }
        }
        if (previous_action != action || previous_row != row) {
            RECT dirty{};
            if (previous_row >= 0 && SendMessageW(hwnd, LB_GETITEMRECT, previous_row,
                reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
            if (row >= 0 && row != previous_row && SendMessageW(hwnd, LB_GETITEMRECT, row,
                reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
        }
        if (selection_changed && !g_state->result_click_redraw_held) UpdateWindow(hwnd);
        // The owner-drawn list already updates selection and action hover
        // state above. Letting LISTBOX process every mouse move causes a full
        // row repaint and visible flashing over the search results.
        return 0;
    }
    if (message == WM_MOUSELEAVE) {
        const int previous_row = g_state->result_hover_row;
        update_result_tooltip(ResultAction::None, -1);
        g_state->result_hover_action = ResultAction::None;
        g_state->result_hover_row = -1;
        RECT dirty{};
        if (previous_row >= 0 && SendMessageW(hwnd, LB_GETITEMRECT, previous_row,
            reinterpret_cast<LPARAM>(&dirty)) != LB_ERR) InvalidateRect(hwnd, &dirty, FALSE);
        return 0;
    }
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL) {
        // LISTBOX scrolls by invalidating and blitting several intermediate
        // regions. Hold redraw while the native control updates its index,
        // then present one complete owner-drawn frame.
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        const LRESULT result = CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        return result;
    }
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const int row = HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit));
        if (defer_result_click(message, row,
            g_state->modal_depth != 0 || g_state->applying_item)) {
            g_state->result_click_pending = true;
            g_state->result_click_row = row;
            g_state->result_click_action = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            // Preserve LISTBOX's normal focus behavior without dispatching its
            // default click painting.
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            g_state->result_click_redraw_held = true;
            SetFocus(hwnd);
            SetCapture(hwnd);
            return 0;
        }
    }
    if (message == WM_LBUTTONUP) {
        if (g_state->result_click_pending) {
            const int pending_row = g_state->result_click_row;
            const auto pending_action = g_state->result_click_action;
            g_state->result_click_pending = false;
            g_state->result_click_row = -1;
            g_state->result_click_action = ResultAction::None;
            if (GetCapture() == hwnd) ReleaseCapture();

            const auto restore_redraw = [&](bool update) {
                if (!g_state->result_click_redraw_held) return;
                g_state->result_click_redraw_held = false;
                if (!IsWindow(hwnd)) return;
                SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                if (update) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateWindow(hwnd);
                }
            };

            const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            if (HIWORD(hit) || static_cast<int>(LOWORD(hit)) != pending_row) {
                restore_redraw(true);
                return 0;
            }
            const auto release_action = result_action_at_point(
                {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (release_action != pending_action) {
                restore_redraw(true);
                return 0;
            }

            // Commit the selection while redraw is held. The next operation is
            // either an action refresh or ApplyPresentation, so no native
            // pressed/selected frame can escape between the click and action.
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            SendMessageW(hwnd, LB_SETCURSEL, pending_row, 0);

            const auto action = release_action;
            if (action != ResultAction::None) {
                const auto data = SendMessageW(hwnd, LB_GETITEMDATA, pending_row, 0);
                if (data != LB_ERR && data != 0) {
                    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
                    if (item->item_type != L"empty") {
                        const auto key = item_key(*item);
                        if (action == ResultAction::Pin) toggle_pinned(key);
                        if (action == ResultAction::Favorite) toggle_favorite_key(key);
                        if (action == ResultAction::Shortcut) assign_next_slot();
                    }
                }
                restore_redraw(true);
                return 0;
            }
            execute_selected();
            restore_redraw(true);
            return 0;
        }
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        const auto action = HIWORD(hit) ? ResultAction::None :
            result_action_at_point({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        const LRESULT result = CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
        if (!HIWORD(hit)) {
            SendMessageW(hwnd, LB_SETCURSEL, LOWORD(hit), 0);
            if (action != ResultAction::None) {
                const auto data = SendMessageW(hwnd, LB_GETITEMDATA, LOWORD(hit), 0);
                if (data != LB_ERR && data != 0) {
                    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
                    if (item->item_type == L"empty") return result;
                    const auto key = item_key(*item);
                    if (action == ResultAction::Pin) toggle_pinned(key);
                    if (action == ResultAction::Favorite) toggle_favorite_key(key);
                    if (action == ResultAction::Shortcut) assign_next_slot();
                }
                return result;
            }
            execute_selected();
        }
        return result;
    }
    if ((message == WM_CAPTURECHANGED || message == WM_CANCELMODE) &&
        g_state->result_click_pending) {
        g_state->result_click_pending = false;
        if (g_state->result_click_redraw_held) {
            g_state->result_click_redraw_held = false;
            if (IsWindow(hwnd)) SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        }
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        if (message == WM_CANCELMODE && GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    if (message == WM_KEYDOWN) {
        if (try_digit_shortcut(wparam, lparam, false)) return 0;
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wparam == VK_RETURN) {
            execute_selected();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wparam == VK_TAB) {
            cycle_filter();
            return 0;
        }
        if (wparam >= '1' && wparam <= '9' && control) {
            const int slot = static_cast<int>(wparam - '1');
            if (shift) assign_slot(slot);
            else execute_slot(slot);
            return 0;
        }
    }
    return CallWindowProcW(g_results_proc, hwnd, message, wparam, lparam);
    });
}

void clear_result_data() {
    if (!g_state || !g_state->results) return;
    const LRESULT count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, i, 0);
        if (data != LB_ERR && data != 0) delete reinterpret_cast<xfx::CatalogItem*>(data);
    }
}

std::wstring filter_name() {
    const LRESULT selected = SendMessageW(g_state->filter, CB_GETCURSEL, 0, 0);
    if (selected == 1) return L"效果";
    if (selected == 2) return L"预设";
    if (selected == 3) return L"收藏";
    if (selected == 4) return L"最近";
    return L"全部";
}

std::wstring item_key(const xfx::CatalogItem& item) {
    return xfx::catalog_item_key(item);
}

std::wstring item_display_label(const xfx::CatalogItem& item) {
    const auto original = xfx::catalog_display_name(item);
    return item.translated_name.empty()
        ? original
        : xfx::bilingual_effect_label(original, item.translated_name);
}

ResultAction result_action_at_point(POINT point) {
    if (!g_state || !g_state->results) return ResultAction::None;
    if (shortcut_list_mode()) return ResultAction::None;
    const LRESULT hit = SendMessageW(g_state->results, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
    if (HIWORD(hit)) return ResultAction::None;
    RECT row{};
    if (SendMessageW(g_state->results, LB_GETITEMRECT, LOWORD(hit), reinterpret_cast<LPARAM>(&row)) == LB_ERR)
        return ResultAction::None;
    const int right = row.right;
    if (point.x >= right - ui_px(kResultActionWidth) * 3 && point.x < right - ui_px(kResultActionWidth) * 2) return ResultAction::Pin;
    if (point.x >= right - ui_px(kResultActionWidth) * 2 && point.x < right - ui_px(kResultActionWidth)) return ResultAction::Favorite;
    if (point.x >= right - ui_px(kResultActionWidth) && point.x < right) return ResultAction::Shortcut;
    return ResultAction::None;
}

void update_result_tooltip(ResultAction action, int row) {
    if (!g_state || !g_state->tooltip) return;
    if (action == g_state->result_hover_action && row == g_state->result_hover_row) return;
    g_state->result_hover_action = action;
    g_state->result_hover_row = row;
    switch (action) {
    case ResultAction::Pin: g_state->result_tooltip = L"置顶排序 / 取消置顶排序"; break;
    case ResultAction::Favorite: g_state->result_tooltip = L"收藏 / 取消收藏"; break;
    case ResultAction::Shortcut: g_state->result_tooltip = L"添加到设置中的下一个快捷槽"; break;
    case ResultAction::None: g_state->result_tooltip = L"鼠标左键应用；右键打开更多操作"; break;
    }
    if (shortcut_list_mode() && row >= 0 && row < 9) {
        g_state->result_tooltip = L"快捷槽 " + std::to_wstring(row + 1) + L"：" + slot_display_label(row) +
            L"\n左键应用；右键设置快捷槽";
    }
    g_state->tooltip_texts[kResultTooltipId] = g_state->result_tooltip;
    TOOLINFOW tool{TTTOOLINFOW_V1_SIZE};
    tool.hwnd = g_state->window;
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.uId = reinterpret_cast<UINT_PTR>(g_state->results);
    tool.lpszText = g_state->tooltip_texts[kResultTooltipId].data();
    SendMessageW(g_state->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(g_state->tooltip, TTM_UPDATE, 0, 0);
}

bool contains_key(const std::vector<std::wstring>& values, const std::wstring& key) {
    return std::find(values.begin(), values.end(), key) != values.end();
}

bool shortcut_list_mode() {
    wchar_t query[512] = {};
    GetWindowTextW(g_state->search, query, 512);
    return !g_state->settings_visible && !*query && filter_name() == L"全部";
}

void update_results_list_style(bool shortcut_mode) {
    if (!g_state || !g_state->results) return;
    const HWND parent_window = GetParent(g_state->results);
    const bool was_shortcut = (GetWindowLongPtrW(g_state->results, GWL_STYLE) & LBS_MULTICOLUMN) != 0;
    // LBS_MULTICOLUMN is a creation-time style: recreate when entering/leaving the shortcut strip.
    if (was_shortcut != shortcut_mode) {
        const HWND old = g_state->results;
        const bool focused = GetFocus() == old;
        if (GetCapture() == old) ReleaseCapture();
        g_state->result_click_pending = false;
        g_state->result_click_redraw_held = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        RECT bounds{}; GetWindowRect(old, &bounds);
        MapWindowPoints(nullptr, parent_window, reinterpret_cast<POINT*>(&bounds), 2);
        if (g_state->tooltip) {
            TOOLINFOW tool{TTTOOLINFOW_V1_SIZE}; tool.hwnd = parent_window;
            tool.uId = reinterpret_cast<UINT_PTR>(old);
            SendMessageW(g_state->tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        clear_result_data();
        DestroyWindow(old);
        g_state->results = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
            (shortcut_mode ? LBS_MULTICOLUMN : WS_VSCROLL),
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            parent_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResultsId)), nullptr, nullptr);
        if (!g_state->results) throw std::runtime_error("Unable to create result list");
        set_font(g_state->results);
        SendMessageW(g_state->results, LB_SETITEMHEIGHT, 0, shortcut_mode ? kShortcutHeight : kResultHeight);
        g_results_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_state->results,
            GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(results_list_proc)));
        add_tooltip_for_control(parent_window, g_state->tooltip, g_state->results,
            kResultTooltipId, L"鼠标左键应用；右键打开更多操作");
        g_state->result_hover_row = -1;
        g_state->result_hover_action = ResultAction::None;
        if (focused) SetFocus(g_state->results);
    }
    RECT parent{};
    GetClientRect(parent_window, &parent);
    const int column_width = shortcut_mode ? (std::max)(1,(static_cast<int>(parent.right)-MulDiv(16,GetDpiForWindow(parent_window),96))/9) : (std::max)(1,static_cast<int>(parent.right)-16);
    if (shortcut_mode) SendMessageW(g_state->results, LB_SETCOLUMNWIDTH, column_width, 0);
    SetWindowPos(g_state->results, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

int recent_rank(const std::wstring& key) {
    const auto found = std::find(g_state->settings.recent.begin(), g_state->settings.recent.end(), key);
    return found == g_state->settings.recent.end()
        ? 10000
        : static_cast<int>(std::distance(g_state->settings.recent.begin(), found));
}

int pinned_rank(const std::wstring& key) {
    const auto found = std::find(g_state->settings.pinned.begin(), g_state->settings.pinned.end(), key);
    return found == g_state->settings.pinned.end()
        ? 10000
        : static_cast<int>(std::distance(g_state->settings.pinned.begin(), found));
}

void remember_recent(const xfx::CatalogItem& item) {
    const std::wstring key = item_key(item);
    auto& recent = g_state->settings.recent;
    recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
    recent.insert(recent.begin(), key);
    if (recent.size() > 20) recent.resize(20);
}

void populate_results() {
    // Applying a menu command can synchronously close the palette before the
    // popup handler returns and requests a refresh. Its old HWNDs own no rows.
    if (!g_state || !IsWindow(g_state->search) || !IsWindow(g_state->results)) return;
    wchar_t query[512] = {};
    GetWindowTextW(g_state->search, query, 512);
    const std::wstring filter = filter_name();
    update_results_list_style(shortcut_list_mode());
    auto matches = g_state->engine.search(query);
    std::stable_sort(matches.begin(), matches.end(), [](const xfx::SearchResult& left,
                                                        const xfx::SearchResult& right) {
        const int left_pinned = pinned_rank(item_key(left.item));
        const int right_pinned = pinned_rank(item_key(right.item));
        if ((left_pinned < 10000) != (right_pinned < 10000)) return left_pinned < right_pinned;
        if (left_pinned != right_pinned) return left_pinned < right_pinned;
        if (left.score != right.score) return left.score < right.score;
        const bool left_favorite = contains_key(g_state->settings.favorites, item_key(left.item));
        const bool right_favorite = contains_key(g_state->settings.favorites, item_key(right.item));
        if (left_favorite != right_favorite) return left_favorite;
        const int left_recent = recent_rank(item_key(left.item));
        const int right_recent = recent_rank(item_key(right.item));
        if (left_recent != right_recent) return left_recent < right_recent;
        return left.score < right.score;
    });
    std::vector<int> match_slots(matches.size(), -1);
    if (shortcut_list_mode()) {
        const auto all = matches;
        matches.clear();
        match_slots.clear();
        for (size_t slot = 0; slot < g_state->settings.slots.size(); ++slot) {
            const auto& key = g_state->settings.slots[slot];
            if (key.empty()) {
                xfx::SearchResult empty;
                empty.item.display_name = L"空";
                empty.item.category = L"快捷槽";
                empty.item.item_type = L"empty";
                matches.push_back(std::move(empty));
                match_slots.push_back(static_cast<int>(slot));
                continue;
            }
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto& result) { return item_key(result.item) == key; });
            if (found != all.end()) { matches.push_back(*found); match_slots.push_back(static_cast<int>(slot)); }
            else {
                xfx::SearchResult stale;
                stale.item.display_name = L"项目已失效";
                stale.item.category = L"快捷槽";
                stale.item.item_type = L"empty";
                matches.push_back(std::move(stale));
                match_slots.push_back(static_cast<int>(slot));
            }
        }
    }

    clear_result_data();
    SendMessageW(g_state->results, LB_RESETCONTENT, 0, 0);
    g_state->visible_slot_numbers.clear();
    size_t match_index = 0;
    for (const auto& result : matches) {
        const int slot = match_slots[match_index++];
        const auto key = item_key(result.item);
        if (!shortcut_list_mode()) {
            if (filter == L"收藏" && !contains_key(g_state->settings.favorites, key)) continue;
            if (filter == L"最近" && !contains_key(g_state->settings.recent, key)) continue;
            if ((filter == L"效果" || filter == L"预设") && result.item.category != filter) continue;
        }
        const std::wstring display = item_display_label(result.item);
        const LRESULT row = SendMessageW(g_state->results, LB_ADDSTRING, 0,
                                         reinterpret_cast<LPARAM>(display.c_str()));
        SendMessageW(g_state->results, LB_SETITEMDATA, row,
                     reinterpret_cast<LPARAM>(new xfx::CatalogItem(result.item)));
        g_state->visible_slot_numbers.push_back(slot);
    }
    const LRESULT result_count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    if (result_count > 0) SendMessageW(g_state->results, LB_SETCURSEL, 0, 0);
    adjust_console_height();
    set_status(result_count == 0 ? L"没有找到匹配项目" : L"Enter 应用 · Tab 切换效果/预设 · 右键编辑");
}

void rebuild_customized_catalog() {
    g_state->catalog = g_state->base_catalog;
    xfx::apply_catalog_customizations(g_state->catalog, g_state->settings);
    for (auto& item : g_state->catalog) xfx::enrich_catalog_item(item);
    g_state->engine = xfx::SearchEngine(g_state->catalog);
}

void refresh_catalog() {
    BusyFeedback feedback(L"正在扫描效果和预设…");
    set_status(L"正在扫描 AE 效果和预设...");
    g_state->base_catalog = scan_catalog();
    rebuild_customized_catalog();
    // Optional support diagnostic, useful for validating a host without touching user data.
    wchar_t report_path[32768] = {};
    const DWORD report_length = GetEnvironmentVariableW(L"XFX_NATIVE_SMOKE_REPORT", report_path, 32768);
    if (report_length && report_length < 32768) {
        std::ofstream report(fs::path(report_path), std::ios::binary);
        report << "catalog=" << g_state->catalog.size() << "\n";
        for (const auto* query : {L"高斯模糊", L"gaosimohu", L"gsmh", L"saoguang", L"sg"}) {
            const auto matches = g_state->engine.search(query);
            report << wide_to_utf8(query) << "=" << (matches.empty() ? "" : matches.front().item.match_name) << "\n";
        }
    }
    size_t effects = 0;
    size_t presets = 0;
    for (const auto& item : g_state->catalog) {
        if (item.item_type == L"effect") ++effects;
        if (item.item_type == L"preset") ++presets;
    }
    wchar_t count_text[128] = {};
    swprintf_s(count_text, L"%zu 个效果  ·  %zu 个预设", effects, presets);
    SetWindowTextW(g_state->count, count_text);
    populate_results();
    set_status(L"扫描完成",true);
}

xfx::ScriptReply run_ae_script(const std::string& script, bool show_feedback = true) {
    ModalGuard modal;
    std::unique_ptr<BusyFeedback> feedback;
    if (show_feedback) feedback = std::make_unique<BusyFeedback>(L"正在处理 AE 操作…");
    xfx::ScriptReply reply;
#ifdef XFX_CONSOLE_TEST
    if (g_test_before_script) g_test_before_script();
    if (g_test_script_reply) return *g_test_script_reply;
#endif
    if (!g_basic) { reply.message = "AE host unavailable"; return reply; }
    AEGP_MemHandle result_handle = nullptr, error_handle = nullptr;
    try {
        AEGP_SuiteHandler suites(g_basic);
        const auto read_handle = [&](AEGP_MemHandle handle) {
            std::string text;
            if (!handle) return text;
            void* data = nullptr;
            AEGP_MemSize size = 0;
            if (!suites.MemorySuite1()->AEGP_GetMemHandleSize(handle, &size) &&
                !suites.MemorySuite1()->AEGP_LockMemHandle(handle, &data)) {
                if (data && size) {
                    const char* bytes = static_cast<const char*>(data);
                    size_t length = 0;
                    while (length < size && bytes[length]) ++length;
                    text.assign(bytes, length);
                }
                suites.MemorySuite1()->AEGP_UnlockMemHandle(handle);
            }
            return text;
        };
        const A_Err error = suites.UtilitySuite6()->AEGP_ExecuteScript(
            g_plugin_id, script.c_str(), FALSE, &result_handle, &error_handle);
        const std::string result = read_handle(result_handle);
        const std::string script_error = read_handle(error_handle);
        if (result_handle) { suites.MemorySuite1()->AEGP_FreeMemHandle(result_handle); result_handle = nullptr; }
        if (error_handle) { suites.MemorySuite1()->AEGP_FreeMemHandle(error_handle); error_handle = nullptr; }
        reply = xfx::parse_script_reply(result);
        if (error) {
            reply.succeeded = false;
            reply.message = script_error.empty() ? "AE script execution failed" : script_error;
        }
    } catch (...) {
        try {
            AEGP_SuiteHandler suites(g_basic);
            if (result_handle) suites.MemorySuite1()->AEGP_FreeMemHandle(result_handle);
            if (error_handle) suites.MemorySuite1()->AEGP_FreeMemHandle(error_handle);
        } catch (...) {}
        reply.message = "AE scripting unavailable";
    }
    return reply;
}

void show_console(bool visible);

struct ApplyPresentation {
    HWND window = g_state->window;
    bool was_visible = IsWindowVisible(window) != FALSE;
    bool completed = false;
    ApplyPresentation() {
        g_state->applying_item = true;
        if (was_visible) {
            if (g_state->tooltip) SendMessageW(g_state->tooltip, TTM_POP, 0, 0);
            if (GetCapture() == window || IsChild(window, GetCapture())) ReleaseCapture();
            // Hide once before AE can pump messages or redraw its composition.
            ShowWindow(window, SW_HIDE);
        }
        g_state->ime.restore();
        g_state->ime_session_active = false;
    }
    ~ApplyPresentation() {
        if (g_state) g_state->applying_item = false;
        if (!completed && was_visible && IsWindow(window)) show_console(true);
    }
    void finish() {
        completed = true;
        if (IsWindow(window)) SendMessageW(window, WM_CLOSE, 0, 0);
    }
};

xfx::ScriptReply execute_item(const xfx::CatalogItem& selected_item) {
    if (g_state->applying_item)
        return xfx::ScriptReply{false, false, 0, "An effect is already being applied"};
    // Copy before executing: AE may pump messages while running ExtendScript.
    const auto item = selected_item;
    const auto override = g_state->settings.overrides.find(item_key(item));
    const auto script = override != g_state->settings.overrides.end()
        ? xfx::script_apply_preset(override->second) : (item.item_type == L"preset"
        ? xfx::script_apply_preset(item.file_path) : xfx::script_apply_effect(item.match_name));
    ModalGuard modal;
    ApplyPresentation presentation;
    const auto reply = run_ae_script(script, false);
    if (reply.succeeded || reply.count) remember_recent(item);
    if (reply.succeeded) {
        set_status(L"已应用到 " + std::to_wstring(reply.count) + L" 个所选图层 · Ctrl+Z 撤销", true);
        presentation.finish();
    } else {
        set_status((reply.partial ? L"部分完成：" : L"操作失败：") + utf8_to_wide(reply.message.c_str()));
    }
    return reply;
}

fs::path capture_directory() {
    if (g_state && !g_state->settings.capture_directory.empty()) return fs::path(g_state->settings.capture_directory);
    return settings_path().parent_path() / L"Capture";
}

void update_gallery_selection();
bool handle_gallery_key(WPARAM key);

void refresh_gallery() {
    if (!g_state || !g_state->gallery_list) return;
    g_state->captures = xfx::scan_captures(capture_directory());
    SendMessageW(g_state->gallery_list, LB_RESETCONTENT, 0, 0);
    for (const auto& path : g_state->captures) {
        SendMessageW(g_state->gallery_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.filename().c_str()));
    }
    if (!g_state->captures.empty()) {
        SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, 0);
        SendMessageW(g_state->gallery_list, LB_SETCARETINDEX, 0, 0);
    }
    update_gallery_selection();
}

void clear_thumbnail_cache() {
    ModalGuard modal;
    const auto result = xfx::clear_thumbnail_cache();
    if (result.failed) {
        set_status(L"缩略图缓存清理失败，请检查 LocalAppData 写入权限");
        return;
    }
    if (g_state->gallery_window) refresh_gallery();
    set_status(L"已清除缩略图缓存 " + std::to_wstring(result.deleted) + L" 个文件；图库已重建",true);
}

void validate_capture_directory(const fs::path& directory) {
    if (!directory.empty() && (!directory.is_absolute() || !fs::is_directory(directory)))
        throw std::invalid_argument("请选择一个已存在的快照保存文件夹。");
}

void set_capture_directory(const fs::path& directory) {
    validate_capture_directory(directory);
    auto updated = g_state->settings;
    updated.capture_directory = directory.wstring();
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    SetWindowTextW(g_state->capture_location, capture_directory().c_str());
    refresh_gallery();
    set_status(L"快照保存位置已更新；原目录中的图片保留不变",true);
}

void choose_capture_directory() {
    ModalGuard modal;
    struct ComSession {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ~ComSession() { if (SUCCEEDED(result)) CoUninitialize(); }
    } session;
    if (FAILED(session.result) && session.result != RPC_E_CHANGED_MODE)
        throw std::runtime_error("无法打开文件夹选择窗口。");
    const auto check = [](HRESULT result) {
        if (FAILED(result)) throw std::runtime_error("无法选择快照保存文件夹。");
    };
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    FILEOPENDIALOGOPTIONS options{};
    check(dialog->GetOptions(&options));
    check(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
    check(dialog->SetTitle(L"选择快照保存文件夹"));
    Microsoft::WRL::ComPtr<IShellItem> initial;
    if (SUCCEEDED(SHCreateItemFromParsingName(capture_directory().c_str(), nullptr, IID_PPV_ARGS(&initial))))
        check(dialog->SetFolder(initial.Get()));
    const HRESULT shown = dialog->Show(g_state->window);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    check(shown);
    Microsoft::WRL::ComPtr<IShellItem> selected;
    check(dialog->GetResult(&selected));
    PWSTR path = nullptr;
    check(selected->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> chosen(path, CoTaskMemFree);
    if (!path || !*path) return;
    set_capture_directory(fs::path(path));
}

xfx::ScriptReply capture_frame(fs::path* saved_path = nullptr) {
    ModalGuard modal;
    std::error_code error;
    fs::create_directories(capture_directory(), error);
    if (error) {
        set_status(L"无法创建快照目录");
        xfx::ScriptReply failure; failure.message = wide_to_utf8(L"无法创建快照目录"); return failure;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[128] = {};
    swprintf_s(name, L"快照-%04u%02u%02u-%02u%02u%02u-%03u-%llu.png", now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetTickCount64());
    const auto file = capture_directory() / name;
    const auto reply = xfx::capture_comp_png(g_basic, g_plugin_id, file, true);
    set_status(reply.succeeded ? L"当前合成帧已保存为 PNG · 点击图库查看" :
        L"快照失败：" + utf8_to_wide(reply.message.c_str()),reply.succeeded);
    if (reply.succeeded) {
        if (saved_path) *saved_path = file;
        refresh_gallery();
    }
    return reply;
}

std::vector<fs::path> selected_captures() {
    std::vector<fs::path> files;
    if (!g_state || !g_state->gallery_list) return files;
    const LRESULT count = SendMessageW(g_state->gallery_list, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) return files;
    std::vector<int> rows(static_cast<size_t>(count));
    const LRESULT received = SendMessageW(g_state->gallery_list, LB_GETSELITEMS, count,
        reinterpret_cast<LPARAM>(rows.data()));
    for (LRESULT i = 0; i < received; ++i) {
        const int row = rows[static_cast<size_t>(i)];
        if (row >= 0 && static_cast<size_t>(row) < g_state->captures.size()) files.push_back(g_state->captures[row]);
    }
    return files;
}

fs::path selected_capture() {
    const auto files = selected_captures();
    return files.size() == 1 ? files.front() : fs::path{};
}

void gallery_status(const std::wstring& text) {
    if (g_state->gallery_status) SetWindowTextW(g_state->gallery_status, text.c_str());
}

void rebuild_gallery_preview(const std::vector<fs::path>& files) {
    g_state->gallery_drag_view = -1;
    if (GetCapture() == g_state->gallery_canvas) ReleaseCapture();
    std::vector<xfx::GallerySource> sources;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> images;
    const size_t count = (std::min)(files.size(), size_t{4});
    sources.reserve(count); images.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        xfx::GallerySource source{files[index], 0, 0};
        std::unique_ptr<Gdiplus::Bitmap> decoded;
        {
            // Decode into a new pixel buffer; Bitmap::Clone can retain the file decoder.
            Gdiplus::Bitmap file(files[index].c_str());
            if (file.GetLastStatus() == Gdiplus::Ok && file.GetWidth() && file.GetHeight() &&
                file.GetWidth() <= INT_MAX && file.GetHeight() <= INT_MAX) {
                decoded = std::make_unique<Gdiplus::Bitmap>(static_cast<INT>(file.GetWidth()),
                    static_cast<INT>(file.GetHeight()), PixelFormat32bppARGB);
                Gdiplus::Status copied = Gdiplus::GenericError;
                if (decoded->GetLastStatus() == Gdiplus::Ok) {
                    Gdiplus::Graphics pixels(decoded.get());
                    pixels.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                    copied = pixels.DrawImage(&file, Gdiplus::Rect(0, 0,
                        static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight())),
                        0, 0, static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight()), Gdiplus::UnitPixel);
                }
                if (copied == Gdiplus::Ok) {
                    source.width = decoded->GetWidth(); source.height = decoded->GetHeight();
                } else decoded.reset();
            }
        }
        sources.push_back(std::move(source)); images.push_back(std::move(decoded));
    }
    auto next = g_state->gallery_model;
    next.set_images(sources);
    g_state->gallery_model = std::move(next);
    g_state->gallery_images.swap(images);
    if (g_state->gallery_canvas) InvalidateRect(g_state->gallery_canvas, nullptr, FALSE);
}

void layout_gallery(HWND hwnd) {
    RECT client{}; GetClientRect(hwnd, &client);
    const int width = (std::max)(0L, client.right), height = (std::max)(0L, client.bottom);
    const int body_top = 40, body_bottom = (std::max)(body_top, height - 8);
    const int body_width = (std::max)(0, width - 16);
    if (g_state->gallery_split < 0) g_state->gallery_split = body_width / 4;
    const int split = (std::min)(std::clamp(g_state->gallery_split, 100, (std::max)(100, body_width - 103)), body_width);
    g_state->gallery_split = split;
    MoveWindow(g_state->gallery_list, 8, body_top, split, body_bottom - body_top, TRUE);
    const int thumbnail_height = (std::max)(24, static_cast<int>((std::max)(0, split - 19) * 0.5625) + 22);
    SendMessageW(g_state->gallery_list, LB_SETITEMHEIGHT, 0, thumbnail_height);
    g_state->gallery_splitter = {8 + split, body_top, 11 + split, body_bottom};
    MoveWindow(g_state->gallery_canvas, 11 + split, body_top, (std::max)(0, body_width - split - 3),
        body_bottom - body_top, TRUE);
    MoveWindow(g_state->gallery_status, 256, 12, (std::max)(0, width - 264), 20, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool route_gallery_wheel(HWND receiver, WPARAM wparam, LPARAM lparam) {
    if (!g_state) return false;
    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    for (HWND target : {g_state->gallery_canvas, g_state->gallery_list}) {
        RECT bounds{};
        if (IsWindow(target) && GetWindowRect(target, &bounds) && PtInRect(&bounds, point)) {
            if (target == receiver) return false;
            SendMessageW(target, WM_MOUSEWHEEL, wparam, lparam);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK gallery_canvas_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
        if (!g_state) return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_SIZE) {
            RECT client{}; GetClientRect(hwnd, &client);
            g_state->gallery_model.set_bounds({0, 0, double(client.right), double(client.bottom)});
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            SetFocus(hwnd);
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const auto& views = g_state->gallery_model.views();
            for (size_t index = 0; index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (point.x >= cell.x && point.x < cell.x + cell.width && point.y >= cell.y &&
                    point.y < cell.y + cell.height && views[index].source.width && views[index].source.height) {
                    g_state->gallery_drag_view = static_cast<int>(index); g_state->gallery_drag_start = point;
                    g_state->gallery_drag_offset_x = views[index].offset_x;
                    g_state->gallery_drag_offset_y = views[index].offset_y;
                    SetCapture(hwnd); break;
                }
            }
            return 0;
        }
        if (message == WM_MOUSEMOVE && g_state->gallery_drag_view >= 0 && GetCapture() == hwnd) {
            g_state->gallery_model.set_offset(static_cast<size_t>(g_state->gallery_drag_view),
                g_state->gallery_drag_offset_x + GET_X_LPARAM(lparam) - g_state->gallery_drag_start.x,
                g_state->gallery_drag_offset_y + GET_Y_LPARAM(lparam) - g_state->gallery_drag_start.y);
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        if (message == WM_LBUTTONUP && g_state->gallery_drag_view >= 0) {
            g_state->gallery_drag_view = -1; if (GetCapture() == hwnd) ReleaseCapture(); return 0;
        }
        if (message == WM_CAPTURECHANGED) g_state->gallery_drag_view = -1;
        if (message == WM_MOUSEWHEEL) {
            if (route_gallery_wheel(hwnd, wparam, lparam)) return 0;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
            const auto delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const auto& views = g_state->gallery_model.views();
            for (size_t index = 0; delta && index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (point.x >= cell.x && point.x < cell.x + cell.width && point.y >= cell.y && point.y < cell.y + cell.height) {
                    g_state->gallery_model.zoom_at(index, delta > 0 ? 1.1 : 1.0 / 1.1, point.x, point.y);
                    InvalidateRect(hwnd, nullptr, FALSE); break;
                }
            }
            return 0;
        }
        if (message == WM_KEYDOWN && handle_gallery_key(wparam)) return 0;
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT || message == WM_PRINTCLIENT) {
            PAINTSTRUCT paint{};
            HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wparam);
            RECT client{}; GetClientRect(hwnd, &client);
            FillRect(dc, &client, g_state->background_brush);
            const auto& views = g_state->gallery_model.views();
            if (views.empty()) {
                SelectObject(dc, g_state->font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, colors().muted);
                DrawTextW(dc, L"选择左侧快照进行预览", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            for (size_t index = 0; index < views.size(); ++index) {
                const auto& cell = views[index].cell;
                if (cell.width <= 0 || cell.height <= 0) continue;
                const auto bounds = g_state->gallery_model.image_rect(index);
                Gdiplus::Graphics graphics(dc);
                graphics.SetClip(Gdiplus::RectF(static_cast<float>(cell.x), static_cast<float>(cell.y),
                    static_cast<float>(cell.width), static_cast<float>(cell.height)));
                if (index < g_state->gallery_images.size() && g_state->gallery_images[index]) {
                    graphics.SetInterpolationMode(views[index].scale == 1.0 ?
                        Gdiplus::InterpolationModeNearestNeighbor : Gdiplus::InterpolationModeHighQualityBicubic);
                    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                    graphics.DrawImage(g_state->gallery_images[index].get(),
                        Gdiplus::RectF(static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                            static_cast<float>(bounds.width), static_cast<float>(bounds.height)),
                        0, 0, static_cast<float>(views[index].source.width),
                        static_cast<float>(views[index].source.height), Gdiplus::UnitPixel);
                } else {
                    Gdiplus::SolidBrush ink(Gdiplus::Color(255, GetRValue(colors().muted),
                        GetGValue(colors().muted), GetBValue(colors().muted)));
                    Gdiplus::Font font(dc, g_state->font);
                    Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter);
                    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    graphics.DrawString(L"图片无法读取", -1, &font,
                        Gdiplus::RectF(static_cast<float>(cell.x), static_cast<float>(cell.y),
                            static_cast<float>(cell.width), static_cast<float>(cell.height)), &format, &ink);
                }
            }
            if (message == WM_PAINT) EndPaint(hwnd, &paint);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}

void update_gallery_selection() {
    if (!g_state->gallery_list) return;
    const auto files = selected_captures();
    const HWND parent = GetParent(g_state->gallery_list);
    for (int id : {kPreviewId, kCopyCaptureId}) EnableWindow(GetDlgItem(parent, id), files.size() == 1);
    for (int id : {kSaveCaptureId, kImportCaptureId, kDeleteCaptureId}) EnableWindow(GetDlgItem(parent, id), !files.empty());
    rebuild_gallery_preview(files);
    gallery_status(L"共 " + std::to_wstring(g_state->captures.size()) + L" 张 · 已选 " +
        std::to_wstring(files.size()) + L" 张 · Z 原尺寸 · A 方向");
}

fs::path g_preview_path;
RECT g_preview_bounds{};
LRESULT CALLBACK preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE && g_state &&
        g_state->settings.auto_close && !g_state->settings.fixed_window && g_state->modal_depth == 1) {
        const HWND next = reinterpret_cast<HWND>(lparam);
        if (!next || GetAncestor(next, GA_ROOTOWNER) != g_state->window) PostMessageW(g_state->window, WM_CLOSE, 0, 0);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        GetClientRect(hwnd, &g_preview_bounds);
        FillRect(dc, &g_preview_bounds, g_state->background_brush);
        InflateRect(&g_preview_bounds, -12, -12);
        xfx::draw_capture(dc, g_preview_bounds, g_preview_path);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        Gdiplus::Bitmap image(g_preview_path.c_str());
        if (!image.GetWidth() || !image.GetHeight()) return 0;
        const double scale = (std::min)(double(g_preview_bounds.right - g_preview_bounds.left) / image.GetWidth(),
            double(g_preview_bounds.bottom - g_preview_bounds.top) / image.GetHeight());
        const int left = g_preview_bounds.left + (g_preview_bounds.right - g_preview_bounds.left - int(image.GetWidth() * scale)) / 2;
        const int top = g_preview_bounds.top + (g_preview_bounds.bottom - g_preview_bounds.top - int(image.GetHeight() * scale)) / 2;
        const int x = GET_X_LPARAM(lparam) - left, y = GET_Y_LPARAM(lparam) - top;
        unsigned int argb = 0;
        if (x >= 0 && y >= 0 && xfx::sample_image_pixel(g_preview_path,
            static_cast<unsigned int>(x / scale), static_cast<unsigned int>(y / scale), argb)) {
            wchar_t color[16] = {};
            swprintf_s(color, L"#%06X", argb & 0xFFFFFF);
            if (OpenClipboard(hwnd)) {
                HGLOBAL text = GlobalAlloc(GMEM_MOVEABLE, sizeof(color));
                if (text) {
                    if (void* bytes = GlobalLock(text)) {
                        memcpy(bytes, color, sizeof(color)); GlobalUnlock(text);
                        EmptyClipboard();
                        if (!SetClipboardData(CF_UNICODETEXT, text)) GlobalFree(text);
                    } else GlobalFree(text);
                }
                CloseClipboard();
            }
            SetWindowTextW(hwnd, (std::wstring(L"颜色已复制：") + color + L" · 点击图片取色").c_str());
        }
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { g_state->preview_window = nullptr; return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void preview_capture() {
    const auto file = selected_capture();
    if (file.empty()) return;
    g_preview_path = file;
    const wchar_t* name = L"XFXCapturePreview";
    WNDCLASSW klass{};
    klass.hInstance = GetModuleHandleW(nullptr); klass.lpfnWndProc = preview_proc;
    klass.lpszClassName = name; klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&klass);
    HWND existing = g_state->preview_window;
    if (existing) { InvalidateRect(existing, nullptr, FALSE); SetForegroundWindow(existing); return; }
    HWND window = CreateWindowExW(0, name, L"快照预览 · 点击图片取色并复制 RGB · Esc 关闭",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
        g_state->gallery_window, nullptr, klass.hInstance, nullptr);
    g_state->preview_window = window;
    ShowWindow(window, SW_SHOW);
}

void save_captures_as_batch(const std::vector<fs::path>& files) {
    ModalGuard modal;
    HMENU formats = CreatePopupMenu();
    AppendMenuW(formats, MF_STRING, 1, L"批量另存 PNG（保留透明通道）");
    AppendMenuW(formats, MF_STRING, 2, L"批量另存 JPG（白色背景）");
    RECT button{}; GetWindowRect(GetDlgItem(g_state->gallery_window, kSaveCaptureId), &button);
    const auto format = xfx::track_themed_popup(formats, TPM_RETURNCMD | TPM_NONOTIFY,
        button.left, button.bottom, g_state->gallery_window, colors(), g_state->font);
    DestroyMenu(formats);
    if (format != 1 && format != 2) return;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW chooser{};
    chooser.hwndOwner = g_state->gallery_window;
    chooser.lpszTitle = L"选择批量导出的图片目录";
    chooser.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE chosen = SHBrowseForFolderW(&chooser);
    wchar_t folder[MAX_PATH]{};
    const bool selected = chosen && SHGetPathFromIDListW(chosen, folder);
    CoTaskMemFree(chosen);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (!selected) return;
    size_t saved = 0, skipped = 0, failed = 0;
    for (const auto& source : files) {
        auto target = fs::path(folder) / source.filename();
        target.replace_extension(format == 2 ? L".jpg" : L".png");
        std::error_code error;
        if (fs::equivalent(source, target, error) && !error) { ++skipped; continue; }
        if (fs::exists(target) && MessageBoxW(g_state->gallery_window,
            (L"覆盖已存在的文件？\n" + target.wstring()).c_str(), L"确认替换图片",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) { ++skipped; continue; }
        if (xfx::export_capture(source, target, format == 2)) ++saved;
        else ++failed;
    }
    gallery_status(L"已导出 " + std::to_wstring(saved) + L" 张 · 跳过 " +
        std::to_wstring(skipped) + L" 张 · 失败 " + std::to_wstring(failed) + L" 张");
}

void save_capture_as() {
    const auto files = selected_captures();
    if (files.size() > 1) { save_captures_as_batch(files); return; }
    const auto source = selected_capture();
    if (source.empty()) return;
    ModalGuard modal;
    wchar_t filename[32768] = L"快照.png";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = g_state->gallery_window;
    dialog.lpstrFile = filename; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0JPEG 白底图片 (*.jpg)\0*.jpg\0";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    const bool jpeg = dialog.nFilterIndex == 2;
    fs::path target(filename);
    target.replace_extension(jpeg ? L".jpg" : L".png");
    std::error_code error;
    if (fs::equivalent(source, target, error) && !error) {
        MessageBoxW(g_state->gallery_window, L"这张图片已经保存在该路径。", L"快照", MB_OK);
        return;
    }
    if (target != fs::path(filename) && fs::exists(target, error) &&
        MessageBoxW(g_state->gallery_window, (L"覆盖已存在的文件？\n" + target.wstring()).c_str(),
            L"确认替换图片", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    const bool success = xfx::export_capture(source, target, jpeg);
    MessageBoxW(g_state->gallery_window, success ? L"图片已保存。" : L"图片保存失败，请检查路径。",
        L"快照导出", MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
}

void copy_selected_capture() {
    const auto file = selected_capture();
    if (file.empty()) return;
    gallery_status(xfx::copy_capture_to_clipboard(g_state->gallery_window, file)
        ? L"图片已复制到剪贴板" : L"图片复制失败，请稍后重试");
}

void delete_selected_captures() {
    const auto files = selected_captures();
    if (files.empty()) return;
    ModalGuard modal;
    const auto question = L"永久删除所选的 " + std::to_wstring(files.size()) + L" 张快照？\n" +
        capture_directory().wstring() + L"\n此操作无法撤销。";
    if (MessageBoxW(g_state->gallery_window, question.c_str(), L"删除快照",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    const auto results = xfx::delete_captures(capture_directory(), files);
    size_t deleted = 0;
    for (const auto& result : results) if (result.deleted) ++deleted;
    refresh_gallery();
    gallery_status(L"已删除 " + std::to_wstring(deleted) + L" 张" + (deleted == files.size()
        ? L"" : L"；其余图片未删除，请检查文件权限后重试"));
}

WNDPROC g_gallery_list_proc = nullptr;
bool handle_gallery_key(WPARAM key) {
    if (!g_state || !IsWindow(g_state->gallery_list)) return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_DELETE) { delete_selected_captures(); return true; }
    if (control && key == 'C') { copy_selected_capture(); return true; }
    if (control && key == 'A') {
        SendMessageW(g_state->gallery_list, LB_SETSEL, TRUE, -1); update_gallery_selection(); return true;
    }
    if (!control && (key == 'A' || key == 'Z')) {
        if (key == 'A') {
            g_state->gallery_drag_view = -1;
            if (GetCapture() == g_state->gallery_canvas) ReleaseCapture();
            g_state->gallery_model.toggle_direction();
        }
        else g_state->gallery_model.toggle_actual_size();
        InvalidateRect(g_state->gallery_canvas, nullptr, FALSE);
        gallery_status(key == 'A' ? L"已切换排列方向 · Z 原尺寸切换" : L"已切换预览缩放 · A 排列方向");
        return true;
    }
    if (key == VK_ESCAPE) { DestroyWindow(GetParent(g_state->gallery_list)); return true; }
    return false;
}

void select_gallery_row(int row, bool control, bool shift, bool right_alt) {
    HWND list = g_state->gallery_list;
    if (row < 0 || static_cast<size_t>(row) >= g_state->captures.size()) return;
    if (control && !right_alt) {
        const bool selected = SendMessageW(list, LB_GETSEL, row, 0) > 0;
        SendMessageW(list, LB_SETSEL, !selected, row);
        SendMessageW(list, LB_SETANCHORINDEX, row, 0);
    } else if (shift) {
        const auto anchor = SendMessageW(list, LB_GETANCHORINDEX, 0, 0);
        const int start = anchor >= 0 && static_cast<size_t>(anchor) < g_state->captures.size() ?
            static_cast<int>(anchor) : row;
        SendMessageW(list, LB_SETSEL, FALSE, -1);
        for (int index = (std::min)(start, row); index <= (std::max)(start, row); ++index)
            SendMessageW(list, LB_SETSEL, TRUE, index);
    } else {
        SendMessageW(list, LB_SETSEL, FALSE, -1);
        SendMessageW(list, LB_SETSEL, TRUE, row);
        SendMessageW(list, LB_SETANCHORINDEX, row, 0);
    }
    SendMessageW(list, LB_SETCARETINDEX, row, FALSE);
    update_gallery_selection();
}

LRESULT CALLBACK gallery_list_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
        if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
        if (message == WM_LBUTTONDOWN) {
            const auto hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            if (!HIWORD(hit)) {
                SetFocus(hwnd);
                select_gallery_row(LOWORD(hit), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_RMENU) & 0x8000) != 0);
                return 0;
            }
        }
        if (message == WM_KEYDOWN) {
            if (wparam == VK_RETURN) { preview_capture(); return 0; }
            if (handle_gallery_key(wparam)) return 0;
        }
        return CallWindowProcW(g_gallery_list_proc, hwnd, message, wparam, lparam);
    });
}

LRESULT CALLBACK gallery_button_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR id, DWORD_PTR) {
    return guarded_callback([&]() -> LRESULT {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, gallery_button_proc, id);
        if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
        if (message == WM_KEYDOWN && g_state && handle_gallery_key(wparam)) return 0;
        return DefSubclassProc(hwnd, message, wparam, lparam);
    });
}

LRESULT CALLBACK gallery_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_MOUSEWHEEL && route_gallery_wheel(hwnd, wparam, lparam)) return 0;
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE && g_state &&
        g_state->settings.auto_close && !g_state->settings.fixed_window && g_state->modal_depth == 1) {
        const HWND next = reinterpret_cast<HWND>(lparam);
        if (!next || GetAncestor(next, GA_ROOTOWNER) != g_state->window) PostMessageW(g_state->window, WM_CLOSE, 0, 0);
    }
    if (message == WM_CREATE) {
        g_state->gallery_model = xfx::GalleryModel{};
        g_state->gallery_split = -1;
        g_state->gallery_list = xfx::create_thumbnail_list(hwnd, kGalleryListId, GetModuleHandleW(nullptr));
        if (!g_state->gallery_list) return -1;
        set_font(g_state->gallery_list);
        g_gallery_list_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_state->gallery_list,
            GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gallery_list_proc)));
        WNDCLASSW canvas{}; canvas.hInstance = GetModuleHandleW(nullptr);
        canvas.lpfnWndProc = gallery_canvas_proc; canvas.lpszClassName = L"XFXGalleryComparison";
        canvas.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); RegisterClassW(&canvas);
        g_state->gallery_canvas = CreateWindowExW(0, canvas.lpszClassName, L"快照比较预览",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 194, 44, 550, 350, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGalleryCanvasId)), canvas.hInstance, nullptr);
        if (!g_state->gallery_canvas) return -1;
        const wchar_t* labels[] = {L"导出", L"删除", L"复制"};
        const int ids[] = {kSaveCaptureId, kDeleteCaptureId, kCopyCaptureId};
        for (int i = 0; i < 3; ++i) {
            HWND button = CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                8 + 80 * i, 8, 72, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
                GetModuleHandleW(nullptr), nullptr);
            if (!button) return -1;
            set_font(button);
            SetWindowSubclass(button, gallery_button_proc, 1, 0);
        }
        g_state->gallery_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_LEFTNOWORDWRAP,
            10, 416, 730, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_state->gallery_status) return -1;
        set_font(g_state->gallery_status);
        layout_gallery(hwnd);
        refresh_gallery();
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {300, 240}; return 0;
    }
    if (message == WM_SIZE) { layout_gallery(hwnd); return 0; }
    if (message == WM_LBUTTONDOWN && PtInRect(&g_state->gallery_splitter,
        POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
        g_state->gallery_dragging_splitter = true; SetCapture(hwnd); return 0;
    }
    if (message == WM_MOUSEMOVE && g_state->gallery_dragging_splitter && GetCapture() == hwnd) {
        g_state->gallery_split = GET_X_LPARAM(lparam) - 8; layout_gallery(hwnd); return 0;
    }
    if (message == WM_LBUTTONUP && g_state->gallery_dragging_splitter) {
        g_state->gallery_dragging_splitter = false; if (GetCapture() == hwnd) ReleaseCapture(); return 0;
    }
    if (message == WM_CAPTURECHANGED) g_state->gallery_dragging_splitter = false;
    if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
        if (PtInRect(&g_state->gallery_splitter, point)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644))); return TRUE;
        }
    }
    if (message == WM_KEYDOWN && handle_gallery_key(wparam)) return 0;
    if (message == WM_CONTEXTMENU) {
        UINT selected = 0;
        {
            ModalGuard modal;
            const auto files = selected_captures();
            HMENU menu = CreatePopupMenu();
            const UINT any = files.empty() ? MF_GRAYED : MF_ENABLED;
            const UINT single = files.size() == 1 ? MF_ENABLED : MF_GRAYED;
            AppendMenuW(menu, MF_STRING | any, kSaveCaptureId, L"导出 PNG / JPG");
            AppendMenuW(menu, MF_STRING | any, kDeleteCaptureId, L"删除所选");
            AppendMenuW(menu, MF_STRING | single, kCopyCaptureId, L"复制图片\tCtrl+C");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | any, kImportCaptureId, L"导回 AE 项目");
            AppendMenuW(menu, MF_STRING | single, kPreviewId, L"单独预览 / 取色");
            AppendMenuW(menu, MF_STRING, kCaptureFolderId, L"打开图片目录");
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) {
                point = {12, 44}; ClientToScreen(hwnd, &point);
            }
            selected = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                point.x, point.y, hwnd, colors(), g_state->font);
            DestroyMenu(menu);
        }
        if (selected && IsWindow(hwnd)) SendMessageW(hwnd, WM_COMMAND, selected, 0);
        return 0;
    }
    if (message == WM_MEASUREITEM) { reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = 116; return TRUE; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        SetDCBrushColor(dc, colors().divider);
        FillRect(dc, &g_state->gallery_splitter, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        EndPaint(hwnd, &paint); return 0;
    }
    if (message == WM_DRAWITEM) {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw->CtlType == ODT_BUTTON) { paint_text_button(*draw); return TRUE; }
        if (draw->CtlType != ODT_LISTBOX) return FALSE;
        if (draw->itemID >= g_state->captures.size()) return TRUE;
        const int saved_dc = SaveDC(draw->hDC);
        SelectObject(draw->hDC, g_state->font);
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        HBRUSH brush = CreateSolidBrush(selected ? colors().selected : colors().background);
        FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
        RECT image = draw->rcItem; InflateRect(&image, -6, -6); image.bottom -= 22;
        if (!xfx::draw_cached_thumbnail(draw->hDC, image, g_state->captures[draw->itemID]))
            xfx::draw_capture(draw->hDC, image, g_state->captures[draw->itemID]);
        RECT label = draw->rcItem; label.top = label.bottom - 22; label.left += 6; label.right -= 6;
        SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text); SetBkMode(draw->hDC, TRANSPARENT);
        DrawTextW(draw->hDC, g_state->captures[draw->itemID].filename().c_str(), -1, &label,
            DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
        if (saved_dc) RestoreDC(draw->hDC, saved_dc);
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN)
        return theme_control_color(message, reinterpret_cast<HDC>(wparam),reinterpret_cast<HWND>(lparam));
    if (message == WM_ERASEBKGND) {
        RECT client{}; GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, g_state->background_brush); return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == kGalleryListId && HIWORD(wparam) == LBN_SELCHANGE) update_gallery_selection();
        if (LOWORD(wparam) == kCopyCaptureId) copy_selected_capture();
        if (LOWORD(wparam) == kDeleteCaptureId) delete_selected_captures();
        if (LOWORD(wparam) == kPreviewId || (LOWORD(wparam) == kGalleryListId && HIWORD(wparam) == LBN_DBLCLK)) preview_capture();
        if (LOWORD(wparam) == kSaveCaptureId) save_capture_as();
        if (LOWORD(wparam) == kCaptureFolderId) ShellExecuteW(hwnd, L"open", capture_directory().c_str(), nullptr, nullptr, SW_SHOW);
        if (LOWORD(wparam) == kImportCaptureId) {
            const auto files = selected_captures();
            size_t imported = 0;
            std::wstring error;
            for (const auto& file : files) {
                const auto reply = run_ae_script(xfx::script_import_image(file.wstring()));
                if (reply.succeeded) ++imported;
                else { error = utf8_to_wide(reply.message.c_str()); break; }
            }
            gallery_status(L"已导入 " + std::to_wstring(imported) + L" 张到 AE 项目" +
                (error.empty() ? L"" : L"；" + error));
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        g_state->gallery_window = nullptr; g_state->gallery_list = nullptr; g_state->gallery_status = nullptr;
        g_state->gallery_canvas = nullptr; g_state->gallery_images.clear();
        g_state->gallery_model = xfx::GalleryModel{};
        g_state->gallery_dragging_splitter = false; g_state->gallery_drag_view = -1;
        if (g_state->modal_depth) --g_state->modal_depth;
        if (g_state->window && !IsWindowVisible(g_state->window) && !g_state->modal_depth)
            PostMessageW(g_state->window, WM_APP + 3, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}

void show_gallery() {
    if (g_state->gallery_window) { SetForegroundWindow(g_state->gallery_window); refresh_gallery(); return; }
    std::error_code error; fs::create_directories(capture_directory(), error);
    const wchar_t* name = L"XFXCaptureGallery";
    WNDCLASSW klass{}; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = gallery_proc; klass.lpszClassName = name;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&klass);
    const unsigned int previous_modal_depth = g_state->modal_depth;
    ++g_state->modal_depth;
    g_state->gallery_window = CreateWindowExW(0, name, L"XFX 快照图库",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 620, g_state->window, nullptr, klass.hInstance, nullptr);
    if (!g_state->gallery_window && g_state->modal_depth > previous_modal_depth) --g_state->modal_depth;
}

void edit_dictionary() {
    ModalGuard modal;
    const auto file = settings_path().parent_path() / L"aliases.user.tsv";
    if (!fs::exists(file)) {
        fs::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary);
        output << "\xEF\xBB\xBF# matchName\tEnglishName\tChineseName\tPinyinFull(optional)\tInitials(optional)\tAliases(use |)\n"
            "# UTF-8 TSV. Save, then click Refresh. Tabs separate columns.\n"
            "# Example (remove # to enable):\n"
            "# ADBE Gaussian Blur 2\tGaussian Blur\t高斯模糊\t\t\t柔化|高斯\n";
    }
    ShellExecuteW(g_state->window, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOW);
    set_status(L"编辑中文译名/别名后保存，再点击刷新索引 · 拼音自动生成");
}

void apply_imported_settings(const xfx::Settings& imported) {
    auto normalized = imported;
    normalized.full_resolution_screenshots = true;
    const auto palette = xfx::resolve_theme(normalized.theme_mode, g_state->host_background);
    ThemeBrushes brushes(palette);
    xfx::SettingsStore::save(settings_path(), normalized);
    g_state->settings = std::move(normalized);
    g_state->palette = palette;
    brushes.install();
    rebuild_customized_catalog();
    SetWindowTextW(g_state->hotkey_edit, g_state->settings.hotkey.c_str());
    SendMessageW(g_state->mode_checkbox, BM_SETCHECK,
                 g_state->settings.use_english_mode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
    const bool hotkey_ok = register_configured_hotkey();
    SetWindowTextW(g_state->hotkey_edit, g_state->settings.hotkey.c_str());
    populate_results();
    set_status(hotkey_ok ? L"配置已导入" : L"配置已导入；快捷键被占用，请重新设置",hotkey_ok);
}

void import_settings() {
    ModalGuard modal;
    wchar_t file_name[MAX_PATH] = {};
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = g_state->window;
    dialog.lpstrFilter = L"XFX 配置 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return;
    apply_imported_settings(xfx::SettingsStore::load(file_name, true));
}

void export_settings() {
    ModalGuard modal;
    wchar_t file_name[MAX_PATH] = L"XFXNativeConsole.settings.ini";
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = g_state->window;
    dialog.lpstrFilter = L"XFX 配置 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) return;
    save_settings();
    try {
        xfx::SettingsStore::export_file(settings_path(), file_name);
    } catch (...) {
        set_status(L"配置导出失败");
        return;
    }
    set_status(L"配置已导出",true);
}

bool execute_selected() {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return false;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0) return false;
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item->item_type == L"empty") {
        set_status(L"该快捷槽为空，请先绑定一个效果或预设");
        return false;
    }
    return execute_item(*item).succeeded;
}

xfx::ScriptReply execute_slot(int slot) {
    xfx::ScriptReply failure;
    failure.message = wide_to_utf8(L"快捷槽中的项目已失效，请重新绑定");
    if (slot < 0 || slot >= static_cast<int>(g_state->settings.slots.size())) return failure;
    const std::wstring key = g_state->settings.slots[static_cast<size_t>(slot)];
    if (key.empty()) {
        set_status(L"该快捷槽为空，请使用 Ctrl+Shift+数字 绑定当前项目");
        failure.message = wide_to_utf8(L"该快捷槽为空，请先绑定一个效果或预设"); return failure;
    }
    for (const auto& item : g_state->catalog) {
        if (item_key(item) == key) {
            return execute_item(item);
        }
    }
    set_status(L"快捷槽中的项目已失效，请重新绑定");
    return failure;
}

void assign_slot(int slot) {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0 || slot < 0 || slot >= 9) return;
    const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item->item_type == L"empty") { set_status(L"请先搜索并选择要绑定的效果或预设"); return; }
    auto updated = g_state->settings;
    const auto key = item_key(*item);
    if (updated.slots[static_cast<size_t>(slot)] != key) updated.slot_labels[static_cast<size_t>(slot)].clear();
    updated.slots[static_cast<size_t>(slot)] = key;
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    wchar_t message[128] = {};
    swprintf_s(message, L"已将当前项目绑定到快捷槽 Ctrl+%d", slot + 1);
    populate_results();
    set_status(message,true);
}

void assign_next_slot() {
    for (size_t index = 0; index < g_state->settings.slots.size(); ++index) {
        if (g_state->settings.slots[index].empty()) {
            assign_slot(static_cast<int>(index));
            return;
        }
    }
    set_status(L"快捷槽已全部占用，请使用 Ctrl+Shift+数字 覆盖");
}

void toggle_favorite_key(const std::wstring& key) {
    if (key.empty()) return;
    auto updated = g_state->settings;
    auto& favorites = updated.favorites;
    const auto found = std::find(favorites.begin(), favorites.end(), key);
    const bool adding = found == favorites.end();
    if (adding) {
        favorites.push_back(key);
    } else {
        favorites.erase(found);
    }
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(adding?L"已加入收藏":L"已取消收藏",true);
}

void toggle_favorite() {
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        set_status(L"请先选择一个项目");
        return;
    }
    const LRESULT data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (data == LB_ERR || data == 0) return;
    toggle_favorite_key(item_key(*reinterpret_cast<const xfx::CatalogItem*>(data)));
}

void toggle_pinned(const std::wstring& key) {
    if (key.empty()) return;
    auto updated = g_state->settings;
    auto& pinned = updated.pinned;
    const auto found = std::find(pinned.begin(), pinned.end(), key);
    const bool adding = found == pinned.end();
    if (adding) {
        pinned.push_back(key);
    } else {
        pinned.erase(found);
    }
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(adding?L"已置顶排序":L"已取消置顶排序",true);
}

void save_settings() {
    auto updated = g_state->settings;
    updated.use_english_mode =
        SendMessageW(g_state->mode_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t hotkey[128] = {};
    GetWindowTextW(g_state->hotkey_edit, hotkey, 128);
    updated.hotkey = hotkey;
    updated.fixed_window = false;
    updated.auto_close = true;
    updated.full_resolution_screenshots = true;
    try { xfx::SettingsStore::save(settings_path(), updated); }
    catch (...) {
        throw;
    }
    g_state->settings = std::move(updated);
}

void save_hotkey() {
    wchar_t hotkey[128] = {};
    GetWindowTextW(g_state->hotkey_edit, hotkey, 128);
    const auto previous = g_state->settings.hotkey;
    g_state->settings.hotkey = hotkey;
    if (!register_configured_hotkey()) {
        g_state->settings.hotkey = previous;
        SetWindowTextW(g_state->hotkey_edit, previous.c_str());
        set_status(L"快捷键格式错误或无法注册，请重试");
        return;
    }
    g_state->settings.hotkey = hotkey;
    try { xfx::SettingsStore::save(settings_path(), g_state->settings); }
    catch (...) {
        g_state->settings.hotkey = previous;
        register_configured_hotkey();
        SetWindowTextW(g_state->hotkey_edit, previous.c_str());
        throw;
    }
    set_status(L"快捷键已保存",true);
}

void save_and_restore() {
    try { save_settings(); } catch (...) { set_status(L"配置保存失败，请检查文档目录的写入权限"); }
    g_state->ime.restore();
    g_state->ime_session_active = false;
}

std::wstring trim_text(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), iswspace);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), iswspace).base();
    return first < last ? std::wstring(first, last) : L"";
}

void save_item_aliases(const std::wstring& key, const std::vector<std::wstring>& aliases) {
    auto updated = g_state->settings;
    if (aliases.empty()) updated.custom_aliases.erase(key);
    else updated.custom_aliases[key] = aliases;
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    // Keep an untouched scan so editing or clearing does not retain a previously merged alias.
    rebuild_customized_catalog();
    populate_results();
}

void edit_item_aliases(const xfx::CatalogItem& item) {
    ModalGuard modal;
    const auto key = item_key(item);
    std::wstring text;
    const auto found = g_state->settings.custom_aliases.find(key);
    if (found != g_state->settings.custom_aliases.end()) {
        for (const auto& alias : found->second) { if (!text.empty()) text += L"\r\n"; text += alias; }
    }
    if (!xfx::prompt_text_palette(g_state->window, L"编辑搜索别名",
        item_display_label(item) + L"\n每行一个别名；中文会自动支持全拼和首字母搜索。",
        true, text, g_state->font, colors())) return;
    std::vector<std::wstring> aliases;
    std::wistringstream input(text);
    std::wstring line;
    while (std::getline(input, line)) {
        line = trim_text(line);
        if (!line.empty() && std::find(aliases.begin(), aliases.end(), line) == aliases.end()) aliases.push_back(line);
    }
    save_item_aliases(key, aliases);
    set_status(aliases.empty() ? L"已清除自定义搜索别名" : L"搜索别名已保存，拼音索引已更新",true);
}

std::wstring slot_display_label(int slot) {
    if (slot < 0 || slot >= 9) return {};
    const auto index = static_cast<size_t>(slot);
    if (g_state->settings.slots[index].empty()) return L"空";
    if (!g_state->settings.slot_labels[index].empty()) return g_state->settings.slot_labels[index];
    for (const auto& item : g_state->catalog) {
        if (item_key(item) == g_state->settings.slots[index]) return item_display_label(item);
    }
    return L"项目已失效";
}

void edit_slot_label(int slot) {
    if (slot < 0 || slot >= 9 || g_state->settings.slots[static_cast<size_t>(slot)].empty()) return;
    ModalGuard modal;
    std::wstring label = g_state->settings.slot_labels[static_cast<size_t>(slot)];
    if (!xfx::prompt_text_palette(g_state->window, L"编辑快捷槽备注",
        L"Ctrl+" + std::to_wstring(slot + 1) + L" · " + slot_display_label(slot) + L"\n留空使用效果或预设名称。",
        false, label, g_state->font, colors())) return;
    auto updated = g_state->settings;
    updated.slot_labels[static_cast<size_t>(slot)] = trim_text(label);
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    set_status(L"快捷槽备注已保存",true);
}

void clear_slot(int slot) {
    if (slot < 0 || slot >= 9) return;
    auto updated = g_state->settings;
    updated.slots[static_cast<size_t>(slot)].clear();
    updated.slot_labels[static_cast<size_t>(slot)].clear();
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
    populate_results();
    set_status(L"快捷槽已清空",true);
}

template<typename Mutation> void persist_settings_update(Mutation mutation) {
    auto updated = g_state->settings;
    mutation(updated);
    xfx::SettingsStore::save(settings_path(), updated);
    g_state->settings = std::move(updated);
}

void set_theme_mode(xfx::ThemeMode mode) {
    try {
        const auto palette = xfx::resolve_theme(mode, g_state->host_background);
        ThemeBrushes brushes(palette);
        persist_settings_update([=](xfx::Settings& settings) {
            settings.theme_mode = mode; settings.light_theme = xfx::is_light_background(palette.background);
        });
        g_state->palette = palette;
        brushes.install();
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(mode), 0);
        set_status(mode == xfx::ThemeMode::Follow ? L"已跟随 AE 主题" : mode == xfx::ThemeMode::Light ? L"已使用浅色界面" : L"已使用深色界面",true);
    } catch (...) {
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
        throw;
    }
}
void set_light_theme(bool light) { set_theme_mode(light ? xfx::ThemeMode::Light : xfx::ThemeMode::Dark); }

std::wstring menu_text(const std::wstring& value) {
    std::wstring text;
    for (wchar_t c : value) { if (c == L'&') text += L'&'; text += c == L'\t' ? L' ' : c; }
    return text;
}

void append_slot_menu(HMENU menu, bool binding) {
    for (int slot = 0; slot < 9; ++slot) {
        HMENU actions = CreatePopupMenu();
        const bool empty = g_state->settings.slots[static_cast<size_t>(slot)].empty();
        AppendMenuW(actions, MF_STRING | (empty ? MF_GRAYED : 0), 70 + slot, L"执行快捷项");
        if (binding) AppendMenuW(actions, MF_STRING, 10 + slot, L"绑定当前项目");
        AppendMenuW(actions, MF_STRING | (empty ? MF_GRAYED : 0), 30 + slot, L"编辑备注…");
        AppendMenuW(actions, MF_STRING | (empty ? MF_GRAYED : 0), 50 + slot, L"清空快捷槽");
        const auto label = L"Ctrl+" + std::to_wstring(slot + 1) + L" · " + menu_text(slot_display_label(slot));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(actions), label.c_str());
    }
}

void handle_slot_command(UINT command) {
    if (command >= 10 && command <= 18) assign_slot(command - 10);
    if (command >= 30 && command <= 38) edit_slot_label(command - 30);
    if (command >= 50 && command <= 58) clear_slot(command - 50);
    if (command >= 70 && command <= 78) execute_slot(command - 70);
}

void show_slot_menu(int slot, POINT point) {
    if (slot < 0 || slot >= 9) return;
    ModalGuard modal;
    HMENU menu = CreatePopupMenu();
    const bool empty = g_state->settings.slots[static_cast<size_t>(slot)].empty();
    AppendMenuW(menu, MF_STRING | (empty ? MF_GRAYED : 0), 70 + slot, L"执行快捷项");
    AppendMenuW(menu, MF_STRING | (empty ? MF_GRAYED : 0), 30 + slot, L"编辑备注…");
    AppendMenuW(menu, MF_STRING | (empty ? MF_GRAYED : 0), 50 + slot, L"清空快捷槽");
    UINT command = 0;
#ifdef XFX_CONSOLE_TEST
    if (g_test_popup_command) {
        command = *g_test_popup_command;
        g_test_popup_command.reset();
    } else
#endif
        command = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            point.x, point.y, g_state->window, colors(), g_state->font);
    DestroyMenu(menu);
    handle_slot_command(command);
}

void show_shortcut_menu() {
    ModalGuard modal;
    HMENU menu = CreatePopupMenu();
    append_slot_menu(menu, SendMessageW(g_state->results, LB_GETCURSEL, 0, 0) != LB_ERR);
    RECT rect{}; GetWindowRect(g_state->slot, &rect);
    const auto command = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        rect.left, rect.bottom, g_state->window, colors(), g_state->font);
    DestroyMenu(menu);
    handle_slot_command(command);
}

void show_result_menu(HWND hwnd, LPARAM location) {
    ModalGuard modal;
    POINT point{GET_X_LPARAM(location), GET_Y_LPARAM(location)};
    if (point.x == -1 && point.y == -1) { GetCursorPos(&point); }
    else {
        POINT client = point; ScreenToClient(g_state->results, &client);
        const LRESULT hit = SendMessageW(g_state->results, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        if (!HIWORD(hit)) SendMessageW(g_state->results, LB_SETCURSEL, LOWORD(hit), 0);
    }
    const LRESULT selected = SendMessageW(g_state->results, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) return;
    if (shortcut_list_mode() && selected >= 0 && selected < 9) {
        show_slot_menu(static_cast<int>(selected), point);
        return;
    }
    const auto data = SendMessageW(g_state->results, LB_GETITEMDATA, selected, 0);
    if (!data || data == LB_ERR) return;
    const auto item = *reinterpret_cast<const xfx::CatalogItem*>(data);
    if (item.item_type == L"empty") { show_shortcut_menu(); return; }
    const auto key = item_key(item);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"收藏 / 取消收藏");
    AppendMenuW(menu, MF_STRING, 8,
        contains_key(g_state->settings.pinned, key) ? L"取消置顶排序" : L"置顶排序");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool has_aliases = g_state->settings.custom_aliases.count(key) != 0;
    AppendMenuW(menu, MF_STRING, 6, has_aliases ? L"编辑搜索别名…" : L"添加搜索别名…");
    AppendMenuW(menu, MF_STRING | (has_aliases ? 0 : MF_GRAYED), 7, L"清除搜索别名");
    if (item.item_type == L"effect") {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"用动画预设覆盖此效果…");
        AppendMenuW(menu, MF_STRING, 5, L"取消预设覆盖");
    }
    HMENU slots = CreatePopupMenu();
    append_slot_menu(slots, true);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(slots), L"快捷槽");
    const UINT command = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, hwnd, colors(), g_state->font);
    DestroyMenu(menu);
    if (command == 1) toggle_favorite();
    if (command == 5) persist_settings_update([&](xfx::Settings& settings) { settings.overrides.erase(key); });
    if (command == 6) edit_item_aliases(item);
    if (command == 7) { save_item_aliases(key, {}); set_status(L"已清除自定义搜索别名",true); }
    if (command == 8) toggle_pinned(key);
    handle_slot_command(command);
    if (command == 4) {
        wchar_t filename[32768] = {};
        OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = hwnd;
        dialog.lpstrFile = filename; dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"AE 动画预设 (*.ffx)\0*.ffx\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileNameW(&dialog)) persist_settings_update([&](xfx::Settings& settings) { settings.overrides[key] = filename; });
    }
    if (command) populate_results();
}

void adjust_console_height() {
    if (!g_state || !g_state->search || !g_state->results) return;
    const HWND hwnd = GetParent(g_state->search);
    const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
    const LRESULT count = SendMessageW(g_state->results, LB_GETCOUNT, 0, 0);
    const int rows = shortcut_list_mode() ? 1 : static_cast<int>((std::max)(static_cast<LRESULT>(1),
        (std::min)(count, static_cast<LRESULT>(8))));
    RECT client{}; GetClientRect(hwnd, &client);
    const double scale = GetDpiForWindow(hwnd) / 96.0;
    RECT desired{0, 0, client.right, static_cast<LONG>((56 + (shortcut_list_mode() ? kShortcutHeight : rows * kResultHeight) + 22 +
        g_state->settings_extent)*scale)};
    AdjustWindowRectEx(&desired, style, FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    const int height = desired.bottom - desired.top;
    RECT current{}; GetWindowRect(hwnd, &current);
    if (current.bottom - current.top == height) return;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    const int top = (std::max)(monitor.rcWork.top, (std::min)(current.top, monitor.rcWork.bottom - height));
    SetWindowPos(hwnd, nullptr, current.left, top, current.right - current.left, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void apply_button_region(HWND button) {
    if (!IsWindow(button)) return;
    RECT rect{};
    GetClientRect(button, &rect);
    const auto old_width = reinterpret_cast<INT_PTR>(GetPropW(button, L"XFXButtonRegionWidth"));
    const auto old_height = reinterpret_cast<INT_PTR>(GetPropW(button, L"XFXButtonRegionHeight"));
    if (old_width == rect.right && old_height == rect.bottom) return;
    const int diameter = ui_px(8);
    HRGN region = CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1, diameter, diameter);
    if (region) {
        if (SetWindowRgn(button, region, FALSE)) {
            SetPropW(button, L"XFXButtonRegionWidth", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(rect.right)));
            SetPropW(button, L"XFXButtonRegionHeight", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(rect.bottom)));
        } else DeleteObject(region);
    }
}

void fit_combo_face_height(HWND combo, int target_height) {
    if (!IsWindow(combo)) return;
    RECT actual{};
    if (!GetWindowRect(combo, &actual)) return;
    const int current_height = actual.bottom - actual.top;
    if (current_height == target_height) return;
    const LRESULT selection_height = SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    if (selection_height == CB_ERR) return;
    // Native ComboBox ignores the requested closed height in MoveWindow. Its
    // selection height plus a native frame determines the actual HWND size.
    // Measure that frame instead of assuming six unscaled pixels for all DPI.
    const int desired_selection = (std::max)(1, static_cast<int>(selection_height) + target_height - current_height);
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), desired_selection);
}

void layout_controls(HWND hwnd) {
    RECT rect{}; GetClientRect(hwnd, &rect);
    const UINT dpi = GetDpiForWindow(hwnd);
    const double scale = (dpi ? dpi : 96) / 96.0;
    const int width = static_cast<int>(rect.right / scale), height = static_cast<int>(rect.bottom / scale);
    const int margin = 8, inner = width - 16;
    const auto move = [hwnd,scale](HWND child, int x, int y, int w, int h) {
        if (!IsWindow(child)) return;
        const int left = static_cast<int>(x*scale), top = static_cast<int>(y*scale);
        const int width = static_cast<int>((std::max)(1,w)*scale), height = static_cast<int>((std::max)(1,h)*scale);
        RECT current{}; GetWindowRect(child, &current);
        POINT origin{current.left,current.top}; ScreenToClient(hwnd,&origin);
        const RECT desired{left,top,left+width,top+height};
        const RECT actual{origin.x,origin.y,origin.x+current.right-current.left,origin.y+current.bottom-current.top};
        if (EqualRect(&desired,&actual)) return;
        MoveWindow(child,left,top,width,height,FALSE);
        if (child == g_state->light_theme) fit_combo_face_height(child, height);
        if (!g_state->redraw_transaction) InvalidateRect(child,nullptr,FALSE);
    };
    const auto toolbar_bounds = toolbar_layout(hwnd);
    const auto place = [hwnd](HWND child, const RECT& bounds) {
        if (!IsWindow(child)) return;
        RECT current{};
        GetWindowRect(child, &current);
        POINT origin{current.left, current.top};
        ScreenToClient(hwnd, &origin);
        if (origin.x == bounds.left && origin.y == bounds.top &&
            current.right - current.left == bounds.right - bounds.left &&
            current.bottom - current.top == bounds.bottom - bounds.top) return;
        MoveWindow(child, bounds.left, bounds.top, bounds.right - bounds.left,
            bounds.bottom - bounds.top, FALSE);
        if (!g_state->redraw_transaction) InvalidateRect(child, nullptr, FALSE);
    };
    place(g_state->search, toolbar_bounds.search_edit);
    place(g_state->filter, toolbar_bounds.filter);
    fit_combo_face_height(g_state->filter, toolbar_bounds.filter.bottom - toolbar_bounds.filter.top);
    place(g_state->capture, toolbar_bounds.buttons[0]);
    place(g_state->slot, toolbar_bounds.buttons[1]);
    place(g_state->preferences, toolbar_bounds.buttons[2]);
    const int footer = height - 22 - g_state->settings_extent;
    move(g_state->results, margin, 56, inner, (std::max)(kResultHeight,footer-56));
    const bool narrow = width < 440;
    const auto setting = [&](HWND child, int x, int y, int w, int h, int visual_height = 0) {
        move(child,x,footer+y,w,h);
        const bool should_show = g_state->settings_extent >= y+(visual_height ? visual_height : h);
        const bool is_visible = IsWindowVisible(child) != FALSE;
        if (should_show != is_visible || !g_state->settings_animation_active)
            ShowWindow(child, should_show ? SW_SHOWNA : SW_HIDE);
    };
    setting(g_state->refresh,8,6,80,28); setting(g_state->dictionary,92,6,80,28);
    setting(g_state->count,180,10,inner-172,22);
    setting(g_state->hotkey_label,8,40,62,26); setting(g_state->hotkey_edit,74,40,128,26);
    setting(g_state->hotkey_save,210,40,52,26);
    setting(g_state->import_settings,narrow?8:270,narrow?72:40,64,26);
    setting(g_state->export_settings,narrow?80:342,narrow?72:40,64,26);
    const int theme_y = narrow ? 106 : 74;
    setting(g_state->mode_checkbox,8,theme_y,narrow?inner:inner-144,24);
    setting(g_state->light_theme,width-136,theme_y+(narrow?30:-2),128,28,24);
    move(g_state->status,8,height-20,inner,18);
    for (HWND button : {g_state->capture, g_state->slot, g_state->preferences,
                        g_state->refresh, g_state->dictionary, g_state->hotkey_save,
                        g_state->import_settings, g_state->export_settings})
        apply_button_region(button);
    SendMessageW(g_state->results, LB_SETITEMHEIGHT, 0, static_cast<LPARAM>((shortcut_list_mode()?kShortcutHeight:kResultHeight)*scale));
    if(shortcut_list_mode()) SendMessageW(g_state->results,LB_SETCOLUMNWIDTH,static_cast<WPARAM>(inner*scale/9),0);
}

void draw_toolbar_icon(HDC dc,const RECT& rect,int id) {
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float scale=(rect.right-rect.left)/40.0f;
    g.TranslateTransform((rect.left+rect.right)*.5f,(rect.top+rect.bottom)*.5f);g.ScaleTransform(scale,scale);
    const HWND button=GetDlgItem(g_state->window,id);
    const bool disabled=!IsWindowEnabled(button),selected=id==kPreferencesId && g_state->settings_visible;
    const auto found=g_state->button_hovers.find(button);
    const bool hovered=found!=g_state->button_hovers.end() && found->second.hovered;
    const auto ink=disabled?colors().disabled:selected?colors().selected_text:hovered?colors().text:colors().icon;
    Gdiplus::Pen pen(vector_color(ink),1.5f);
    pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
    // Every symbol fits the same 18px ink box, including its 1.5px stroke.
    if(id==kCaptureId) {
        g.DrawRectangle(&pen,-8.25f,-4.25f,16.5f,11.5f);g.DrawRectangle(&pen,-4.25f,-7.25f,7.5f,3.0f);
        g.DrawEllipse(&pen,-3.0f,-2.0f,6.0f,6.0f);
    } else if(id==kSlotId) {
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)g.DrawRectangle(&pen,-8.25f+x*6,-8.25f+y*6,4.5f,4.5f);
    } else if(id==kPreferencesId) {
        Gdiplus::PointF points[32];
        for(int n=0;n<32;++n){const float angle=(n-.5f)*3.14159265f/16,r=n%4==1 || n%4==2?8.25f:6.25f;points[n]={std::cos(angle)*r,std::sin(angle)*r};}
        g.DrawPolygon(&pen,points,32);g.DrawEllipse(&pen,-2.75f,-2.75f,5.5f,5.5f);
    }
}

void create_control(HWND parent, HWND* target, const wchar_t* class_name,
                    const wchar_t* text, DWORD style, int id) {
    *target = CreateWindowExW(0, class_name, text, style, 0, 0, 0, 0, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    set_font(*target);
    if(id==kStatusId && g_state->status_font) SendMessageW(*target,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_font),TRUE);
}
void draw_combo_face(HWND hwnd, HDC dc) {
    const int saved = SaveDC(dc);
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto& c = colors();
    FillRect(dc, &rect, g_state->background_brush);
    xfx::paint_round_rect(dc, rect, c.input, static_cast<float>(ui_px(4)), GetFocus() == hwnd ? c.accent : c.divider);
    SelectObject(dc, g_state->font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(hwnd) ? c.text : c.disabled);
    const auto selection = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    wchar_t label[128]{};
    if (selection >= 0) SendMessageW(hwnd, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(label));
    // Both normal painting and ODS_COMBOBOXEDIT use full-client coordinates.
    // Native rcItem is a clipping boundary, never a second text alignment box.
    RECT text = rect;
    DrawTextW(dc, label, -1, &text, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
    const int arrow = MulDiv(20, GetDpiForWindow(hwnd), 96);
    {
        Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(vector_color(c.icon), 1.5f);
        const float x = static_cast<float>(rect.right - arrow / 2), y = rect.bottom * .5f;
        graphics.DrawLine(&pen, x - 3, y - 1, x, y + 2); graphics.DrawLine(&pen, x, y + 2, x + 3, y - 1);
    }
    if (saved) RestoreDC(dc, saved);
}

LRESULT CALLBACK combo_face_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (forward_edge_drag(hwnd, message, wp, lp)) return 0;
    if(message==WM_NCDESTROY) {RemoveWindowSubclass(hwnd,combo_face_proc,id);return DefSubclassProc(hwnd,message,wp,lp);}
    if(message==WM_ERASEBKGND) return TRUE;
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{}; HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wp);
        draw_combo_face(hwnd, dc);
        if (message == WM_PAINT) EndPaint(hwnd, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    const bool native_face_update = message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == CB_SETCURSEL || message == CB_SHOWDROPDOWN || message == WM_ENABLE ||
        message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP;
    if (native_face_update && IsWindow(hwnd) && !g_state->redraw_transaction) {
        // Native ComboBox draws directly during these messages, including its
        // arrow and focus frame. Complete the custom face before returning;
        // queued WM_PAINT would leave a visibly different intermediate face.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }
    return result;
}

constexpr UINT_PTR kComboPopupSubclassId = 0xB008;

LRESULT CALLBACK combo_popup_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, combo_popup_proc, id);
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    if (message == WM_ERASEBKGND) {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wp), &rect, g_state->input_brush);
        return TRUE;
    }
    if (message == WM_THEMECHANGED || message == WM_SETFONT) InvalidateRect(hwnd, nullptr, FALSE);
    return DefSubclassProc(hwnd, message, wp, lp);
}

void theme_combo_popup(HWND combo) {
    if (!combo) return;
    const DWORD process_id = GetCurrentProcessId();
    for (HWND popup = FindWindowExW(nullptr, nullptr, L"ComboLBox", nullptr);
         popup; popup = FindWindowExW(nullptr, popup, L"ComboLBox", nullptr)) {
        DWORD owner_process = 0;
        GetWindowThreadProcessId(popup, &owner_process);
        if (owner_process != process_id) continue;
        if (GetWindow(popup, GW_OWNER) != combo && GetParent(popup) != combo) continue;
        SetWindowTheme(popup, L"", L"");
        SetWindowSubclass(popup, combo_popup_proc, kComboPopupSubclassId, 0);
        const HWND list = FindWindowExW(popup, nullptr, L"ListBox", nullptr);
        if (list) {
            SetWindowTheme(list, L"", L"");
            SetWindowSubclass(list, combo_popup_proc, kComboPopupSubclassId, 0);
            set_font(list);
            InvalidateRect(list, nullptr, FALSE);
        }
        InvalidateRect(popup, nullptr, FALSE);
    }
}

LRESULT CALLBACK checkbox_face_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (forward_edge_drag(hwnd, message, wp, lp)) return 0;
    if(message==WM_NCDESTROY) {RemoveWindowSubclass(hwnd,checkbox_face_proc,id);return DefSubclassProc(hwnd,message,wp,lp);}
    if(message==WM_MOUSEMOVE && !GetPropW(hwnd,L"XFXCheckHover")) {
        SetPropW(hwnd,L"XFXCheckHover",reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};
        TrackMouseEvent(&tracking);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE && GetPropW(hwnd,L"XFXCheckHover")) {
        RemovePropW(hwnd,L"XFXCheckHover");
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    if(message==WM_ERASEBKGND) return TRUE;
    if(message==WM_PAINT || message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wp);
        RECT rect{};GetClientRect(hwnd,&rect);const auto& c=colors();
        SetDCBrushColor(dc,c.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool enabled=IsWindowEnabled(hwnd)!=FALSE,checked=SendMessageW(hwnd,BM_GETCHECK,0,0)==BST_CHECKED;
        const int size=ui_px(13),top=(rect.bottom-size)/2;
        RECT box{0,top,size,top+size};SetDCBrushColor(dc,checked?c.selected:GetPropW(hwnd,L"XFXCheckHover")?c.button_pressed:c.input);
        FillRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetDCBrushColor(dc,GetFocus()==hwnd?c.accent:c.divider);FrameRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(checked) {
            Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Pen pen(vector_color(enabled?c.selected_text:c.disabled),static_cast<float>(ui_px(2)));
            graphics.DrawLine(&pen,static_cast<float>(ui_px(3)),static_cast<float>(top+ui_px(6)),static_cast<float>(ui_px(6)),static_cast<float>(top+ui_px(9)));
            graphics.DrawLine(&pen,static_cast<float>(ui_px(6)),static_cast<float>(top+ui_px(9)),static_cast<float>(ui_px(11)),static_cast<float>(top+ui_px(3)));
        }
        const auto old=SelectObject(dc,g_state->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,enabled?c.text:c.disabled);
        wchar_t label[256]{};GetWindowTextW(hwnd,label,256);RECT text=rect;text.left+=ui_px(19);
        DrawTextW(dc,label,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);
        if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;
    }
    const auto result=DefSubclassProc(hwnd,message,wp,lp);
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==BM_SETCHECK || message==BM_SETSTATE) InvalidateRect(hwnd,nullptr,FALSE);
    return result;
}
void update_console_fonts(HWND hwnd) {
    const int dpi=GetDpiForWindow(hwnd);
    const auto make=[dpi](int pixels){return CreateFontW(-MulDiv(pixels,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,xfx::kUiFontFamily);};
    HFONT font=make(xfx::kUiBodyPixels),slot_font=make(xfx::kUiSlotNumberPixels),status_font=make(xfx::kUiStatusPixels);
    if(!font || !slot_font || !status_font){if(font)DeleteObject(font);if(slot_font)DeleteObject(slot_font);if(status_font)DeleteObject(status_font);return;}
    const auto old_font=g_state->font,old_small=g_state->slot_number_font,old_status=g_state->status_font;
    g_state->font=font;g_state->slot_number_font=slot_font;g_state->status_font=status_font;
    EnumChildWindows(hwnd,[](HWND child,LPARAM)->BOOL{set_font(child);return TRUE;},0);
    if(IsWindow(g_state->status))SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_important?font:status_font),TRUE);
    for(HWND combo:{g_state->filter,g_state->light_theme})if(IsWindow(combo)){
        SendMessageW(combo,CB_SETITEMHEIGHT,0,MulDiv(26,dpi,96));
    }
    if(old_font)DeleteObject(old_font);if(old_small)DeleteObject(old_small);if(old_status)DeleteObject(old_status);
}

bool add_tooltip_for_control(HWND parent, HWND tooltip, HWND control, UINT_PTR id, const wchar_t* text) {
    if (!parent || !tooltip || !control) return false;
    TOOLINFOW tool{TTTOOLINFOW_V1_SIZE};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = parent;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.hinst = GetModuleHandleW(nullptr);
    g_state->tooltip_texts[id] = text ? std::wstring(text) : std::wstring();
    tool.lpszText = g_state->tooltip_texts[id].data();
    const auto added = SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    return added != FALSE;
}


LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
    switch (message) {
    case WM_NCHITTEST: {
        POINT screen_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (point_is_window_edge(hwnd, screen_point)) return HTCAPTION;
        POINT point = screen_point;
        ScreenToClient(hwnd, &point);
        if (point_is_drag_edge(hwnd, point)) return HTCAPTION;
        break;
    }
    case WM_APP + 3:
        if (!IsWindowVisible(hwnd) && !g_state->gallery_window && !g_state->modal_depth) {
            save_and_restore(); DestroyWindow(hwnd);
        }
        return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wparam) == g_state->results) show_result_menu(hwnd, lparam);
        return 0;
    case WM_LBUTTONDOWN:
        if (!g_state->modal_depth) {
            const auto search = toolbar_layout(hwnd).search_surface;
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (PtInRect(&search, point)) {
                SetFocus(g_state->search);
                return 0;
            }
        }
        if (!g_state->modal_depth && point_is_drag_edge(hwnd,
            POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
            g_state->drag_pending = false;
            g_state->drag_active = true;
            SetCapture(hwnd);
            GetCursorPos(&g_state->drag_anchor_cursor);
            RECT window{};
            GetWindowRect(hwnd, &window);
            g_state->drag_anchor_window = {window.left, window.top};
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_state->drag_pending || g_state->drag_active) {
            g_state->drag_pending = false;
            g_state->drag_active = false;
            KillTimer(hwnd, kDragTimerId);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        g_state->drag_pending = false;
        g_state->drag_active = false;
        KillTimer(hwnd, kDragTimerId);
        break;
    case WM_MOUSEMOVE:
        if (g_state->drag_active && GetCapture() == hwnd) {
            POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ClientToScreen(hwnd, &cursor);
            SetWindowPos(hwnd, nullptr,
                g_state->drag_anchor_window.x + cursor.x - g_state->drag_anchor_cursor.x,
                g_state->drag_anchor_window.y + cursor.y - g_state->drag_anchor_cursor.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == 0xB003) {
            if (g_state->status_expires && GetTickCount64() >= g_state->status_expires) {
                SetWindowTextW(g_state->status,L"就绪"); g_state->status_expires = 0;g_state->status_important=false;
                SendMessageW(g_state->status,WM_SETFONT,reinterpret_cast<WPARAM>(g_state->status_font),TRUE);
            }
            KillTimer(hwnd,0xB003); return 0;
        }
        if (wparam == kSettingsAnimationTimerId) {
            if (!g_state->settings_animation_active) {
                KillTimer(hwnd, kSettingsAnimationTimerId);
                return 0;
            }
            const double t = (std::min)(1.0,(GetTickCount64()-g_state->expand_started)/200.0);
            const double ease = 1-std::pow(1-t,3);
            g_state->settings_extent = static_cast<int>(g_state->expand_from+(g_state->expand_to-g_state->expand_from)*ease);
            layout_controls(hwnd);
            if (t>=1) {
                g_state->settings_extent = g_state->expand_to;
                g_state->settings_animation_active = false;
                if (g_state->expand_to == 0) adjust_console_height();
                layout_controls(hwnd);
                KillTimer(hwnd, kSettingsAnimationTimerId);
            }
            return 0;
        }
        if (wparam == kDragTimerId && g_state->drag_pending) {
            if (drag_button_is_held() && GetCapture() == hwnd) {
                g_state->drag_pending = false;
                g_state->drag_active = true;
                KillTimer(hwnd, kDragTimerId);
                GetCursorPos(&g_state->drag_anchor_cursor);
                RECT window{}; GetWindowRect(hwnd, &window);
                g_state->drag_anchor_window = {window.left, window.top};
            } else {
                g_state->drag_pending = false;
                g_state->drag_active = false;
                KillTimer(hwnd, kDragTimerId);
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            return 0;
        }
        break;
    case WM_CREATE: {
        g_state->window = hwnd;
        g_state->initial_background_primed = false;
        g_state->initial_frame_painted = false;
        g_state->corner_size={};
        update_console_fonts(hwnd);
        create_control(hwnd, &g_state->search, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, kSearchId);
        SendMessageW(g_state->search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"输入中文、拼音、英文或 matchName"));
        create_control(hwnd, &g_state->filter, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
            CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kFilterId);
        SetWindowTheme(g_state->filter, L"", L"");
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"效果"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"预设"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"收藏"));
        SendMessageW(g_state->filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"最近"));
        SendMessageW(g_state->filter, CB_SETCURSEL, 0, 0);

        create_control(hwnd, &g_state->results, L"LISTBOX", L"",
                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                           LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                       kResultsId);
        create_control(hwnd, &g_state->slot, L"BUTTON", L"快捷项", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kSlotId);
        create_control(hwnd, &g_state->refresh, L"BUTTON", L"刷新索引", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kRefreshId);
        create_control(hwnd, &g_state->capture, L"BUTTON", L"合成快照", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kCaptureId);
        create_control(hwnd, &g_state->dictionary, L"BUTTON", L"编辑译名", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kDictionaryId);
        create_control(hwnd, &g_state->preferences, L"BUTTON", L"设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kPreferencesId);
        create_control(hwnd, &g_state->count, L"STATIC", L"正在统计...", WS_CHILD | WS_VISIBLE | SS_RIGHT, kCountId);
        create_control(hwnd, &g_state->hotkey_label, L"STATIC", L"快捷键：",
                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, kHotkeyLabelId);
        WNDCLASSEXW capture_class{sizeof(capture_class)};
        capture_class.hInstance = GetModuleHandleW(nullptr);
        capture_class.lpfnWndProc = hotkey_edit_proc;
        capture_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        capture_class.lpszClassName = kHotkeyCaptureClassName;
        if (!RegisterClassExW(&capture_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return -1;
        create_control(hwnd, &g_state->hotkey_edit, kHotkeyCaptureClassName, L"Ctrl+Space",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP, kHotkeyEditId);
        if (!g_state->hotkey_edit) return -1;
        create_control(hwnd, &g_state->hotkey_save, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kHotkeySaveId);
        create_control(hwnd, &g_state->import_settings, L"BUTTON", L"导入", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kImportId);
        create_control(hwnd, &g_state->export_settings, L"BUTTON", L"导出", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, kExportId);
        create_control(hwnd, &g_state->mode_checkbox, L"BUTTON",
                       L"使用时切换英文输入（关闭后恢复）",
                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, kModeId);
        create_control(hwnd, &g_state->light_theme, L"COMBOBOX", L"界面主题",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kLightThemeId);
        for (const auto* label : {L"跟随", L"浅色", L"深色"})
            SendMessageW(g_state->light_theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SetWindowTheme(g_state->light_theme, L"", L"");
        for(HWND combo : {g_state->filter,g_state->light_theme}) SetWindowSubclass(combo,combo_face_proc,0xB005,0);
        for(HWND combo : {g_state->filter,g_state->light_theme}) {
            SendMessageW(combo,CB_SETITEMHEIGHT,0,MulDiv(26,GetDpiForWindow(hwnd),96));
        }
        for (HWND checkbox : {g_state->mode_checkbox})
            SetWindowTheme(checkbox, L"", L"");
        SetWindowSubclass(g_state->mode_checkbox,checkbox_face_proc,0xB007,0);
        create_control(hwnd, &g_state->status, L"STATIC", L"正在扫描...",
                       WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTERIMAGE | SS_ENDELLIPSIS, kStatusId);
        SetWindowSubclass(g_state->status, edge_drag_proc, kEdgeDragSubclassId, 0);
        g_state->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (g_state->tooltip) {
            SetWindowTheme(g_state->tooltip, L"", L"");
            set_font(g_state->tooltip);
            SendMessageW(g_state->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
            SendMessageW(g_state->tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 600);
            SendMessageW(g_state->tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
        }
        SetWindowTextW(g_state->hotkey_edit, g_state->settings.hotkey.c_str());
        SendMessageW(g_state->mode_checkbox, BM_SETCHECK,
                     g_state->settings.use_english_mode ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_state->light_theme, CB_SETCURSEL, static_cast<int>(g_state->settings.theme_mode), 0);
        repaint_theme_windows();
        g_search_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_state->search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(search_edit_proc)));
        g_results_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_state->results, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(results_list_proc)));
        if (!register_configured_hotkey()) {
            set_status(L"默认快捷键不可用，请在下方修改");
        }
        layout_controls(hwnd);
        const HWND animated_buttons[] = {g_state->capture, g_state->slot, g_state->preferences,
            g_state->refresh, g_state->dictionary, g_state->hotkey_save,
            g_state->import_settings, g_state->export_settings};
        for (HWND button : animated_buttons) {
            if (SetWindowSubclass(button, button_hover_proc, kButtonHoverSubclassId, 0))
                g_state->button_hovers.emplace(button, ButtonHoverState{});
        }
        const HWND tools[] = {
            g_state->search, g_state->filter, g_state->capture, g_state->slot, g_state->preferences,
            g_state->refresh, g_state->dictionary,
            g_state->hotkey_edit, g_state->hotkey_save, g_state->import_settings, g_state->export_settings,
            g_state->mode_checkbox, g_state->light_theme
        };
        const wchar_t* tips[] = {
            L"搜索效果、预设、中文、拼音、英文或 matchName",
            L"筛选全部、效果、预设、收藏或最近",
            L"保存当前合成快照",
            L"执行或编辑快捷项",
            L"展开或收起设置",
            L"重新扫描效果与预设",
            L"编辑中文译名",
            L"点击后按下新的快捷键",
            L"保存快捷键",
            L"导入配置",
            L"导出配置",
            L"使用期间切换输入法为英文模式",
            L"界面主题：跟随 AE、浅色或深色；选择会保存"
        };
        for (size_t index = 0; index < std::size(tools) && g_state->tooltip; ++index)
            add_tooltip_for_control(hwnd, g_state->tooltip, tools[index], 100 + index, tips[index]);
        if (g_state->tooltip) {
            g_state->result_tooltip = L"鼠标左键应用；右键打开更多操作";
            add_tooltip_for_control(hwnd, g_state->tooltip, g_state->results, kResultTooltipId,
                g_state->result_tooltip.c_str());
        }
        refresh_catalog();
        return 0;
    }
    case WM_SIZE:
        if (g_state && g_state->search) {
            if(g_state->settings_visible && !g_state->settings_animation_active) {
                RECT client{};GetClientRect(hwnd,&client);
                const int target=client.right*96/GetDpiForWindow(hwnd)<440?166:kSettingsHeight;
                g_state->expand_to=target;
                if(GetTickCount64()-g_state->expand_started>=200 && g_state->settings_extent!=target) {
                    g_state->settings_extent=target;adjust_console_height();
                }
            }
            layout_controls(hwnd);
            RECT bounds{};GetWindowRect(hwnd,&bounds);
            const SIZE size{bounds.right-bounds.left,bounds.bottom-bounds.top};
            if(size.cx!=g_state->corner_size.cx || size.cy!=g_state->corner_size.cy) {
                g_state->corner_size=size;
                HRGN region=CreateRoundRectRgn(0,0,size.cx+1,size.cy+1,ui_px(16),ui_px(16));
                if(region && !SetWindowRgn(hwnd,region,FALSE))DeleteObject(region);
            }
        }
        return 0;
    case WM_DPICHANGED: {
        update_console_fonts(hwnd);
        const auto* suggested=reinterpret_cast<RECT*>(lparam);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
            suggested->bottom-suggested->top,SWP_NOACTIVATE|SWP_NOZORDER);
        adjust_console_height();layout_controls(hwnd);return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = MulDiv(360,GetDpiForWindow(hwnd),96);
        info->ptMinTrackSize.y = MulDiv(114+g_state->settings_extent,GetDpiForWindow(hwnd),96);
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure->CtlID == kResultsId) measure->itemHeight = shortcut_list_mode() ? kShortcutHeight : kResultHeight;
        if (measure->CtlID == kFilterId || measure->CtlID == kLightThemeId) measure->itemHeight = MulDiv(26,GetDpiForWindow(hwnd),96);
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        struct RestoreDrawDc {HDC dc;int saved;~RestoreDrawDc(){if(saved)RestoreDC(dc,saved);}} restore{draw->hDC,SaveDC(draw->hDC)};
        SelectObject(draw->hDC,g_state->font);
        if (draw->CtlID == kFilterId || draw->CtlID == kLightThemeId) {
            if (draw->itemState & ODS_COMBOBOXEDIT) {
                IntersectClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
                draw_combo_face(draw->hwndItem, draw->hDC);
                return TRUE;
            }
            const bool selected = (draw->itemState & ODS_SELECTED) && !(draw->itemState & ODS_COMBOBOXEDIT);
            HBRUSH brush = CreateSolidBrush(selected ? colors().selected : colors().input);
            FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
            if (draw->itemID != static_cast<UINT>(-1)) {
                wchar_t label[64]{}; SendMessageW(draw->hwndItem, CB_GETLBTEXT, draw->itemID, reinterpret_cast<LPARAM>(label));
                RECT text = draw->rcItem; text.left += 4;
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text);
                DrawTextW(draw->hDC, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            return TRUE;
        }
        if (draw->CtlID == kResultsId && draw->itemID != static_cast<UINT>(-1)) {
#ifdef XFX_CONSOLE_TEST
            ++g_test_result_draw_count;
#endif
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            HBRUSH brush = CreateSolidBrush(colors().input);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            const LRESULT data = SendMessageW(draw->hwndItem, LB_GETITEMDATA, draw->itemID, 0);
            const auto* item = reinterpret_cast<const xfx::CatalogItem*>(data);
            SetBkMode(draw->hDC, TRANSPARENT);
            if (shortcut_list_mode()) {
                const int slot = static_cast<int>(draw->itemID);
                RECT card = draw->rcItem; InflateRect(&card, -2, -2);
                xfx::paint_round_rect(draw->hDC,card,selected?colors().selected:colors().surface,
                    static_cast<float>(ui_px(3)), selected ? std::optional<COLORREF>(colors().accent) : std::nullopt);
                const auto px=[hwnd](int v){return MulDiv(v,GetDpiForWindow(hwnd),96);};
                RECT number = card; number.left += px(3); number.top += px(1); number.bottom = number.top + px(10);
                const auto previous_font = SelectObject(draw->hDC,
                    g_state->slot_number_font ? g_state->slot_number_font : g_state->font);
                SetTextColor(draw->hDC, selected ? colors().selected_text : colors().success);
                DrawTextW(draw->hDC, std::to_wstring(slot + 1).c_str(), -1, &number, DT_LEFT | DT_SINGLELINE);
                SelectObject(draw->hDC, previous_font);
                RECT label = card; label.left += px(3); label.right -= px(3); label.top += px(10);
                auto name = slot_display_label(slot);
                SetTextColor(draw->hDC,selected?colors().selected_text:g_state->settings.slots[static_cast<size_t>(slot)].empty()?colors().quiet:colors().text);
                DrawTextW(draw->hDC, name.c_str(), -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                return TRUE;
            }

            if (selected) {
                RECT selection = draw->rcItem;
                InflateRect(&selection, -ui_px(2), -ui_px(2));
                xfx::paint_round_rect(draw->hDC, selection, colors().selected,
                    static_cast<float>(ui_px(3)), colors().accent);
            }

            const auto px=[](int value){return ui_px(value);};
            RECT icon_rect = draw->rcItem;
            icon_rect.left += px(8);
            icon_rect.right = icon_rect.left + px(18);
            icon_rect.top += px(5);
            icon_rect.bottom = icon_rect.top + px(18);
            const bool empty = item && item->item_type == L"empty";
            const bool preset = item && item->item_type == L"preset";
            HBRUSH icon_brush = CreateSolidBrush(empty ? colors().muted :
                (preset ? colors().preset_badge : colors().effect_badge));
            FillRect(draw->hDC, &icon_rect, icon_brush);
            DeleteObject(icon_brush);
            SetTextColor(draw->hDC, colors().selected_text);
            DrawTextW(draw->hDC, empty ? L"" : (preset ? L"P" : L"E"), -1, &icon_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(draw->hDC, selected ? colors().selected_text : colors().text);
            RECT primary = draw->rcItem;
            const bool shortcut_mode = shortcut_list_mode();
            primary.left += shortcut_mode ? px(28) : px(34);
            primary.right -= shortcut_mode ? px(8) : ui_px(kResultActionWidth) * 3 + 4;
            std::wstring primary_text = L"未命名项目";
            if (item) {
                primary_text = item_display_label(*item);
                // Favorite state is drawn as a vector in the action cell below.
            }
            const int slot = draw->itemID < g_state->visible_slot_numbers.size()
                ? g_state->visible_slot_numbers[draw->itemID] : -1;
            if (slot >= 0 && slot < 9) {
                const auto& label = g_state->settings.slot_labels[static_cast<size_t>(slot)];
                if (!label.empty()) primary_text = label;
                RECT number = draw->rcItem;
                if (shortcut_mode) {
                    number.left += px(4);
                    number.right = number.left + px(20);
                    primary.left = number.right + px(4);
                } else {
                    number.left = number.right - ui_px(kResultActionWidth) * 3 - 30;
                    number.right = draw->rcItem.right - ui_px(kResultActionWidth) * 3 - 4;
                }
                DrawTextW(draw->hDC, std::to_wstring(slot + 1).c_str(), -1, &number,
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            DrawTextW(draw->hDC, primary_text.c_str(), -1, &primary,
                      DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            const auto draw_action = [&](int index, ResultAction action) {
                RECT action_rect = draw->rcItem;
                action_rect.left = action_rect.right - ui_px(kResultActionWidth) * (3 - index);
                action_rect.right = action_rect.left + ui_px(kResultActionWidth);
                action_rect.top += px(3);
                action_rect.bottom -= px(3);
                const bool hovered = g_state->result_hover_row == static_cast<int>(draw->itemID) &&
                    g_state->result_hover_action == action;
                HBRUSH action_brush = CreateSolidBrush(hovered ? colors().button_pressed :
                    (selected ? colors().selected : colors().input));
                FillRect(draw->hDC, &action_rect, action_brush);
                DeleteObject(action_brush);
                const COLORREF icon_color = selected ? colors().selected_text : colors().muted;
                Gdiplus::Graphics graphics(draw->hDC);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.TranslateTransform((action_rect.left+action_rect.right)*.5f,(action_rect.top+action_rect.bottom)*.5f);
                const float scale=ui_px(100)/100.0f;graphics.ScaleTransform(scale,scale);
                Gdiplus::Pen pen(vector_color(icon_color),1.5f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
                if(action==ResultAction::Pin) {
                    graphics.DrawLine(&pen,-8.25f,-8.25f,8.25f,-8.25f);
                    graphics.DrawLine(&pen,0.0f,8.25f,0.0f,-4.0f);
                    graphics.DrawLine(&pen,-7.0f,3.0f,0.0f,-4.0f);graphics.DrawLine(&pen,0.0f,-4.0f,7.0f,3.0f);
                } else if(action==ResultAction::Favorite) {
                    Gdiplus::PointF points[10];
                    for(int n=0;n<10;++n){const float angle=-1.5707963f+n*3.14159265f/5,r=n%2?3.5f:8.25f;points[n]={std::cos(angle)*r,std::sin(angle)*r};}
                    if(item && contains_key(g_state->settings.favorites,item_key(*item))){Gdiplus::SolidBrush fill(vector_color(icon_color));graphics.FillPolygon(&fill,points,10);}
                    else graphics.DrawPolygon(&pen,points,10);
                } else {
                    graphics.DrawLine(&pen,-8.25f,0.0f,8.25f,0.0f);graphics.DrawLine(&pen,0.0f,-8.25f,0.0f,8.25f);
                }
            };
            if (!shortcut_mode) {
                draw_action(0, ResultAction::Pin);
                draw_action(1, ResultAction::Favorite);
                draw_action(2, ResultAction::Shortcut);
            }
            HPEN divider = CreatePen(PS_SOLID, 1, selected ? colors().selected : colors().divider);
            const HGDIOBJ previous_pen = SelectObject(draw->hDC, divider);
            MoveToEx(draw->hDC, draw->rcItem.left, draw->rcItem.bottom - 1, nullptr);
            LineTo(draw->hDC, draw->rcItem.right, draw->rcItem.bottom - 1);
            SelectObject(draw->hDC, previous_pen);
            DeleteObject(divider);
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON) {
            const auto paint_button = [](const DRAWITEMSTRUCT& item) {
                const bool icon = item.CtlID == kCaptureId ||
                    item.CtlID == kSlotId || item.CtlID == kPreferencesId;
                if (!icon) { paint_text_button(item); return; }
                paint_button_surface(item);
                draw_toolbar_icon(item.hDC, item.rcItem, item.CtlID);
            };
            const int width = draw->rcItem.right - draw->rcItem.left;
            const int height = draw->rcItem.bottom - draw->rcItem.top;
            if (width <= 0 || height <= 0) return TRUE;
            HDC buffer = CreateCompatibleDC(draw->hDC);
            HBITMAP bitmap = CreateCompatibleBitmap(draw->hDC, width, height);
            if (buffer && bitmap) {
                const auto previous_bitmap = SelectObject(buffer, bitmap);
                const auto previous_font = SelectObject(buffer, g_state->font);
                DRAWITEMSTRUCT frame = *draw;
                frame.hDC = buffer;
                frame.rcItem = {0, 0, width, height};
                paint_button(frame);
                // Present the complete face at once; hover/ripple repainting
                // must not expose the cleared background between draw calls.
                BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top, width, height,
                    buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, previous_font);
                SelectObject(buffer, previous_bitmap);
            } else {
                paint_button(*draw);
            }
            if (bitmap) DeleteObject(bitmap);
            if (buffer) DeleteDC(buffer);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == kCaptureId) { save_current_snapshot(); return 0; }
        if (LOWORD(wparam) == kDictionaryId) { edit_dictionary(); return 0; }
        if (LOWORD(wparam) == kModeId && HIWORD(wparam) == BN_CLICKED) {
            const bool enabled = SendMessageW(g_state->mode_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_state->settings.use_english_mode = enabled;
            if (enabled) {
                SetFocus(g_state->search);
                if (!ensure_search_english_mode()) {
                    g_state->ime.restore();
                    g_state->ime_session_active = false;
                    g_state->settings.use_english_mode = false;
                    SendMessageW(g_state->mode_checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
                } else {
                    set_status(L"当前输入法已切换为英文模式", true);
                }
            } else {
                g_state->ime.restore();
                g_state->ime_session_active = false;
                set_status(L"已恢复插件使用前的输入法模式", true);
            }
            return 0;
        }
        if (LOWORD(wparam) == kPreferencesId) {
            RECT rect{}; GetWindowRect(hwnd, &rect);
            g_state->settings_visible = !g_state->settings_visible;
            RECT client{}; GetClientRect(hwnd,&client);
            const int target = g_state->settings_visible ? (client.right*96/GetDpiForWindow(hwnd)<440 ? 166 : kSettingsHeight) : 0;
            g_state->expand_from = g_state->settings_extent; g_state->expand_to = target;
            g_state->expand_started = GetTickCount64();
            SetWindowTextW(g_state->preferences, g_state->settings_visible ? L"设置" : L"设置");
            // AE can invalidate the floating native window while child controls
            // are being moved. A timer-driven expand causes visible flashing and
            // transient misalignment in the host, so apply the final layout in
            // one transaction.
            g_state->settings_animation_active = false;
            KillTimer(hwnd, kSettingsAnimationTimerId);
            const bool parent_visible = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
            std::vector<HWND> redraw_children;
            EnumChildWindows(hwnd, [](HWND child, LPARAM state) -> BOOL {
                // DefWindowProc's WM_SETREDRAW(TRUE) adds WS_VISIBLE. Never
                // send it to a child that was hidden before this transaction.
                if (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)
                    reinterpret_cast<std::vector<HWND>*>(state)->push_back(child);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&redraw_children));
            struct RestoreRedraw {
                HWND parent;
                bool parent_visible;
                const std::vector<HWND>& children;
                bool active = true;
                void finish() {
                    if (!active) return;
                    active = false;
                    for (HWND child : children)
                        if (IsWindow(child)) SendMessageW(child, WM_SETREDRAW, TRUE, 0);
                    if (IsWindow(parent)) {
                        // Restoring a visible child also restores WS_VISIBLE;
                        // reapply the responsive visibility before presentation.
                        layout_controls(parent);
                        if (parent_visible) SendMessageW(parent, WM_SETREDRAW, TRUE, 0);
                        RedrawWindow(parent, nullptr, nullptr,
                            RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    }
                    if (g_state) g_state->redraw_transaction = false;
                }
                ~RestoreRedraw() { finish(); }
            } redraw{hwnd, parent_visible, redraw_children};
            g_state->redraw_transaction = true;
            if (parent_visible) SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            for (HWND child : redraw_children) SendMessageW(child, WM_SETREDRAW, FALSE, 0);
            g_state->settings_extent = target;
            populate_results();
            adjust_console_height();
            layout_controls(hwnd);
            redraw.finish();
            return 0;
        }
        if (LOWORD(wparam) == kSearchId && HIWORD(wparam) == EN_CHANGE) {
            populate_results();
            return 0;
        }
        if (LOWORD(wparam) == kFilterId && HIWORD(wparam) == CBN_SELCHANGE) {
            populate_results();
            return 0;
        }
        if ((LOWORD(wparam) == kFilterId || LOWORD(wparam) == kLightThemeId) &&
            HIWORD(wparam) == CBN_DROPDOWN) {
            theme_combo_popup(LOWORD(wparam) == kFilterId ? g_state->filter : g_state->light_theme);
            return 0;
        }
        if (LOWORD(wparam) == kSlotId && HIWORD(wparam) == BN_CLICKED) {
            show_shortcut_menu();
            return 0;
        }
        if (LOWORD(wparam) == kRefreshId && HIWORD(wparam) == BN_CLICKED) {
            refresh_catalog();
            return 0;
        }
        if (LOWORD(wparam) == kHotkeySaveId && HIWORD(wparam) == BN_CLICKED) {
            save_hotkey();
            return 0;
        }
        if (LOWORD(wparam) == kImportId && HIWORD(wparam) == BN_CLICKED) {
            import_settings();
            return 0;
        }
        if (LOWORD(wparam) == kExportId && HIWORD(wparam) == BN_CLICKED) {
            export_settings();
            return 0;
        }
        if (LOWORD(wparam) == kLightThemeId && HIWORD(wparam) == CBN_SELCHANGE) {
            const auto choice = SendMessageW(g_state->light_theme, CB_GETCURSEL, 0, 0);
            if (choice >= 0 && choice <= 2) set_theme_mode(static_cast<xfx::ThemeMode>(choice));
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wparam == kHotkeyId) {
            toggle_console();
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && g_state && xfx::should_close_console(
            true, false, g_state->modal_depth)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        save_and_restore();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,0xB003); KillTimer(hwnd,kSettingsAnimationTimerId);
        KillTimer(hwnd, kDragTimerId);
        for (const auto& entry : g_state->button_hovers) KillTimer(entry.first, kButtonHoverTimerId);
        g_state->button_hovers.clear();
        g_state->drag_pending = false;
        g_state->drag_active = false;
        g_state->result_click_pending = false;
        g_state->result_click_row = -1;
        g_state->result_click_action = ResultAction::None;
        clear_result_data();
        if (g_state->font) DeleteObject(g_state->font);
        if (g_state->slot_number_font) DeleteObject(g_state->slot_number_font);
        if(g_state->status_font)DeleteObject(g_state->status_font);
        if (g_state->background_brush) DeleteObject(g_state->background_brush);
        if (g_state->input_brush) DeleteObject(g_state->input_brush);
        if(g_state->search_brush)DeleteObject(g_state->search_brush);
        g_state->font = nullptr;
        g_state->slot_number_font = nullptr;
        g_state->status_font=nullptr;
        g_state->background_brush = nullptr;
        g_state->input_brush = nullptr;
        g_state->search_brush=nullptr;
        g_state->hotkey_edit = nullptr;
        g_state->status = nullptr;
        g_state->window = nullptr;
        g_state->ime_source_window = nullptr;
        g_state->tooltip = nullptr;
        g_state->initial_background_primed = false;
        g_state->initial_frame_painted = false;
        return 0;

    case WM_PAINT: {
#ifdef XFX_CONSOLE_TEST
        ++g_test_window_paint_count;
#endif
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        FillRect(dc, &paint.rcPaint, g_state->background_brush);
        const int dpi = GetDpiForWindow(hwnd);
        const auto px = [dpi](int value){return MulDiv(value,dpi,96);};
        const RECT search_area = toolbar_layout(hwnd).search_surface;
        xfx::paint_round_rect(dc,search_area,colors().search,static_cast<float>(px(4)),GetFocus()==g_state->search?colors().accent:colors().divider);
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.TranslateTransform(static_cast<float>(px(23)),static_cast<float>(px(28)));graphics.ScaleTransform(dpi/96.0f,dpi/96.0f);
        Gdiplus::Pen pen(vector_color(colors().icon),1.5f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawEllipse(&pen,-7.5f,-7.5f,10.5f,10.5f);graphics.DrawLine(&pen,1.5f,1.5f,7.5f,7.5f);
        EndPaint(hwnd, &paint);
        g_state->initial_frame_painted = true;
        return 0;
    }
    }

    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN) {
        return theme_control_color(message, reinterpret_cast<HDC>(wparam),reinterpret_cast<HWND>(lparam));
    }
    if (message == WM_ERASEBKGND) {
#ifdef XFX_CONSOLE_TEST
        if (g_state->initial_frame_painted) ++g_test_window_erase_count;
#endif
        if (!g_state->initial_background_primed && !g_state->initial_frame_painted) {
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, g_state->background_brush);
            g_state->initial_background_primed = true;
#ifdef XFX_CONSOLE_TEST
            ++g_test_initial_background_fill_count;
            LOGBRUSH brush{};
            if (GetObjectW(g_state->background_brush, sizeof(brush), &brush) == sizeof(brush))
                g_test_initial_background_color = brush.lbColor;
#endif
        }
        return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    } catch (const std::exception& error) {
        set_status(L"操作失败："+utf8_to_wide(error.what()));
        return message == WM_CREATE ? -1 : 0;
    } catch (...) {
        set_status(L"操作失败，请重新打开控制台");
        return message == WM_CREATE ? -1 : 0;
    }
}

void show_console(bool visible = true) {
    if (!g_state) return;
    const HWND host_input = current_host_input_window();
    if (host_input) g_state->ime_source_window = host_input;
    else if (!IsWindow(g_state->window)) g_state->ime_source_window = nullptr;
    if (g_state->window && IsWindow(g_state->window)) {
        if (!visible) return;
        if (!g_state->ime_session_active)
            g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
        ShowWindow(g_state->window, SW_SHOWNORMAL);
        SetForegroundWindow(g_state->window);
        SetFocus(g_state->search);
        ensure_search_english_mode();
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
        return;
    }

    g_state->settings = xfx::SettingsStore::load(settings_path());
    g_state->settings_visible = false;
    g_state->settings_extent = 0;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    static const wchar_t class_name[] = L"XFXNativeConsoleWindow";
    WNDCLASSEXW klass = {sizeof(WNDCLASSEXW)};
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = window_proc;
    klass.lpszClassName = class_name;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    // The parent and every custom child paint its own background. A system
    // class brush here would erase the dark/light surface white between
    // resize and WM_PAINT, especially while settings are being expanded.
    klass.hbrBackground = nullptr;
    RegisterClassExW(&klass);

    g_state->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_state->slot_number_font = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    refresh_host_theme(true);

    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int window_width = 520;
    const int window_height = 126;
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) work_area = monitor.rcWork;
    const int window_x = (std::max)(work_area.left, (std::min)(cursor.x - 30, work_area.right - window_width));
    const int window_y = (std::max)(work_area.top, (std::min)(cursor.y - 20, work_area.bottom - window_height));
    // Child controls and owner-drawn surfaces paint their own backgrounds.
    // Avoid WS_EX_COMPOSITED here: it repaints the whole floating palette for
    // every hover/timer invalidation and produces visible flashes in AE.
    const DWORD window_ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    // The system nonclient border uses its own light colors. The floating
    // palette has a single themed client surface and a rounded window region.
    const DWORD window_style = WS_POPUP;
    g_state->window = CreateWindowExW(window_ex_style, class_name, L"效果控制台",
                                      window_style | WS_CLIPCHILDREN, window_x, window_y, window_width, window_height,
                                      nullptr, nullptr, klass.hInstance, nullptr);
    if (!g_state->window) {
        g_state->ime.restore();
        g_state->ime_session_active = false;
        MessageBoxW(nullptr, L"无法创建插件窗口。", L"效果控制台", MB_OK | MB_ICONERROR);
        return;
    }
    // The window is still hidden. Its DPI is now known for the destination
    // monitor; size it in logical units before the first visible frame.
    const int actual_dpi=GetDpiForWindow(g_state->window);
    const int scaled_width=MulDiv(520,actual_dpi,96),scaled_height=MulDiv(126,actual_dpi,96);
    const int scaled_x=(std::max)(work_area.left,(std::min)(cursor.x-MulDiv(30,actual_dpi,96),work_area.right-scaled_width));
    const int scaled_y=(std::max)(work_area.top,(std::min)(cursor.y-MulDiv(20,actual_dpi,96),work_area.bottom-scaled_height));
    SetWindowPos(g_state->window,nullptr,scaled_x,scaled_y,scaled_width,scaled_height,SWP_NOACTIVATE|SWP_NOZORDER);
    adjust_console_height();
    if (visible) {
        g_state->ime_session_active = g_state->ime.capture_original(g_state->search, g_state->ime_source_window);
        HDC initial_dc = GetDC(g_state->window);
        if (initial_dc) {
            SendMessageW(g_state->window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(initial_dc), 0);
            ReleaseDC(g_state->window, initial_dc);
        }
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
    }
    if (visible) {
        ShowWindow(g_state->window, SW_SHOWNORMAL);
        SetForegroundWindow(g_state->window);
        SetFocus(g_state->search);
        ensure_search_english_mode();
        RedrawWindow(g_state->window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
    }
}

void toggle_console() {
    if (!g_state) return;
    if (g_state->window && IsWindow(g_state->window) && IsWindowVisible(g_state->window)) {
        PostMessageW(g_state->window, WM_CLOSE, 0, 0);
        return;
    }
    show_console();
}

LRESULT CALLBACK hotkey_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return guarded_callback([&]() -> LRESULT {
    if (message == WM_APP + 1 || (message == WM_HOTKEY && wparam == kHotkeyId)) {
        if (!g_state || g_state->modal_depth ||
            (g_state->hotkey_edit && GetFocus() == g_state->hotkey_edit)) return 0;
        DWORD foreground_process = 0;
#ifdef XFX_CONSOLE_TEST
        if (g_test_hotkey_foreground_process) foreground_process = *g_test_hotkey_foreground_process;
        else
#endif
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
        if (foreground_process == GetCurrentProcessId()) toggle_console();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
    });
}


xfx::BridgeCompletion bridge_script_result(const xfx::ScriptReply& reply, const std::wstring& success) {
    std::string message = reply.succeeded ? wide_to_utf8(success) : wide_to_utf8(utf8_to_wide(reply.message.c_str()));
    if (reply.partial) message = wide_to_utf8(L"部分完成（" + std::to_wstring(reply.count) + L" 个图层）：") + message;
    return {reply.succeeded, reply.count, reply.partial ? "partial" : "host_error", message};
}

xfx::BridgeCompletion save_current_snapshot() {
    ModalGuard modal;
    const auto composition = xfx::current_capture_comp_name(g_basic, g_plugin_id);
    if (!composition.reply.succeeded) {
        set_status(L"快照失败：" + utf8_to_wide(composition.reply.message.c_str()));
        return bridge_script_result(composition.reply, L"");
    }
    wchar_t filename[32768]{};
    const auto initial_name = xfx::snapshot_png_filename(composition.name);
    wcscpy_s(filename, initial_name.c_str());
    HWND owner = IsWindowVisible(g_state->window) ? g_state->window : GetForegroundWindow();
    DWORD owner_process = 0;
    GetWindowThreadProcessId(owner, &owner_process);
    if (owner_process != GetCurrentProcessId()) owner = nullptr;
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = filename; dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"保存当前合成快照";
    dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        const auto error = CommDlgExtendedError();
        if (error) {
            const auto message = L"无法打开保存对话框（" + std::to_wstring(error) + L"）";
            set_status(message);
            return {false, 0, "save_dialog_error", wide_to_utf8(message)};
        }
        set_status(L"已取消保存快照");
        return {false, 0, "cancelled", wide_to_utf8(L"已取消保存快照")};
    }
    fs::path target(filename);
    if (CompareStringOrdinal(target.extension().c_str(), -1, L".png", -1, TRUE) != CSTR_EQUAL) {
        target += L".png";
        if (fs::exists(target) && MessageBoxW(owner,
                (L"覆盖已存在的文件？\n" + target.wstring()).c_str(), L"确认替换图片",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            set_status(L"已取消保存快照");
            return {false, 0, "cancelled", wide_to_utf8(L"已取消保存快照")};
        }
    }
    BusyFeedback feedback(L"正在渲染并保存当前帧…");
    const auto rendered = xfx::capture_comp_png(g_basic, g_plugin_id, target, true);
    const auto message = rendered.succeeded ? L"快照已保存：" + target.wstring() :
        L"快照失败：" + utf8_to_wide(rendered.message.c_str());
    set_status(message,rendered.succeeded);
    return bridge_script_result(rendered, message);
}

xfx::BridgeCompletion export_current_frame() {
    ModalGuard modal;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"保存当前帧为 PNG…");
    AppendMenuW(menu, MF_STRING, 2, L"保存当前帧为 JPG…");
    AppendMenuW(menu, MF_STRING, 3, L"复制当前帧图片");
    POINT cursor{}; GetCursorPos(&cursor);
    const auto choice = xfx::track_themed_popup(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x, cursor.y, g_state->window, colors(), g_state->font);
    DestroyMenu(menu);
    if (!choice) return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
    fs::path target;
    bool jpeg = choice == 2;
    if (choice != 3) {
        wchar_t filename[32768] = L"快照.png";
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = g_state->window; dialog.lpstrFile = filename; dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"PNG 透明图片 (*.png)\0*.png\0JPEG 白底图片 (*.jpg)\0*.jpg\0";
        dialog.nFilterIndex = jpeg ? 2 : 1; dialog.lpstrDefExt = jpeg ? L"jpg" : L"png";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&dialog)) return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
        jpeg = dialog.nFilterIndex == 2;
        target = filename; target.replace_extension(jpeg ? L".jpg" : L".png");
        if (target != fs::path(filename) && fs::exists(target) &&
            MessageBoxW(g_state->window, (L"覆盖已存在的文件？\n" + target.wstring()).c_str(),
                L"确认替换图片", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return {false, 0, "cancelled", wide_to_utf8(L"操作已取消")};
        }
    }
    GUID temporary_id{};
    if (FAILED(CoCreateGuid(&temporary_id)))
        return {false, 0, "export_error", wide_to_utf8(L"无法创建临时导出文件")};
    wchar_t temporary_name[40]{};
    StringFromGUID2(temporary_id, temporary_name, 40);
    const auto temporary_directory = fs::temp_directory_path() / (L"XFXExport-" + std::wstring(temporary_name));
    if (!fs::create_directory(temporary_directory))
        return {false, 0, "export_error", wide_to_utf8(L"无法创建临时导出目录")};
    const auto source = temporary_directory / L"frame.png";
    struct ExportCleanup {
        fs::path source, directory;
        ~ExportCleanup() {
            std::error_code ignored;
            fs::remove(source, ignored);
            fs::remove(directory, ignored);
        }
    } cleanup{source, temporary_directory};
    BusyFeedback feedback(L"正在渲染并导出当前帧…");
    const auto rendered = xfx::capture_comp_png(g_basic, g_plugin_id, source, true);
    if (!rendered.succeeded) return bridge_script_result(rendered, L"");
    bool success = false;
    if (choice == 3) success = xfx::copy_capture_to_clipboard(g_state->hotkey_window, source);
    else {
        std::error_code error;
        success = (fs::equivalent(source, target, error) && !error) || xfx::export_capture(source, target, jpeg);
    }
    return {success, success ? 1U : 0U, success ? "" : "export_error", wide_to_utf8(success
        ? (choice == 3 ? L"当前帧图片已复制" : L"当前帧图片已保存") : L"图片导出失败，请检查路径或剪贴板后重试")};
}

xfx::BridgeCompletion dispatch_bridge_request(const xfx::BridgeRequest& request) {
    using Command = xfx::BridgeCommand;
    if (request.command == Command::Capture) return save_current_snapshot();
    if (request.command == Command::Show) {
        show_console();
        const bool opened = IsWindow(g_state->window) != FALSE;
        return {opened, 0, "window_error", wide_to_utf8(opened ? L"搜索控制台已打开" : L"无法打开搜索控制台")};
    }
    if (request.command == Command::Gallery) {
        return {false, 0, "feature_removed", wide_to_utf8(L"快照图库已移除，请使用保存当前合成快照")};
    }
    if (request.command == Command::Preferences) {
        show_console();
        if (g_state->window && !g_state->settings_visible) SendMessageW(g_state->window, WM_COMMAND, kPreferencesId, 0);
        const bool opened = IsWindow(g_state->window) && g_state->settings_visible;
        return {opened, 0, "window_error", wide_to_utf8(opened ? L"设置已打开" : L"无法打开设置")};
    }
    show_console(false);
    if (!g_state->window) return {false, 0, "window_error", wide_to_utf8(L"无法创建插件窗口")};
    if (request.command == Command::Export) return export_current_frame();
    if (request.command == Command::Shortcut) return bridge_script_result(execute_slot(request.slot), L"快捷项已应用");
    if (request.command == Command::About) {
        ModalGuard modal;
        MessageBoxW(g_state->window,
            L"效果控制台\n中文、全拼与首字母搜索\n\n当前为功能对齐测试版，原版完整对照仍在进行。",
            L"关于 效果控制台", MB_OK | MB_ICONINFORMATION);
        return {true, 0, "", wide_to_utf8(L"关于窗口已关闭")};
    }
    return {false, 0, "unknown_command", wide_to_utf8(L"不支持此操作")};
}

bool g_bridge_processing = false;
A_Err bridge_idle_hook(AEGP_GlobalRefcon, AEGP_IdleRefcon, A_long* max_sleep) {
    std::optional<xfx::BridgeRequest> request;
    try {
        if (!g_state || g_bridge_processing) return A_Err_NONE;
        refresh_host_theme();
        ensure_hotkey_window();
        const unsigned int gallery_depth = IsWindow(g_state->gallery_window) ? 1U : 0U;
        if (g_state->modal_depth > gallery_depth) return A_Err_NONE;
        if (max_sleep && *max_sleep > 6) *max_sleep = 6;
        request = xfx::script_bridge().take_request();
        if (!request) return A_Err_NONE;
        struct ProcessingGuard {
            ProcessingGuard() { g_bridge_processing = true; }
            ~ProcessingGuard() { g_bridge_processing = false; }
        } processing;
        auto result = dispatch_bridge_request(*request);
        xfx::script_bridge().complete(request->id, std::move(result));
        if (g_state && g_state->window && !IsWindowVisible(g_state->window) &&
            !g_state->gallery_window && !g_state->modal_depth) PostMessageW(g_state->window, WM_APP + 3, 0, 0);
    } catch (...) {
        if (request) {
            try { xfx::script_bridge().complete(request->id,
                {false, 0, "host_error", wide_to_utf8(L"操作失败，请检查文件权限或重新打开控制台")}); } catch (...) {}
        }
    }
    return A_Err_NONE;
}

A_Err command_hook(AEGP_GlobalRefcon, AEGP_CommandRefcon, AEGP_Command command,
                   AEGP_HookPriority, A_Boolean, A_Boolean* handled) {
    if (command == g_command) {
        ensure_hotkey_window();
        show_console();
        if (handled) *handled = TRUE;
    }
    return A_Err_NONE;
}

A_Err update_menu_hook(AEGP_GlobalRefcon, AEGP_UpdateMenuRefcon, AEGP_WindowType) {
    try {
        AEGP_SuiteHandler suites(g_basic);
        suites.CommandSuite1()->AEGP_EnableCommand(g_command);
    } catch (...) {
    }
    return A_Err_NONE;
}

A_Err death_hook(AEGP_GlobalRefcon, AEGP_DeathRefcon refcon) {
    try { xfx::script_bridge().set_available(false); } catch (...) {}
    auto* state = reinterpret_cast<PluginState*>(refcon);
    if (state) {
        if (state->window && IsWindow(state->window)) {
            save_and_restore();
            DestroyWindow(state->window);
        }
        state->hotkey.unregister();
        if (state->keyboard_hook) UnhookWindowsHookEx(state->keyboard_hook);
        if (state->hotkey_window && IsWindow(state->hotkey_window)) {
            DestroyWindow(state->hotkey_window);
        }
        if (state->gdiplus_token) Gdiplus::GdiplusShutdown(state->gdiplus_token);
        delete state;
    }
    g_state = nullptr;
    return A_Err_NONE;
}

} // namespace

extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;

A_Err EntryPointFunc(SPBasicSuite* pica_basicP, A_long, A_long,
                     AEGP_PluginID aegp_plugin_id, AEGP_GlobalRefcon* global_refconP) {
    g_basic = pica_basicP;
    g_plugin_id = aegp_plugin_id;
    try {
        xfx::script_bridge().set_available(false);
        AEGP_SuiteHandler suites(pica_basicP);
        suites.CommandSuite1()->AEGP_GetUniqueCommand(&g_command);
        const std::string menu_name = wide_to_ansi(L"效果控制台");
        suites.CommandSuite1()->AEGP_InsertMenuCommand(
            g_command, reinterpret_cast<const A_char*>(menu_name.c_str()),
            AEGP_Menu_WINDOW, AEGP_MENU_INSERT_SORTED);

        g_state = new PluginState();
        Gdiplus::GdiplusStartupInput startup;
        Gdiplus::GdiplusStartup(&g_state->gdiplus_token, &startup, nullptr);
        *global_refconP = reinterpret_cast<AEGP_GlobalRefcon>(g_state);

        g_state->settings = xfx::SettingsStore::load(settings_path());
        refresh_host_theme(true);
        suites.RegisterSuite5()->AEGP_RegisterCommandHook(
            aegp_plugin_id, AEGP_HP_BeforeAE, g_command, command_hook,
            reinterpret_cast<AEGP_CommandRefcon>(g_state));
        suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(
            aegp_plugin_id, update_menu_hook, reinterpret_cast<AEGP_UpdateMenuRefcon>(g_state));
        suites.RegisterSuite5()->AEGP_RegisterDeathHook(
            aegp_plugin_id, death_hook, reinterpret_cast<AEGP_DeathRefcon>(g_state));
        const A_Err bridge_registration = suites.RegisterSuite5()->AEGP_RegisterIdleHook(
            aegp_plugin_id, bridge_idle_hook, reinterpret_cast<AEGP_IdleRefcon>(g_state));
        xfx::script_bridge().set_available(bridge_registration == A_Err_NONE);
    } catch (...) {
        delete g_state;
        g_state = nullptr;
        return A_Err_GENERIC;
    }
    return A_Err_NONE;
}
