#pragma once
#include <windows.h>

namespace xfx {

// Gallery-only fixed-height thumbnail list. This is not a general LISTBOX
// replacement. Supports RESETCONTENT/ADDSTRING/GETCOUNT/GETTEXT/LEN, selection,
// caret/anchor, item geometry/height and top-index LB messages used by the gallery.
// Heights are positive int pixel values, including values above LISTBOX's 255 cap.
// Mouse selection remains the caller's subclass responsibility; basic navigation
// keys and double clicks send LBN_SELCHANGE/LBN_DBLCLK to the parent. Painting
// borrows its WM_CTLCOLORLISTBOX brush and sends clipped ODT_LISTBOX WM_DRAWITEM.
// The HWND owns all component state; fonts and parent brushes remain caller-owned.
HWND create_thumbnail_list(HWND parent, int control_id, HINSTANCE instance);

} // namespace xfx
