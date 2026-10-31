#pragma once

// Capture the actually visible DC, never PrintWindow: printing forces a repaint
// and can conceal precisely the stale parent pixels this regression exercises.
static void save_filter_visible_surface(const fs::path& destination) {
    if(destination.empty())return;
    RECT r{};GetClientRect(g_state->window,&r);
    HDC source=GetDC(g_state->window),memory=CreateCompatibleDC(source);
    HBITMAP bitmap=CreateCompatibleBitmap(source,r.right,r.bottom);
    const auto old=SelectObject(memory,bitmap);
    assert(BitBlt(memory,0,0,r.right,r.bottom,source,0,0,SRCCOPY));
    {
        Gdiplus::Bitmap image(bitmap,nullptr);CLSID png{};
        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}",&png);
        assert(image.Save(destination.c_str(),&png)==Gdiplus::Ok);
    }
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(g_state->window,source);
}

static void check_filter_background_transitions(const fs::path& artifacts={}) {
    const auto settings=g_state->settings;
    const auto base=g_state->base_catalog;
    const auto filter=SendMessageW(g_state->filter,CB_GETCURSEL,0,0);
    wchar_t query[512]{};GetWindowTextW(g_state->search,query,512);
    std::vector<xfx::CatalogItem> catalog;
    g_state->settings.favorites.clear();g_state->settings.recent.clear();
    for(int i=0;i<10;++i){
        xfx::CatalogItem item{L"绘制测试 "+std::to_wstring(i),"","","Paint fixture "+std::to_string(i),
            "fixture-filter-"+std::to_string(i),L"效果","ae"};
        catalog.push_back(item);g_state->settings.recent.push_back(item_key(item));
        if(i==0)g_state->settings.favorites.push_back(item_key(item));
        item.item_type=L"preset";item.category=L"预设";item.source="preset";
        item.file_path=L"C:\\fixture-only\\"+std::to_wstring(i)+L".ffx";
        item.match_name.clear();catalog.push_back(item);
    }
    g_state->base_catalog=catalog;rebuild_customized_catalog();
    SetWindowTextW(g_state->search,L"");pump();
    const auto transition=[&](int selection){
        // Deliberately corrupt parent-owned visible pixels. A completed layout
        // transaction must repaint all exposed gaps, not only the LISTBOX.
        HDC dc=GetDC(g_state->window);RECT r{};GetClientRect(g_state->window,&r);
        HBRUSH poison=CreateSolidBrush(RGB(241,11,173));
        FillRect(dc,&r,poison);DeleteObject(poison);ReleaseDC(g_state->window,dc);
        SendMessageW(g_state->filter,CB_SETCURSEL,selection,0);
        SendMessageW(g_state->window,WM_COMMAND,MAKEWPARAM(kFilterId,CBN_SELCHANGE),reinterpret_cast<LPARAM>(g_state->filter));
        pump();GdiFlush();
        RECT client{};GetClientRect(g_state->window,&client);
        const auto style=child_rect_in_parent(g_state->window,g_state->layer_style_buttons[0]);
        const auto status=child_rect_in_parent(g_state->window,g_state->status);
        HRGN region=CreateRectRgn(0,0,0,0);assert(GetWindowRgn(g_state->window,region)!=ERROR);
        assert(PtInRegion(region,client.right/2,client.bottom-2));
        assert(PtInRegion(region,(style.left+style.right)/2,style.top+2));DeleteObject(region);
        const POINT samples[]{{ui_px(2),style.top+ui_px(8)},
            {client.right/2,style.top-ui_px(2)},
            {client.right/2,style.bottom+ui_px(1)},
            {client.right/2,client.bottom-2},
            {ui_px(3),(status.top+status.bottom)/2}};
        dc=GetDC(g_state->window);
        bool good=true;
        for(auto point:samples){
            const auto actual=GetPixel(dc,point.x,point.y);
            if(actual!=colors().background){
                std::cerr<<"Filter parent pixel not covered selection="<<selection<<" point="<<point.x<<","<<point.y
                    <<" rgb="<<actual<<" expected="<<colors().background<<"\n";good=false;
            }
        }
        ReleaseDC(g_state->window,dc);
        if(!artifacts.empty())save_filter_visible_surface(artifacts/(L"filter-visible-"+
            std::wstring(g_state->settings.theme_mode==xfx::ThemeMode::Light?L"light-":L"dark-")+std::to_wstring(selection)+L".png"));
        assert(good && "Filter resize failed to repaint parent footer/gaps before a forced repaint");
        RECT window{};GetWindowRect(g_state->window,&window);
        const int rows=selection==3?static_cast<int>(g_state->settings.favorites.size()):8;
        assert(client.bottom==ui_px(56+rows*kResultHeight+kLayerStyleRowHeight+22));
        assert(window.bottom-window.top==client.bottom);
    };
    for(auto mode:{xfx::ThemeMode::Dark,xfx::ThemeMode::Light}){
        set_theme_mode(mode);
        for(int selection:{1,3,2,3,4,3,1,3})transition(selection);
        g_state->settings.favorites.push_back(item_key(catalog[2]));transition(3);
        g_state->settings.favorites.pop_back();transition(3);
        g_state->settings.favorites.push_back(item_key(catalog[2]));transition(3);
        g_state->settings.favorites.pop_back();
    }
    g_state->settings=settings;set_theme_mode(settings.theme_mode);
    g_state->base_catalog=base;rebuild_customized_catalog();
    SendMessageW(g_state->filter,CB_SETCURSEL,filter,0);SetWindowTextW(g_state->search,query);populate_results();pump();
    std::cout<<"PASS actual filter growth/shrink Effects/Favorites/Presets/Recent parent-background coverage and region (dark/light)\n";
}

static void check_filter_combo_group_center() {
    HDC metrics=GetDC(g_state->filter);const auto old_font=SelectObject(metrics,g_state->font);
    for(int dpi:{96,120,144,192})for(const auto* label:{L"全部",L"效果",L"预设",L"收藏",L"最近"}){
        const RECT face{0,0,MulDiv(64,dpi,96),MulDiv(36,dpi,96)};
        const auto geometry=combo_face_content(metrics,face,dpi,label);
        const float group_right=geometry.arrow_x+MulDiv(8,dpi,96)*.5f;
        assert(std::abs(geometry.text.left+group_right-face.right)<=1.01f);
        assert(geometry.text.right<geometry.arrow_x && geometry.arrow_y==face.bottom*.5f);
    }
    SelectObject(metrics,old_font);ReleaseDC(g_state->filter,metrics);
    const auto previous=SendMessageW(g_state->filter,CB_GETCURSEL,0,0);
    SetFocus(g_state->search);
    for(int selection=0;selection<5;++selection){
        SendMessageW(g_state->filter,CB_SETCURSEL,selection,0);
        const auto sample=sample_input_paint(g_state->filter,colors().background,false);
        RECT client{};GetClientRect(g_state->filter,&client);
        RECT ink{client.right,client.bottom,0,0};
        const int margin=ui_px(5);
        for(int y=margin;y<client.bottom-margin;++y)for(int x=margin;x<client.right-margin;++x){
            const auto color=sample.pixels[static_cast<size_t>(y)*client.right+x];
            if(paint_colors_near(color,colors().input,32))continue;
            if(!paint_colors_near(color,colors().text,70) && !paint_colors_near(color,colors().icon,70))continue;
            ink.left=(std::min)(ink.left,static_cast<LONG>(x));ink.right=(std::max)(ink.right,static_cast<LONG>(x+1));
            ink.top=(std::min)(ink.top,static_cast<LONG>(y));ink.bottom=(std::max)(ink.bottom,static_cast<LONG>(y+1));
        }
        assert(ink.right>ink.left);
        std::cerr<<"Combo group selection="<<selection<<" ink="<<ink.left<<","<<ink.top<<","<<ink.right<<","<<ink.bottom<<" size="<<client.right<<","<<client.bottom<<"\n";
        assert(std::abs(ink.left+ink.right-client.right)<=ui_px(3));
        assert(std::abs(ink.top+ink.bottom-client.bottom)<=ui_px(4));
    }
    SendMessageW(g_state->filter,CB_SETCURSEL,previous,0);
    std::cout<<"PASS five filter labels and chevrons center together as one group\n";
}
