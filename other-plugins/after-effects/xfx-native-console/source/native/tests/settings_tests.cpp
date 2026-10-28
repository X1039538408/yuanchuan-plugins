#include "settings.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
static void put(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    output << value;
}
static std::string system_ansi(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

int main() {
    const fs::path root = fs::temp_directory_path() / (L"xfx-settings-tests-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root);
    int failures = 0;
    auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("new configuration persists explicit follow mode", [&] {
        const auto path = root / L"follow.ini";
        xfx::SettingsStore::save(path, xfx::Settings{});
        require(bytes(path).find("ThemeMode=follow\r\n") != std::string::npos,
            "default configuration does not persist host following");
    });
    test("three theme choices round trip and explicit mode wins over legacy flags", [&] {
        const auto path = root / L"theme-modes.ini";
        for (const auto mode : {xfx::ThemeMode::Follow,xfx::ThemeMode::Light,xfx::ThemeMode::Dark}) {
            xfx::Settings settings; settings.theme_mode=mode; settings.hotkey=L"Ctrl+K";
            xfx::SettingsStore::save(path,settings);
            const auto loaded=xfx::SettingsStore::load(path);
            require(loaded.theme_mode==mode && loaded.hotkey==L"Ctrl+K","theme mode or unrelated data changed");
        }
        put(path,"ThemeMode=follow\r\nLightTheme=1\r\n");
        require(xfx::SettingsStore::load(path).theme_mode==xfx::ThemeMode::Follow,"legacy flag overrode modern choice");
        put(path,"LightTheme=1\r\nThemeMode=dark\r\n");
        require(xfx::SettingsStore::load(path).theme_mode==xfx::ThemeMode::Dark,"modern dark choice lost");
        put(path,"LightTheme=1\r\n");
        require(xfx::SettingsStore::load(path).theme_mode==xfx::ThemeMode::Light,"legacy light selection lost");
        put(path,"ThemeMode=invalid\r\n");
        bool rejected=false;try{xfx::SettingsStore::load(path);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid theme was silently accepted");
    });
    test("Unicode capture directory survives import and export", [&] {
        const auto path = root / L"capture-location.ini";
        put(path, u8"CaptureDirectory=E:\\中文快照🎬\\测试=目录\r\nLightTheme=1\r\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        const auto raw = bytes(path);
        require(raw.find(u8"CaptureDirectory=E:\\中文快照🎬\\测试=目录\r\n") != std::string::npos,
                "capture directory was discarded or lost Unicode");
        require(raw.substr(0, 3) == "\xEF\xBB\xBF", "capture directory export must use UTF-8 BOM");
        require(xfx::SettingsStore::load(path).light_theme, "directory export changed theme");
    });
    test("full resolution screenshots are always enabled", [&] {
        const auto path = root / L"full-resolution.ini";
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(root / L"absent-full-resolution.ini"));
        require(bytes(path).find("FullResolutionScreenshots=1\r\n") != std::string::npos,
                "full resolution default must be on");
        for (const std::string value : {"1", "0"}) {
            put(path, "FullResolutionScreenshots=" + value + "\nFavorite=E:Keep\n");
            xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
            require(bytes(path).find("FullResolutionScreenshots=1\r\n") != std::string::npos,
                    "full resolution option was not normalized to on");
            require(xfx::SettingsStore::load(path).full_resolution_screenshots,
                    "full resolution setting was not enabled");
            require(xfx::SettingsStore::load(path).favorites == std::vector<std::wstring>{L"E:Keep"},
                    "screenshot choice changed favorites");
        }
    });
    test("old and missing configuration keep the existing default capture directory", [&] {
        const auto path = root / L"old-capture-location.ini";
        put(path, "AutoClose=0\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        require(bytes(path).find("CaptureDirectory=\r\n") != std::string::npos,
                "old configuration must retain the default capture directory");
        require(!xfx::SettingsStore::load(path).auto_close, "old option was changed");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(root / L"absent-capture-location.ini"));
        require(bytes(path).find("CaptureDirectory=\r\n") != std::string::npos,
                "missing configuration must retain the default capture directory");
    });
    test("clearing and repeated capture directory records use the last value", [&] {
        const auto path = root / L"clear-capture-location.ini";
        put(path, "CaptureDirectory=E:\\Old\nCaptureDirectory=\\\\server\\share\\New\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        require(bytes(path).find("CaptureDirectory=\\\\server\\share\\New\r\n") != std::string::npos,
                "last absolute UNC directory must win");
        put(path, "CaptureDirectory=E:\\Old\nCaptureDirectory=\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        require(bytes(path).find("CaptureDirectory=\r\n") != std::string::npos,
                "cleared location must return to the default");
        require(bytes(path).find("E:\\Old") == std::string::npos, "old directory returned after clearing");
    });
    test("capture location rejects relative and control-containing imports", [&] {
        const auto path = root / L"invalid-capture-location.ini";
        for (const std::string& value : std::vector<std::string>{"relative\\Capture", "C:Capture", "\\Capture", "C:\\Bad\tName",
                                                               std::string("C:\\Bad\0Name", 11)}) {
            put(path, "CaptureDirectory=" + value + "\n");
            bool threw = false;
            try { (void)xfx::SettingsStore::load(path); } catch (const std::exception&) { threw = true; }
            require(threw, "unsafe capture location import must throw");
        }
    });
    test("an unreadable existing configuration cannot become empty defaults", [&] {
        const auto path = root / L"locked-import.ini";
        put(path, "Favorite=E:Old\nCaptureDirectory=C:\\Old\n");
        const auto original = bytes(path);
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "cannot lock the import fixture");
        bool threw = false;
        try { (void)xfx::SettingsStore::load(path); } catch (const std::exception&) { threw = true; }
        CloseHandle(locked);
        require(threw, "unreadable existing configuration must report an error, not defaults");
        require(bytes(path) == original, "failed read changed existing configuration");
    });
    test("explicit import requires a file while startup keeps missing defaults", [&] {
        const auto path = root / L"missing-required-import.ini";
        require(xfx::SettingsStore::load(path).favorites.empty(), "missing startup configuration must remain optional");
        bool threw = false;
        try { (void)xfx::SettingsStore::load(path, true); } catch (const std::exception&) { threw = true; }
        require(threw, "a missing explicit import must not reset settings to defaults");
    });
    test("UTF-8 BOM preserves Chinese and emoji preset paths", [&] {
        const fs::path path = root / L"中文配置🎨.ini";
        xfx::Settings settings;
        settings.hotkey = L"Ctrl+Alt+F5";
        settings.favorites = {L"P:E:\\动画素材🎬\\发光=渐变.ffx", L"E:ADBE Gaussian Blur 2",
                              L"P:E:\\素材\\漢字_ひらがな_한글_العربية_е́_😀.ffx"};
        settings.recent = {L"P:D:\\预设\\光晕✨.ffx"};
        settings.slots[8] = L"P:E:\\转场🚀.ffx";
        xfx::SettingsStore::save(path, settings);
        const auto raw = bytes(path);
        require(raw.size() >= 3 && raw.substr(0, 3) == "\xEF\xBB\xBF", "saved file must start with UTF-8 BOM");
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.favorites == settings.favorites, "favorites lost Unicode");
        require(loaded.recent == settings.recent && loaded.slots == settings.slots, "recent/slots lost Unicode");
        require(loaded.hotkey == L"Ctrl+Alt+F5", "custom hotkey lost");
    });
    test("light and dark theme survive UTF-8 export and import", [&] {
        const auto path = root / L"主题导出✨.ini";
        xfx::Settings settings;
        settings.favorites = {L"P:D:\\中文🎬\\柔光.ffx"};
        for (const bool light : {true, false}) {
            settings.light_theme = light;
            xfx::SettingsStore::save(path, settings);
            const auto loaded = xfx::SettingsStore::load(path);
            require(loaded.light_theme == light, "theme did not survive export/import");
            require(loaded.favorites == settings.favorites, "theme export changed Unicode preset paths");
            require(bytes(path).substr(0, 3) == "\xEF\xBB\xBF", "theme export must remain UTF-8 BOM");
        }
    });
    test("old and missing configuration retain the default dark theme", [&] {
        require(!xfx::Settings{}.light_theme, "new settings must default to dark");
        require(!xfx::SettingsStore::load(root / L"missing-theme.ini").light_theme, "missing file must default to dark");
        const auto path = root / L"old-theme.ini";
        put(path, u8"Favorite=P:D:\\旧预设.ffx\r\nAutoClose=0\r\n");
        const auto loaded = xfx::SettingsStore::load(path);
        require(!loaded.light_theme && !loaded.auto_close, "old config changed default theme or other options");
    });
    test("import reads explicit light and dark theme options", [&] {
        const auto path = root / L"import-theme.ini";
        put(path, "LightTheme=1\r\n");
        require(xfx::SettingsStore::load(path).light_theme, "light theme import ignored");
        put(path, "LightTheme=0\n");
        require(!xfx::SettingsStore::load(path).light_theme, "dark theme import ignored");
        put(path, "LightTheme=1\nLightTheme=0\n");
        require(!xfx::SettingsStore::load(path).light_theme, "last theme record must win like other options");
    });
    test("failed theme save retains the previous theme and file bytes", [&] {
        const auto path = root / L"readonly-theme.ini";
        xfx::Settings previous;
        previous.light_theme = true;
        previous.favorites = {L"P:D:\\旧收藏✨.ffx"};
        xfx::SettingsStore::save(path, previous);
        const auto original = bytes(path);
        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "cannot set theme fixture read-only");
        xfx::Settings replacement = previous;
        replacement.light_theme = false;
        bool threw = false;
        try { xfx::SettingsStore::save(path, replacement); } catch (const std::exception&) { threw = true; }
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        require(threw, "theme save must report a denied replacement");
        require(bytes(path) == original, "failed theme save changed old bytes");
        const auto retained = xfx::SettingsStore::load(path);
        require(retained.light_theme && retained.favorites == previous.favorites, "failed save changed persisted theme or presets");
    });
    test("reads UTF-8 without BOM and CRLF", [&] {
        const auto path = root / L"utf8.ini";
        put(path, u8"UseEnglishMode=1\r\nFixedWindow=1\r\nAutoClose=0\r\nFavorite=P:D:\\预设🌟.ffx\r\nSlot1=P:D:\\中文.ffx\r\n");
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.favorites == std::vector<std::wstring>{L"P:D:\\预设🌟.ffx"}, "UTF-8 input decoded incorrectly");
        require(loaded.slots[0] == L"P:D:\\中文.ffx", "CRLF left an extra carriage return");
        require(loaded.use_english_mode && loaded.fixed_window && !loaded.auto_close, "boolean options not loaded");
    });
    test("legacy hidden records are ignored while Unicode preset overrides persist", [&] {
        const auto path = root / L"overrides.ini";
        xfx::Settings settings;
        settings.overrides[L"E:ADBE Gaussian Blur 2"] = L"E:\\预设🎬\\柔化=更新.ffx";
        settings.overrides[L"P:D:\\自定义✨.ffx"] = L"D:\\替换预设\\动画🚀.ffx";
        xfx::SettingsStore::save(path, settings);
        const auto original = bytes(path);
        put(path, u8"Hide=E:ADBE Fill\r\nHide=P:D:\\已隐藏🙈.ffx\r\n" + original);
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.overrides == settings.overrides, "override keys or paths lost");
        xfx::SettingsStore::save(path, loaded);
        const auto raw = bytes(path);
        require(raw.find("Hide=") == std::string::npos, "legacy Hide records were retained");
        require(raw.find("Override=E:ADBE Gaussian Blur 2\t") != std::string::npos, "Override delimiter missing");
    });
    test("custom alias records survive loading and saving", [&] {
        const auto path = root / L"alias-records.ini";
        put(path, u8"Alias=ADBE Glo2\t辉光✨\r\nAlias=ADBE Glo2\t灯光扩散\r\nAlias=D:\\预设🎬.ffx\t梦幻转场\r\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        const auto raw = bytes(path);
        require(raw.find(u8"Alias=ADBE Glo2\t辉光✨\r\n") != std::string::npos, "first Unicode alias was discarded");
        require(raw.find(u8"Alias=ADBE Glo2\t灯光扩散\r\n") != std::string::npos, "second alias was discarded");
        require(raw.find(u8"Alias=D:\\预设🎬.ffx\t梦幻转场\r\n") != std::string::npos, "preset alias was discarded");
    });
    test("Unicode slot label records survive loading and saving", [&] {
        const auto path = root / L"slot-label-records.ini";
        put(path, u8"Slot1=ADBE Glo2\r\nSlotLabel1=辉光快捷键✨\r\nSlotLabel9=旅行转场🚀\r\n");
        xfx::SettingsStore::save(path, xfx::SettingsStore::load(path));
        const auto raw = bytes(path);
        require(raw.find(u8"SlotLabel1=辉光快捷键✨\r\n") != std::string::npos, "first label was discarded");
        require(raw.find(u8"SlotLabel9=旅行转场🚀\r\n") != std::string::npos, "last label was discarded");
    });
    test("custom aliases and all slot labels round trip without changing identities", [&] {
        const auto path = root / L"customizations.ini";
        xfx::Settings settings;
        settings.custom_aliases[L"Vendor 中文效果✨"] = {L"梦幻发光", L"Dream Glow=Soft|Warm\tExtra", L"别名😀"};
        settings.custom_aliases[L"D:\\预设\\旅行🚀.ffx"] = {L"旅行转场", L"Travel Intro"};
        settings.custom_aliases[L"Empty List"] = {};
        settings.custom_aliases[L"Empty Alias"] = {L""};
        for (size_t i = 0; i < settings.slot_labels.size(); ++i) {
            settings.slots[i] = L"Vendor ID " + std::to_wstring(i + 1);
            settings.slot_labels[i] = L"快捷槽🎬=" + std::to_wstring(i + 1);
        }
        xfx::SettingsStore::save(path, settings);
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.custom_aliases == settings.custom_aliases, "custom aliases did not round trip");
        require(loaded.slot_labels == settings.slot_labels, "slot labels did not round trip");
        require(loaded.slots == settings.slots, "slot labels changed shortcut IDs");
    });
    test("pinned items round trip and keep their order", [&] {
        const auto path = root / L"pinned.ini";
        xfx::Settings settings;
        settings.pinned = {L"ADBE Glo2", L"ADBE Gaussian Blur 2"};
        xfx::SettingsStore::save(path, settings);
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.pinned == settings.pinned, "pinned items did not round trip");
    });
    test("adding editing and clearing aliases and labels persists", [&] {
        const auto path = root / L"edit-customizations.ini";
        xfx::Settings settings;
        settings.custom_aliases[L"ADBE Glo2"] = {L"辉光"};
        settings.slot_labels[0] = L"旧标签";
        xfx::SettingsStore::save(path, settings);
        settings = xfx::SettingsStore::load(path);
        settings.custom_aliases[L"ADBE Glo2"] = {L"梦幻辉光", L"柔光"};
        settings.slot_labels[0] = L"新标签✨";
        xfx::SettingsStore::save(path, settings);
        settings = xfx::SettingsStore::load(path);
        require(settings.custom_aliases.at(L"ADBE Glo2") == std::vector<std::wstring>{L"梦幻辉光", L"柔光"}, "edited aliases were not saved");
        require(settings.slot_labels[0] == L"新标签✨", "edited label was not saved");
        settings.custom_aliases.erase(L"ADBE Glo2");
        settings.slot_labels[0].clear();
        xfx::SettingsStore::save(path, settings);
        settings = xfx::SettingsStore::load(path);
        require(settings.custom_aliases.empty() && settings.slot_labels[0].empty(), "cleared customization returned after reload");
    });
    test("newline injection is rejected before replacing old settings", [&] {
        const auto path = root / L"newline.ini";
        put(path, "Favorite=Old\n");
        xfx::Settings settings;
        settings.favorites = {L"Old\nHotkey=Alt+F4"};
        bool threw = false;
        try { xfx::SettingsStore::save(path, settings); } catch (const std::exception&) { threw = true; }
        require(threw, "newline-containing field must throw");
        require(bytes(path) == "Favorite=Old\n", "rejected field changed old settings");
    });
    test("invalid alias and slot-label fields preserve old configuration", [&] {
        const auto path = root / L"invalid-customizations.ini";
        std::vector<xfx::Settings> invalid(6);
        invalid[0].custom_aliases[L"Vendor ID"] = {L"Bad\nFavorite=Injected"};
        invalid[1].custom_aliases[L"Vendor\rID"] = {L"Alias"};
        invalid[2].slot_labels[0] = L"Label\nHotkey=Alt+F4";
        invalid[3].custom_aliases[L"Vendor\tID"] = {L"Alias"};
        invalid[4].custom_aliases[L"Vendor ID"] = {std::wstring(L"Bad\0Alias", 9)};
        invalid[5].slot_labels[8] = std::wstring(1, static_cast<wchar_t>(0xD800));
        for (const auto& settings : invalid) {
            put(path, "Favorite=Old\n");
            bool threw = false;
            try { xfx::SettingsStore::save(path, settings); } catch (const std::exception&) { threw = true; }
            require(threw, "invalid customization must throw");
            require(bytes(path) == "Favorite=Old\n", "invalid customization changed old settings");
        }
    });
    test("reads legacy system ACP configuration", [&] {
        const auto path = root / L"legacy.ini";
        put(path, system_ansi(L"UseEnglishInputMethod=1\r\nFavorite=P:D:\\预设\\中文.ffx\r\nHotkey=Alt+Space\r\n"));
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.favorites == std::vector<std::wstring>{L"P:D:\\预设\\中文.ffx"}, "legacy ACP path lost");
        require(loaded.use_english_mode && loaded.hotkey == L"Alt+Space", "legacy options not preserved");
    });
    test("failed replacement preserves read-only old settings", [&] {
        const auto path = root / L"readonly.ini";
        put(path, "Favorite=E:Old\n");
        const auto original = bytes(path);
        require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "cannot set fixture read-only");
        xfx::Settings settings;
        settings.favorites = {L"E:New"};
        bool threw = false;
        try { xfx::SettingsStore::save(path, settings); } catch (const std::exception&) { threw = true; }
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        require(threw, "save must throw on denied replacement");
        require(bytes(path) == original, "failed save damaged old configuration");
    });
    test("directory target failure preserves existing directory", [&] {
        const auto path = root / L"directory.ini";
        fs::create_directories(path);
        put(path / L"keep.txt", "keep");
        bool threw = false;
        try { xfx::SettingsStore::save(path, {}); } catch (const std::exception&) { threw = true; }
        require(threw, "saving over a directory must throw");
        require(bytes(path / L"keep.txt") == "keep", "save changed target directory contents");
    });
    test("missing and old configuration keep default hotkey", [&] {
        const auto missing = xfx::SettingsStore::load(root / L"missing.ini");
        require(missing.hotkey == L"Ctrl+Space" && missing.auto_close, "default behavior changed");
        put(root / L"old.ini", "Favorite=E:ADBE Fill\n");
        require(xfx::SettingsStore::load(root / L"old.ini").hotkey == L"Ctrl+Space", "old config without hotkey broke default");
    });
    test("successful replacement updates existing configuration", [&] {
        const auto path = root / L"replace.ini";
        xfx::Settings first;
        first.favorites = {L"E:Old"};
        xfx::SettingsStore::save(path, first);
        xfx::Settings second;
        second.favorites = {L"P:D:\\新收藏🎨.ffx"};
        xfx::SettingsStore::save(path, second);
        const auto loaded = xfx::SettingsStore::load(path);
        require(loaded.favorites == second.favorites, "existing file was not atomically replaced");
    });
    test("export replaces an existing target after overwrite confirmation", [&] {
        const auto source = root / L"export-source.ini";
        const auto target = root / L"导出目标.ini";
        xfx::Settings source_settings;
        source_settings.light_theme = true;
        source_settings.favorites = {L"P:D:\\中文预设🎬.ffx"};
        xfx::SettingsStore::save(source, source_settings);
        put(target, "Favorite=E:Old\n");
        xfx::SettingsStore::export_file(source, target);
        require(bytes(target) == bytes(source), "export did not replace the existing target");
        xfx::SettingsStore::export_file(source, source);
        require(bytes(source) == bytes(target), "exporting to the source changed the file");
    });
    test("invalid Unicode save preserves old configuration", [&] {
        const auto path = root / L"invalid-unicode.ini";
        put(path, "Favorite=E:Old\n");
        xfx::Settings settings;
        settings.favorites = {std::wstring(1, static_cast<wchar_t>(0xD800))};
        bool threw = false;
        try { xfx::SettingsStore::save(path, settings); } catch (const std::exception&) { threw = true; }
        require(threw, "invalid UTF-16 must throw");
        require(bytes(path) == "Favorite=E:Old\n", "encoding failure changed old settings");
    });
    test("success and failed saves leave no temporary files", [&] {
        for (const auto& entry : fs::directory_iterator(root))
            require(entry.path().filename().wstring().find(L".tmp-") == std::wstring::npos, "temporary save file leaked");
    });
    fs::remove_all(root);
    return failures ? 1 : 0;
}
