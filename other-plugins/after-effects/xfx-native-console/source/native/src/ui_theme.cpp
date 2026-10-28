#include "ui_theme.h"
#include <algorithm>

namespace xfx {
namespace {

// Badge labels share selected_text (white) in both themes. Their red/green
// backgrounds retain sufficient contrast at the small result-row label size.
constexpr ThemeColors kDark{
    RGB(32, 32, 32),     // background: preview AE fallback
    RGB(41, 41, 41),     // surface
    RGB(29, 29, 29),     // input
    RGB(200, 200, 200),  // text
    RGB(170, 170, 170),  // muted
    RGB(20, 111, 203),   // selected: white-label contrast
    RGB(255, 255, 255),  // selected_text
    RGB(73, 73, 73),     // divider
    RGB(50, 50, 50),     // button
    RGB(68, 68, 68),     // button_pressed
    RGB(200, 200, 200),  // icon: match text ink
    RGB(189, 54, 68),    // effect_badge
    RGB(34, 123, 57),    // preset_badge
    RGB(130, 130, 130),  // disabled
    RGB(55, 142, 240), RGB(57, 184, 106), RGB(227, 93, 106),
    RGB(48,48,48), RGB(128,128,128), RGB(112,112,112)
};

constexpr ThemeColors kLight{
    RGB(237, 237, 237),  // background
    RGB(245, 245, 245),  // surface
    RGB(255, 255, 255),  // input
    RGB(37, 37, 37),     // text
    RGB(90, 90, 90),     // muted
    RGB(20, 111, 203),   // selected
    RGB(255, 255, 255),  // selected_text
    RGB(190, 190, 190),  // divider
    RGB(225, 225, 225),  // button
    RGB(207, 207, 207),  // button_pressed
    RGB(37, 37, 37),     // icon: match text ink
    RGB(189, 54, 68),    // effect_badge
    RGB(34, 123, 57),    // preset_badge
    RGB(115, 115, 115),  // disabled
    RGB(20, 111, 203), RGB(22, 111, 60), RGB(176, 37, 54),
    RGB(255,255,255), RGB(120,120,120), RGB(120,120,120)
};

} // namespace

const ThemeColors& theme_colors(bool light_theme) noexcept {
    return light_theme ? kLight : kDark;
}

bool is_light_background(COLORREF background) noexcept {
    return GetRValue(background) * 0.2126 + GetGValue(background) * 0.7152 +
        GetBValue(background) * 0.0722 >= 140;
}

ThemeColors resolve_theme(ThemeMode mode, std::optional<COLORREF> host_background) noexcept {
    if (mode == ThemeMode::Light) return kLight;
    if (mode == ThemeMode::Dark) {
        auto c = kDark;
        c.background = RGB(19,19,19); c.surface = RGB(37,37,37);
        c.input = RGB(22,22,22); c.button = RGB(31,31,31); c.divider = RGB(50,50,50);
        return c;
    }
    if (!host_background) return kDark;
    const bool light = is_light_background(*host_background);
    auto c = light ? kLight : kDark;
    const auto shade = [background = *host_background](int delta) {
        const auto channel = [delta](int value) { return (std::clamp)(value + delta, 0, 255); };
        return RGB(channel(GetRValue(background)), channel(GetGValue(background)), channel(GetBValue(background)));
    };
    c.background = *host_background;
    c.surface = shade(light ? 8 : 9); c.input = shade(light ? 18 : -3);
    c.button = shade(light ? -12 : 18); c.button_pressed = shade(light ? -26 : 36);
    c.divider = shade(light ? -48 : 41);
    c.search = shade(light ? 18 : 16);
    return c;
}

} // namespace xfx
