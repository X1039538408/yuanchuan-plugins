#include "themed_menu.h"
#include <commctrl.h>
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xfx {
namespace menu_action_detail {
Layout layout(RECT row, int dpi) {
    const auto scale = [dpi](int n) { return MulDiv(n, dpi > 0 ? dpi : 96, 96); };
    Layout result;
    result.label = row;
    result.label.left = (std::min)(row.right, row.left + scale(28));
    result.label.right = result.label.left;
    if (row.right - row.left < scale(136) || row.bottom - row.top < scale(12)) return result;
    const LONG right = row.right - scale(8), width = scale(28);
    for (int i = 0; i < 3; ++i)
        result.cells[i] = {right - (3 - i) * width, row.top + scale(2),
                           right - (2 - i) * width, row.bottom - scale(2)};
    result.label.right = (std::max)(result.label.left, result.cells[0].left - scale(8));
    return result;
}
int hit_test(const Layout& geometry, POINT point) {
    for (int i = 0; i < 3; ++i) if (PtInRect(&geometry.cells[i], point)) return i;
    return -1;
}
int next_enabled(const std::array<MenuAction, 3>& actions, int current, int direction) {
    const int step = direction < 0 ? -1 : 1;
    int cell = current >= 0 && current < 3 ? current : (step > 0 ? 2 : 0);
    for (int count = 0; count < 3; ++count) {
        cell = (cell + step + 3) % 3;
        if (actions[cell].enabled) return cell;
    }
    return -1;
}
UINT released_command(const std::array<MenuAction, 3>& actions, int pressed, int released) {
    return pressed >= 0 && pressed < 3 && pressed == released && actions[pressed].enabled ? actions[pressed].command : 0;
}
} // namespace menu_action_detail
namespace {

struct MenuItem {
    HMENU menu{};
    UINT position = 0;
    MENUITEMINFOW original{sizeof(MENUITEMINFOW)};
    std::wstring label;
    bool changed = false;
    const MenuActionRow* actions = nullptr;
};
struct MenuBackground { HMENU menu{}; HBRUSH original{}; bool changed = false; };

class PopupSession {
public:
    PopupSession(HWND owner, const ThemeColors& colors, HFONT font, const MenuActionRows& actions = {}, UINT flags = 0)
        : actions_(actions), input_flags_(flags), owner_(owner), colors_(colors), font_(font) {
        background_ = CreateSolidBrush(colors.surface);
        if (!background_) throw std::runtime_error("Cannot create menu background");
        if (!font_) {
            NONCLIENTMETRICSW metrics{sizeof(metrics)};
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
                font_ = CreateFontIndirectW(&metrics.lfMenuFont);
                owns_font_ = font_ != nullptr;
            }
            if (!font_) font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        LOGFONTW description{};
        if (GetObjectW(font_, sizeof(description), &description)) {
            description.lfWeight = FW_BOLD;
            bold_font_ = CreateFontIndirectW(&description);
        }
        const HDC dc = GetDC(owner_);
        if (dc) { dpi_ = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(owner_, dc); }
        if (dpi_ <= 0) dpi_ = 96;
        previous_ = top_; top_ = this;
    }
    ~PopupSession() {
        // Pop even actionless sessions: an outer hook must never handle an inner
        // menu loop. Hook dispatch always consults exactly the top session.
        if (hook_user_ && --hook_users_ == 0) {
            UnhookWindowsHookEx(filter_hook_); filter_hook_ = nullptr;
        }
        if (tooltip_ && IsWindow(tooltip_)) DestroyWindow(tooltip_);
        // Pointers remain alive until every modified menu item has been restored.
        for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
            if (!(*it)->changed || !IsMenu((*it)->menu)) continue;
            auto original = (*it)->original;
            // MFS_HILITE is controlled by HiliteMenuItem, not reliably reset by
            // MIIM_STATE after a manually highlighted action cell.
            if (owner_) HiliteMenuItem(owner_, (*it)->menu, (*it)->position,
                           MF_BYPOSITION | ((original.fState & MFS_HILITE) ? MF_HILITE : MF_UNHILITE));
            original.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_DATA;
            SetMenuItemInfoW((*it)->menu, (*it)->position, TRUE, &original);
        }
        for (auto it = backgrounds_.rbegin(); it != backgrounds_.rend(); ++it) {
            if (!it->changed || !IsMenu(it->menu)) continue;
            MENUINFO info{sizeof(info)}; info.fMask = MIM_BACKGROUND; info.hbrBack = it->original;
            SetMenuInfo(it->menu, &info);
        }
        if (owner_ && subclassed_ && IsWindow(owner_))
            RemoveWindowSubclass(owner_, subclass_proc, reinterpret_cast<UINT_PTR>(this));
        if (bold_font_) DeleteObject(bold_font_);
        if (owns_font_) DeleteObject(font_);
        if (background_) DeleteObject(background_);
        top_ = previous_;
    }
    PopupSession(const PopupSession&) = delete;
    PopupSession& operator=(const PopupSession&) = delete;

    void prepare(HMENU menu) {
        collect(menu);
        validate_actions();
        visible_.reserve(backgrounds_.size());
        if (!actions_.empty()) {
            if (!filter_hook_) filter_hook_ = SetWindowsHookExW(WH_MSGFILTER, filter_proc, nullptr, GetCurrentThreadId());
            if (!filter_hook_) throw std::runtime_error("Cannot hook menu input");
            ++hook_users_; hook_user_ = true;
            tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!tooltip_) throw std::runtime_error("Cannot create action tooltip");
            // WindowFromPoint explicitly ignores disabled windows. Transparent
            // painting/tool forwarding alone does not provide that guarantee;
            // disabling this display-only tracking tip keeps lower rows hittable.
            EnableWindow(tooltip_, FALSE);
            TOOLINFOW tool{TTTOOLINFOW_V2_SIZE};
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT; tool.hwnd = owner_; tool.uId = reinterpret_cast<UINT_PTR>(this);
            tool.lpszText = const_cast<wchar_t*>(L"");
            if (!SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)))
                throw std::runtime_error("Cannot initialize action tooltip");
            SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
            SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, scale(360));
        }
        if (!SetWindowSubclass(owner_, subclass_proc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Cannot subclass menu owner");
        subclassed_ = true;
        for (auto& background : backgrounds_) {
            MENUINFO info{sizeof(info)}; info.fMask = MIM_BACKGROUND; info.hbrBack = background_;
            if (!SetMenuInfo(background.menu, &info)) throw std::runtime_error("Cannot theme menu background");
            background.changed = true;
        }
        for (auto& item : items_) {
            MENUITEMINFOW themed{sizeof(themed)};
            themed.fMask = MIIM_FTYPE | MIIM_DATA;
            themed.fType = item->original.fType | MFT_OWNERDRAW;
            themed.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
            if (!SetMenuItemInfoW(item->menu, item->position, TRUE, &themed))
                throw std::runtime_error("Cannot theme menu item");
            item->changed = true;
        }
    }

    UINT chosen() const { return chosen_; }

private:
    inline static thread_local PopupSession* top_ = nullptr;
    inline static thread_local HHOOK filter_hook_ = nullptr;
    inline static thread_local unsigned hook_users_ = 0;
    PopupSession* previous_ = nullptr;
    MenuActionRows actions_;
    UINT input_flags_ = 0, press_up_ = 0;
    std::vector<HMENU> visible_;
    MenuItem* hovered_ = nullptr;
    MenuItem* focused_ = nullptr;
    MenuItem* pressed_ = nullptr;
    int hover_cell_ = -1, focus_cell_ = -1, press_cell_ = -1;
    UINT chosen_ = 0;
    bool hook_user_ = false;
    HWND tooltip_ = nullptr;
    std::wstring tooltip_text_;
    HWND owner_{};
    ThemeColors colors_{};
    HFONT font_{}, bold_font_{};
    HBRUSH background_{};
    int dpi_ = 96;
    bool owns_font_ = false, subclassed_ = false;
    std::set<HMENU> visited_;
    std::vector<std::unique_ptr<MenuItem>> items_;
    std::vector<MenuBackground> backgrounds_;

    int scale(int value) const { return MulDiv(value, dpi_, 96); }
    void validate_actions() {
        std::set<std::pair<HMENU, UINT>> rows;
        std::set<UINT> commands;
        for (const auto& row : actions_) {
            if (!rows.insert({row.menu, row.position}).second) throw std::invalid_argument("Duplicate action row");
            MenuItem* found = nullptr;
            for (const auto& item : items_) if (item->menu == row.menu && item->position == row.position) found = item.get();
            if (!found || found->original.hSubMenu || (found->original.fType & MFT_SEPARATOR))
                throw std::invalid_argument("Action row must be a leaf in this menu tree");
            for (const auto& action : row.actions) {
                if (!action.command || !commands.insert(action.command).second || action.label.empty() ||
                    (action.icon != MenuActionIcon::Bind && action.icon != MenuActionIcon::Edit && action.icon != MenuActionIcon::Clear))
                    throw std::invalid_argument("Invalid action metadata");
                for (const auto& item : items_) if (!item->original.hSubMenu && item->original.wID == action.command)
                    throw std::invalid_argument("Action command collides with native item");
            }
            found->actions = &row;
        }
    }
    bool enabled(const MenuItem& item, int cell) const {
        return item.actions && cell >= 0 && cell < 3 && !(item.original.fState & MFS_DISABLED) && item.actions->actions[cell].enabled;
    }
    static bool menu_window(HWND window) {
        wchar_t name[32]{};
        return window && GetClassNameW(window, name, 32) && lstrcmpW(name, L"#32768") == 0;
    }
    static bool window_has_menu(HWND window, HMENU menu) {
        MENUBARINFO info{sizeof(info)};
        return menu_window(window) && GetMenuBarInfo(window, OBJID_CLIENT, 0, &info) && info.hMenu == menu;
    }
    void refresh_visible() {
        // TPM_NONOTIFY suppresses WM_INITMENUPOPUP as well as command notices on
        // supported Windows versions. Discover actual native popup windows; do
        // not infer visibility from cached item rectangles or notifications.
        visible_.clear();
        for (const auto& menu : backgrounds_) {
            struct Probe { HMENU menu; bool visible; } probe{menu.menu, false};
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
                auto* probe = reinterpret_cast<Probe*>(data);
                if (IsWindowVisible(window) && window_has_menu(window, probe->menu)) { probe->visible = true; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&probe));
            // Collection order is parent before child; the last visible menu
            // therefore owns keyboard input, independent of window z-order.
            if (probe.visible) visible_.push_back(menu.menu);
        }
    }
    bool visible_rect(const MenuItem& item, RECT& rect) const {
        if (std::find(visible_.begin(), visible_.end(), item.menu) == visible_.end() ||
            !GetMenuItemRect(nullptr, item.menu, item.position, &rect)) return false;
        // GetMenuItemRect can return stale/cached geometry for unopened menus.
        // The centre must actually belong to a visible native popup window.
        return window_has_menu(WindowFromPoint({(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}), item.menu);
    }
    MenuItem* hit_row(POINT point, int& cell) const {
        cell = -1;
        if (!menu_window(WindowFromPoint(point))) return nullptr;
        for (auto menu = visible_.rbegin(); menu != visible_.rend(); ++menu) {
            for (const auto& item : items_) {
                if (!item->actions || item->menu != *menu) continue;
                RECT rect{};
                if (visible_rect(*item, rect) && PtInRect(&rect, point) && window_has_menu(WindowFromPoint(point), item->menu)) {
                    cell = menu_action_detail::hit_test(menu_action_detail::layout(rect, dpi_), point);
                    return item.get();
                }
            }
        }
        return nullptr;
    }
    MenuItem* highlighted() const {
        if (visible_.empty()) return nullptr;
        // Only the deepest open menu receives keyboard input. A highlighted
        // parent must not steal Right/Enter from an opening child popup.
        for (const auto& item : items_) {
            if (item->menu != visible_.back()) continue;
            MENUITEMINFOW now{sizeof(now)}; now.fMask = MIIM_STATE;
            if (GetMenuItemInfoW(item->menu, item->position, TRUE, &now) && (now.fState & MFS_HILITE))
                return item->actions ? item.get() : nullptr;
        }
        return nullptr;
    }
    void redraw(MenuItem* item) const {
        if (!item) return;
        RECT rect{};
        if (!visible_rect(*item, rect)) return;
        const HWND window = WindowFromPoint({(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
        if (menu_window(window)) RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    void tooltip(MenuItem* item, int cell) {
        if (!tooltip_ || !IsWindow(tooltip_)) return;
        TOOLINFOW tool{TTTOOLINFOW_V2_SIZE};
        tool.hwnd = owner_; tool.uId = reinterpret_cast<UINT_PTR>(this);
        RECT rect{};
        if (!item || cell < 0 || !visible_rect(*item, rect)) {
            SendMessageW(tooltip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool)); return;
        }
        tooltip_text_ = item->actions->actions[cell].label;
        if (!enabled(*item, cell)) tooltip_text_ += L"（不可用）";
        tool.lpszText = tooltip_text_.data();
        SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        const auto geometry = menu_action_detail::layout(rect, dpi_);
        SendMessageW(tooltip_, TTM_TRACKPOSITION, 0, MAKELPARAM(geometry.cells[cell].left, rect.bottom + scale(5)));
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    }
    void clear_focus() {
        auto* old = focused_; focused_ = nullptr; focus_cell_ = -1; redraw(old);
    }
    void choose(UINT command) {
        if (!command || !owner_ || !IsWindow(owner_)) return;
        chosen_ = command;
        tooltip(nullptr, -1);
        // No owner callback/WM_COMMAND here: the API returns this after cleanup.
        // A failed request to leave the loop has not completed an activation.
        if (!EndMenu()) chosen_ = 0;
    }
    bool filter(MSG& message) {
        if (!owner_ || actions_.empty()) return false;
        refresh_visible();
        const UINT id = message.message;
        if (id == WM_MOUSEMOVE || id == WM_LBUTTONDOWN || id == WM_LBUTTONUP || id == WM_LBUTTONDBLCLK ||
            id == WM_RBUTTONDOWN || id == WM_RBUTTONUP || id == WM_RBUTTONDBLCLK) {
            int cell = -1;
            auto* item = hit_row(message.pt, cell);
            if (id == WM_MOUSEMOVE) {
                if (hovered_ != item || hover_cell_ != cell) {
                    auto* old = hovered_; hovered_ = item; hover_cell_ = cell;
                    clear_focus(); redraw(old); redraw(item); tooltip(item, cell);
                }
                return false; // Native menu retains row hover/navigation.
            }
            const bool down = id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK || id == WM_RBUTTONDOWN || id == WM_RBUTTONDBLCLK;
            if (down) {
                const bool left = id == WM_LBUTTONDOWN || id == WM_LBUTTONDBLCLK;
                pressed_ = item;
                press_cell_ = item && enabled(*item, cell) && (left || (input_flags_ & TPM_RIGHTBUTTON)) ? cell : -1;
                press_up_ = left ? WM_LBUTTONUP : WM_RBUTTONUP;
                clear_focus();
                if (item) {
                    HiliteMenuItem(owner_, item->menu, item->position, MF_BYPOSITION | MF_HILITE);
                    redraw(item); tooltip(item, cell);
                }
                return item != nullptr;
            }
            auto* pressed = pressed_; const int pressed_cell = press_cell_;
            const bool same_button = press_up_ == id;
            pressed_ = nullptr; press_cell_ = -1; press_up_ = 0;
            if (same_button && item && item == pressed && enabled(*item, cell))
                choose(menu_action_detail::released_command(item->actions->actions, pressed_cell, cell));
            redraw(pressed);
            return item != nullptr || pressed != nullptr; // Label/cross-row release is inert, not native execute.
        }
        if (id == WM_KEYDOWN || id == WM_SYSKEYDOWN) {
            auto* item = highlighted();
            if (focused_ != item) clear_focus();
            pressed_ = nullptr; press_cell_ = -1;
            switch (message.wParam) {
            case VK_RIGHT:
            case VK_LEFT:
                if (!item || (message.wParam == VK_LEFT && focused_ != item)) return false;
                if (message.wParam == VK_LEFT && focus_cell_ ==
                    menu_action_detail::next_enabled(item->actions->actions, -1, 1)) {
                    // First Left leaves the strip; a subsequent Left is native
                    // parent-menu navigation, rather than trapping focus here.
                    clear_focus(); hovered_ = nullptr; hover_cell_ = -1;
                    tooltip(nullptr, -1); return true;
                }
                if (!(item->original.fState & MFS_DISABLED)) {
                    focused_ = item;
                    focus_cell_ = menu_action_detail::next_enabled(item->actions->actions, focus_cell_, message.wParam == VK_RIGHT ? 1 : -1);
                    hovered_ = nullptr; hover_cell_ = -1; redraw(item); tooltip(item, focus_cell_);
                }
                return true;
            case VK_RETURN:
            case VK_SPACE:
                if (!item) return false;
                if (focused_ == item && enabled(*item, focus_cell_)) choose(item->actions->actions[focus_cell_].command);
                return true; // Enter on a row label must never execute its inert ID.
            case VK_UP: case VK_DOWN: case VK_HOME: case VK_END: case VK_ESCAPE:
                clear_focus(); hovered_ = nullptr; hover_cell_ = -1; tooltip(nullptr, -1); return false;
            default: break;
            }
        }
        if ((id == WM_CHAR || id == WM_SYSCHAR) && (message.wParam == L'\r' || message.wParam == L' ') && highlighted()) return true;
        return false;
    }
    static LRESULT CALLBACK filter_proc(int code, WPARAM wparam, LPARAM lparam) {
        try {
            if (code == MSGF_MENU && lparam && top_ && top_->filter(*reinterpret_cast<MSG*>(lparam))) return 1;
        } catch (...) { // Never unwind across User32; fail closed without executing.
            if (top_) { top_->chosen_ = 0; EndMenu(); }
        }
        return CallNextHookEx(filter_hook_, code, wparam, lparam);
    }
    HFONT item_font(const MenuItem& item) const {
        return (item.original.fState & MFS_DEFAULT) && bold_font_ ? bold_font_ : font_;
    }
    MenuItem* lookup(ULONG_PTR data) const {
        // Never dereference untrusted dwItemData from another owner-drawn control.
        for (const auto& item : items_) if (reinterpret_cast<ULONG_PTR>(item.get()) == data) return item.get();
        return nullptr;
    }
    void collect(HMENU menu) {
        // Never stack owner-draw data over an active session for this same tree:
        // the outer pointers/flags must remain owned by exactly one session.
        // Check every child too, including a new wrapper sharing an active child.
        for (auto* ancestor = previous_; ancestor; ancestor = ancestor->previous_)
            if (ancestor->visited_.count(menu)) throw std::invalid_argument("Menu tree is already tracked by an active session");
        if (!visited_.insert(menu).second) return;
        MENUINFO info{sizeof(info)}; info.fMask = MIM_BACKGROUND;
        if (!GetMenuInfo(menu, &info)) throw std::runtime_error("Cannot inspect menu background");
        backgrounds_.push_back({menu, info.hbrBack, false});
        const int count = GetMenuItemCount(menu);
        if (count < 0) throw std::runtime_error("Cannot inspect menu");
        for (int position = 0; position < count; ++position) {
            auto item = std::make_unique<MenuItem>();
            item->menu = menu; item->position = static_cast<UINT>(position);
            item->original.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(menu, item->position, TRUE, &item->original)) throw std::runtime_error("Cannot inspect menu item");
            MENUITEMINFOW text{sizeof(text)}; text.fMask = MIIM_STRING;
            if (!GetMenuItemInfoW(menu, item->position, TRUE, &text)) throw std::runtime_error("Cannot inspect menu text");
            item->label.resize(static_cast<size_t>(text.cch) + 1, L'\0');
            text.dwTypeData = item->label.data(); ++text.cch;
            if (!GetMenuItemInfoW(menu, item->position, TRUE, &text)) throw std::runtime_error("Cannot read menu text");
            item->label.resize(text.cch);
            const HMENU child = item->original.hSubMenu;
            items_.push_back(std::move(item));
            if (child) collect(child);
        }
    }
    void measure(const MenuItem& item, MEASUREITEMSTRUCT& result) const {
        if (item.original.fType & MFT_SEPARATOR) {
            result.itemWidth = static_cast<UINT>(scale(48)); result.itemHeight = static_cast<UINT>(scale(9)); return;
        }
        const HDC dc = GetDC(owner_);
        if (!dc) { result.itemWidth = static_cast<UINT>(scale(item.actions ? 360 : 180)); result.itemHeight = static_cast<UINT>(scale(28)); return; }
        const HGDIOBJ previous = SelectObject(dc, item_font(item));
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        const auto tab = item.label.find(L'\t');
        const std::wstring_view label(item.label);
        const std::wstring_view name = label.substr(0, tab);
        const std::wstring_view shortcut = tab == std::wstring::npos ? std::wstring_view{} : label.substr(tab + 1);
        RECT name_size{}, shortcut_size{};
        DrawTextW(dc, name.data(), static_cast<int>(name.size()), &name_size, DT_CALCRECT | DT_SINGLELINE);
        DrawTextW(dc, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        result.itemWidth = static_cast<UINT>(item.actions ?
            (std::max)(scale(256), static_cast<int>((std::min)(name_size.right, static_cast<LONG>(scale(320)))) + scale(128)) :
            name_size.right + shortcut_size.right + scale(shortcut.empty() ? 48 : 72));
        result.itemHeight = static_cast<UINT>((std::max)(metrics.tmHeight, name_size.bottom) + scale(10));
        if (item.actions) result.itemHeight = (std::max)(result.itemHeight, static_cast<UINT>(scale(28)));
        SelectObject(dc, previous); ReleaseDC(owner_, dc);
    }
    void draw_icon(HDC dc, RECT rect, MenuActionIcon icon, COLORREF color) const {
        const HPEN pen = CreatePen(PS_SOLID, (std::max)(1, scale(1)), color);
        if (!pen) return;
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
        const auto line = [dc](int x1, int y1, int x2, int y2) { MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); };
        switch (icon) {
        case MenuActionIcon::Bind:
            RoundRect(dc, x - scale(8), y - scale(4), x + scale(2), y + scale(4), scale(4), scale(4));
            RoundRect(dc, x - scale(2), y - scale(4), x + scale(8), y + scale(4), scale(4), scale(4));
            line(x - scale(4), y, x + scale(4), y); break;
        case MenuActionIcon::Edit: {
            POINT pencil[] = {{x - scale(6), y + scale(3)}, {x + scale(3), y - scale(6)},
                {x + scale(6), y - scale(3)}, {x - scale(3), y + scale(6)}, {x - scale(7), y + scale(7)}};
            Polygon(dc, pencil, 5);
            line(x + scale(1), y - scale(4), x + scale(4), y - scale(1)); break;
        }
        case MenuActionIcon::Clear:
            line(x - scale(7), y - scale(5), x + scale(7), y - scale(5));
            Rectangle(dc, x - scale(5), y - scale(3), x + scale(6), y + scale(7));
            line(x - scale(2), y - scale(7), x + scale(3), y - scale(7));
            line(x - scale(2), y - scale(1), x - scale(2), y + scale(4));
            line(x + scale(2), y - scale(1), x + scale(2), y + scale(4)); break;
        }
        SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
    }
    void draw_actions(const MenuItem& item, const DRAWITEMSTRUCT& draw) const {
        const auto geometry = menu_action_detail::layout(draw.rcItem, dpi_);
        for (int cell = 0; cell < 3; ++cell) {
            const RECT rect = geometry.cells[cell];
            if (IsRectEmpty(&rect)) continue;
            const bool active = enabled(item, cell);
            const bool focus = active && focused_ == &item && focus_cell_ == cell;
            const bool hover = active && hovered_ == &item && hover_cell_ == cell;
            if (focus || hover) {
                SetDCBrushColor(draw.hDC, colors_.selected);
                FillRect(draw.hDC, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                if (hover && !focus) {
                    RECT outline = rect; InflateRect(&outline, -scale(2), -scale(2));
                    SetDCBrushColor(draw.hDC, colors_.divider);
                    FrameRect(draw.hDC, &outline, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                }
            }
            draw_icon(draw.hDC, rect, item.actions->actions[cell].icon,
                      !active ? colors_.disabled : ((draw.itemState & ODS_SELECTED) || hover || focus ? colors_.selected_text : colors_.icon));
            if (focus) {
                RECT outline = rect; InflateRect(&outline, -scale(2), -scale(2));
                SetTextColor(draw.hDC, colors_.selected_text); DrawFocusRect(draw.hDC, &outline);
            }
        }
    }
    void draw(const MenuItem& item, const DRAWITEMSTRUCT& draw) const {
        const int saved = SaveDC(draw.hDC);
        if (!saved) return;
        IntersectClipRect(draw.hDC, draw.rcItem.left, draw.rcItem.top, draw.rcItem.right, draw.rcItem.bottom);
        const bool disabled = (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0 || (item.original.fState & MFS_DISABLED) != 0;
        const bool selected = (draw.itemState & ODS_SELECTED) != 0 && !disabled;
        SetDCBrushColor(draw.hDC, selected ? colors_.selected : colors_.surface);
        FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (item.original.fType & MFT_SEPARATOR) {
            RECT line{draw.rcItem.left + scale(8), (draw.rcItem.top + draw.rcItem.bottom) / 2,
                      draw.rcItem.right - scale(8), (draw.rcItem.top + draw.rcItem.bottom) / 2 + 1};
            SetDCBrushColor(draw.hDC, colors_.divider);
            FillRect(draw.hDC, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            RestoreDC(draw.hDC, saved); return;
        }
        const COLORREF foreground = disabled ? colors_.disabled : (selected ? colors_.selected_text : colors_.text);
        SelectObject(draw.hDC, item_font(item)); SetBkMode(draw.hDC, TRANSPARENT); SetTextColor(draw.hDC, foreground);
        const auto tab = item.label.find(L'\t');
        const std::wstring_view label(item.label);
        const std::wstring_view name = label.substr(0, tab);
        const std::wstring_view shortcut = tab == std::wstring::npos ? std::wstring_view{} : label.substr(tab + 1);
        RECT text = draw.rcItem; text.left += scale(28); text.right -= scale(20);
        if (item.actions) text = menu_action_detail::layout(draw.rcItem, dpi_).label;
        const UINT prefix = (draw.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0;
        if (!item.actions && !shortcut.empty()) {
            RECT shortcut_size{};
            DrawTextW(draw.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            RECT shortcut_rect = text;
            DrawTextW(draw.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            text.right -= shortcut_size.right + scale(24);
        }
        DrawTextW(draw.hDC, name.data(), static_cast<int>(name.size()), &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | (item.actions ? DT_END_ELLIPSIS : 0) | prefix);
        if (item.actions) draw_actions(item, draw);
        if ((draw.itemState & ODS_CHECKED) || (item.original.fState & MFS_CHECKED)) {
            RECT mark = draw.rcItem; mark.left += scale(6); mark.right = mark.left + scale(16);
            if (item.original.fType & MFT_RADIOCHECK) {
                const int middle_y = (mark.top + mark.bottom) / 2;
                SelectObject(draw.hDC, GetStockObject(NULL_PEN)); SelectObject(draw.hDC, GetStockObject(DC_BRUSH));
                SetDCBrushColor(draw.hDC, foreground);
                Ellipse(draw.hDC, mark.left + scale(5), middle_y - scale(3), mark.left + scale(11), middle_y + scale(3));
            } else DrawTextW(draw.hDC, L"\u2713", 1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (item.original.hSubMenu) {
            const int x = draw.rcItem.right - scale(10), y = (draw.rcItem.top + draw.rcItem.bottom) / 2;
            POINT arrow[] = {{x - scale(2), y - scale(4)}, {x + scale(2), y}, {x - scale(2), y + scale(4)}};
            SelectObject(draw.hDC, GetStockObject(NULL_PEN)); SelectObject(draw.hDC, GetStockObject(DC_BRUSH));
            SetDCBrushColor(draw.hDC, foreground); Polygon(draw.hDC, arrow, 3);
        }
        RestoreDC(draw.hDC, saved);
    }
    LRESULT mnemonic(HMENU menu, wchar_t key) const {
        CharLowerBuffW(&key, 1);
        std::vector<UINT> matches;
        int highlighted = -1;
        for (const auto& item : items_) {
            if (item->menu != menu) continue;
            MENUITEMINFOW now{sizeof(now)}; now.fMask = MIIM_STATE;
            if (!GetMenuItemInfoW(menu, item->position, TRUE, &now)) continue;
            if (now.fState & MFS_HILITE) highlighted = static_cast<int>(item->position);
            if ((now.fState & MFS_DISABLED) || (item->original.fType & MFT_SEPARATOR)) continue;
            for (size_t i = 0; i + 1 < item->label.size() && item->label[i] != L'\t'; ++i) {
                if (item->label[i] != L'&') continue;
                if (item->label[i + 1] == L'&') { ++i; continue; }
                wchar_t candidate = item->label[i + 1]; CharLowerBuffW(&candidate, 1);
                if (candidate == key) matches.push_back(item->position);
                break;
            }
        }
        if (matches.empty()) return MAKELRESULT(0, MNC_IGNORE);
        if (matches.size() == 1) {
            for (const auto& item : items_) if (item->menu == menu && item->position == matches.front())
                return MAKELRESULT(matches.front(), item->actions ? MNC_SELECT : MNC_EXECUTE);
        }
        const auto next = std::find_if(matches.begin(), matches.end(), [&](UINT position) { return static_cast<int>(position) > highlighted; });
        return MAKELRESULT(next == matches.end() ? matches.front() : *next, MNC_SELECT);
    }
    static LRESULT CALLBACK subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<PopupSession*>(data);
        if (message == WM_DESTROY) {
            // HiliteMenuItem requires a live HWND; restore this read-only state
            // now, before WM_NCDESTROY invalidates the owner handle.
            for (const auto& item : self->items_) if (item->changed && IsMenu(item->menu))
                HiliteMenuItem(hwnd, item->menu, item->position,
                    MF_BYPOSITION | ((item->original.fState & MFS_HILITE) ? MF_HILITE : MF_UNHILITE));
            self->chosen_ = 0;
            if (top_ == self) EndMenu();
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, subclass_proc, id); self->owner_ = nullptr; self->chosen_ = 0;
            if (top_ == self) EndMenu();
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        try {
            if (top_ == self && message == WM_INITMENUPOPUP && self->visited_.count(reinterpret_cast<HMENU>(wparam))) {
                const auto menu = reinterpret_cast<HMENU>(wparam);
                self->visible_.erase(std::remove(self->visible_.begin(), self->visible_.end(), menu), self->visible_.end());
                self->visible_.push_back(menu);
            } else if (top_ == self && message == WM_UNINITMENUPOPUP) {
                const auto menu = reinterpret_cast<HMENU>(wparam);
                self->visible_.erase(std::remove(self->visible_.begin(), self->visible_.end(), menu), self->visible_.end());
                self->clear_focus(); self->hovered_ = self->pressed_ = nullptr;
                self->hover_cell_ = self->press_cell_ = -1; self->tooltip(nullptr, -1);
            }
            if (message == WM_MEASUREITEM && lparam) {
                auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (measure->CtlType == ODT_MENU) if (auto* item = self->lookup(measure->itemData)) {
                    self->measure(*item, *measure); return TRUE;
                }
            } else if (message == WM_DRAWITEM && lparam) {
                const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if (draw->CtlType == ODT_MENU) if (auto* item = self->lookup(draw->itemData)) {
                    self->draw(*item, *draw); return TRUE;
                }
            } else if (message == WM_MENUCHAR && self->visited_.count(reinterpret_cast<HMENU>(lparam))) {
                return self->mnemonic(reinterpret_cast<HMENU>(lparam), LOWORD(wparam));
            }
        } catch (...) { /* Never unwind through User32/host window callbacks. */ }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
};

} // namespace

UINT track_themed_popup(HMENU menu, UINT flags, int x, int y, HWND owner,
                       const ThemeColors& colors, HFONT font) {
    if (!IsMenu(menu) || !IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_PARAMETER); return 0;
    }
    try {
        PopupSession session(owner, colors, font);
        session.prepare(menu);
        return static_cast<UINT>(TrackPopupMenuEx(menu, flags, x, y, owner, nullptr));
    } catch (const std::invalid_argument&) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
      catch (const std::bad_alloc&) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
      catch (...) { SetLastError(ERROR_GEN_FAILURE); return 0; }
}

UINT track_themed_action_popup(HMENU menu, UINT flags, int x, int y, HWND owner,
                              const ThemeColors& colors, HFONT font, const MenuActionRows& rows) {
    if (!IsMenu(menu) || !IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId() ||
        (flags & (TPM_RETURNCMD | TPM_NONOTIFY)) != (TPM_RETURNCMD | TPM_NONOTIFY)) {
        SetLastError(ERROR_INVALID_PARAMETER); return 0;
    }
    try {
        UINT result = 0;
        {
            PopupSession session(owner, colors, font, rows, flags);
            session.prepare(menu);
            const UINT native_result = static_cast<UINT>(TrackPopupMenuEx(menu, flags, x, y, owner, nullptr));
            result = session.chosen() ? session.chosen() : native_result;
            // Defense in depth: an inert action-row ID is never an API result,
            // including native activation paths not seen by our message filter.
            if (!session.chosen()) for (const auto& row : rows) {
                MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_ID;
                if (GetMenuItemInfoW(row.menu, row.position, TRUE, &item) && item.wID == result) result = 0;
            }
        }
        return result;
    } catch (const std::invalid_argument&) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
      catch (const std::bad_alloc&) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
      catch (...) { SetLastError(ERROR_GEN_FAILURE); return 0; }
}
} // namespace xfx
