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
namespace {

struct MenuItem {
    HMENU menu{};
    UINT position = 0;
    MENUITEMINFOW original{sizeof(MENUITEMINFOW)};
    std::wstring label;
    bool changed = false;
};
struct MenuBackground { HMENU menu{}; HBRUSH original{}; bool changed = false; };

class PopupSession {
public:
    PopupSession(HWND owner, const ThemeColors& colors, HFONT font) : owner_(owner), colors_(colors), font_(font) {
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
    }
    ~PopupSession() {
        // Pointers remain alive until every modified menu item has been restored.
        for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
            if (!(*it)->changed || !IsMenu((*it)->menu)) continue;
            auto original = (*it)->original;
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
    }
    PopupSession(const PopupSession&) = delete;
    PopupSession& operator=(const PopupSession&) = delete;

    void prepare(HMENU menu) {
        collect(menu);
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

private:
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
    HFONT item_font(const MenuItem& item) const {
        return (item.original.fState & MFS_DEFAULT) && bold_font_ ? bold_font_ : font_;
    }
    MenuItem* lookup(ULONG_PTR data) const {
        // Never dereference untrusted dwItemData from another owner-drawn control.
        for (const auto& item : items_) if (reinterpret_cast<ULONG_PTR>(item.get()) == data) return item.get();
        return nullptr;
    }
    void collect(HMENU menu) {
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
        if (!dc) { result.itemWidth = static_cast<UINT>(scale(180)); result.itemHeight = static_cast<UINT>(scale(28)); return; }
        const HGDIOBJ previous = SelectObject(dc, item_font(item));
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        const auto tab = item.label.find(L'\t');
        const std::wstring_view label(item.label);
        const std::wstring_view name = label.substr(0, tab);
        const std::wstring_view shortcut = tab == std::wstring::npos ? std::wstring_view{} : label.substr(tab + 1);
        RECT name_size{}, shortcut_size{};
        DrawTextW(dc, name.data(), static_cast<int>(name.size()), &name_size, DT_CALCRECT | DT_SINGLELINE);
        DrawTextW(dc, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        result.itemWidth = static_cast<UINT>(name_size.right + shortcut_size.right + scale(shortcut.empty() ? 48 : 72));
        result.itemHeight = static_cast<UINT>((std::max)(metrics.tmHeight, name_size.bottom) + scale(10));
        SelectObject(dc, previous); ReleaseDC(owner_, dc);
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
        const UINT prefix = (draw.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0;
        if (!shortcut.empty()) {
            RECT shortcut_size{};
            DrawTextW(draw.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            RECT shortcut_rect = text;
            DrawTextW(draw.hDC, shortcut.data(), static_cast<int>(shortcut.size()), &shortcut_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            text.right -= shortcut_size.right + scale(24);
        }
        DrawTextW(draw.hDC, name.data(), static_cast<int>(name.size()), &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | prefix);
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
        if (matches.size() == 1) return MAKELRESULT(matches.front(), MNC_EXECUTE);
        const auto next = std::find_if(matches.begin(), matches.end(), [&](UINT position) { return static_cast<int>(position) > highlighted; });
        return MAKELRESULT(next == matches.end() ? matches.front() : *next, MNC_SELECT);
    }
    static LRESULT CALLBACK subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<PopupSession*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, subclass_proc, id); self->owner_ = nullptr;
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        try {
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
    } catch (const std::bad_alloc&) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
      catch (...) { SetLastError(ERROR_GEN_FAILURE); return 0; }
}
} // namespace xfx
