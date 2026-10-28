#include "../src/ime_controller.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

struct ImeState {
    DWORD conversion = 0;
    DWORD sentence = 0;
    bool open = false;
};

ImeState read_state(HWND window) {
    HIMC context = ImmGetContext(window);
    require(context != nullptr, "The real EDIT must expose an input context");
    ImeState state;
    const bool conversion_read = ImmGetConversionStatus(context, &state.conversion, &state.sentence) != FALSE;
    state.open = ImmGetOpenStatus(context) != FALSE;
    ImmReleaseContext(window, context);
    require(conversion_read, "The installed input method must expose its conversion state");
    return state;
}

void write_state(HWND window, const ImeState& state) {
    HIMC context = ImmGetContext(window);
    require(context != nullptr, "Cannot change the real EDIT input context");
    // Setter return values are diagnostic only. The readback below is the
    // precondition for every regression, including an actually open Chinese IME.
    const BOOL conversion_set = ImmSetConversionStatus(context, state.conversion, state.sentence);
    const BOOL open_set = ImmSetOpenStatus(context, state.open ? TRUE : FALSE);
    ImmReleaseContext(window, context);
    pump();
    const auto actual = read_state(window);
    std::printf("PRECONDITION setters conversion=%d open=%d; readback conversion=%lu sentence=%lu open=%d\n",
        conversion_set, open_set, actual.conversion, actual.sentence, actual.open);
    require(actual.conversion == state.conversion && actual.sentence == state.sentence && actual.open == state.open,
        "The installed input method did not establish the requested regression precondition");
}

struct InputFixture {
    HWND parent = nullptr;
    HWND input = nullptr;
    ImeState initial;
    bool captured = false;

    InputFixture() {
        parent = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"XFX IME regression", WS_OVERLAPPEDWINDOW,
            20, 20, 320, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create the IME regression window");
        input = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 8, 8, 280, 30,
            parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(input != nullptr, "Cannot create the IME regression EDIT");
        ShowWindow(parent, SW_SHOWNORMAL);
        SetForegroundWindow(parent);
        SetFocus(input);
        pump();
        initial = read_state(input);
        captured = true;
    }

    ~InputFixture() {
        if (captured && IsWindow(input)) {
            HIMC context = ImmGetContext(input);
            if (context) {
                ImmSetConversionStatus(context, initial.conversion, initial.sentence);
                ImmSetOpenStatus(context, initial.open ? TRUE : FALSE);
                ImmReleaseContext(input, context);
            }
        }
        if (IsWindow(parent)) DestroyWindow(parent);
    }
};

struct AssociatedContext {
    HWND window;
    HIMC context;
    HIMC previous;

    explicit AssociatedContext(HWND input) : window(input), context(ImmCreateContext()), previous(nullptr) {
        require(context != nullptr, "Cannot allocate an independent real IMM context");
        previous = ImmAssociateContext(window, context);
    }

    ~AssociatedContext() {
        if (IsWindow(window)) ImmAssociateContext(window, previous);
        ImmDestroyContext(context);
    }
};

void check_direct_english_typing(const InputFixture& fixture) {
    if (GetForegroundWindow() != fixture.parent || GetFocus() != fixture.input ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        std::puts("SKIP actual keyboard delivery: regression EDIT is not the unmodified foreground input");
        return;
    }
    SetWindowTextW(fixture.input, L"");
    INPUT keys[4]{};
    for (auto& key : keys) key.type = INPUT_KEYBOARD;
    keys[0].ki.wVk = 'N';
    keys[1].ki.wVk = 'N'; keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[2].ki.wVk = 'I';
    keys[3].ki.wVk = 'I'; keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(4, keys, sizeof(INPUT)) == 4, "Cannot deliver the actual English keyboard regression input");
    wchar_t text[16]{};
    const ULONGLONG deadline = GetTickCount64() + 1000;
    do {
        pump();
        GetWindowTextW(fixture.input, text, 16);
        if (text[0] && text[1]) break;
        Sleep(5);
    } while (GetTickCount64() < deadline && GetForegroundWindow() == fixture.parent);
    HIMC context = ImmGetContext(fixture.input);
    require(context != nullptr, "The typing check lost its actual input context");
    const LONG composition = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    ImmReleaseContext(fixture.input, context);
    require((text[0] == L'n' || text[0] == L'N') && (text[1] == L'i' || text[1] == L'I') &&
        text[2] == 0 && composition == 0, "Real N/I key input must produce direct English text with no IME composition");
    std::puts("PASS actual N/I keyboard input produces direct English text with no Chinese IME composition");
}

void check_open_english_and_restore() {
    InputFixture fixture;
    ImeState chinese = fixture.initial;
    chinese.conversion |= IME_CMODE_NATIVE;
    chinese.open = true;
    write_state(fixture.input, chinese);

    xfx::ImeController controller;
    require(controller.capture_original(fixture.input), "Cannot capture the open Chinese input state");
    const bool enabled = controller.enable_english_mode();
    pump();
    const auto english = read_state(fixture.input);
    std::printf("ENGLISH enabled=%d conversion=%lu sentence=%lu open=%d\n",
        enabled, english.conversion, english.sentence, english.open);
    if (enabled && !english.open) check_direct_english_typing(fixture);
    controller.restore();
    pump();
    const auto restored = read_state(fixture.input);
    std::printf("RESTORED conversion=%lu sentence=%lu open=%d\n",
        restored.conversion, restored.sentence, restored.open);
    require(enabled && !english.open, "English mode must read back an actually closed IME, not just a changed conversion bit");
    require(restored.conversion == chinese.conversion && restored.sentence == chinese.sentence && restored.open == chinese.open,
        "Closing the panel must restore conversion, sentence, and the original open state");

    // The same controller must support a second panel session, and must restore
    // a state whose original open flag is FALSE as well as TRUE.
    chinese.open = false;
    write_state(fixture.input, chinese);
    require(controller.capture_original(fixture.input), "A restored controller must capture a fresh session");
    require(controller.enable_english_mode(), "A fresh session must enter English mode");
    HIMC context = ImmGetContext(fixture.input);
    require(context != nullptr, "Cannot exercise a user change while the panel is open");
    ImmSetOpenStatus(context, TRUE);
    ImmReleaseContext(fixture.input, context);
    pump();
    require(read_state(fixture.input).open, "User-change precondition must read back an open IME");
    controller.restore();
    pump();
    require(!read_state(fixture.input).open, "Restore must recover an originally closed IME after a user change");
    std::puts("PASS real IMM English open-state readback, complete restore and repeated sessions");
}

void check_original_host_context_restoration() {
    InputFixture fixture;
    HWND host_input = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 8, 48, 280, 30,
        fixture.parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host_input != nullptr, "Cannot create the original host EDIT");
    AssociatedContext target_context(fixture.input);
    AssociatedContext host_context(host_input);
    require(target_context.context != host_context.context, "The original and search contexts must really differ");

    ImeState target = fixture.initial;
    target.conversion |= IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE;
    target.open = false;
    ImeState host = fixture.initial;
    host.conversion |= IME_CMODE_NATIVE;
    host.open = true;
    SetFocus(host_input);
    write_state(host_input, host);
    write_state(fixture.input, target);
    // TSF may share conversion bits across a thread even when the HIMCs differ.
    // Snapshot the actual pre-panel state, while requiring distinct real open
    // states rather than assuming that two contexts isolate every IME setting.
    host = read_state(host_input);
    target = read_state(fixture.input);
    require(host.open && !target.open, "The original and search contexts must retain distinct actual open states");

    xfx::ImeController controller;
    require(controller.capture_original(fixture.input, host_input), "Cannot capture both original and search contexts");
    SetFocus(fixture.input);
    require(controller.enable_english_mode(), "The search context must enter English mode after gaining focus");
    require(!read_state(fixture.input).open, "The focused search context must really close");
    auto altered_host = host;
    altered_host.open = false;
    altered_host.conversion &= ~IME_CMODE_NATIVE;
    write_state(host_input, altered_host);
    controller.restore();
    pump();
    const auto restored_target = read_state(fixture.input);
    const auto restored_host = read_state(host_input);
    std::printf("DUAL RESTORE target conversion=%lu sentence=%lu open=%d; host conversion=%lu sentence=%lu open=%d\n",
        restored_target.conversion, restored_target.sentence, restored_target.open,
        restored_host.conversion, restored_host.sentence, restored_host.open);
    require(restored_target.conversion == target.conversion && restored_target.sentence == target.sentence &&
        restored_target.open == target.open, "Restore must recover the search field's independent original context");
    require(restored_host.conversion == host.conversion && restored_host.sentence == host.sentence &&
        restored_host.open == host.open, "Restore must recover the original host focus context as well as the search field");
    SetFocus(host_input);
    pump();
    require(read_state(host_input).open == host.open, "Restored host input state must survive focus returning from the panel");
    std::puts("PASS distinct real IMM host/search contexts restore their own pre-panel states and host refocus");
}

void check_restore_retry_keeps_snapshot() {
    InputFixture fixture;
    AssociatedContext target_context(fixture.input);
    auto original = fixture.initial;
    original.conversion |= IME_CMODE_NATIVE;
    original.open = true;
    write_state(fixture.input, original);
    xfx::ImeController controller;
    require(controller.capture_original(fixture.input) && controller.enable_english_mode(),
        "Cannot prepare a changed input context for the restoration retry");
    HIMC detached = ImmAssociateContext(fixture.input, nullptr);
    require(detached == target_context.context, "The actual changed context must be detached");
    HIMC inaccessible = ImmGetContext(fixture.input);
    if (inaccessible) ImmReleaseContext(fixture.input, inaccessible);
    require(!inaccessible, "The refusal precondition must expose no context to restore");
    controller.restore();
    const bool snapshot_was_protected = !controller.capture_original(fixture.input);
    ImmAssociateContext(fixture.input, detached);
    controller.restore();
    pump();
    const auto restored = read_state(fixture.input);
    require(snapshot_was_protected, "A refused restoration must prevent a new capture from replacing its snapshot");
    require(restored.open == original.open && restored.conversion == original.conversion && restored.sentence == original.sentence,
        "A later restoration retry must still have the complete original state");
    std::puts("PASS actual context unavailability retains the snapshot for restoration retry");
}

} // namespace

int main() {
    std::printf("ACTIVE keyboard layout=%p IME=%d\n", GetKeyboardLayout(0), ImmIsIME(GetKeyboardLayout(0)));
    if (!ImmIsIME(GetKeyboardLayout(0))) {
        std::puts("SKIP real Chinese IME regression: the test thread does not have an IME layout");
        return 0;
    }
    try {
        check_open_english_and_restore();
        check_original_host_context_restoration();
        check_restore_retry_keeps_snapshot();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
