#include "catalog_customization.h"
#include <windows.h>
#include <algorithm>

namespace xfx {

std::wstring catalog_item_key(const CatalogItem& item) {
    if (item.item_type == L"preset") return item.file_path;
    if (item.match_name.empty()) return catalog_display_name(item);
    // AE identifiers normally use UTF-8. Match the plug-in's ACP fallback for
    // older third-party effect identifiers so existing configuration keys stay valid.
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(code_page, flags, item.match_name.c_str(), -1, nullptr, 0);
    if (!count) {
        code_page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(code_page, flags, item.match_name.c_str(), -1, nullptr, 0);
    }
    if (!count) return {};
    std::wstring key(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(code_page, flags, item.match_name.c_str(), -1, key.data(), count)) return {};
    key.resize(static_cast<size_t>(count - 1));
    return key;
}

void apply_catalog_customizations(std::vector<CatalogItem>& items, const Settings& settings) {
    for (auto& item : items) {
        const auto entry = settings.custom_aliases.find(catalog_item_key(item));
        if (entry == settings.custom_aliases.end()) continue;
        for (const auto& alias : entry->second) {
            if (!alias.empty() && std::find(item.aliases.begin(), item.aliases.end(), alias) == item.aliases.end()) {
                item.aliases.push_back(alias);
            }
        }
    }
}

} // namespace xfx
