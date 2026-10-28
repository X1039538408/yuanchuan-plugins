#pragma once

static void check_result_click_is_deferred() {
    assert(defer_result_click(WM_LBUTTONDOWN, 0, false));
    assert(defer_result_click(WM_LBUTTONUP, 0, false));
    assert(!defer_result_click(WM_LBUTTONDOWN, -1, false));
    assert(!defer_result_click(WM_LBUTTONDOWN, 0, true));
    std::cout << "PASS result clicks defer native LISTBOX painting until the apply transaction\n";
}
