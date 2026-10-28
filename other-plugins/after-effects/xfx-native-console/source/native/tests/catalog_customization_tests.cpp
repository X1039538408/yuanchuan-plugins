#include "catalog_customization.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static std::vector<xfx::CatalogItem> catalog() {
    xfx::CatalogItem effect;
    effect.display_name = L"Vendor Team Glow";
    effect.english_name = "Vendor Team Glow";
    effect.match_name = "Vendor Team Glow ID";
    effect.translated_name = L"团队辉光";
    effect.category = L"效果";
    effect.source = "ae";
    effect.aliases = {L"官方柔光"};
    xfx::CatalogItem preset;
    preset.display_name = L"原始片头";
    preset.english_name = "Original Intro";
    preset.item_type = L"preset";
    preset.file_path = L"D:\\预设🎬\\旅行🚀.ffx";
    preset.category = L"预设";
    preset.source = "file";
    preset.aliases = {L"原预设别名"};
    return {effect, preset};
}

int main() {
    int failures = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("stable keys use effect matchName and preset path", [&] {
        auto items = catalog();
        require(xfx::catalog_item_key(items[0]) == L"Vendor Team Glow ID", "effect key does not match plugin identity");
        require(xfx::catalog_item_key(items[1]) == items[1].file_path, "preset key lost its path");
        items[0].match_name = "Vendor \xE4\xB8\xAD\xE6\x96\x87 ID";
        require(xfx::catalog_item_key(items[0]) == L"Vendor 中文 ID", "UTF-8 matchName decoded incorrectly");
        items[0].match_name.clear();
        require(xfx::catalog_item_key(items[0]) == items[0].display_name, "missing matchName has no display-name fallback");
    });
    test("multiple aliases merge without duplicates or identifier changes", [&] {
        auto items = catalog();
        const auto original = items;
        xfx::Settings settings;
        settings.custom_aliases[L"Vendor Team Glow ID"] = {L"梦幻发光", L"Dream Glow", L"官方柔光", L"梦幻发光", L""};
        settings.custom_aliases[items[1].file_path] = {L"旅行转场"};
        xfx::apply_catalog_customizations(items, settings);
        xfx::apply_catalog_customizations(items, settings);
        require(items[0].aliases == std::vector<std::wstring>{L"官方柔光", L"梦幻发光", L"Dream Glow"}, "effect alias merge duplicated or omitted values");
        require(items[1].aliases == std::vector<std::wstring>{L"原预设别名", L"旅行转场"}, "preset aliases did not merge");
        for (size_t i = 0; i < items.size(); ++i) {
            require(items[i].match_name == original[i].match_name && items[i].file_path == original[i].file_path, "custom aliases changed executable identity");
            require(items[i].display_name == original[i].display_name && items[i].english_name == original[i].english_name, "custom aliases renamed an original or brand");
            require(items[i].translated_name == original[i].translated_name, "custom aliases changed translation");
        }
    });
    test("effect and preset aliases support Chinese full pinyin initials and English", [&] {
        auto items = catalog();
        xfx::Settings settings;
        settings.custom_aliases[L"Vendor Team Glow ID"] = {L"梦幻发光", L"Dream Glow"};
        settings.custom_aliases[items[1].file_path] = {L"旅行转场"};
        xfx::apply_catalog_customizations(items, settings);
        const xfx::SearchEngine engine(items);
        for (const auto* query : {L"梦幻发光", L"menghuanfaguang", L"mhfg", L"DREAM GLOW"}) {
            const auto results = engine.search(query);
            require(results.size() == 1 && results[0].item.match_name == "Vendor Team Glow ID", "effect alias is not searchable");
        }
        for (const auto* query : {L"旅行转场", L"lvxingzhuanchang", L"lxzc"}) {
            const auto results = engine.search(query);
            require(results.size() == 1 && results[0].item.file_path == items[1].file_path, "preset alias is not searchable");
        }
    });
    test("editing and clearing aliases takes effect on a fresh catalog", [&] {
        xfx::Settings settings;
        settings.custom_aliases[L"Vendor Team Glow ID"] = {L"旧别名"};
        auto items = catalog();
        xfx::apply_catalog_customizations(items, settings);
        require(xfx::SearchEngine(items).search(L"jiubieming").size() == 1, "added alias was not indexed");
        settings.custom_aliases[L"Vendor Team Glow ID"] = {L"新别名"};
        items = catalog();
        xfx::apply_catalog_customizations(items, settings);
        require(xfx::SearchEngine(items).search(L"新别名").size() == 1, "edited alias was not indexed");
        require(xfx::SearchEngine(items).search(L"旧别名").empty(), "old alias survived replacement");
        settings.custom_aliases.clear();
        items = catalog();
        xfx::apply_catalog_customizations(items, settings);
        require(xfx::SearchEngine(items).search(L"新别名").empty(), "cleared alias survived rescan");
        require(xfx::SearchEngine(items).search(L"官方柔光").size() == 1, "clearing user aliases removed bundled aliases");
    });
    test("unknown customization keys do not affect catalog items", [&] {
        auto items = catalog();
        const auto original = items;
        xfx::Settings settings;
        settings.custom_aliases[L"Unknown Vendor ID"] = {L"不应出现"};
        settings.custom_aliases[L"D:\\Missing.ffx"] = {L"不应出现"};
        xfx::apply_catalog_customizations(items, settings);
        require(items[0].aliases == original[0].aliases && items[1].aliases == original[1].aliases, "unknown key modified an item");
        require(xfx::SearchEngine(items).search(L"不应出现").empty(), "unknown aliases leaked into catalog");
    });
    return failures ? 1 : 0;
}
