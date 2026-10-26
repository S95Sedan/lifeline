#pragma once
#include "common.hpp"

// Plain GDI drawing helpers and owner-drawn controls for the setup wizard.
// Small shapes are rendered supersampled so circles, check marks and rounded
// buttons stay smooth at every DPI without additional runtime dependencies.
namespace lifeline::setup::ui {
namespace colour {
inline constexpr COLORREF window = RGB(11, 15, 22), rail = RGB(8, 11, 17), footer = RGB(9, 12, 18),
                          card = RGB(17, 23, 33), card_hot = RGB(23, 31, 44), field = RGB(7, 10, 15),
                          border = RGB(33, 44, 60), border_hot = RGB(56, 74, 98),
                          divider = RGB(24, 32, 45), text = RGB(236, 242, 248),
                          text_dim = RGB(150, 166, 186), text_faint = RGB(92, 106, 126),
                          accent = RGB(56, 206, 236), accent_hot = RGB(116, 224, 246),
                          accent_press = RGB(34, 162, 192), accent_shade = RGB(14, 46, 60),
                          on_accent = RGB(4, 20, 28), signal = RGB(255, 170, 64),
                          signal_shade = RGB(52, 36, 16), success = RGB(84, 214, 150),
                          success_shade = RGB(14, 46, 34), danger = RGB(240, 96, 88),
                          danger_shade = RGB(56, 20, 22);
COLORREF mix(COLORREF from, COLORREF to, int amount_of_255);
} // namespace colour

enum class Icon { none, info, warning, check, cross };

int scale(int value, int dpi);
const wchar_t *display_face();
HFONT make_font(int pixels, int weight, int dpi, const wchar_t *face = L"Segoe UI");
bool keyboard_cues(HWND control);

void fill(HDC dc, RECT area, COLORREF value);
void gradient(HDC dc, RECT area, COLORREF from, COLORREF to, bool horizontal);
void text(HDC dc, const std::wstring &value, RECT area, HFONT font, COLORREF value_colour,
          UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER);
void spaced_text(HDC dc, const std::wstring &value, RECT area, HFONT font, COLORREF value_colour,
                 int spacing, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER);
int text_height(HDC dc, const std::wstring &value, HFONT font, int width);

// Smooth shapes. `backdrop` is the colour already beneath the shape.
void smooth_rounded(HDC dc, RECT area, COLORREF backdrop, COLORREF fill, COLORREF outline,
                    int radius, int outline_width = 1);
void badge(HDC dc, RECT area, COLORREF backdrop, COLORREF fill, COLORREF ring, Icon mark,
           COLORREF mark_colour);
void icon(HDC dc, Icon kind, RECT area, COLORREF backdrop, COLORREF value_colour);
void progress(HDC dc, RECT area, int percent, COLORREF backdrop);

// Paints into an off-screen bitmap and copies it on destruction.
class Buffer {
    HDC target, memory;
    HBITMAP bitmap, previous;
    RECT area;

  public:
    Buffer(HDC dc, RECT bounds);
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    HDC dc() const { return memory; }
};

void decorate_button(HWND control, bool primary, COLORREF surface, int dpi);
void decorate_checkbox(HWND control, const std::wstring &hint, COLORREF surface, int dpi);
void draw_button(const DRAWITEMSTRUCT &item);
} // namespace lifeline::setup::ui
