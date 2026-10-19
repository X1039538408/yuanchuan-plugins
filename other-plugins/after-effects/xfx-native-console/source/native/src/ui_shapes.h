#pragma once
#include "ui_theme.h"
#include <gdiplus.h>
#include <algorithm>

namespace xfx {
inline Gdiplus::Color ui_color(COLORREF c) { return {255,GetRValue(c),GetGValue(c),GetBValue(c)}; }
inline void rounded_path(Gdiplus::GraphicsPath& path, Gdiplus::RectF rect, float radius) {
    const float d=(std::min)(radius*2,(std::min)(rect.Width,rect.Height));
    path.AddArc(rect.X,rect.Y,d,d,180,90);
    path.AddArc(rect.GetRight()-d,rect.Y,d,d,270,90);
    path.AddArc(rect.GetRight()-d,rect.GetBottom()-d,d,d,0,90);
    path.AddArc(rect.X,rect.GetBottom()-d,d,d,90,90);path.CloseFigure();
}
inline void paint_round_rect(HDC dc, const RECT& rect, COLORREF fill, float radius,
    std::optional<COLORREF> border = {}) {
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // The inset half-pixel path must use the same raster origin on all sides.
    // The default origin shifts the right/bottom arcs by one physical pixel.
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    rounded_path(path,{static_cast<float>(rect.left)+.5f,static_cast<float>(rect.top)+.5f,
        static_cast<float>(rect.right-rect.left)-1,static_cast<float>(rect.bottom-rect.top)-1},radius);
    Gdiplus::SolidBrush brush(ui_color(fill));g.FillPath(&brush,&path);
    if(border){Gdiplus::Pen pen(ui_color(*border),1);g.DrawPath(&pen,&path);}
}
}
