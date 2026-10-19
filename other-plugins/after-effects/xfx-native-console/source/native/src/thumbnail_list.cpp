#include "thumbnail_list.h"
#include <windowsx.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace xfx {
namespace {

constexpr wchar_t kClassName[] = L"XFXGalleryThumbnailList";
struct Row { std::wstring text; bool selected = false; };
struct ListState {
    HWND window = nullptr;
    std::vector<Row> rows;
    int item_height = 100;
    int caret = -1, anchor = -1;
    int wheel_remainder = 0;
    std::int64_t offset = 0;
    HFONT font = nullptr;
    bool focused = false, destroyed = false, updating_scroll = false;
};
using StateHolder = std::shared_ptr<ListState>;

int client_height(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    return (std::max)(0L, client.bottom);
}
std::int64_t content_height(const ListState& state) {
    return static_cast<std::int64_t>(state.rows.size()) * state.item_height;
}
std::int64_t max_offset(const ListState& state) {
    return (std::max)(std::int64_t{0}, content_height(state) - (std::max)(1, client_height(state.window)));
}
void update_scroll(ListState& state) {
    if (state.destroyed || state.updating_scroll) return;
    state.updating_scroll = true;
    state.offset = (std::clamp)(state.offset, std::int64_t{0}, max_offset(state));
    const auto total = content_height(state);
    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    info.nMin = 0;
    info.nMax = static_cast<int>((std::min)((std::max)(total - 1, std::int64_t{0}),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)())));
    info.nPage = static_cast<UINT>((std::max)(1, client_height(state.window)));
    const auto maximum = max_offset(state);
    const auto bar_maximum = (std::max)(0, info.nMax - static_cast<int>(info.nPage) + 1);
    info.nPos = total <= (std::numeric_limits<int>::max)() ? static_cast<int>(state.offset) :
        (maximum ? static_cast<int>(static_cast<long double>(state.offset) * bar_maximum / maximum) : 0);
    SetScrollInfo(state.window, SB_VERT, &info, TRUE);
    state.updating_scroll = false;
}
void scroll_to(ListState& state, std::int64_t offset) {
    state.offset = (std::clamp)(offset, std::int64_t{0}, max_offset(state));
    update_scroll(state);
    if (!state.destroyed) InvalidateRect(state.window, nullptr, FALSE);
}
void scroll_by(ListState& state, long double distance) {
    const long double target = static_cast<long double>(state.offset) + distance;
    const auto maximum = max_offset(state);
    scroll_to(state, target <= 0 ? 0 : target >= maximum ? maximum : static_cast<std::int64_t>(target));
}
bool valid_index(const ListState& state, WPARAM index) { return index < state.rows.size(); }
LONG coordinate(std::int64_t value) {
    return static_cast<LONG>((std::clamp)(value,
        static_cast<std::int64_t>((std::numeric_limits<LONG>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<LONG>::max)())));
}
RECT item_rect(const ListState& state, size_t index) {
    RECT client{}; GetClientRect(state.window, &client);
    const auto top = static_cast<std::int64_t>(index) * state.item_height - state.offset;
    return {0, coordinate(top), client.right, coordinate(top + state.item_height)};
}
void ensure_visible(ListState& state, int index, bool partial) {
    const auto top = static_cast<std::int64_t>(index) * state.item_height;
    const auto bottom = top + state.item_height;
    const auto height = client_height(state.window);
    if (partial && bottom > state.offset && top < state.offset + height) return;
    if (state.item_height > height || top < state.offset) scroll_to(state, top);
    else if (bottom > state.offset + height) scroll_to(state, bottom - height);
}
void notify_parent(const ListState& state, WORD notification) {
    if (state.destroyed) return;
    SendMessageW(GetParent(state.window), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(state.window), notification),
        reinterpret_cast<LPARAM>(state.window));
}
LRESULT hit_test(const ListState& state, LPARAM point) {
    RECT client{}; GetClientRect(state.window, &client);
    const int x = GET_X_LPARAM(point), y = GET_Y_LPARAM(point);
    const bool outside = state.rows.empty() || x < 0 || y < 0 || x >= client.right || y >= client.bottom;
    const auto position = (std::max)(std::int64_t{0}, state.offset + y) / state.item_height;
    const auto index = state.rows.empty() ? 0 : (std::min)(position, static_cast<std::int64_t>(state.rows.size() - 1));
    return MAKELRESULT(static_cast<WORD>(index), static_cast<WORD>(outside));
}
class SavedDC {
public:
    explicit SavedDC(HDC dc) : dc_(dc), saved_(SaveDC(dc)) {}
    ~SavedDC() { if (saved_) RestoreDC(dc_, saved_); }
    explicit operator bool() const { return saved_ != 0; }
    SavedDC(const SavedDC&) = delete;
    SavedDC& operator=(const SavedDC&) = delete;
private:
    HDC dc_; int saved_;
};
void paint(ListState& state, HDC dc) {
    SavedDC saved(dc);
    if (!saved || state.destroyed) return;
    RECT client{}; GetClientRect(state.window, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    IntersectClipRect(dc, 0, 0, client.right, client.bottom);
    const HWND parent = GetParent(state.window);
    auto brush = reinterpret_cast<HBRUSH>(SendMessageW(parent, WM_CTLCOLORLISTBOX,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(state.window)));
    if (state.destroyed) return;
    if (!brush) brush = GetSysColorBrush(COLOR_WINDOW);
    FillRect(dc, &client, brush);
    SelectObject(dc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    const auto first = static_cast<size_t>(state.offset / state.item_height);
    const auto last = state.rows.size();
    for (size_t index = first; index < last && index < state.rows.size() && !state.destroyed; ++index) {
        const auto rect = item_rect(state, index);
        if (rect.top >= client.bottom) break;
        if (rect.bottom <= 0) continue;
        SavedDC row_dc(dc);
        if (!row_dc) break;
        IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_LISTBOX;
        draw.CtlID = static_cast<UINT>(GetDlgCtrlID(state.window));
        draw.itemID = static_cast<UINT>(index);
        draw.itemAction = ODA_DRAWENTIRE;
        if (state.rows[index].selected) draw.itemState |= ODS_SELECTED;
        if (state.focused && state.caret == static_cast<int>(index)) draw.itemState |= ODS_FOCUS;
        if (!IsWindowEnabled(state.window)) draw.itemState |= ODS_DISABLED;
        draw.hwndItem = state.window;
        draw.hDC = dc;
        draw.rcItem = rect;
        SendMessageW(parent, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    }
}
bool navigate(ListState& state, WPARAM key) {
    if (state.rows.empty() || !IsWindowEnabled(state.window)) return false;
    int next = (std::max)(0, state.caret);
    const auto last = static_cast<int>(state.rows.size() - 1);
    const int page = (std::max)(1, client_height(state.window) / state.item_height);
    switch (key) {
    case VK_UP: next = (std::max)(0, next - 1); break;
    case VK_DOWN: next = (std::min)(last, next + 1); break;
    case VK_HOME: next = 0; break;
    case VK_END: next = last; break;
    case VK_PRIOR: next = (std::max)(0, next - page); break;
    case VK_NEXT: next = static_cast<int>((std::min)(static_cast<std::int64_t>(last), static_cast<std::int64_t>(next) + page)); break;
    default: return false;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool changed = false;
    if (!control || shift) {
        if (!shift || state.anchor < 0 || state.anchor > last) state.anchor = next;
        const int first_selected = shift ? (std::min)(state.anchor, next) : next;
        const int last_selected = shift ? (std::max)(state.anchor, next) : next;
        for (size_t index = 0; index < state.rows.size(); ++index) {
            auto& row = state.rows[index];
            const bool in_range = static_cast<int>(index) >= first_selected && static_cast<int>(index) <= last_selected;
            const bool wanted = control && shift ? row.selected || in_range : in_range;
            changed = changed || wanted != row.selected;
            row.selected = wanted;
        }
    }
    state.caret = next;
    ensure_visible(state, next, false);
    InvalidateRect(state.window, nullptr, FALSE);
    if (changed) notify_parent(state, LBN_SELCHANGE);
    return true;
}

LRESULT CALLBACK thumbnail_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
        if (message == WM_NCCREATE) {
            auto holder = std::make_unique<StateHolder>(std::make_shared<ListState>());
            (*holder)->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder.release()));
            return TRUE;
        }
        auto* holder = reinterpret_cast<StateHolder*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!holder) return DefWindowProcW(window, message, wparam, lparam);
        // Parent painting/notifications may synchronously destroy this HWND.
        // Keep state alive until each active callback has returned.
        auto state = *holder;
        switch (message) {
        case WM_NCDESTROY:
            state->destroyed = true;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete holder;
            return DefWindowProcW(window, message, wparam, lparam);
        case LB_RESETCONTENT:
            state->rows.clear(); state->caret = state->anchor = -1; state->wheel_remainder = 0;
            scroll_to(*state, 0); return 0;
        case LB_ADDSTRING:
            if (!lparam) return LB_ERR;
            if (state->rows.size() >= static_cast<size_t>((std::numeric_limits<int>::max)())) return LB_ERRSPACE;
            state->rows.push_back({reinterpret_cast<const wchar_t*>(lparam), false});
            if (state->caret < 0) state->caret = state->anchor = 0;
            update_scroll(*state); InvalidateRect(window, nullptr, FALSE);
            return static_cast<LRESULT>(state->rows.size() - 1);
        case LB_GETCOUNT: return static_cast<LRESULT>(state->rows.size());
        case LB_GETTEXTLEN:
            return valid_index(*state, wparam) ? static_cast<LRESULT>(state->rows[wparam].text.size()) : LB_ERR;
        case LB_GETTEXT:
            if (!valid_index(*state, wparam) || !lparam) return LB_ERR;
            std::copy(state->rows[wparam].text.c_str(), state->rows[wparam].text.c_str() + state->rows[wparam].text.size() + 1,
                reinterpret_cast<wchar_t*>(lparam));
            return static_cast<LRESULT>(state->rows[wparam].text.size());
        case LB_SETSEL:
            if (lparam == -1) {
                for (auto& row : state->rows) row.selected = wparam != 0;
            } else {
                if (lparam < 0 || !valid_index(*state, static_cast<WPARAM>(lparam))) return LB_ERR;
                state->rows[static_cast<size_t>(lparam)].selected = wparam != 0;
            }
            InvalidateRect(window, nullptr, FALSE); return 0;
        case LB_GETSEL: return valid_index(*state, wparam) ? static_cast<LRESULT>(state->rows[wparam].selected) : LB_ERR;
        case LB_GETSELCOUNT:
            return std::count_if(state->rows.begin(), state->rows.end(), [](const Row& row) { return row.selected; });
        case LB_GETSELITEMS: {
            if (static_cast<INT_PTR>(wparam) < 0 || (wparam && !lparam)) return LB_ERR;
            auto* indices = reinterpret_cast<int*>(lparam);
            size_t copied = 0;
            for (size_t index = 0; index < state->rows.size() && copied < wparam; ++index)
                if (state->rows[index].selected) indices[copied++] = static_cast<int>(index);
            return static_cast<LRESULT>(copied);
        }
        case LB_SETCARETINDEX:
            if (!valid_index(*state, wparam)) return LB_ERR;
            state->caret = static_cast<int>(wparam); ensure_visible(*state, state->caret, lparam != 0);
            InvalidateRect(window, nullptr, FALSE); return 0;
        case LB_GETCARETINDEX: return state->caret;
        case LB_SETANCHORINDEX:
            if (!valid_index(*state, wparam)) return LB_ERR;
            state->anchor = static_cast<int>(wparam); return 0;
        case LB_GETANCHORINDEX: return state->anchor;
        case LB_ITEMFROMPOINT: return hit_test(*state, lparam);
        case LB_GETITEMRECT:
            if (!valid_index(*state, wparam) || !lparam) return LB_ERR;
            *reinterpret_cast<RECT*>(lparam) = item_rect(*state, wparam); return 0;
        case LB_SETITEMHEIGHT: {
            if (lparam <= 0 || lparam > (std::numeric_limits<int>::max)()) return LB_ERR;
            const auto first = state->offset / state->item_height;
            const auto fraction = state->offset % state->item_height;
            const auto height = static_cast<int>(lparam);
            state->offset = first * height + fraction * height / state->item_height;
            state->item_height = height;
            scroll_to(*state, state->offset); return 0;
        }
        case LB_GETITEMHEIGHT: return state->item_height;
        case LB_GETTOPINDEX: return state->offset / state->item_height;
        case LB_SETTOPINDEX:
            if (static_cast<INT_PTR>(wparam) < 0) return LB_ERR;
            scroll_to(*state, state->rows.empty() ? 0 : static_cast<std::int64_t>((std::min)(wparam,
                static_cast<WPARAM>(state->rows.size() - 1))) * state->item_height);
            return 0;
        case WM_SIZE: update_scroll(*state); InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_SETFONT: state->font = reinterpret_cast<HFONT>(wparam); if (lparam) InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_GETFONT: return reinterpret_cast<LRESULT>(state->font);
        case WM_SETFOCUS: state->focused = true; InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_KILLFOCUS: state->focused = false; InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_ENABLE: InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS;
        case WM_KEYDOWN: if (navigate(*state, wparam)) return 0; break;
        case WM_LBUTTONDBLCLK:
            if (IsWindowEnabled(window) && !HIWORD(hit_test(*state, lparam))) notify_parent(*state, LBN_DBLCLK);
            return 0;
        case WM_MOUSEWHEEL: {
            if (!IsWindowEnabled(window)) return 0;
            state->wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
            const int notches = state->wheel_remainder / WHEEL_DELTA;
            state->wheel_remainder %= WHEEL_DELTA;
            UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            const long double distance = lines == WHEEL_PAGESCROLL ? client_height(window) :
                static_cast<long double>(lines) * state->item_height;
            if (notches && lines) scroll_by(*state, -static_cast<long double>(notches) * distance);
            return 0;
        }
        case WM_VSCROLL: {
            if (!IsWindowEnabled(window)) return 0;
            switch (LOWORD(wparam)) {
            case SB_TOP: scroll_to(*state, 0); break;
            case SB_BOTTOM: scroll_to(*state, max_offset(*state)); break;
            case SB_LINEUP: scroll_by(*state, -static_cast<long double>(state->item_height)); break;
            case SB_LINEDOWN: scroll_by(*state, state->item_height); break;
            case SB_PAGEUP: scroll_by(*state, -static_cast<long double>(client_height(window))); break;
            case SB_PAGEDOWN: scroll_by(*state, client_height(window)); break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: {
                SCROLLINFO info{sizeof(info)}; info.fMask = SIF_ALL;
                if (GetScrollInfo(window, SB_VERT, &info)) {
                    const auto bar_maximum = (std::max)(0, info.nMax - static_cast<int>(info.nPage) + 1);
                    const auto maximum = max_offset(*state);
                    const auto target = bar_maximum ? static_cast<std::int64_t>(
                        static_cast<long double>(info.nTrackPos) * maximum / bar_maximum) : 0;
                    scroll_to(*state, target);
                }
                break;
            }
            default: break;
            }
            return 0;
        }
        case WM_ERASEBKGND: return TRUE;
        case WM_PRINTCLIENT: paint(*state, reinterpret_cast<HDC>(wparam)); return 0;
        case WM_PAINT: {
            PAINTSTRUCT drawing{}; const auto dc = BeginPaint(window, &drawing);
            paint(*state, dc); EndPaint(window, &drawing); return 0;
        }
        default: break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return message == LB_ADDSTRING ? LB_ERRSPACE : 0;
    } catch (...) {
        SetLastError(ERROR_INVALID_DATA);
        return 0;
    }
}
} // namespace

HWND create_thumbnail_list(HWND parent, int control_id, HINSTANCE instance) {
    if (!IsWindow(parent)) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return nullptr; }
    if (GetWindowThreadProcessId(parent, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_THREAD_ID); return nullptr;
    }
    if (!instance) instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW klass{sizeof(klass)};
    if (!GetClassInfoExW(instance, kClassName, &klass)) {
        klass.style = CS_DBLCLKS;
        klass.hInstance = instance;
        klass.lpfnWndProc = thumbnail_proc;
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        klass.lpszClassName = kClassName;
        if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    } else if (klass.lpfnWndProc != thumbnail_proc) {
        SetLastError(ERROR_CLASS_ALREADY_EXISTS); return nullptr;
    }
    return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), instance, nullptr);
}
} // namespace xfx
