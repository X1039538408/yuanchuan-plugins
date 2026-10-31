#include "search_core.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>
#include <unordered_map>

#include "data/pinyin_data.h"
#include "data/effect_translations.h"
#include "data/display_label_cleanups.h"

namespace xfx {
namespace {

bool is_cjk(char32_t ch) {
    return ch == 0x3007 || (ch >= 0x3400 && ch <= 0x9FFF) ||
           (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0x20000 && ch <= 0x323AF);
}

void append_wide(std::wstring& target, char32_t codepoint) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (codepoint > 0xFFFF) {
            const auto offset = codepoint - 0x10000;
            target.push_back(static_cast<wchar_t>(0xD800 + (offset >> 10)));
            target.push_back(static_cast<wchar_t>(0xDC00 + (offset & 0x3FF)));
            return;
        }
    }
    target.push_back(static_cast<wchar_t>(codepoint));
}

std::u32string codepoints(const std::wstring& value) {
    std::u32string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        char32_t ch = static_cast<std::uint32_t>(value[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < value.size()) {
                const auto low = static_cast<std::uint32_t>(value[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    ch = 0x10000 + ((ch - 0xD800) << 10) + low - 0xDC00;
                    ++i;
                }
            }
        }
        result.push_back(ch);
    }
    return result;
}

std::wstring decode_utf8(const std::string& value) {
    std::wstring result;
    for (size_t i = 0; i < value.size();) {
        const auto lead = static_cast<unsigned char>(value[i]);
        char32_t ch = 0;
        unsigned length = 0;
        char32_t minimum = 0;
        if (lead < 0x80) { ch = lead; length = 1; }
        else if (lead >= 0xC2 && lead <= 0xDF) { ch = lead & 0x1F; length = 2; minimum = 0x80; }
        else if (lead >= 0xE0 && lead <= 0xEF) { ch = lead & 0x0F; length = 3; minimum = 0x800; }
        else if (lead >= 0xF0 && lead <= 0xF4) { ch = lead & 0x07; length = 4; minimum = 0x10000; }
        bool valid = length != 0 && i + length <= value.size();
        for (unsigned j = 1; valid && j < length; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xC0) != 0x80) valid = false;
            else ch = (ch << 6) | (next & 0x3F);
        }
        valid = valid && ch >= minimum && ch <= 0x10FFFF && !(ch >= 0xD800 && ch <= 0xDFFF);
        append_wide(result, valid ? ch : 0xFFFD);
        i += valid ? length : 1;
    }
    return result;
}

char32_t folded_latin(char32_t ch) {
    if (ch >= 0xFF01 && ch <= 0xFF5E) ch -= 0xFEE0;
    if (ch >= U'A' && ch <= U'Z') return ch + U'a' - U'A';
    switch (ch) {
    case U'Ā': case U'Á': case U'Ǎ': case U'À': case U'ā': case U'á': case U'ǎ': case U'à': return U'a';
    case U'Ē': case U'É': case U'Ě': case U'È': case U'Ê':
    case U'ē': case U'é': case U'ě': case U'è': case U'ê': return U'e';
    case U'Ī': case U'Í': case U'Ǐ': case U'Ì': case U'ī': case U'í': case U'ǐ': case U'ì': return U'i';
    case U'Ō': case U'Ó': case U'Ǒ': case U'Ò': case U'ō': case U'ó': case U'ǒ': case U'ò': return U'o';
    case U'Ū': case U'Ú': case U'Ǔ': case U'Ù': case U'ū': case U'ú': case U'ǔ': case U'ù': return U'u';
    case U'Ü': case U'Ǖ': case U'Ǘ': case U'Ǚ': case U'Ǜ':
    case U'ü': case U'ǖ': case U'ǘ': case U'ǚ': case U'ǜ': return U'v';
    case U'Ń': case U'Ň': case U'Ǹ': case U'ń': case U'ň': case U'ǹ': return U'n';
    case U'Ḿ': case U'ḿ': return U'm';
    default: return ch;
    }
}

bool is_ascii_alnum(char32_t ch) {
    return (ch >= U'a' && ch <= U'z') || (ch >= U'0' && ch <= U'9');
}

std::wstring normalize_wide(const std::wstring& value) {
    const auto chars = codepoints(value);
    std::wstring result;
    result.reserve(chars.size());
    for (size_t i = 0; i < chars.size(); ++i) {
        char32_t ch = folded_latin(chars[i]);
        if (ch == U'u' && i + 1 < chars.size() && (chars[i + 1] == U':' || chars[i + 1] == 0xFF1A)) {
            ch = U'v';
            ++i;
        } else if (ch == 0x0308 && !result.empty() && result.back() == L'u') {
            result.back() = L'v';
            continue;
        }
        if (is_cjk(ch) || is_ascii_alnum(ch)) append_wide(result, ch);
    }
    return result;
}

std::wstring normalize_utf8(const std::string& value) {
    return normalize_wide(decode_utf8(value));
}

const search_data::CharacterPinyin* character_pinyin(char32_t ch) {
    const auto* begin = std::begin(search_data::characters);
    const auto* end = std::end(search_data::characters);
    const auto* found = std::lower_bound(begin, end, static_cast<std::uint32_t>(ch),
        [](const auto& entry, std::uint32_t key) { return entry.codepoint < key; });
    return found != end && found->codepoint == ch ? found : nullptr;
}

const search_data::PhrasePinyin* phrase_pinyin(std::u32string_view phrase) {
    const auto* begin = std::begin(search_data::phrases);
    const auto* end = std::end(search_data::phrases);
    const auto* found = std::lower_bound(begin, end, phrase,
        [](const auto& entry, std::u32string_view key) { return std::u32string_view(search_data::phrase_string(entry.text_offset)) < key; });
    return found != end && std::u32string_view(search_data::phrase_string(found->text_offset)) == phrase ? found : nullptr;
}

struct Pinyin {
    std::string full;
    std::string initials;
    std::string chinese_full;
    std::string chinese_initials;
    bool has_chinese = false;
};

Pinyin transliterate(const std::wstring& value) {
    const auto chars = codepoints(value);
    Pinyin result;
    for (size_t i = 0; i < chars.size();) {
        const search_data::PhrasePinyin* phrase = nullptr;
        size_t phrase_length = std::min<size_t>(search_data::max_phrase_length, chars.size() - i);
        if (is_cjk(chars[i])) {
            for (; phrase_length > 1; --phrase_length) {
                phrase = phrase_pinyin(std::u32string_view(chars).substr(i, phrase_length));
                if (phrase) break;
            }
        }
        if (phrase) {
            const auto* full = search_data::pinyin_string(phrase->full_offset);
            const auto* initials = search_data::pinyin_string(phrase->initials_offset);
            result.full += full;
            result.initials += initials;
            result.chinese_full += full;
            result.chinese_initials += initials;
            result.has_chinese = true;
            i += phrase_length;
            continue;
        }
        if (const auto* entry = character_pinyin(chars[i])) {
            const auto* full = search_data::pinyin_string(entry->full_offset);
            result.full += full;
            result.initials += full[0];
            result.chinese_full += full;
            result.chinese_initials += full[0];
            result.has_chinese = true;
        } else if (is_cjk(chars[i])) {
            // Unlisted rare ideographs keep their original searchable name; do not invent a reading.
            return {};
        } else {
            const auto folded = folded_latin(chars[i]);
            if (is_ascii_alnum(folded)) {
                result.full += static_cast<char>(folded);
                result.initials += static_cast<char>(folded);
            }
        }
        ++i;
    }
    return result;
}

void add_unique(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

void append_pinyin(CatalogItem& item, const std::wstring& value) {
    const auto pinyin = transliterate(value);
    if (!pinyin.has_chinese) return;
    add_unique(item.pinyin_full_variants, pinyin.full);
    add_unique(item.pinyin_full_variants, pinyin.chinese_full);
    add_unique(item.pinyin_initial_variants, pinyin.initials);
    add_unique(item.pinyin_initial_variants, pinyin.chinese_initials);
}

std::string canonical_effect_key(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    size_t begin = 0, end = value.size();
    const auto space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    while (begin < end && space(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && space(static_cast<unsigned char>(value[end - 1]))) --end;
    bool pending_space = false;
    for (size_t i = begin; i < end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (space(ch)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result.push_back(' ');
        pending_space = false;
        result.push_back(static_cast<char>(folded_latin(ch)));
    }
    if (result.rfind("bcc+ ", 0) == 0) result.erase(4, 1);
    return result;
}

struct TranslationIndex {
    std::unordered_map<std::string, const search_data::EffectTranslation*> match;
    std::unordered_map<std::string, const search_data::EffectTranslation*> english;
    TranslationIndex() {
        for (const auto& row : search_data::translations) {
            // Preserve the old first-row-wins semantics and matchName before
            // English fallback, without renormalizing the entire expanded
            // dictionary for every item on initial scan/rebuild.
            if (*row.match_name) match.emplace(canonical_effect_key(row.match_name), &row);
            if (*row.english_name) english.emplace(canonical_effect_key(row.english_name), &row);
        }
    }
};

void apply_known_translation(CatalogItem& item) {
    if (item.item_type != L"effect") return;
    static const TranslationIndex index;
    const auto find = [](const auto& rows, const std::string& key) -> const search_data::EffectTranslation* {
        if (key.empty()) return nullptr;
        const auto found = rows.find(canonical_effect_key(key));
        return found == rows.end() ? nullptr : found->second;
    };
    const auto* found = find(index.match, item.match_name);
    if (!found) found = find(index.english, item.english_name);
    if (!found) return;
    if (item.translated_name.empty()) item.translated_name = found->chinese_name;
    const std::wstring_view aliases(found->aliases);
    for (size_t start = 0; start < aliases.size();) {
        const auto separator = aliases.find(L'|', start);
        const auto length = (separator == std::wstring_view::npos ? aliases.size() : separator) - start;
        const std::wstring alias(aliases.substr(start, length));
        if (!alias.empty() && std::find(item.aliases.begin(), item.aliases.end(), alias) == item.aliases.end()) {
            item.aliases.push_back(alias);
        }
        if (separator == std::wstring_view::npos) break;
        start = separator + 1;
    }
}

int field_score(const std::wstring& value, const std::wstring& query, int exact, int prefix, int contains) {
    if (value.empty()) return std::numeric_limits<int>::max();
    if (value == query) return exact;
    if (value.starts_with(query)) return prefix;
    if (value.find(query) != std::wstring::npos) return contains;
    return std::numeric_limits<int>::max();
}

SearchEngine::IndexedField indexed_wide_field(const std::wstring& value,
                                                int exact, int prefix, int contains) {
    return {normalize_wide(value), exact, prefix, contains};
}

SearchEngine::IndexedField indexed_utf8_field(const std::string& value,
                                               int exact, int prefix, int contains) {
    return {normalize_utf8(value), exact, prefix, contains};
}

void append_indexed_field(std::vector<SearchEngine::IndexedField>& fields,
                          SearchEngine::IndexedField field) {
    fields.push_back(std::move(field));
}

SearchEngine::IndexedItem index_item(CatalogItem item) {
    enrich_catalog_item(item);
    SearchEngine::IndexedItem indexed;
    indexed.item = std::move(item);
    const auto add_wide = [&](const std::wstring& value, int exact, int prefix, int contains) {
        append_indexed_field(indexed.fields, indexed_wide_field(value, exact, prefix, contains));
    };
    const auto add_utf8 = [&](const std::string& value, int exact, int prefix, int contains) {
        append_indexed_field(indexed.fields, indexed_utf8_field(value, exact, prefix, contains));
    };
    add_wide(indexed.item.display_name, 0, 10, 20);
    add_wide(indexed.item.translated_name, 5, 15, 25);
    for (const auto& alias : indexed.item.aliases) add_wide(alias, 25, 28, 29);
    add_utf8(indexed.item.pinyin_full, 30, 35, 40);
    for (const auto& pinyin : indexed.item.pinyin_full_variants) add_utf8(pinyin, 30, 35, 40);
    add_utf8(indexed.item.pinyin_initials, 50, 55, 60);
    for (const auto& initials : indexed.item.pinyin_initial_variants) add_utf8(initials, 50, 55, 60);
    add_utf8(indexed.item.english_name, 70, 75, 80);
    add_utf8(indexed.item.match_name, 90, 95, 95);
    return indexed;
}

int score_item(const SearchEngine::IndexedItem& item, const std::wstring& query) {
    if (query.empty()) return 1000;
    int best = std::numeric_limits<int>::max();
    for (const auto& field : item.fields) {
        best = std::min(best, field_score(field.value, query,
                                          field.exact, field.prefix, field.contains));
    }
    return best;
}

} // namespace

void enrich_catalog_item(CatalogItem& item) {
    apply_known_translation(item);
    // Repair only the exact previously shipped redundant labels. This also
    // covers a legacy dictionary shadowing the updated defaults, without
    // rewriting personal files or guessing at arbitrary user/brand names.
    if (item.item_type == L"effect") for (const auto& rule : kDisplayLabelCleanups) {
        const bool identity = *rule.match_name ?
            canonical_effect_key(item.match_name) == canonical_effect_key(rule.match_name) :
            (canonical_effect_key(item.english_name) == canonical_effect_key(rule.english_name) ||
             normalize_wide(item.display_name) == normalize_wide(decode_utf8(rule.english_name)));
        if (!identity || item.translated_name != rule.previous) continue;
        if (std::find(item.aliases.begin(),item.aliases.end(),rule.previous)==item.aliases.end())
            item.aliases.emplace_back(rule.previous);
        item.translated_name=rule.replacement;
        break;
    }
    if (item.display_name.empty()) item.display_name = catalog_display_name(item);
    item.pinyin_full_variants.clear();
    item.pinyin_initial_variants.clear();
    append_pinyin(item, item.translated_name);
    append_pinyin(item, item.display_name);
    for (const auto& alias : item.aliases) append_pinyin(item, alias);
    if (item.pinyin_full.empty() && !item.pinyin_full_variants.empty()) item.pinyin_full = item.pinyin_full_variants.front();
    if (item.pinyin_initials.empty() && !item.pinyin_initial_variants.empty()) item.pinyin_initials = item.pinyin_initial_variants.front();
}

SearchEngine::SearchEngine(std::vector<CatalogItem> items) {
    items_.reserve(items.size());
    for (auto& item : items) items_.push_back(index_item(std::move(item)));
}

std::wstring catalog_display_name(const CatalogItem& item) {
    if (!item.display_name.empty()) return item.display_name;
    if (!item.english_name.empty()) return decode_utf8(item.english_name);
    if (!item.match_name.empty()) return decode_utf8(item.match_name);
    return L"Unnamed Item";
}

std::wstring bilingual_effect_label(const std::wstring& english_name, const std::wstring& translated_name, bool explicit_translation) {
    if (english_name.empty()) return translated_name;
    // A valid host-localized CJK name is already the display label. Keep
    // translated synonyms indexed rather than appending a second Chinese name.
    if (!explicit_translation && std::any_of(english_name.begin(),english_name.end(),[](wchar_t ch){return is_cjk(static_cast<char32_t>(ch));}))
        return english_name;
    if (translated_name.empty() || normalize_wide(english_name) == normalize_wide(translated_name)) return english_name;
    return english_name + L"\uFF08" + translated_name + L"\uFF09";
}

std::vector<SearchResult> SearchEngine::search(const std::wstring& query) const {
    const auto normalized_query = normalize_wide(query);
    std::vector<SearchResult> results;
    for (const auto& indexed : items_) {
        const int score = score_item(indexed, normalized_query);
        if (score != std::numeric_limits<int>::max()) results.push_back({indexed.item, score});
    }
    std::sort(results.begin(), results.end(), [](const SearchResult& left, const SearchResult& right) {
        if (left.score != right.score) return left.score < right.score;
        return left.item.display_name < right.item.display_name;
    });
    return results;
}

} // namespace xfx
