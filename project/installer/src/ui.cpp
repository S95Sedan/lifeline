#include "ui.hpp"
#include <commctrl.h>
#include <cmath>

namespace lifeline::setup::ui {
namespace {
constexpr int supersampling = 4;

struct Widget {
    bool primary = false, checkbox = false, hovered = false;
    int dpi = 96;
    COLORREF surface = colour::window;
    std::wstring hint;
    HFONT small_font = nullptr;
    int px(int n) const { return MulDiv(n, dpi, 96); }
};
std::wstring caption(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, 0);
    value.resize(GetWindowTextW(window, value.data(), int(value.size())));
    return value;
}
int width_of(const RECT &area) { return int(area.right - area.left); }
int height_of(const RECT &area) { return int(area.bottom - area.top); }

// Draws at a larger scale, then halftones the result into the destination so
// curved edges are antialiased. `draw` receives a DC in local coordinates and
// the scale factor applied to them.
template <class Draw> void supersample(HDC dc, RECT area, COLORREF backdrop, Draw draw) {
    auto w = width_of(area), h = height_of(area);
    if (w <= 0 || h <= 0)
        return;
    auto big = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, w * supersampling, h * supersampling);
    auto previous = SelectObject(big, bitmap);
    fill(big, RECT{0, 0, w * supersampling, h * supersampling}, backdrop);
    draw(big, supersampling);
    POINT origin;
    auto mode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, &origin);
    StretchBlt(dc, area.left, area.top, w, h, big, 0, 0, w * supersampling, h * supersampling, SRCCOPY);
    SetBrushOrgEx(dc, origin.x, origin.y, nullptr);
    SetStretchBltMode(dc, mode);
    SelectObject(big, previous);
    DeleteObject(bitmap);
    DeleteDC(big);
}
void shape(HDC dc, RECT area, COLORREF inside, COLORREF outline, int outline_width, int radius, bool ellipse) {
    auto brush = CreateSolidBrush(inside);
    auto pen = outline_width > 0 ? CreatePen(PS_INSIDEFRAME, outline_width, outline) : nullptr;
    auto old_brush = SelectObject(dc, brush);
    auto old_pen = SelectObject(dc, pen ? static_cast<HGDIOBJ>(pen) : GetStockObject(NULL_PEN));
    // A null pen leaves the right and bottom edges one pixel short.
    auto right = area.right + (pen ? 0 : 1), bottom = area.bottom + (pen ? 0 : 1);
    if (ellipse)
        Ellipse(dc, area.left, area.top, right, bottom);
    else
        RoundRect(dc, area.left, area.top, right, bottom, radius * 2, radius * 2);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    if (pen)
        DeleteObject(pen);
}
// Icons are designed on a 24 unit grid and stroked with round caps.
void draw_icon(HDC dc, Icon kind, double left, double top, double size, COLORREF value, double stroke) {
    auto unit = size / 24.0;
    auto at = [&](double x, double y) { return POINT{LONG(std::lround(left + x * unit)), LONG(std::lround(top + y * unit))}; };
    LOGBRUSH description{BS_SOLID, value, 0};
    auto pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                            DWORD(std::max(1.0, stroke * unit)), &description, 0, nullptr);
    auto brush = CreateSolidBrush(value);
    auto old_pen = SelectObject(dc, pen);
    auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    auto circle = [&](double x, double y, double radius) {
        auto a = at(x - radius, y - radius), b = at(x + radius, y + radius);
        Ellipse(dc, a.x, a.y, b.x, b.y);
    };
    auto dot = [&](double x, double y, double radius) {
        SelectObject(dc, brush);
        circle(x, y, radius);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
    };
    auto lines = [&](std::initializer_list<std::pair<double, double>> points, bool closed = false) {
        std::vector<POINT> path;
        for (auto [x, y] : points)
            path.push_back(at(x, y));
        if (closed)
            path.push_back(path.front());
        Polyline(dc, path.data(), int(path.size()));
    };
    auto box = [&](double x1, double y1, double x2, double y2, double radius) {
        auto a = at(x1, y1), b = at(x2, y2);
        auto r = int(std::lround(radius * unit * 2));
        RoundRect(dc, a.x, a.y, b.x, b.y, r, r);
    };
    switch (kind) {
    case Icon::none:
        break;
    case Icon::info:
        circle(12, 12, 9.5);
        lines({{12, 11}, {12, 16.5}});
        dot(12, 7.6, 1.1);
        break;
    case Icon::warning:
        lines({{12, 3}, {21.5, 20}, {2.5, 20}}, true);
        lines({{12, 9.5}, {12, 14}});
        dot(12, 17, 1.1);
        break;
    case Icon::check:
        lines({{5, 12.5}, {10, 17.5}, {19, 7}});
        break;
    case Icon::cross:
        lines({{6.5, 6.5}, {17.5, 17.5}});
        lines({{17.5, 6.5}, {6.5, 17.5}});
        break;
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}
// Option card in the style of the reference installer: a quiet outlined row
// whose border picks up the accent when selected.
void check(HDC target, HWND window, Widget &widget) {
    RECT bounds;
    GetClientRect(window, &bounds);
    Buffer buffer(target, bounds);
    auto dc = buffer.dc();
    bool enabled = IsWindowEnabled(window), selected = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED,
         focused = GetFocus() == window && keyboard_cues(window), hot = widget.hovered && enabled;
    fill(dc, bounds, widget.surface);
    auto face = hot ? colour::card_hot : widget.surface;
    auto edge = focused ? colour::accent
                : selected && enabled ? colour::mix(colour::border, colour::accent, 150)
                : hot ? colour::border_hot : colour::border;
    smooth_rounded(dc, bounds, widget.surface, face, edge, widget.px(6), widget.px(focused ? 2 : 1));
    auto padding = widget.px(12);
    RECT box{padding, padding + widget.px(2), padding + widget.px(20), padding + widget.px(22)};
    supersample(dc, box, face, [&](HDC big, int k) {
        RECT local{0, 0, width_of(box) * k, height_of(box) * k};
        if (selected) {
            shape(big, local, enabled ? colour::accent : colour::border, 0, 0, widget.px(4) * k, false);
            draw_icon(big, Icon::check, 0, 0, local.right, enabled ? colour::on_accent : colour::text_faint, 3.0);
        } else
            shape(big, local, colour::field, hot ? colour::border_hot : colour::border, widget.px(1) * k,
                  widget.px(4) * k, false);
    });
    auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    RECT title{box.right + widget.px(12), padding, bounds.right - padding, padding + widget.px(22)};
    text(dc, caption(window), title, font, enabled ? colour::text : colour::text_faint,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    title.top = title.bottom + widget.px(2);
    title.bottom = bounds.bottom - widget.px(4);
    text(dc, widget.hint, title, widget.small_font, enabled ? colour::text_dim : colour::text_faint,
         DT_LEFT | DT_TOP | DT_WORDBREAK);
}
LRESULT CALLBACK subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                          DWORD_PTR data) {
    auto widget = reinterpret_cast<Widget *>(data);
    switch (message) {
    case WM_MOUSEMOVE:
        if (!widget->hovered) {
            widget->hovered = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        widget->hovered = false;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_UPDATEUISTATE:
    case BM_SETCHECK:
    case BM_SETSTATE: {
        auto value = DefSubclassProc(window, message, wparam, lparam);
        InvalidateRect(window, nullptr, FALSE);
        return value;
    }
    case WM_LBUTTONUP:
    case WM_KEYUP: {
        auto value = DefSubclassProc(window, message, wparam, lparam);
        if (IsWindow(window))
            InvalidateRect(window, nullptr, FALSE);
        return value;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (widget->checkbox) {
            PAINTSTRUCT paint;
            auto dc = BeginPaint(window, &paint);
            check(dc, window, *widget);
            EndPaint(window, &paint);
            return 0;
        }
        break;
    case WM_PRINTCLIENT:
        if (widget->checkbox) {
            check(reinterpret_cast<HDC>(wparam), window, *widget);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, subclass, 1);
        RemovePropW(window, L"Lifeline.Widget");
        if (widget->small_font)
            DeleteObject(widget->small_font);
        delete widget;
        return DefSubclassProc(window, message, wparam, lparam);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
void attach(HWND control, Widget *widget) {
    require(SetWindowSubclass(control, subclass, 1, reinterpret_cast<DWORD_PTR>(widget)),
            "Cannot style setup control");
    SetPropW(control, L"Lifeline.Widget", widget);
}
int CALLBACK found_face(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found) {
    *reinterpret_cast<bool *>(found) = true;
    return 0;
}
} // namespace

COLORREF colour::mix(COLORREF from, COLORREF to, int amount) {
    auto channel = [&](int a, int b) { return BYTE(a + (b - a) * amount / 255); };
    return RGB(channel(GetRValue(from), GetRValue(to)), channel(GetGValue(from), GetGValue(to)),
               channel(GetBValue(from), GetBValue(to)));
}
int scale(int value, int dpi) { return MulDiv(value, dpi, 96); }
const wchar_t *display_face() {
    static const wchar_t *face = [] {
        LOGFONTW query{};
        query.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(query.lfFaceName, L"Bahnschrift", LF_FACESIZE);
        bool found = false;
        auto dc = GetDC(nullptr);
        EnumFontFamiliesExW(dc, &query, found_face, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
        return found ? L"Bahnschrift" : L"Segoe UI";
    }();
    return face;
}
HFONT make_font(int pixels, int weight, int dpi, const wchar_t *face) {
    return CreateFontW(-scale(pixels, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       face);
}
bool keyboard_cues(HWND control) {
    return !(SendMessageW(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
}
void fill(HDC dc, RECT area, COLORREF value) {
    auto brush = CreateSolidBrush(value);
    FillRect(dc, &area, brush);
    DeleteObject(brush);
}
void gradient(HDC dc, RECT area, COLORREF from, COLORREF to, bool horizontal) {
    auto length = horizontal ? width_of(area) : height_of(area);
    for (int i = 0, step = 2; i < length; i += step) {
        auto band = area;
        if (horizontal) {
            band.left = area.left + i;
            band.right = std::min<LONG>(area.right, band.left + step);
        } else {
            band.top = area.top + i;
            band.bottom = std::min<LONG>(area.bottom, band.top + step);
        }
        fill(dc, band, colour::mix(from, to, length > 1 ? i * 255 / (length - 1) : 0));
    }
}
void text(HDC dc, const std::wstring &value, RECT area, HFONT font, COLORREF value_colour, UINT flags) {
    if (value.empty())
        return;
    auto old = SelectObject(dc, font);
    auto mode = SetBkMode(dc, TRANSPARENT);
    auto previous = SetTextColor(dc, value_colour);
    DrawTextW(dc, value.c_str(), int(value.size()), &area, flags | DT_NOPREFIX);
    SetTextColor(dc, previous);
    SetBkMode(dc, mode);
    SelectObject(dc, old);
}
void spaced_text(HDC dc, const std::wstring &value, RECT area, HFONT font, COLORREF value_colour, int spacing,
                 UINT flags) {
    auto previous = SetTextCharacterExtra(dc, spacing);
    if (flags & (DT_RIGHT | DT_CENTER)) {
        // DrawText ignores character extra when aligning, so position the text manually.
        SIZE extent{};
        auto old = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, value.c_str(), int(value.size()), &extent);
        SelectObject(dc, old);
        auto spare = width_of(area) - int(extent.cx - spacing);
        area.left += flags & DT_RIGHT ? spare : spare / 2;
        flags &= ~UINT(DT_RIGHT | DT_CENTER);
    }
    text(dc, value, area, font, value_colour, flags);
    SetTextCharacterExtra(dc, previous);
}
int text_height(HDC dc, const std::wstring &value, HFONT font, int width) {
    if (value.empty())
        return 0;
    RECT area{0, 0, width, 0};
    auto old = SelectObject(dc, font);
    DrawTextW(dc, value.c_str(), int(value.size()), &area, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, old);
    return height_of(area);
}
void smooth_rounded(HDC dc, RECT area, COLORREF backdrop, COLORREF inside, COLORREF outline, int radius,
                    int outline_width) {
    auto w = width_of(area), h = height_of(area);
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    auto corner = radius + outline_width;
    auto draw = [&](RECT tile) {
        supersample(dc, tile, backdrop, [&](HDC big, int k) {
            shape(big,
                  RECT{(area.left - tile.left) * k, (area.top - tile.top) * k, (area.right - tile.left) * k,
                       (area.bottom - tile.top) * k},
                  inside, outline, outline_width * k, radius * k, false);
        });
    };
    if (radius == 0 || corner * 2 >= std::min(w, h)) {
        draw(area);
        return;
    }
    // Only the corners need supersampling; straight edges are filled directly.
    fill(dc, RECT{area.left + corner, area.top, area.right - corner, area.bottom}, inside);
    fill(dc, RECT{area.left, area.top + corner, area.left + corner, area.bottom - corner}, inside);
    fill(dc, RECT{area.right - corner, area.top + corner, area.right, area.bottom - corner}, inside);
    if (outline_width > 0) {
        fill(dc, RECT{area.left + corner, area.top, area.right - corner, area.top + outline_width}, outline);
        fill(dc, RECT{area.left + corner, area.bottom - outline_width, area.right - corner, area.bottom}, outline);
        fill(dc, RECT{area.left, area.top + corner, area.left + outline_width, area.bottom - corner}, outline);
        fill(dc, RECT{area.right - outline_width, area.top + corner, area.right, area.bottom - corner}, outline);
    }
    draw(RECT{area.left, area.top, area.left + corner, area.top + corner});
    draw(RECT{area.right - corner, area.top, area.right, area.top + corner});
    draw(RECT{area.left, area.bottom - corner, area.left + corner, area.bottom});
    draw(RECT{area.right - corner, area.bottom - corner, area.right, area.bottom});
}
void badge(HDC dc, RECT area, COLORREF backdrop, COLORREF inside, COLORREF ring, Icon mark, COLORREF mark_colour) {
    supersample(dc, area, backdrop, [&](HDC big, int k) {
        RECT local{0, 0, width_of(area) * k, height_of(area) * k};
        shape(big, local, inside, ring, std::max(1, width_of(area) / 16) * k, 0, true);
        auto size = local.right * 0.56;
        draw_icon(big, mark, (local.right - size) / 2, (local.bottom - size) / 2, size, mark_colour, 2.8);
    });
}
void icon(HDC dc, Icon kind, RECT area, COLORREF backdrop, COLORREF value_colour) {
    supersample(dc, area, backdrop, [&](HDC big, int k) {
        draw_icon(big, kind, 0, 0, width_of(area) * k, value_colour, 1.8);
    });
}
void progress(HDC dc, RECT area, int percent, COLORREF backdrop) {
    percent = std::clamp(percent, 0, 100);
    supersample(dc, area, backdrop, [&](HDC big, int k) {
        auto w = width_of(area) * k, h = height_of(area) * k;
        shape(big, RECT{0, 0, w, h}, RGB(22, 30, 43), 0, 0, h / 2, false);
        if (percent > 0)
            shape(big, RECT{0, 0, std::max(h, w * percent / 100), h}, colour::accent, 0, 0, h / 2, false);
    });
}

Buffer::Buffer(HDC dc, RECT bounds) : target(dc), area(bounds) {
    memory = CreateCompatibleDC(dc);
    bitmap = CreateCompatibleBitmap(dc, std::max(1, width_of(area)), std::max(1, height_of(area)));
    previous = static_cast<HBITMAP>(SelectObject(memory, bitmap));
    SetViewportOrgEx(memory, -area.left, -area.top, nullptr);
}
Buffer::~Buffer() {
    BitBlt(target, area.left, area.top, width_of(area), height_of(area), memory, area.left, area.top, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void decorate_button(HWND control, bool primary, COLORREF surface, int dpi) {
    auto widget = new Widget;
    widget->primary = primary;
    widget->surface = surface;
    widget->dpi = dpi;
    attach(control, widget);
}
void decorate_checkbox(HWND control, const std::wstring &hint, COLORREF surface, int dpi) {
    auto widget = new Widget;
    widget->checkbox = true;
    widget->hint = hint;
    widget->surface = surface;
    widget->dpi = dpi;
    widget->small_font = make_font(12, FW_NORMAL, dpi);
    attach(control, widget);
}
void draw_button(const DRAWITEMSTRUCT &item) {
    auto widget = reinterpret_cast<Widget *>(GetPropW(item.hwndItem, L"Lifeline.Widget"));
    if (!widget)
        return;
    Buffer buffer(item.hDC, item.rcItem);
    auto dc = buffer.dc();
    bool enabled = !(item.itemState & ODS_DISABLED), pressed = item.itemState & ODS_SELECTED,
         hot = widget->hovered && enabled,
         focused = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT) && enabled;
    COLORREF face, edge, label;
    if (!enabled) {
        face = colour::mix(widget->surface, colour::card, 120);
        edge = colour::divider;
        label = colour::text_faint;
    } else if (widget->primary) {
        face = pressed ? colour::accent_press : hot ? colour::accent_hot : colour::accent;
        edge = face;
        label = colour::on_accent;
    } else {
        face = pressed ? colour::field : hot ? colour::card_hot : colour::card;
        edge = hot || pressed ? colour::border_hot : colour::border;
        label = colour::text;
    }
    fill(dc, item.rcItem, widget->surface);
    auto radius = widget->px(6);
    smooth_rounded(dc, item.rcItem, widget->surface, face, edge, radius);
    if (focused) {
        auto ring = item.rcItem;
        InflateRect(&ring, -widget->px(3), -widget->px(3));
        smooth_rounded(dc, ring, face, face, widget->primary ? colour::on_accent : colour::accent,
                       std::max(1, radius - widget->px(3)), std::max(1, widget->px(2) * 3 / 4));
    }
    auto area = item.rcItem;
    if (pressed)
        OffsetRect(&area, 0, widget->px(1));
    text(dc, caption(item.hwndItem), area, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)),
         label, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}
} // namespace lifeline::setup::ui
