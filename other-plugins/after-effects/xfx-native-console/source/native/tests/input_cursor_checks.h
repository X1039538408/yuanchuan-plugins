#pragma once

static void check_search_cursor_stability() {
    // Deterministic WM_SETCURSOR coordinates; never race the user's physical mouse.
    const auto original_test_position=g_test_cursor_position;
    const auto arrow=LoadCursorW(nullptr,IDC_ARROW),ibeam=LoadCursorW(nullptr,IDC_IBEAM);
    wchar_t original_query[512]{};GetWindowTextW(g_state->search,original_query,512);
    const auto original_focus=GetFocus();
    for(const auto* query:{L"",L"gaussian",L"高斯"}) {
        SetWindowTextW(g_state->search,query);
        for(HWND focus:{g_state->search,g_state->results}) {
            SetFocus(focus);
            const auto surface=toolbar_layout(g_state->window).search_surface;
            const POINT points[]{ {surface.left+2,surface.top+2}, {surface.left+15,surface.top+4},
                {(surface.left+surface.right)/2,surface.top+2},
                {(surface.left+surface.right)/2,(surface.top+surface.bottom)/2},
                {surface.right-2,surface.bottom-2}, {surface.left+2,surface.bottom-2} };
            for(auto point:points) {
                POINT screen=point;ClientToScreen(g_state->window,&screen);g_test_cursor_position=screen;
                assert(SendMessageW(g_state->window,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y))==HTCLIENT);
                for(int repeat=0;repeat<40;++repeat) {
                    SetCursor(arrow);
                    const auto handled=SendMessageW(g_state->window,WM_SETCURSOR,reinterpret_cast<WPARAM>(g_state->window),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
                    if(handled!=TRUE){POINT actual{};GetCursorPos(&actual);POINT client=actual;ScreenToClient(g_state->window,&client);
                        std::cerr<<"cursor mismatch query="<<GetWindowTextLengthW(g_state->search)<<" wanted="<<screen.x<<","<<screen.y
                            <<" actual="<<actual.x<<","<<actual.y<<" client="<<client.x<<","<<client.y
                            <<" seen="<<g_test_cursor_point.x<<","<<g_test_cursor_point.y<<" in="<<g_test_cursor_in_surface<<" calls="<<g_test_cursor_calls<<" enabled="<<IsWindowEnabled(g_state->search)<<" surface="<<surface.left<<","<<surface.top<<","<<surface.right<<","<<surface.bottom<<"\n";}
                    assert(handled==TRUE);
                    assert(GetCursor()==ibeam);
                    assert(SendMessageW(g_state->search,WM_SETCURSOR,reinterpret_cast<WPARAM>(g_state->search),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE))==TRUE);
                    assert(GetCursor()==ibeam);
                }
                SendMessageW(g_state->window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
                assert(GetFocus()==g_state->search && !g_state->drag_active);
                SendMessageW(g_state->search,WM_LBUTTONUP,0,MAKELPARAM(0,0));
                assert(!forward_edge_drag(g_state->search,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(0,0)));
            }
        }
    }
    SetWindowTextW(g_state->search,original_query);
    if(IsWindow(original_focus))SetFocus(original_focus);
    g_test_cursor_position=original_test_position;
    std::cout<<"PASS complete search surface hit-test and 1440 deterministic parent/EDIT cursor sequences stay I-beam; no drag capture (physical mouse not tested)\n";
}

static void check_home_layout_restored() {
    const auto settings=g_state->settings;
    assert(GetWindowTextLengthW(g_state->search)==0 && shortcut_list_mode());
    assert(SendMessageW(g_state->filter,CB_GETCOUNT,0,0)==5);
    assert(SendMessageW(g_state->results,LB_GETCOUNT,0,0)==9);
    assert((GetWindowLongPtrW(g_state->results,GWL_STYLE)&LBS_MULTICOLUMN)!=0);
    for(int id:{1120,1140,1150,1151,1152,1153})assert(!IsWindow(GetDlgItem(g_state->window,id)));
    const auto search=toolbar_layout(g_state->window).search_surface;
    assert(search.right<child_rect_in_parent(g_state->window,g_state->filter).left);
    assert(child_rect_in_parent(g_state->window,g_state->results).top==ui_px(56));
    for(HWND button:g_state->layer_style_buttons)assert(IsWindowVisible(button));
    HMENU menu=CreatePopupMenu();xfx::MenuActionRows rows;append_slot_menu(menu,true,&rows);
    assert(GetMenuItemCount(menu)==9 && rows.size()==9);
    for(size_t slot=0;slot<rows.size();++slot){
        assert(!GetSubMenu(menu,static_cast<int>(slot)) && rows[slot].position==slot);
        assert(rows[slot].actions[0].command==10+slot && rows[slot].actions[0].enabled);
        assert(rows[slot].actions[1].command==30+slot && rows[slot].actions[2].command==50+slot);
        assert(rows[slot].actions[1].enabled==!settings.slots[slot].empty());
        assert(rows[slot].actions[2].enabled==!settings.slots[slot].empty());
    }
    DestroyMenu(menu);
    assert(g_state->settings.slots==settings.slots && g_state->settings.slot_labels==settings.slot_labels);
    std::cout<<"PASS 1.0.22 homepage, compact named slot strip, filter combo and style row unchanged; nine leaf menu rows with only three inline actions; no settings migration\n";
}
