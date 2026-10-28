#include <windows.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef XFX_THUMBNAIL_NATIVE_BASELINE
namespace xfx {
static HWND create_thumbnail_list(HWND parent, int id, HINSTANCE instance) {
    return CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
        LBS_EXTENDEDSEL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, 0, 220, 450, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}
}
#else
#include "thumbnail_list.h"
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct DrawRecord { UINT id, state, control; RECT row, clip; HFONT font; HWND child; };
struct ParentState {
    HBRUSH background = CreateSolidBrush(RGB(101, 102, 103));
    std::vector<DrawRecord> draws;
    int changes = 0, doubles = 0;
    HWND source = nullptr;
    bool destroy_during_draw = false, paint_past_row = false;
    ~ParentState() { DeleteObject(background); }
};
static LRESULT CALLBACK parent_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    }
    auto* state = reinterpret_cast<ParentState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_MEASUREITEM) {
        reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = 100; return TRUE;
    }
    if (state && message == WM_CTLCOLORLISTBOX) return reinterpret_cast<LRESULT>(state->background);
    if (state && message == WM_DRAWITEM) {
        const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw.CtlType != ODT_LISTBOX) return FALSE;
        RECT clip{}; GetClipBox(draw.hDC, &clip);
        state->draws.push_back({draw.itemID, draw.itemState, static_cast<UINT>(wparam),
            draw.rcItem, clip, static_cast<HFONT>(GetCurrentObject(draw.hDC, OBJ_FONT)), draw.hwndItem});
        SetDCBrushColor(draw.hDC, RGB(20 + draw.itemID, 40, 60));
        const RECT spill{0, -100, 230, 800};
        FillRect(draw.hDC, state->paint_past_row ? &spill : &draw.rcItem,
            static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (state->destroy_during_draw) DestroyWindow(draw.hwndItem);
        return TRUE;
    }
    if (state && message == WM_COMMAND) {
        state->source = reinterpret_cast<HWND>(lparam);
        if (HIWORD(wparam) == LBN_SELCHANGE) ++state->changes;
        if (HIWORD(wparam) == LBN_DBLCLK) ++state->doubles;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

class OwnedList {
public:
    ParentState state;
    HWND parent = nullptr, list = nullptr;
    explicit OwnedList(int rows = 0, int height = 450) {
        const auto instance = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"XFXThumbnailListTestParent", L"", WS_OVERLAPPED | WS_CLIPCHILDREN,
            0, 0, 250, 500, nullptr, nullptr, instance, &state);
        require(parent != nullptr, "cannot create owned test parent");
        list = xfx::create_thumbnail_list(parent, 101, instance);
        if (!list) { DestroyWindow(parent); parent = nullptr; throw std::runtime_error("cannot create thumbnail list"); }
        MoveWindow(list, 0, 0, 220, height, FALSE);
        for (int i = 0; i < rows; ++i) {
            const auto text = L"中文快照 " + std::to_wstring(i) + L".png";
            require(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())) == i,
                "cannot add thumbnail row");
        }
    }
    ~OwnedList() { if (parent) DestroyWindow(parent); }
    OwnedList(const OwnedList&) = delete;
    OwnedList& operator=(const OwnedList&) = delete;
};
static RECT row_rect(HWND list, int row) {
    RECT rect{};
    require(SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&rect)) == 0,
        "cannot query thumbnail row bounds");
    return rect;
}
class PaintSurface {
public:
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    PaintSurface() {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 240; info.bmiHeader.biHeight = -800;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(dc && bitmap, "cannot create owned test drawing surface");
        previous = SelectObject(dc, bitmap);
    }
    ~PaintSurface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    PaintSurface(const PaintSurface&) = delete;
    PaintSurface& operator=(const PaintSurface&) = delete;
    void paint(OwnedList& owned, UINT message = WM_PRINTCLIENT) {
        owned.state.draws.clear();
        SendMessageW(owned.list, message, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    }
};
static void check_draws(const OwnedList& owned, std::initializer_list<UINT> rows) {
    require(owned.state.draws.size() == rows.size(), "offscreen rows were drawn or visible rows omitted");
    size_t index = 0;
    for (const auto id : rows) {
        const auto& draw = owned.state.draws[index++];
        require(draw.id == id && draw.control == 101 && draw.child == owned.list,
            "owner-draw message identified the wrong thumbnail row/control");
    }
}
static std::vector<int> selected(HWND list) {
    const auto count = SendMessageW(list, LB_GETSELCOUNT, 0, 0);
    require(count >= 0, "cannot query selected count");
    std::vector<int> rows(static_cast<size_t>(count));
    if (count) require(SendMessageW(list, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(rows.data())) == count,
        "cannot query selected indices");
    return rows;
}

int wmain() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    int failures = 0;
    const auto run = [&](const char* name, const std::function<void()>& test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    try {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW klass{}; klass.hInstance = instance; klass.lpfnWndProc = parent_proc;
        klass.lpszClassName = L"XFXThumbnailListTestParent";
        require(RegisterClassW(&klass) != 0, "cannot register owned test parent");
        run("300/600 pixel rows and height validation", [] {
            OwnedList owned(1);
            require(SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 300) != LB_ERR,
                "300 pixel thumbnail rows were rejected");
            require(SendMessageW(owned.list, LB_GETITEMHEIGHT, 0, 0) == 300,
                "300 pixel thumbnail row height was truncated");
            require(SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 600) != LB_ERR,
                "600 pixel thumbnail rows were rejected");
            const auto row = row_rect(owned.list, 0);
            require(row.top == 0 && row.bottom == 600, "600 pixel thumbnail row bounds were truncated");
            require(SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 0) == LB_ERR &&
                SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, -1) == LB_ERR &&
                SendMessageW(owned.list, LB_GETITEMHEIGHT, 0, 0) == 600, "invalid height altered thumbnail layout");
        });
        run("selection messages, anchors and copied Chinese strings", [] {
            OwnedList owned(5);
            require(SendMessageW(owned.list, LB_GETCOUNT, 0, 0) == 5, "wrong item count");
            require(SendMessageW(owned.list, LB_SETSEL, TRUE, 3) == 0 &&
                SendMessageW(owned.list, LB_SETSEL, TRUE, 1) == 0, "programmatic selection failed");
            require(selected(owned.list) == std::vector<int>({1, 3}), "selected indices were not ascending");
            int one = -1;
            require(SendMessageW(owned.list, LB_GETSELITEMS, 1, reinterpret_cast<LPARAM>(&one)) == 1 && one == 1,
                "selected query ignored output capacity");
            require(SendMessageW(owned.list, LB_GETSEL, 1, 0) == 1 &&
                SendMessageW(owned.list, LB_GETSEL, 0, 0) == 0 &&
                SendMessageW(owned.list, LB_GETSEL, 5, 0) == LB_ERR, "selected query accepted invalid row");
            require(SendMessageW(owned.list, LB_SETANCHORINDEX, 3, 0) == 0 &&
                SendMessageW(owned.list, LB_GETANCHORINDEX, 0, 0) == 3, "selection anchor was lost");
            require(SendMessageW(owned.list, LB_SETCARETINDEX, 1, TRUE) == 0 &&
                SendMessageW(owned.list, LB_GETCARETINDEX, 0, 0) == 1, "caret index was lost");
            require(SendMessageW(owned.list, LB_SETSEL, FALSE, -1) == 0 && selected(owned.list).empty(),
                "clear-all selection failed");
            require(SendMessageW(owned.list, LB_SETSEL, TRUE, -1) == 0 && selected(owned.list).size() == 5,
                "select-all selection failed");
            require(owned.state.changes == 0, "LB_SETSEL emitted a user selection notification");
            std::wstring text = L"临时中文.png";
            require(SendMessageW(owned.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())) == 5,
                "appending a Chinese filename failed");
            text.assign(L"changed");
            wchar_t buffer[64]{};
            require(SendMessageW(owned.list, LB_GETTEXTLEN, 5, 0) == 8 &&
                SendMessageW(owned.list, LB_GETTEXT, 5, reinterpret_cast<LPARAM>(buffer)) == 8 &&
                std::wstring(buffer) == L"临时中文.png", "thumbnail strings were not copied as UTF-16");
            require(SendMessageW(owned.list, LB_SETSEL, TRUE, 6) == LB_ERR &&
                SendMessageW(owned.list, LB_SETANCHORINDEX, 6, 0) == LB_ERR &&
                SendMessageW(owned.list, LB_SETCARETINDEX, 6, 0) == LB_ERR, "invalid selection indices were accepted");
            SendMessageW(owned.list, LB_RESETCONTENT, 0, 0);
            require(SendMessageW(owned.list, LB_GETCOUNT, 0, 0) == 0 && selected(owned.list).empty() &&
                SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 0, "reset retained thumbnail contents/scroll");
        });
        run("partial rows owner-draw with local clipped rectangles", [] {
            OwnedList owned(6); PaintSurface surface;
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 300);
            SendMessageW(owned.list, LB_SETTOPINDEX, 1, 0);
            surface.paint(owned); check_draws(owned, {1, 2});
            require(owned.state.draws[0].row.top == 0 && owned.state.draws[0].row.bottom == 300 &&
                owned.state.draws[1].row.top == 300 && owned.state.draws[1].row.bottom == 600,
                "owner draw used screen coordinates or shortened partial item bounds");
            SendMessageW(owned.list, WM_VSCROLL, SB_PAGEDOWN, 0);
            surface.paint(owned); check_draws(owned, {2, 3});
            require(owned.state.draws[0].row.top == -150 && owned.state.draws[1].row.top == 150,
                "pixel page scrolling discarded partially visible rows");
            require(owned.state.draws[0].clip.top == 0 && owned.state.draws[0].clip.bottom == 150 &&
                owned.state.draws[1].clip.top == 150 && owned.state.draws[1].clip.bottom == 450,
                "thumbnail draw context was not clipped to the visible part of each row");
            require(GetPixel(surface.dc, 10, 0) == RGB(22, 40, 60) &&
                GetPixel(surface.dc, 10, 449) == RGB(23, 40, 60), "visible thumbnail pixels were not painted");
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 600);
            require(SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 2 && row_rect(owned.list, 2).top == -300,
                "thumbnail resizing lost the current partially visible row");
        });
        run("scrollbar, resize, wheel and caret visibility boundaries", [] {
            OwnedList owned(6);
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 600);
            SendMessageW(owned.list, WM_VSCROLL, SB_BOTTOM, 0);
            auto row = row_rect(owned.list, 5);
            require(row.top == -150 && row.bottom == 450 &&
                SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 5, "bottom scroll left blank space/truncated final row");
            SendMessageW(owned.list, WM_VSCROLL, SB_PAGEDOWN, 0);
            require(row_rect(owned.list, 5).top == -150, "scroll escaped lower content boundary");
            SCROLLINFO info{sizeof(info)}; info.fMask = SIF_ALL;
            require(GetScrollInfo(owned.list, SB_VERT, &info) && info.nMax == 3599 && info.nPage == 450 && info.nPos == 3150,
                "native scrollbar did not describe the full pixel extent");
            MoveWindow(owned.list, 0, 0, 220, 700, FALSE);
            require(row_rect(owned.list, 5).top == 100 && row_rect(owned.list, 5).bottom == 700,
                "resize did not clamp scroll to new client extent");
            MoveWindow(owned.list, 0, 0, 220, 450, FALSE);
            SendMessageW(owned.list, WM_VSCROLL, SB_TOP, 0);
            require(row_rect(owned.list, 0).top == 0, "top scroll escaped upper content boundary");
            SendMessageW(owned.list, LB_SETCARETINDEX, 3, FALSE);
            require(row_rect(owned.list, 3).top == 0, "oversized caret row was not aligned into view");
            SendMessageW(owned.list, LB_SETCARETINDEX, 3, TRUE);
            require(row_rect(owned.list, 3).top == 0, "partially-visible caret request scrolled unnecessarily");
            SendMessageW(owned.list, LB_SETTOPINDEX, 4, 0);
            SendMessageW(owned.list, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
            require(row_rect(owned.list, 4).top == 0, "fractional wheel delta scrolled before a full notch");
            SendMessageW(owned.list, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
            UINT lines = 0; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            require(lines ? row_rect(owned.list, 4).top > 0 : row_rect(owned.list, 4).top == 0,
                "wheel delta accumulation ignored native wheel preferences");
            SendMessageW(owned.list, LB_SETTOPINDEX, 99, 0);
            require(SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 5, "top-index request was not clamped to valid content");
            MoveWindow(owned.list, 0, 0, 220, 0, FALSE);
            SendMessageW(owned.list, WM_VSCROLL, SB_BOTTOM, 0);
            require(SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 5,
                "zero-height viewport returned a top index beyond the final row");
        });
        run("row clipping and WM_PRINT owner-draw forwarding", [] {
            OwnedList owned(2); PaintSurface surface;
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 100);
            owned.state.paint_past_row = true;
            surface.paint(owned, WM_PRINT); check_draws(owned, {0, 1});
            require(GetPixel(surface.dc, 10, 10) == RGB(20, 40, 60) &&
                GetPixel(surface.dc, 10, 150) == RGB(21, 40, 60) &&
                GetPixel(surface.dc, 10, 350) == RGB(101, 102, 103),
                "owner draw escaped its thumbnail row and corrupted adjacent/blank pixels");
        });
        run("scroll extents beyond 16-bit and 32-bit pixel ranges", [] {
            OwnedList owned(400);
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 600);
            SendMessageW(owned.list, WM_VSCROLL, SB_BOTTOM, 0);
            require(SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 399 &&
                row_rect(owned.list, 399).top == -150 && row_rect(owned.list, 399).bottom == 450,
                "scroll extent was truncated to 16 bits");
            SendMessageW(owned.list, LB_RESETCONTENT, 0, 0);
            for (int i = 0; i < 4; ++i) SendMessageW(owned.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"large.png"));
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 1000000000);
            SendMessageW(owned.list, WM_VSCROLL, SB_BOTTOM, 0);
            require(SendMessageW(owned.list, LB_GETTOPINDEX, 0, 0) == 3 &&
                row_rect(owned.list, 3).top == -999999550 && row_rect(owned.list, 3).bottom == 450,
                "scroll extent overflowed the 32-bit scrollbar range");
            SCROLLINFO info{sizeof(info)}; info.fMask = SIF_ALL;
            require(GetScrollInfo(owned.list, SB_VERT, &info) && info.nMax == 2147483647 &&
                info.nPos == 2147483198, "large pixel extent was not mapped to native scrollbar range");
            PaintSurface surface; surface.paint(owned); check_draws(owned, {3});
        });
        run("navigation keys select, notify and retain range anchor", [] {
            OwnedList owned(12);
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 100);
            BYTE old_keys[256]{}, neutral[256]{}; GetKeyboardState(old_keys); SetKeyboardState(neutral);
            const auto key = [&](WPARAM value) { SendMessageW(owned.list, WM_KEYDOWN, value, 0); };
            key(VK_DOWN);
            require(selected(owned.list) == std::vector<int>({1}) && owned.state.changes == 1 &&
                owned.state.source == owned.list, "Down did not select/notify through parent");
            key(VK_NEXT);
            require(SendMessageW(owned.list, LB_GETCARETINDEX, 0, 0) == 5 && selected(owned.list) == std::vector<int>({5}),
                "Page Down did not advance by visible rows");
            key(VK_END); require(selected(owned.list) == std::vector<int>({11}), "End did not select the last row");
            key(VK_HOME); key(VK_UP); require(selected(owned.list) == std::vector<int>({0}), "Home/Up crossed first row");
            neutral[VK_SHIFT] = 0x80; SetKeyboardState(neutral); key(VK_DOWN); key(VK_DOWN);
            require(selected(owned.list) == std::vector<int>({0, 1, 2}) &&
                SendMessageW(owned.list, LB_GETANCHORINDEX, 0, 0) == 0, "Shift navigation discarded the selection anchor");
            neutral[VK_SHIFT] = 0; neutral[VK_CONTROL] = 0x80; SetKeyboardState(neutral);
            const int changes = owned.state.changes; key(VK_DOWN);
            require(SendMessageW(owned.list, LB_GETCARETINDEX, 0, 0) == 3 &&
                selected(owned.list) == std::vector<int>({0, 1, 2}) && owned.state.changes == changes,
                "Ctrl navigation changed selection or emitted a false selection change");
            neutral[VK_CONTROL] = 0; SetKeyboardState(neutral); key(VK_PRIOR);
            require(selected(owned.list) == std::vector<int>({0}), "Page Up did not clamp/select the first row");
            SetKeyboardState(old_keys);
            const auto code = SendMessageW(owned.list, WM_GETDLGCODE, 0, 0);
            require((code & DLGC_WANTARROWS) && !(code & DLGC_WANTTAB), "thumbnail list swallowed Tab navigation");
        });
        run("point hit testing and double-click forwarding", [] {
            OwnedList owned(3);
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 100);
            RECT client{}; GetClientRect(owned.list, &client);
            auto hit = SendMessageW(owned.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(8, 150));
            require(LOWORD(hit) == 1 && HIWORD(hit) == 0, "hit testing chose the wrong thumbnail");
            hit = SendMessageW(owned.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(8, 350));
            require(LOWORD(hit) == 2 && HIWORD(hit) == 0, "blank client space did not return its nearest row");
            hit = SendMessageW(owned.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(-1, -1));
            require(LOWORD(hit) == 0 && HIWORD(hit) == 1, "negative client point was not outside");
            hit = SendMessageW(owned.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.right, 100));
            require(HIWORD(hit) == 1, "right client boundary was treated as an item hit");
            SendMessageW(owned.list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 150));
            require(selected(owned.list).empty(), "base component duplicated outer gallery mouse selection");
            SendMessageW(owned.list, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(8, 150));
            require(owned.state.doubles == 1 && owned.state.source == owned.list, "double click did not notify parent");
            SendMessageW(owned.list, LB_RESETCONTENT, 0, 0);
            hit = SendMessageW(owned.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(8, 10));
            require(HIWORD(hit) == 1, "empty list reported an actionable item hit");
        });
        run("theme brush, borrowed font, item state and DC preservation", [] {
            OwnedList owned(2); PaintSurface surface;
            const HFONT font = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            require(font != nullptr, "cannot create borrowed test font");
            SendMessageW(owned.list, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 100);
            SendMessageW(owned.list, LB_SETSEL, TRUE, 1);
            SendMessageW(owned.list, LB_SETCARETINDEX, 1, TRUE);
            SendMessageW(owned.list, WM_SETFOCUS, 0, 0);
            const auto previous_font = GetCurrentObject(surface.dc, OBJ_FONT);
            const auto previous_color = SetTextColor(surface.dc, RGB(11, 12, 13));
            surface.paint(owned); check_draws(owned, {0, 1});
            require(owned.state.draws[1].font == font && (owned.state.draws[1].state & ODS_SELECTED) &&
                (owned.state.draws[1].state & ODS_FOCUS), "owner draw lost font/selection/focus state");
            require(GetPixel(surface.dc, 10, 350) == RGB(101, 102, 103), "parent theme brush did not paint blank list space");
            require(GetCurrentObject(surface.dc, OBJ_FONT) == previous_font &&
                GetTextColor(surface.dc) == RGB(11, 12, 13), "thumbnail painting leaked DC state");
            SetTextColor(surface.dc, previous_color);
            EnableWindow(owned.list, FALSE); surface.paint(owned);
            require((owned.state.draws[0].state & ODS_DISABLED) != 0, "disabled thumbnail draw lacked ODS_DISABLED");
            require(reinterpret_cast<HFONT>(SendMessageW(owned.list, WM_GETFONT, 0, 0)) == font, "borrowed font was not retained");
            DestroyWindow(owned.list); LOGFONTW details{};
            require(GetObjectW(font, sizeof(details), &details) != 0, "thumbnail list deleted its caller-owned font");
            DeleteObject(font);
        });
        run("reentrant draw destruction and repeated resource cleanup", [] {
            {
                OwnedList owned(3); PaintSurface surface;
                owned.state.destroy_during_draw = true;
                surface.paint(owned);
                require(!IsWindow(owned.list) && owned.state.draws.size() == 1,
                    "destroying a thumbnail from owner draw did not stop drawing safely");
            }
            { OwnedList warmup(1); }
            const auto process = GetCurrentProcess();
            const DWORD gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
            for (int i = 0; i < 100; ++i) { OwnedList owned(8); SendMessageW(owned.list, LB_SETITEMHEIGHT, 0, 600); }
            require(GetGuiResources(process, GR_GDIOBJECTS) <= gdi + 2 &&
                GetGuiResources(process, GR_USEROBJECTS) <= user + 2, "thumbnail lifetime leaked Win32 resources");
        });
        UnregisterClassW(klass.lpszClassName, instance);
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
