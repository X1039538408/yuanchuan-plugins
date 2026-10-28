#include "ime_controller.h"

namespace xfx {

ImeController::InputState ImeController::capture(HWND window) {
    InputState state;
    state.window = window;
    if (!IsWindow(window)) return state;
    HIMC context = ImmGetContext(window);
    if (!context) return state;
    state.context = context;
    state.has_conversion = ImmGetConversionStatus(context, &state.conversion, &state.sentence) != FALSE;
    state.open = ImmGetOpenStatus(context) != FALSE;
    state.valid = true;
    ImmReleaseContext(window, context);
    return state;
}

bool ImeController::capture_original(HWND input_window, HWND original_focus) {
    if (changed_) {
        restore();
        // A refused restoration must not be overwritten by a new session.
        if (changed_) return false;
    }
    // Capture the host input before applying anything to the search field.
    // Their contexts may be distinct, or may share the UI thread's default HIMC.
    original_focus_ = capture(original_focus);
    target_ = capture(input_window);
    captured_ = target_.valid;
    changed_ = false;
    return captured_;
}

bool ImeController::enable_english_mode() {
    if (!captured_ || !IsWindow(target_.window)) return false;
    HIMC context = ImmGetContext(target_.window);
    if (!context) return false;
    // Some TSF-backed Chinese IMEs accept an alphanumeric conversion value
    // without closing their composition mode. Close the IME explicitly and
    // read its actual state rather than trusting either setter's return value.
    changed_ = true;
    if (target_.has_conversion) {
        const DWORD conversion = target_.conversion &
            ~(IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_KATAKANA);
        ImmSetConversionStatus(context, conversion, target_.sentence);
    }
    ImmSetOpenStatus(context, FALSE);
    const bool english = ImmGetOpenStatus(context) == FALSE;
    ImmReleaseContext(target_.window, context);
    if (!english) restore();
    return english;
}

bool ImeController::restore_state(const InputState& state) {
    if (!state.valid || !IsWindow(state.window)) return true;
    HIMC context = ImmGetContext(state.window);
    if (!context) return false;
    if (state.has_conversion)
        ImmSetConversionStatus(context, state.conversion, state.sentence);
    ImmSetOpenStatus(context, state.open ? TRUE : FALSE);
    DWORD conversion = 0, sentence = 0;
    const bool restored = (ImmGetOpenStatus(context) != FALSE) == state.open &&
        (!state.has_conversion || (ImmGetConversionStatus(context, &conversion, &sentence) &&
            conversion == state.conversion && sentence == state.sentence));
    ImmReleaseContext(state.window, context);
    return restored;
}

void ImeController::restore() {
    bool restored = true;
    if (captured_ && changed_) {
        // Restore a shared context only once, using the host's pre-panel state.
        // If its window disappeared, the search field still provides a fallback.
        if (!original_focus_.valid || !IsWindow(original_focus_.window) ||
            original_focus_.context != target_.context)
            restored = restore_state(target_);
        // Attempt both independent contexts even if the first one refused.
        if (!restore_state(original_focus_)) restored = false;
    }
    if (!restored) return;
    captured_ = false;
    changed_ = false;
}

} // namespace xfx
