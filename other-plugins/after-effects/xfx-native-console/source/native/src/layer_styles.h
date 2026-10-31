#pragma once
#include <array>
#include <string_view>

namespace xfx {
struct LayerStyle {
    const char* match_name;
    const wchar_t* label;
    const wchar_t* short_label;
    const char* english_name;
    int command_id;
};
// Native Layer > Layer Styles actions, NOT effect/preset catalog entries.
// Stable property group verified after every menu action; never toggle a
// previously created style through the menu or reset its parameter values.
inline constexpr std::array<LayerStyle, 9> layer_styles{{
    {"dropShadow/enabled", L"投影", L"投影", "Drop Shadow", 9000},
    {"innerShadow/enabled", L"内阴影", L"内影", "Inner Shadow", 9001},
    {"outerGlow/enabled", L"外发光", L"外光", "Outer Glow", 9002},
    {"innerGlow/enabled", L"内发光", L"内光", "Inner Glow", 9003},
    {"bevelEmboss/enabled", L"斜面和浮雕", L"浮雕", "Bevel and Emboss", 9004},
    {"chromeFX/enabled", L"光泽", L"光泽", "Satin", 9005},
    {"solidFill/enabled", L"颜色叠加", L"颜色", "Color Overlay", 9006},
    {"gradientFill/enabled", L"渐变叠加", L"渐变", "Gradient Overlay", 9007},
    {"frameFX/enabled", L"描边", L"描边", "Stroke", 9008},
}};
inline const LayerStyle* find_layer_style(std::string_view match_name) {
    for (const auto& style : layer_styles) if (match_name == style.match_name) return &style;
    return nullptr;
}
} // namespace xfx
