#include "ui_theme.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static double luminance(COLORREF color) {
    const auto linear = [](BYTE channel) {
        const double value = static_cast<double>(channel) / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color)) + 0.0722 * linear(GetBValue(color));
}
static double contrast(COLORREF a, COLORREF b) {
    const double first = luminance(a), second = luminance(b);
    return ((std::max)(first, second) + 0.05) / ((std::min)(first, second) + 0.05);
}

int main() {
    int failures = 0;
    auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("dark mode retains current main colors and blue selection", [] {
        const auto& colors = xfx::theme_colors(false);
        require(colors.background == RGB(32, 32, 32), "AE gray fallback must match preview");
        require(colors.input == RGB(29, 29, 29) && colors.text == colors.icon && colors.text == RGB(200, 200, 200), "normal text must match icon color");
        require(GetBValue(colors.selected) > GetRValue(colors.selected), "selection must be blue");
        require(luminance(colors.input) < luminance(colors.background), "dark input must be recessed");
    });
    test("light mode uses pale neutral surfaces and dark text", [] {
        const auto& colors = xfx::theme_colors(true);
        require(colors.background == RGB(237, 237, 237), "light background must match preview");
        require(colors.input == RGB(255, 255, 255) && colors.text == colors.icon && colors.text == RGB(37, 37, 37), "light normal text must match icon color");
        require(luminance(colors.input) > luminance(colors.background), "light input must remain white");
        require(GetBValue(colors.selected) > GetGValue(colors.selected) && GetGValue(colors.selected) > GetRValue(colors.selected), "light selection must be blue");
    });
    test("follow reflects changing host RGB while manual themes ignore host changes", [] {
        const auto dark=xfx::resolve_theme(xfx::ThemeMode::Follow,RGB(28,28,28));
        const auto light=xfx::resolve_theme(xfx::ThemeMode::Follow,RGB(230,230,230));
        require(dark.background==RGB(28,28,28) && light.background==RGB(230,230,230),"host RGB ignored");
        require(!xfx::is_light_background(dark.background) && xfx::is_light_background(light.background),"text polarity failed");
        require(contrast(dark.text,dark.background)>=7 && contrast(light.text,light.background)>=7,"host text is unreadable");
        require(xfx::resolve_theme(xfx::ThemeMode::Follow).background==RGB(32,32,32),"missing host fallback changed");
        require(xfx::resolve_theme(xfx::ThemeMode::Light,RGB(28,28,28)).background==RGB(237,237,237),"manual light follows host");
        require(xfx::resolve_theme(xfx::ThemeMode::Dark,RGB(230,230,230)).background==RGB(19,19,19),"manual dark follows host");
    });
    test("body input and secondary text remain readable in both modes", [] {
        for (const bool light : {false, true}) {
            const auto& c = xfx::theme_colors(light);
            for (const COLORREF background : {c.background, c.surface, c.input}) {
                require(contrast(c.text, background) >= 7.0, "body/input text needs readable contrast");
                require(contrast(c.muted, background) >= 4.5, "secondary text needs readable contrast");
            }
            require(contrast(c.icon, c.background) >= 3.0, "toolbar icon disappears against background");
            require(contrast(c.disabled, c.background) >= 3.0, "disabled labels are too faint to locate");
        }
    });
    test("normal and pressed buttons retain text contrast and state feedback", [] {
        for (const bool light : {false, true}) {
            const auto& c = xfx::theme_colors(light);
            require(contrast(c.text, c.button) >= 4.5, "button label is unreadable");
            require(contrast(c.text, c.button_pressed) >= 4.5, "pressed button label is unreadable");
            require(c.button != c.button_pressed, "pressed button has no visible color change");
            require(c.divider != c.background && c.surface != c.background, "panel borders/surfaces disappear");
        }
    });
    test("selected rows and effect preset badges support white labels", [] {
        for (const bool light : {false, true}) {
            const auto& c = xfx::theme_colors(light);
            require(c.selected_text == RGB(255, 255, 255), "selected label must remain white");
            for (const COLORREF background : {c.selected, c.effect_badge, c.preset_badge})
                require(contrast(c.selected_text, background) >= 4.5, "selected/badge label is unreadable");
            require(GetRValue(c.effect_badge) > GetGValue(c.effect_badge), "effect badge must remain red");
            require(GetGValue(c.preset_badge) > GetRValue(c.preset_badge), "preset badge must remain green");
        }
    });
    test("all color roles are plain COLORREF without alpha bits", [] {
        for (const bool light : {false, true}) {
            const auto& c = xfx::theme_colors(light);
            for (const COLORREF color : {c.background,c.surface,c.input,c.text,c.muted,c.selected,c.selected_text,
                 c.divider,c.button,c.button_pressed,c.icon,c.effect_badge,c.preset_badge,c.disabled})
                require((color & 0xFF000000u) == 0, "GDI color contains alpha/flags");
        }
    });
    return failures ? 1 : 0;
}
