#pragma once
#include <string_view>

namespace xfx {

// Discovery policy, not a claim that a plug-in can render or is licensed.
// Keep this separate from context-dependent canAddProperty checks on a layer.
inline bool effect_is_discoverable(std::string_view match_name,
                                   std::string_view display_name,
                                   std::string_view host_category,
                                   bool category_known = true) {
    const auto trim = [](std::string_view value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos) return std::string_view{};
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    };
    // Narrow default-discovery policy. _Obsolete is the exact hidden legacy
    // bucket observed in the host inventory; do NOT generalize to categories
    // containing "Obsolete", visible deprecated groups, or arbitrary _Vendor.
    // Unclassified helpers and unknown metadata remain in the diagnostic
    // inventory/translation table, not the add-effect picker.
    return category_known && !trim(match_name).empty() && !trim(display_name).empty() &&
           !trim(host_category).empty() && trim(host_category) != "_Obsolete";
}

} // namespace xfx
