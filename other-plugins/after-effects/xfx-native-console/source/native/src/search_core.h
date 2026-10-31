#pragma once

#include <string>
#include <vector>

namespace xfx {

struct CatalogItem {
    std::wstring display_name;
    std::string pinyin_full;
    std::string pinyin_initials;
    std::string english_name;
    std::string match_name;
    std::wstring category;
    std::string source;
    std::wstring item_type = L"effect";
    std::wstring file_path;
    std::wstring translated_name;
    std::vector<std::wstring> aliases;
    // Derived offline from all Chinese names/aliases; callers need not supply these.
    std::vector<std::string> pinyin_full_variants;
    std::vector<std::string> pinyin_initial_variants;
    // Preserve host classification separately from the generic UI type filter.
    std::wstring host_category;
    // Explicit legacy/user dictionary translations remain visible on localized hosts.
    bool translated_name_custom = false;
};

struct SearchResult {
    CatalogItem item;
    int score = 0;
};

class SearchEngine {
public:
    explicit SearchEngine(std::vector<CatalogItem> items);
    std::vector<SearchResult> search(const std::wstring& query) const;

    struct IndexedField {
        std::wstring value;
        int exact = 0;
        int prefix = 0;
        int contains = 0;
    };
    struct IndexedItem {
        CatalogItem item;
        std::vector<IndexedField> fields;
    };

private:
    std::vector<IndexedItem> items_;
};

std::wstring catalog_display_name(const CatalogItem& item);
// Preserve original identifiers and user translations; fill known translations and pinyin.
// Safe to call repeatedly. SearchEngine also calls this when accepting a catalog.
void enrich_catalog_item(CatalogItem& item);
std::wstring bilingual_effect_label(const std::wstring& english_name,
                                    const std::wstring& translated_name, bool explicit_translation = false);

} // namespace xfx
