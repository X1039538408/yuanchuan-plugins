#pragma once
#include <windows.h>
#include "ui_theme.h"

namespace xfx {

// Runs synchronously on owner's thread. Menus/font remain caller-owned and must
// stay valid and unmodified until this returns. Returns TrackPopupMenuEx's value
// (selected command with TPM_RETURNCMD, otherwise BOOL); 0 also means cancel/error.
// Temporarily themes the whole menu tree and restores data, type/state and
// background brushes before returning. No process/system theme is changed.
UINT track_themed_popup(HMENU menu, UINT flags, int x, int y, HWND owner,
                       const ThemeColors& colors, HFONT font = nullptr);

} // namespace xfx
