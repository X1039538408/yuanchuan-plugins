#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>
#include "ui_theme.h"

namespace xfx {

// Runs synchronously on owner's thread. Menus/font remain caller-owned and must
// stay valid and unmodified until this returns. Returns TrackPopupMenuEx's value
// (selected command with TPM_RETURNCMD, otherwise BOOL); 0 also means cancel/error.
// Temporarily themes the whole menu tree and restores data, type/state and
// background brushes before returning. No process/system theme is changed.
// Reentrant calls may track disjoint menus only: sharing any HMENU with an
// active session tree is rejected before mutation (ERROR_INVALID_PARAMETER).
UINT track_themed_popup(HMENU menu, UINT flags, int x, int y, HWND owner,
                       const ThemeColors& colors, HFONT font = nullptr);

enum class MenuActionIcon { Bind, Edit, Clear };
struct MenuAction {
    UINT command;
    MenuActionIcon icon;
    bool enabled;
    std::wstring label;
};
struct MenuActionRow {
    HMENU menu;
    UINT position;
    std::array<MenuAction, 3> actions;
};
using MenuActionRows = std::vector<MenuActionRow>;

// Action rows are leaf items; their native label/ID never executes. Metadata is
// copied for this synchronous call. Commands must be nonzero and unique, labels
// nonempty, and each (menu, position) must occur once in this menu tree.
// Requires TPM_RETURNCMD | TPM_NONOTIFY: the chosen cell command is returned only
// after EndMenu and complete session cleanup, never sent as WM_COMMAND here.
UINT track_themed_action_popup(HMENU menu, UINT flags, int x, int y, HWND owner,
                              const ThemeColors& colors, HFONT font, const MenuActionRows& rows);

// Shared production/test geometry. All rectangles are half-open, in the same
// coordinate system as row; tiny rows have no actionable cells, never overlap.
namespace menu_action_detail {
struct Layout { RECT label{}; std::array<RECT, 3> cells{}; };
Layout layout(RECT row, int dpi = 96);
int hit_test(const Layout& geometry, POINT point);
int next_enabled(const std::array<MenuAction, 3>& actions, int current, int direction);
UINT released_command(const std::array<MenuAction, 3>& actions, int pressed, int released);
} // namespace menu_action_detail

} // namespace xfx
