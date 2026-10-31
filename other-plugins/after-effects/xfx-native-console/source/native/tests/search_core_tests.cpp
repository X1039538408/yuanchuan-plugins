#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../src/search_core.h"
#include "../src/effect_catalog.h"
#include "../src/data/display_label_cleanups.h"
#include "../src/settings.h"
#include "../src/hotkey.h"
#include "../src/data/effect_translations.h"

using xfx::CatalogItem;
using xfx::SearchEngine;

static void localization_require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::wstring localization_utf8(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    localization_require(count > 0, "translation source contains invalid UTF-8");
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

struct LocalizationFixture {
    const char* match_name;
    const char* english_name;
    const wchar_t* chinese_name;
    const wchar_t* query;
    const wchar_t* full;
    const wchar_t* initials;
};

// The expected names/identities come from the independent PiPL and official-document
// audit. Removing a stable identity must break this even on a Chinese AE host.
static void real_cc_identifiers_translate_localized_host_names_and_keep_legacy_ids() {
    const LocalizationFixture fixtures[] = {
        {"CS BlockLoad", "CC Block Load", L"CC 块加载", L"块加载", L"kuaijiazai", L"kjz"},
        {"CS Color Neutralizer", "CC Color Neutralizer", L"CC 颜色中和剂", L"颜色中和剂", L"yansezhongheji", L"yszhj"},
        {"CS Composite", "CC Composite", L"CC合成", L"合成", L"hecheng", L"hc"},
        {"CS CrossBlur", "CC Cross Blur", L"CC交叉模糊", L"交叉模糊", L"jiaochamohu", L"jcmh"},
        {"CS HexTile", "CC HexTile", L"CC 六边形平铺", L"六边形平铺", L"liubianxingpingpu", L"lbxpp"},
        {"CS Kernel", "CC Kernel", L"CC 内核", L"内核", L"neihe", L"nh"},
        {"CS LineSweep", "CC Line Sweep", L"CC 线条扫描", L"线条扫描", L"xiantiaosaomiao", L"xtsm"},
        {"CSRainfall", "CC Rainfall", L"CC 降雨", L"降雨", L"jiangyu", L"jy"},
        {"CSSnowfall", "CC Snowfall", L"CC 降雪", L"降雪", L"jiangxue", L"jx"},
        {"CS Threads", "CC Threads", L"CC线条", L"线条", L"xiantiao", L"xt"},
        {"CS Vignette", "CC Vignette", L"CC 暗角", L"暗角", L"anjiao", L"aj"},
    };
    for (const auto& fixture : fixtures) {
        for (const char* identity : {fixture.match_name, fixture.english_name}) {
            CatalogItem item{L"宿主中文标签", "", "", "Localized host label", identity, L"效果", "ae"};
            xfx::enrich_catalog_item(item);
            localization_require(item.translated_name == fixture.chinese_name,
                                 std::string("stable CC identity has no expected translation: ") + identity);
            localization_require(item.display_name == L"宿主中文标签" && item.english_name == "Localized host label" &&
                                 item.match_name == identity, "localization rewrote a host name or effect identity");
            SearchEngine engine({item});
            for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
                const auto found = engine.search(query);
                localization_require(found.size() == 1 && found.front().item.match_name == identity,
                                     std::string("localized CC identity is not searchable: ") + identity);
            }
        }
    }
    // Host labels may coincide for legacy and current Composite. Chinese search
    // must preserve both effect identities even in that case.
    SearchEngine composites({
        {L"旧合成宿主标签", "", "", "CC Composite", "CC Composite", L"效果", "ae"},
        {L"新合成宿主标签", "", "", "CC Composite", "CS Composite", L"效果", "ae"},
    });
    const auto found = composites.search(L"合成");
    localization_require(found.size() == 2, "legacy and current Composite were merged");
    localization_require(found[0].item.match_name != found[1].item.match_name,
                         "Composite lost its distinct effect identities");
}

static void current_cc_additions_support_chinese_full_pinyin_and_initials() {
    const LocalizationFixture fixtures[] = {
        {"CC Bubbles", "CC Bubbles", L"CC 气泡", L"气泡", L"qipao", L"qp"},
        {"CC Griddler", "CC Griddler", L"CC 网格切片", L"网格切片", L"wanggeqiepian", L"wgqp"},
        {"CC Lens", "CC Lens", L"CC 镜头扭曲", L"镜头扭曲", L"jingtouniuqu", L"jtnq"},
        {"CC Light Wipe", "CC Light Wipe", L"CC 光擦除", L"光擦除", L"guangcachu", L"gcc"},
        {"CC Overbrights", "CC Overbrights", L"CC 超亮检测", L"超亮检测", L"chaoliangjiance", L"cljc"},
        {"CC Radial ScaleWipe", "CC Radial ScaleWipe", L"CC 径向缩放擦除", L"径向缩放擦除", L"jingxiangsuofangcachu", L"jxsfcc"},
        {"CC Ripple Pulse", "CC Ripple Pulse", L"CC 波纹脉冲", L"波纹脉冲", L"bowenmaichong", L"bwmc"},
        {"CC Simple Wire Removal", "CC Simple Wire Removal", L"CC 简单丝线移除", L"简单丝线移除", L"jiandansixianyichu", L"jdsxyc"},
        {"CC Split 2", "CC Split 2", L"CC 分裂 2", L"分裂", L"fenlie", L"fl"},
        {"CC Tiler", "CC Tiler", L"CC 平铺", L"平铺", L"pingpu", L"pp"},
    };
    for (const auto& fixture : fixtures) {
        CatalogItem item{L"宿主中文标签", "", "", "Localized host label", fixture.match_name, L"效果", "ae"};
        SearchEngine engine({item});
        for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
            const auto found = engine.search(query);
            localization_require(found.size() == 1, std::string("new CC query did not match: ") + fixture.match_name);
            localization_require(found.front().item.translated_name == fixture.chinese_name &&
                                 found.front().item.match_name == fixture.match_name,
                                 std::string("new CC translation changed effect identity: ") + fixture.match_name);
        }
    }
}

// Exact IDs come from the real AE 25.4 app.effects export recorded in
// docs/localization-coverage.md, not from English-name spelling guesses.
static void confirmed_keying_and_expression_ids_translate_chinese_host_labels() {
    const LocalizationFixture fixtures[] = {
        {"ADBE KeyCleaner", "Key Cleaner", L"抠像清理器", L"抠像清理器", L"kouxiangqingliqi", L"kxqlq"},
        {"ADBE Spill2", "Advanced Spill Suppressor", L"高级溢出抑制器", L"高级溢出抑制器", L"gaojiyichuyizhiqi", L"gjycyzq"},
        {"ADBE Checkbox Control", "Checkbox Control", L"复选框控制", L"复选框控制", L"fuxuankuangkongzhi", L"fxkkz"},
        {"ADBE Slider Control", "Slider Control", L"滑块控制", L"滑块控制", L"huakuaikongzhi", L"hkkz"},
        {"ADBE Layer Control", "Layer Control", L"图层控制", L"图层控制", L"tucengkongzhi", L"tckz"},
        {"ADBE Color Control", "Color Control", L"颜色控制", L"颜色控制", L"yansekongzhi", L"yskz"},
        {"ADBE Point3D Control", "3D Point Control", L"3D 点控制", L"三维点控制", L"sanweidiankongzhi", L"swdkz"},
        {"ADBE Point Control", "Point Control", L"点控制", L"点控制", L"diankongzhi", L"dkz"},
        {"ADBE Angle Control", "Angle Control", L"角度控制", L"角度控制", L"jiaodukongzhi", L"jdkz"},
        {"ADBE Dropdown Control", "Dropdown Menu Control", L"下拉菜单控件", L"下拉菜单控件", L"xialacaidankongjian", L"xlcdkj"},
    };
    for (const auto& fixture : fixtures) {
        // AE supplies its localized label in the field historically named
        // english_name. Neither label below can match an English fallback row.
        CatalogItem item{L"已本地化的宿主标签", "", "", "已本地化的宿主标签",
                         fixture.match_name, L"效果", "ae"};
        xfx::enrich_catalog_item(item);
        localization_require(item.translated_name == fixture.chinese_name,
                             std::string("confirmed stable identity lacks its translation: ") + fixture.match_name);
        localization_require(item.display_name == L"已本地化的宿主标签" &&
                             item.english_name == "已本地化的宿主标签" && item.match_name == fixture.match_name &&
                             item.item_type == L"effect" && item.source == "ae" && item.file_path.empty(),
                             "stable-ID translation rewrote the host label or application identity");
        SearchEngine engine({item});
        for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
            const auto found = engine.search(query);
            localization_require(found.size() == 1 && found.front().item.translated_name == fixture.chinese_name &&
                                 found.front().item.match_name == fixture.match_name &&
                                 found.front().item.item_type == L"effect" && found.front().item.file_path.empty(),
                                 std::string("confirmed stable identity is not searchable: ") + fixture.match_name);
        }
    }
}

static void expression_controls_keep_english_fallback_rows_and_host_identities() {
    const LocalizationFixture fixtures[] = {
        {"", "Checkbox Control", L"复选框控制", L"复选框控制", L"fuxuankuangkongzhi", L"fxkkz"},
        {"", "Slider Control", L"滑块控制", L"滑块控制", L"huakuaikongzhi", L"hkkz"},
        {"", "Layer Control", L"图层控制", L"图层控制", L"tucengkongzhi", L"tckz"},
        {"", "Color Control", L"颜色控制", L"颜色控制", L"yansekongzhi", L"yskz"},
        {"", "3D Point Control", L"3D 点控制", L"三维点控制", L"sanweidiankongzhi", L"swdkz"},
        {"", "Point Control", L"点控制", L"点控制", L"diankongzhi", L"dkz"},
        {"", "Angle Control", L"角度控制", L"角度控制", L"jiaodukongzhi", L"jdkz"},
        {"", "Dropdown Menu Control", L"下拉菜单控件", L"下拉菜单控件", L"xialacaidankongjian", L"xlcdkj"},
    };
    for (const auto& fixture : fixtures) {
        // Opaque identities model an ID received from AE; they are not dictionary keys.
        CatalogItem item{localization_utf8(fixture.english_name), "", "", fixture.english_name,
                         "Opaque host control identity", L"表达式控件", "ae"};
        SearchEngine engine({item});
        for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
            const auto found = engine.search(query);
            localization_require(found.size() == 1 && found.front().item.translated_name == fixture.chinese_name,
                                 std::string("expression control fallback is missing: ") + fixture.english_name);
            localization_require(found.front().item.match_name == "Opaque host control identity",
                                 "expression control fallback replaced the host effect identity");
        }
        bool english_only = false;
        for (const auto& row : xfx::search_data::translations) {
            if (std::string(row.english_name) == fixture.english_name && !*row.match_name) {
                english_only = true;
            }
        }
        localization_require(english_only, "expression control English fallback row is missing");
    }
}

static void openexr_product_names_are_preserved_with_chinese_search_aliases() {
    for (const auto& fixture : std::vector<LocalizationFixture>{
             {"EXtractoR", "EXtractoR", L"多通道图像提取", L"提取器", L"tiquqi", L"tqq"},
             {"IDentifier", "IDentifier", L"对象编号遮罩提取", L"标识符", L"biaoshifu", L"bsf"}}) {
        CatalogItem item{localization_utf8(fixture.english_name), "", "", fixture.english_name,
                         fixture.match_name, L"3D 通道", "ae"};
        SearchEngine engine({item});
        for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
            const auto found = engine.search(query);
            localization_require(found.size() == 1 && found.front().item.translated_name == fixture.chinese_name &&
                                 found.front().item.english_name == fixture.english_name &&
                                 found.front().item.match_name == fixture.match_name,
                                 std::string("product name or Chinese search alias is wrong: ") + fixture.english_name);
        }
    }
}

static void obsolete_and_pseudo_effects_get_descriptive_chinese_search_names() {
    const auto fixtures = std::vector<LocalizationFixture>{
        {"CC PS Classic", "CC PS Classic", L"CC 粒子系统（旧版）", L"粒子系统", L"lizi xitong", L"lzx"},
        {"CC Rain", "CC Rain", L"CC 雨（旧版）", L"雨", L"yu", L"y"},
        {"CC Time Blend", "CC Time Blend", L"CC 时间混合（旧版）", L"时间混合", L"shijian hunhe", L"sjhh"},
        {"ISL MochaShapeImporter", "mocha shape", L"mocha 形状导入（旧版）", L"形状导入", L"xingzhuang daoru", L"xzdr"},
        {"Pseudo/ADBE Counter Controls", "Counter Controls", L"计数器控件", L"计数器", L"jishuqi", L"jsq"},
    };
    for (const auto& fixture : fixtures) {
        CatalogItem item{localization_utf8(fixture.english_name), "", "", fixture.english_name,
                         fixture.match_name, L"效果", "ae"};
        SearchEngine engine({item});
        for (const auto* query : {fixture.query, fixture.full, fixture.initials}) {
            const auto found = engine.search(query);
            localization_require(found.size() == 1 &&
                                 found.front().item.translated_name == fixture.chinese_name &&
                                 found.front().item.match_name == fixture.match_name,
                                 std::string("descriptive Chinese translation is not searchable: ") +
                                     fixture.match_name);
        }
    }
}

static void translation_tsv_and_embedded_rows_agree_without_ambiguous_english_fallbacks() {
    const auto source = std::filesystem::path(localization_utf8(__FILE__)).parent_path().parent_path().parent_path() /
                        "installer" / "XFXNativeConsole.aliases.tsv";
    std::ifstream input(source, std::ios::binary);
    localization_require(input.good(), "cannot open the installed translation source");
    std::map<std::string, std::string> by_match, by_english;
    std::string line;
    size_t index = 0;
    for (; std::getline(input, line);) {
        if (index == 0 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::stringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t')) fields.push_back(field);
        fields.resize((std::max)(size_t{6}, fields.size()));
        localization_require(fields.size() == 6, "TSV row contains extra columns");
        localization_require(index < std::size(xfx::search_data::translations), "embedded translations omitted TSV rows");
        const auto& builtin = xfx::search_data::translations[index++];
        localization_require(fields[0] == builtin.match_name && fields[1] == builtin.english_name &&
                             localization_utf8(fields[2]) == builtin.chinese_name &&
                             localization_utf8(fields[5]) == builtin.aliases,
                             "installed TSV and embedded translations disagree: " + fields[1]);
        const auto lower = [](std::string value) {
            for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            return value;
        };
        if (!fields[0].empty()) localization_require(by_match.emplace(lower(fields[0]), fields[1]).second,
                                                   "duplicate translation matchName: " + fields[0]);
        if (!fields[1].empty()) {
            const auto meaning = fields[2] + "\t" + fields[5];
            const auto [existing, added] = by_english.emplace(lower(fields[1]), meaning);
            localization_require(added || existing->second == meaning,
                                 "same English name has conflicting fallback translations: " + fields[1]);
        }
    }
    localization_require(index == std::size(xfx::search_data::translations), "embedded translations contain stale extra rows");
}

static std::vector<CatalogItem> sample_catalog() {
    return {
        {L"模糊", "mohu", "mh", "Blur", "ADBE Blur", L"效果", "ae"},
        {L"高斯模糊", "gaosimohu", "gsmh", "Gaussian Blur", "ADBE Gaussian Blur 2", L"效果", "ae"},
        {L"高斯噪波", "gaosizao bo", "gszb", "Fractal Noise", "ADBE Fractal Noise", L"效果", "ae"},
    };
}

static void search_matches_chinese() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"高斯模糊");
    assert(!results.empty());
    assert(results.front().item.match_name == "ADBE Gaussian Blur 2");
}

static void search_matches_full_pinyin() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"gaosimohu");
    assert(!results.empty());
    assert(results.front().item.display_name == L"高斯模糊");
}

static void search_matches_initials() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"gsmh");
    assert(!results.empty());
    assert(results.front().item.display_name == L"高斯模糊");
}

static void search_matches_english_case_insensitively() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"GAUSSIAN BLUR");
    assert(!results.empty());
    assert(results.front().item.display_name == L"高斯模糊");
}

static void chinese_search_excludes_unrelated_items() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"高斯");
    assert(results.size() == 2);
    assert(std::any_of(results.begin(), results.end(), [](const xfx::SearchResult& result) {
        return result.item.display_name == L"高斯模糊";
    }));
}

static void search_ignores_separators_in_pinyin() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"gao-si mo.hu");
    assert(!results.empty());
    assert(results.front().item.display_name == L"高斯模糊");
}

static void english_search_works_when_display_name_is_missing() {
    SearchEngine engine({
        {L"", "", "", "Gaussian Blur", "ADBE Gaussian Blur 2", L"效果", "ae"},
    });
    const auto results = engine.search(L"gaussian");
    assert(results.size() == 1);
    assert(results.front().item.english_name == "Gaussian Blur");
    assert(catalog_display_name(results.front().item) == L"Gaussian Blur");
}

static void translated_name_is_searchable() {
    CatalogItem item{L"CC Ball Action", "", "", "CC Ball Action",
                     "CC Ball Action", L"效果", "ae"};
    item.translated_name = L"CC球动作";
    SearchEngine engine({item});
    const auto results = engine.search(L"球动作");
    assert(results.size() == 1);
}

static void aliases_are_searchable() {
    CatalogItem item{L"CC Ball Action", "", "", "CC Ball Action",
                     "CC Ball Action", L"效果", "ae"};
    item.aliases = {L"球动作", L"ballaction"};
    SearchEngine engine({item});
    assert(engine.search(L"ballaction").size() == 1);
    assert(engine.search(L"球动作").size() == 1);
}

static void display_cleanup_rules_are_exact_and_search_preserving() {
    assert(std::size(xfx::kDisplayLabelCleanups)==82);
    for(const auto& rule:xfx::kDisplayLabelCleanups) {
        CatalogItem item{localization_utf8(*rule.english_name?rule.english_name:rule.match_name),"","",
            *rule.english_name?rule.english_name:rule.match_name,rule.match_name,L"效果","ae"};
        item.translated_name=rule.previous;
        const auto id=item.match_name, english=item.english_name;
        xfx::enrich_catalog_item(item);
        assert(item.translated_name==rule.replacement && item.match_name==id && item.english_name==english);
        assert(std::find(item.aliases.begin(),item.aliases.end(),rule.previous)!=item.aliases.end());
        const SearchEngine engine({item});assert(engine.search(rule.previous).size()==1);
        auto custom=item;custom.translated_name=L"团队自定义译名";xfx::enrich_catalog_item(custom);
        assert(custom.translated_name==L"团队自定义译名");
    }
}

static void effect_label_uses_single_line_bilingual_format() {
    CatalogItem deep{L"Deep Glow 2", "", "", "Deep Glow 2", "PEDG2", L"效果", "ae"};
    xfx::enrich_catalog_item(deep);
    assert(deep.translated_name == L"深度辉光");
    assert(xfx::bilingual_effect_label(deep.display_name,deep.translated_name) == L"Deep Glow 2（深度辉光）");
    assert(xfx::bilingual_effect_label(L"定向模糊",L"方向模糊") == L"定向模糊");
    assert(xfx::bilingual_effect_label(L"投影",L"投影") == L"投影");
    assert(xfx::bilingual_effect_label(L"CC Ball Action", L"CC球动作") ==
           L"CC Ball Action（CC球动作）");
}

static void exact_match_ranks_first() {
    SearchEngine engine(sample_catalog());
    const auto results = engine.search(L"模糊");
    assert(!results.empty());
    assert(results.front().item.display_name == L"模糊");
}

// Removing automatic transliteration would make these user-facing searches fail.
static void translated_effect_generates_pinyin_without_manual_fields() {
    CatalogItem item{L"CC Light Sweep", "", "", "CC Light Sweep",
                     "CC Light Sweep", L"效果", "ae"};
    item.translated_name = L"CC扫光";
    SearchEngine engine({item});
    assert(engine.search(L"saoguang").size() == 1);
    assert(engine.search(L"sg").size() == 1);
    assert(engine.search(L"ccsg").size() == 1);
}

static void chinese_preset_generates_phrase_aware_full_pinyin_and_initials() {
    CatalogItem item{L"旅行日记_转场", "", "", "", "", L"预设", "file"};
    item.item_type = L"preset";
    SearchEngine engine({item});
    assert(engine.search(L"lvxingrijizhuanchang").size() == 1);
    assert(engine.search(L"lxrjzc").size() == 1);
    assert(engine.search(L"lv-xing ri.ji zhuan_chang").size() == 1);
}

static void custom_chinese_alias_generates_pinyin() {
    CatalogItem item{L"My Custom Effect", "", "", "My Custom Effect",
                     "Vendor Custom", L"效果", "ae"};
    item.aliases = {L"焦点移到右侧"};
    SearchEngine engine({item});
    assert(engine.search(L"jiaodianyidaoyouce").size() == 1);
    assert(engine.search(L"jdydyc").size() == 1);
    assert(engine.search(L"焦点").size() == 1);
}

static void pinyin_accepts_umlaut_v_and_tone_marks() {
    CatalogItem item{L"绿色边缘", "", "", "", "", L"预设", "file"};
    item.item_type = L"preset";
    SearchEngine engine({item});
    assert(engine.search(L"lvse").size() == 1);
    assert(engine.search(L"lü se").size() == 1);
    assert(engine.search(L"lu:se").size() == 1);
    assert(engine.search(L"lǜsè").size() == 1);
    assert(engine.search(L"lsby").size() == 1);
}

static void english_fallback_decodes_utf8_without_corrupting_names() {
    CatalogItem item{L"", "", "", "Caf\xC3\xA9 Blur", "Vendor Cafe", L"效果", "ae"};
    assert(xfx::catalog_display_name(item) == L"Café Blur");
    SearchEngine engine({item});
    assert(engine.search(L"café").size() == 1);
}

static void built_in_translation_is_applied_without_rewriting_identifiers() {
    CatalogItem item{L"Turbulent Displace", "", "", "Turbulent Displace",
                     "ADBE Turbulent Displace", L"效果", "ae"};
    SearchEngine engine({item});
    const auto results = engine.search(L"tuanliuzhihuan");
    assert(results.size() == 1);
    assert(results.front().item.translated_name == L"湍流置换");
    assert(results.front().item.english_name == "Turbulent Displace");
    assert(results.front().item.match_name == "ADBE Turbulent Displace");
}

static void unknown_effect_keeps_original_name_and_custom_translation() {
    CatalogItem unknown{L"Unlisted Vendor Thing", "", "", "Unlisted Vendor Thing",
                        "Vendor Unique ID", L"效果", "ae"};
    SearchEngine engine({unknown});
    const auto results = engine.search(L"unlisted");
    assert(results.size() == 1);
    assert(results.front().item.display_name == L"Unlisted Vendor Thing");
    assert(results.front().item.translated_name.empty());

    unknown.translated_name = L"团队自定义中文";
    SearchEngine translated({unknown});
    assert(translated.search(L"tuanduizidingyizhongwen").size() == 1);
}

static void third_party_effects_keep_brands_and_gain_chinese_pinyin_search() {
    CatalogItem motion{L"S_BlurMoCurves", "", "", "S_BlurMoCurves",
                       "Vendor Motion ID", L"效果", "ae"};
    CatalogItem glow{L"S_UltraGlow", "", "", "S_UltraGlow",
                     "Vendor Glow ID", L"效果", "ae"};
    SearchEngine engine({motion, glow});
    const auto results = engine.search(L"yundongquxianmohu");
    assert(results.size() == 1);
    assert(results.front().item.translated_name == L"Sapphire 运动曲线模糊");
    assert(results.front().item.match_name == "Vendor Motion ID");
    assert(engine.search(L"ydqxmh").size() == 1);
    assert(engine.search(L"chaojifaguang").size() == 1);
}

static void sapphire_warp_effects_translate_with_padded_or_case_changed_host_names() {
    const std::vector<std::pair<const char*, const wchar_t*>> fixtures = {
        {"S_WarpBubble", L"Sapphire 气泡扭曲"},
        {"S_WarpBubble2", L"Sapphire 气泡扭曲 2"},
        {"S_WarpCornerPin", L"Sapphire 角点扭曲"},
        {"S_WarpDrops", L"Sapphire 液滴扭曲"},
        {"S_WarpFishEye", L"Sapphire 鱼眼扭曲"},
        {"S_WarpMagnify", L"Sapphire 放大扭曲"},
        {"S_WarpPerspective", L"Sapphire 透视扭曲"},
    };
    for (const auto& fixture : fixtures) {
        CatalogItem item{localization_utf8(fixture.first), "", "", "  " +
                         std::string(fixture.first) + "  ", "Vendor stable ID", L"效果", "ae"};
        SearchEngine engine({item});
        const auto found = engine.search(L"气泡扭曲");
        if (std::string(fixture.first) != "S_WarpBubble" &&
            std::string(fixture.first) != "S_WarpBubble2") {
            // The remaining effects have distinct Chinese labels; search by
            // the translated text to ensure the generated row is indexed.
            const auto translated = engine.search(fixture.second);
            localization_require(translated.size() == 1, "Sapphire Warp translation is not searchable");
        } else {
            localization_require(found.size() == 1, "Sapphire Bubble translation is not searchable");
        }
        localization_require(engine.search(L"qipao").size() ==
                                 (std::string(fixture.first).find("Bubble") != std::string::npos ? 1u : 0u),
                             "Sapphire Warp pinyin unexpectedly matched the wrong effect");
        const auto all = engine.search(L"sapphire");
        localization_require(all.size() == 1 && all.front().item.translated_name == fixture.second,
                             "Sapphire Warp label was not applied");
    }
    CatalogItem padded{L"s_warpbubble2", "", "", " s_warpbubble2 ", "Vendor stable ID 2", L"效果", "ae"};
    SearchEngine padded_engine({padded});
    localization_require(padded_engine.search(L"气泡扭曲").size() == 1,
                         "case/space tolerant translation lookup failed");
}

static void external_wipes_from_user_screenshot_translate_and_keep_host_ids() {
    const LocalizationFixture fixtures[] = {
        {"fixture-sapphire-bubble", "S_WipeBubble", L"Sapphire 气泡擦除", L"气泡擦除", L"qipaocachu", L"qpcc"},
        {"fixture-bcc-depth", "BCC+Depth Wipe ML", L"BCC+ 深度擦除 ML", L"深度擦除", L"shenducachu", L"sdcc"},
        {"fixture-bcc-linear", "BCC+Linear Wipe", L"BCC+ 线性擦除", L"线性擦除", L"xianxingcachu", L"xxcc"},
        {"fixture-bcc-radial", "BCC+Radial Wipe", L"BCC+ 径向擦除", L"径向擦除", L"jingxiangcachu", L"jxcc"},
        {"fixture-bcc-rect", "BCC+Rectangular Wipe", L"BCC+ 矩形擦除", L"矩形擦除", L"juxingcachu", L"jxcc"},
        {"fixture-bcc-smoke", "BCC+Smoke Wipe", L"BCC+ 烟雾擦除", L"烟雾擦除", L"yanwucachu", L"ywcc"},
        {"fixture-bcc-texture", "BCC+Texture Wipe", L"BCC+ 纹理擦除", L"纹理擦除", L"wenlicachu", L"wlcc"},
        {"fixture-bcc-glow", "BCC+Film Glow", L"BCC+ 胶片发光", L"胶片发光", L"jiaopianfaguang", L"jpfg"},
    };
    for (const auto& fixture : fixtures) {
        std::vector<std::string> names{fixture.english_name};
        if (names.front().rfind("BCC+", 0) == 0) {
            names.push_back("BCC+ " + names.front().substr(4));
            names.push_back("  bcc+\t" + names.front().substr(4) + "  ");
        }
        for (const auto& name : names) {
            CatalogItem item{localization_utf8(name), "", "", name, fixture.match_name, L"效果", "ae"};
            SearchEngine engine({item});
            const auto all = engine.search(L"");
            localization_require(all.size() == 1 && all[0].item.translated_name == fixture.chinese_name,
                                 "missing screenshot effect translation: " + name);
            localization_require(all[0].item.match_name == fixture.match_name &&
                                 all[0].item.english_name == name && all[0].item.display_name == item.display_name,
                                 "translation changed a host label or application identifier");
            for (const auto* query : {fixture.query, fixture.full, fixture.initials})
                localization_require(engine.search(query).size() == 1, "Chinese/pinyin search missed: " + name);
        }
    }
    CatalogItem custom{L"BCC+Rectangular Wipe", "", "", "BCC+Rectangular Wipe", "fixture-custom", L"效果", "ae"};
    custom.translated_name = L"团队矩形转场";
    localization_require(SearchEngine({custom}).search(L"团队矩形转场").size() == 1,
                         "bundled translation overwrote a user translation");
    for (const auto* name : {"BCCLinear Wipe", "BCC+LinearWipe", "Other+Linear Wipe"}) {
        CatalogItem unrelated{localization_utf8(name), "", "", name, "fixture-unknown", L"效果", "ae"};
        localization_require(SearchEngine({unrelated}).search(L"")[0].item.translated_name.empty(),
                             "BCC+ spacing normalization leaked into other names");
    }
}

static void actual_ae_inventory_has_chinese_without_rewriting_effect_identities() {
    const auto fixture = std::filesystem::path(localization_utf8(__FILE__)).parent_path() /
                         "data" / "ae-25.4-effects.txt";
    std::ifstream input(fixture, std::ios::binary);
    localization_require(input.good(), "cannot read real AE effect inventory fixture");
    std::vector<CatalogItem> items;
    std::string line;
    size_t missing = 0;
    std::string first_missing;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.find('\t');
        const auto second = line.find('\t', tab == std::string::npos ? 0 : tab + 1);
        localization_require(tab != std::string::npos && second != std::string::npos, "invalid inventory row");
        const auto id = line.substr(0, tab), label = line.substr(tab + 1, second - tab - 1);
        CatalogItem item{localization_utf8(label), "", "", label, id, L"效果", "ae"};
        const auto expected_display = xfx::catalog_display_name(item);
        xfx::enrich_catalog_item(item);
        localization_require(item.match_name == id && item.english_name == label &&
                             item.display_name == expected_display, "inventory identity changed: " + id);
        const bool chinese = std::any_of(item.translated_name.begin(), item.translated_name.end(),
            [](wchar_t ch) { return (ch >= 0x3400 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF); });
        if (!chinese) { ++missing; if (first_missing.empty()) first_missing = id; }
        else localization_require(!item.pinyin_full_variants.empty() && !item.pinyin_initial_variants.empty(),
                                  "Chinese inventory label has no generated pinyin: " + id);
        items.push_back(std::move(item));
    }
    localization_require(items.size() == 1401, "actual AE inventory fixture count changed");
    localization_require(!missing, "actual AE inventory missing translations: " + std::to_string(missing) +
                                  "; first=" + first_missing);
    SearchEngine engine(items);
    // Sample the public search interface across the complete inventory, not a
    // table-derived expected output. Every sampled ID must remain applicable.
    for (size_t i = 0; i < items.size(); i += 19) {
        const auto& item = items[i];
        for (const auto& query : {item.translated_name,
                                  localization_utf8(item.pinyin_full_variants.front()),
                                  localization_utf8(item.pinyin_initial_variants.front())}) {
            const auto results = engine.search(query);
            localization_require(std::any_of(results.begin(), results.end(), [&](const auto& result) {
                return result.item.match_name == item.match_name;
            }), "inventory Chinese/pinyin search lost ID: " + item.match_name);
        }
    }
}

static void actual_catalog_excludes_hidden_registration_not_valid_same_name_effects() {
    const auto fixture = std::filesystem::path(localization_utf8(__FILE__)).parent_path() /
                         "data" / "ae-25.4-effects.txt";
    std::ifstream input(fixture, std::ios::binary);
    localization_require(input.good(), "catalog inventory fixture missing");
    std::map<std::string, size_t> labels;
    std::map<std::string, std::string> visible;
    size_t total = 0, hidden = 0, internal = 0, bcc_deprecated = 0, adobe_deprecated = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto first = line.find('\t'), second = line.find('\t', first + 1);
        localization_require(first != std::string::npos && second != std::string::npos, "invalid inventory row");
        const auto id = line.substr(0, first), label = line.substr(first + 1, second - first - 1), category = line.substr(second + 1);
        ++total;
        const bool included = xfx::effect_is_discoverable(id, label, category);
        if (category == "_Obsolete") { ++hidden; localization_require(!included, "hidden registration exposed: " + id); }
        else if (category.empty()) { ++internal; localization_require(!included, "internal registration exposed: " + id); }
        else {
            localization_require(included, "user-facing category removed: " + id);
            visible.emplace(id, label);
            ++labels[label];
            if (category == "BCC Obsolete") ++bcc_deprecated;
            if (category == "过时") ++adobe_deprecated;
        }
    }
    localization_require(total == 1401 && hidden == 90 && internal == 108 && visible.size() == 1203,
                         "whole-inventory discovery policy counts changed");
    localization_require(bcc_deprecated == 117 && adobe_deprecated == 10, "visible deprecated effects were wrongly hidden");
    localization_require(!visible.count("ADBE Geometry") && visible.at("ADBE Geometry2") == "变换",
                         "current Transform not preserved or hidden Transform still exposed");
    localization_require(!visible.count("ADBE PSL Drop Shadow") && visible.count("ADBE Drop Shadow"),
                         "Photoshop helper exposure or normal Drop Shadow lost");
    for (const auto& [label, count] : labels) localization_require(count == 1, "unexplained inventory duplicate: " + label);
    localization_require(xfx::effect_is_discoverable("VendorA", "Same Name", "Vendor") &&
                         xfx::effect_is_discoverable("VendorB", "Same Name", "Vendor") &&
                         xfx::effect_is_discoverable("Custom", "Custom", "_Vendor"),
                         "same-label distinct valid effects or arbitrary vendor category removed");
    localization_require(!xfx::effect_is_discoverable("", "Effect", "Vendor") &&
                         !xfx::effect_is_discoverable("ID", "  ", "Vendor") &&
                         !xfx::effect_is_discoverable("ID", "Effect", "  \t") &&
                         !xfx::effect_is_discoverable("ID", "Effect", "Vendor", false),
                         "invalid or unknown effect metadata leaked into discovery");
}

static void settings_round_trip() {
    const auto path = std::filesystem::temp_directory_path() / "xfx-native-settings-test.ini";
    xfx::Settings settings;
    settings.use_english_mode = true;
    settings.fixed_window = true;
    settings.auto_close = false;
    settings.hotkey = L"Ctrl+Space";
    settings.favorites = {L"CC Ball Action"};
    settings.slots[0] = L"CC Ball Action";
    xfx::SettingsStore::save(path, settings);
    const auto loaded = xfx::SettingsStore::load(path);
    assert(loaded.use_english_mode);
    assert(loaded.fixed_window);
    assert(!loaded.auto_close);
    assert(loaded.hotkey == L"Ctrl+Space");
    assert(loaded.favorites.size() == 1);
    assert(loaded.slots[0] == L"CC Ball Action");
    std::filesystem::remove(path);
}

static void hotkey_parser_accepts_custom_shortcuts() {
    xfx::HotkeySpec spec;
    assert(xfx::parse_hotkey(L"Ctrl+Alt+K", &spec));
    assert((spec.modifiers & MOD_CONTROL) != 0);
    assert((spec.modifiers & MOD_ALT) != 0);
    assert(spec.virtual_key == 'K');
}

static void hotkey_parser_accepts_function_keys() {
    xfx::HotkeySpec spec;
    assert(xfx::parse_hotkey(L"Shift+F8", &spec));
    assert((spec.modifiers & MOD_SHIFT) != 0);
    assert(spec.virtual_key == VK_F8);
}

static void hotkey_parser_rejects_missing_key() {
    xfx::HotkeySpec spec;
    assert(!xfx::parse_hotkey(L"Ctrl+Alt", &spec));
}

static void hotkey_parser_rejects_invalid_function_key() {
    xfx::HotkeySpec spec;
    bool accepted = false;
    try {
        accepted = xfx::parse_hotkey(L"Ctrl+Fabc", &spec);
    } catch (...) {
        assert(false);
    }
    assert(!accepted);
}

static void hotkey_formatter_formats_two_key_shortcut() {
    xfx::HotkeySpec spec;
    spec.modifiers = MOD_CONTROL | MOD_SHIFT;
    spec.virtual_key = 'K';
    assert(xfx::format_hotkey(spec) == L"Ctrl+Shift+K");
}

int main() {
    int localization_failures = 0;
    const auto check_localization = [&](const char* name, auto action) {
        try { action(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++localization_failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    check_localization("real CC matchNames and legacy identities on a Chinese host",
                       real_cc_identifiers_translate_localized_host_names_and_keep_legacy_ids);
    check_localization("current CC Chinese full-pinyin and initials",
                       current_cc_additions_support_chinese_full_pinyin_and_initials);
    check_localization("confirmed keying/expression stable IDs with Chinese host labels and search",
                       confirmed_keying_and_expression_ids_translate_chinese_host_labels);
    check_localization("expression-control English fallbacks and unchanged host identities",
                       expression_controls_keep_english_fallback_rows_and_host_identities);
    check_localization("OpenEXR product names and Chinese search aliases",
                       openexr_product_names_are_preserved_with_chinese_search_aliases);
    check_localization("obsolete and pseudo effects gain descriptive Chinese search names",
                       obsolete_and_pseudo_effects_get_descriptive_chinese_search_names);
    check_localization("installed and embedded translation data consistency",
                       translation_tsv_and_embedded_rows_agree_without_ambiguous_english_fallbacks);
    check_localization("screenshot external wipes, BCC+ spacing, Chinese/pinyin and unchanged identities",
                       external_wipes_from_user_screenshot_translate_and_keep_host_ids);
    check_localization("actual 1401-effect AE inventory Chinese labels, pinyin and preserved application IDs",
                       actual_ae_inventory_has_chinese_without_rewriting_effect_identities);
    check_localization("whole catalog discovery removes hidden/internal entries but retains valid deprecated/same-name effects",
                       actual_catalog_excludes_hidden_registration_not_valid_same_name_effects);
    search_matches_chinese();
    search_matches_full_pinyin();
    search_matches_initials();
    search_matches_english_case_insensitively();
    chinese_search_excludes_unrelated_items();
    search_ignores_separators_in_pinyin();
    english_search_works_when_display_name_is_missing();
    translated_name_is_searchable();
    aliases_are_searchable();
    display_cleanup_rules_are_exact_and_search_preserving();
    effect_label_uses_single_line_bilingual_format();
    exact_match_ranks_first();
    translated_effect_generates_pinyin_without_manual_fields();
    chinese_preset_generates_phrase_aware_full_pinyin_and_initials();
    custom_chinese_alias_generates_pinyin();
    pinyin_accepts_umlaut_v_and_tone_marks();
    english_fallback_decodes_utf8_without_corrupting_names();
    built_in_translation_is_applied_without_rewriting_identifiers();
    unknown_effect_keeps_original_name_and_custom_translation();
    third_party_effects_keep_brands_and_gain_chinese_pinyin_search();
    sapphire_warp_effects_translate_with_padded_or_case_changed_host_names();
    settings_round_trip();
    hotkey_parser_accepts_custom_shortcuts();
    hotkey_parser_accepts_function_keys();
    hotkey_parser_rejects_missing_key();
    hotkey_parser_rejects_invalid_function_key();
    hotkey_formatter_formats_two_key_shortcut();
    if (!localization_failures) std::cout << "native core tests passed\n";
    return localization_failures ? 1 : 0;
}
