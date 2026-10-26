#include "install.hpp"
#include "movies.hpp"
#include "disc.hpp"
#include "ui.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <dwmapi.h>

using namespace lifeline::setup;
namespace {
constexpr UINT status_message = WM_APP + 1, finished_message = WM_APP + 2;
constexpr int next_id = 1001, back_id = 1002, cancel_id = 1003, iso_browse_id = 1004, folder_browse_id = 1005;
using namespace lifeline::setup::ui;
std::wstring text(HWND control) {
    auto length = GetWindowTextLengthW(control);
    std::wstring value(length + 1, 0);
    value.resize(GetWindowTextW(control, value.data(), length + 1));
    return value;
}
std::wstring browse_iso(HWND window, const fs::path &initial) {
    wchar_t buffer[32768]{};
    auto value = initial.wstring();
    std::copy_n(value.begin(), std::min(value.size(), std::size(buffer) - 1), buffer);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window;
    dialog.lpstrTitle = L"Select your Lifeline ISO";
    dialog.lpstrFilter = L"ISO disc image (*.iso)\0*.iso\0All files\0*.*\0\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? buffer : L"";
}
fs::path browse_folder(HWND window, const fs::path &initial) {
    IFileDialog *dialog = nullptr;
    require(SUCCEEDED(
                CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))),
            "Cannot open folder chooser");
    struct Release {
        IFileDialog *p;
        ~Release() { p->Release(); }
    } release{dialog};
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose the Lifeline installation folder");
    if (fs::is_directory(initial)) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dialog->SetFolder(item);
            item->Release();
        }
    }
    if (FAILED(dialog->Show(window)))
        return {};
    IShellItem *item = nullptr;
    require(SUCCEEDED(dialog->GetResult(&item)), "No folder was selected");
    PWSTR value = nullptr;
    auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &value);
    item->Release();
    require(SUCCEEDED(result), "Cannot read selected folder");
    fs::path path(value);
    CoTaskMemFree(value);
    return path;
}
class Wizard {
    // Layout is authored at 96 DPI on an 880 x 660 client area and scaled by px().
    // The frame follows the reference installer: header and accent rule, a step
    // rail on the left, a footer with the navigation buttons and padded pages.
    static constexpr int client_width = 880, client_height = 660, header_height = 104, rule_height = 3,
                         rail_width = 232, footer_height = 74, page_padding = 34,
                         body_top = header_height + rule_height, footer_top = client_height - footer_height,
                         content_left = rail_width + page_padding, content_right = client_width - page_padding,
                         content_width = content_right - content_left, content_top = body_top + page_padding,
                         content_bottom = footer_top - page_padding,
                         // Shared panel holding a path field on the first two pages.
                         panel_top = content_top + 78, panel_height = 160, field_top = panel_top + 36,
                         field_height = 38, browse_width = 120, field_right = content_right - 16 - browse_width - 10,
                         button_width = 116, button_height = 36, row_height = 30;
    static constexpr unsigned long long required_space = 6ull << 30;
    // STATIC label roles, stored as a window property for WM_CTLCOLORSTATIC.
    static constexpr LONG_PTR role_dim = 1, role_card = 2;
    static constexpr const wchar_t *role_property = L"Lifeline.Role";
    static constexpr UINT_PTR clock_timer = 1;
    HWND window = nullptr, next = nullptr, back = nullptr, cancel_button = nullptr, iso_edit = nullptr,
         destination_edit = nullptr, desktop = nullptr, start_menu = nullptr, launch = nullptr, status = nullptr;
    HINSTANCE instance = nullptr;
    HBRUSH window_brush = CreateSolidBrush(colour::window), card_brush = CreateSolidBrush(colour::card),
           field_brush = CreateSolidBrush(colour::field);
    HFONT body = nullptr, heading = nullptr, subheading = nullptr, brand = nullptr, small_font = nullptr,
          strong = nullptr;
    HBITMAP header_art = nullptr;
    std::vector<HWND> controls;
    InstallOptions options;
    int page = 0, dpi = 96, error_height = 20;
    std::thread worker;
    std::atomic_bool cancel{false};
    Json result;
    std::wstring error, warning, space_note;
    bool finished = false, success = false, publishing = false, space_known = false, space_ok = false;
    int progress_value = 0;
    std::chrono::steady_clock::time_point started;
    int px(int value) const { return MulDiv(value, dpi, 96); }
    RECT box(int x, int y, int width, int height) const { return RECT{px(x), px(y), px(x + width), px(y + height)}; }

    // ---------------------------------------------------------------- controls
    HWND control(const wchar_t *type, const std::wstring &value, DWORD style, int x, int y, int width,
                 int height, int id = 0, HFONT font = nullptr) {
        auto handle =
            CreateWindowExW(0, type, value.c_str(), WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(width),
                            px(height), window, reinterpret_cast<HMENU>(INT_PTR(id)), instance, nullptr);
        require(handle != nullptr, "Cannot create setup control");
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : body), TRUE);
        controls.push_back(handle);
        return handle;
    }
    HWND label(const std::wstring &value, int x, int y, int width, int height, HFONT font, LONG_PTR role) {
        auto handle = control(L"STATIC", value, SS_LEFT | SS_NOPREFIX, x, y, width, height, 0, font);
        if (role) {
            // The label has already painted once in response to WM_SETFONT.
            SetPropW(handle, role_property, reinterpret_cast<HANDLE>(role));
            InvalidateRect(handle, nullptr, TRUE);
        }
        return handle;
    }
    HWND button(const std::wstring &value, int x, int y, int width, int height, int id, COLORREF surface) {
        auto primary = id == next_id;
        auto handle =
            control(L"BUTTON", value, BS_OWNERDRAW | WS_TABSTOP, x, y, width, height, id, primary ? strong : body);
        ui::decorate_button(handle, primary, surface, dpi);
        return handle;
    }
    HWND checkbox(const std::wstring &title, const std::wstring &hint, int x, int y, int width, int height,
                  bool checked) {
        auto handle = control(L"BUTTON", title, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, height, 0, strong);
        ui::decorate_checkbox(handle, hint, colour::window, dpi);
        SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        return handle;
    }
    // Path fields share one layout: an inset field inside the page panel with Browse beside it.
    HWND field(const std::wstring &value, int browse_id) {
        auto left = content_left + 16;
        auto handle = control(L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL, left + 12, field_top + 8,
                              field_right - left - 24, 22);
        SendMessageW(handle, EM_SETLIMITTEXT, 32767, 0);
        SendMessageW(handle, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        button(L"Browse...", field_right + 10, field_top, browse_width, field_height, browse_id, colour::window);
        return handle;
    }
    void clear() {
        for (auto handle : controls) {
            RemovePropW(handle, role_property);
            DestroyWindow(handle);
        }
        controls.clear();
        iso_edit = destination_edit = desktop = start_menu = launch = status = nullptr;
    }
    bool cancelled() const { return finished && !success && cancel.load(); }
    // Colour of the final outcome: green for success, amber when cancelled, red on failure.
    COLORREF outcome() const { return success ? colour::success : cancelled() ? colour::signal : colour::danger; }
    COLORREF outcome_shade() const {
        return success ? colour::success_shade : cancelled() ? colour::signal_shade : colour::danger_shade;
    }
    void show() {
        clear();
        // Footer buttons: Back and the primary action are grouped, Cancel stands apart.
        const int top = footer_top + (footer_height - button_height) / 2, right = client_width - 28;
        const int next_left = right - button_width * 2 - 24;
        next = button(finished ? (success ? L"Finish" : L"Close") : page == 2 ? L"Install" : L"Next", next_left, top,
                      button_width, button_height, next_id, colour::footer);
        back = button(L"Back", next_left - 10 - button_width, top, button_width, button_height, back_id,
                      colour::footer);
        cancel_button = button(L"Cancel", right - button_width, top, button_width, button_height, cancel_id,
                               colour::footer);
        EnableWindow(back, page > 0 && page < 3);
        EnableWindow(next, page < 3 || finished);
        EnableWindow(cancel_button, !finished);
        // Page headings are real labels so assistive tools can read them.
        auto title = [&](const std::wstring &heading_text, const std::wstring &subtitle, int indent = 0) {
            label(heading_text, content_left + indent, content_top, content_width - indent, 34, heading, 0);
            if (!subtitle.empty())
                label(subtitle, content_left + indent, content_top + 40, content_width - indent, 20, subheading,
                      role_dim);
        };
        if (page == 0) {
            title(L"Game disc", L"Select the ISO image of your original Lifeline disc.");
            iso_edit = field(options.iso.wstring(), iso_browse_id);
        } else if (page == 1) {
            title(L"Install location", L"Choose where Lifeline will be installed.");
            destination_edit = field(options.destination.wstring(), folder_browse_id);
            refresh_space();
            const int width = (content_width - 12) / 2, y = panel_top + panel_height + 52;
            start_menu = checkbox(L"Start menu shortcut", L"Add Lifeline to the Start menu.", content_left, y,
                                  width, 64, options.start_menu);
            desktop = checkbox(L"Desktop shortcut", L"Add Lifeline to the desktop.", content_left + width + 12, y,
                               width, 64, options.desktop);
        } else if (page == 2) {
            title(L"Ready to install", L"Check the summary below, then start the installation.");
        } else if (!finished) {
            title(L"Installing Lifeline", L"");
            status = label(L"Checking your disc...", content_left, content_top + 58, content_width - 90, 22, body, 0);
        } else if (success) {
            title(L"Installation complete", L"Lifeline was installed and verified.", 56);
            launch = checkbox(L"Launch Lifeline", L"Start the game when setup closes.", content_left,
                              content_bottom - 64, content_width, 64, true);
        } else {
            title(cancelled() ? L"Installation cancelled" : L"Installation failed",
                  cancelled() ? L"You can run setup again at any time." : L"Lifeline was not installed.", 56);
            // The details panel grows with the message, up to the space above the footer.
            auto dc = GetDC(window);
            auto measured = MulDiv(ui::text_height(dc, error, body, px(content_width - 32)), 96, dpi);
            ReleaseDC(window, dc);
            error_height = std::clamp(measured, 20, content_bottom - panel_top - 28);
            label(error, content_left + 16, panel_top + 14, content_width - 32, error_height, body, role_card);
        }
        SetFocus(page == 3 && !finished ? cancel_button : next);
        InvalidateRect(window, nullptr, FALSE);
    }
    void update_time() {
        if (page == 3 && !finished) {
            auto area = box(content_left, content_bottom - 22, content_width, 22);
            InvalidateRect(window, &area, FALSE);
        }
    }
    std::wstring duration() const {
        if (started.time_since_epoch().count() == 0)
            return L"0m 00s";
        auto seconds = finished && success ? int(result.value("seconds", 0.0))
                                          : int(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        return std::to_wstring(seconds / 60) + L"m " + (seconds % 60 < 10 ? L"0" : L"") +
               std::to_wstring(seconds % 60) + L"s";
    }
    static std::wstring gigabytes(unsigned long long bytes) {
        auto tenths = bytes * 10 / (1ull << 30);
        return std::to_wstring(tenths / 10) + (tenths < 1000 ? L"." + std::to_wstring(tenths % 10) : L"") + L" GB";
    }
    void refresh_space() {
        space_known = false;
        if (!destination_edit)
            return;
        fs::path path = text(destination_edit);
        std::error_code failure;
        while (!path.empty() && !fs::is_directory(path, failure)) {
            auto parent = path.parent_path();
            if (parent == path)
                break;
            path = parent;
        }
        ULARGE_INTEGER available{};
        if (path.empty() || !path.is_absolute() || !GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
            return;
        space_known = true;
        space_ok = available.QuadPart >= required_space;
        space_note = gigabytes(available.QuadPart) + L" on " + path.root_path().wstring() +
                     (space_ok ? L"" : L" - not enough");
    }
    static std::wstring friendly(std::wstring value) {
        if (value.starts_with(L"Install game resources:"))
            return L"Installing game graphics, music and voice recordings...";
        if (value.starts_with(L"Preparing movie:") || value.starts_with(L"Prepared movie:"))
            return L"Preparing original movies and audio...";
        if (value.starts_with(L"Validate the original"))
            return L"Checking your disc...";
        if (value.starts_with(L"Prepare the installation folder"))
            return L"Preparing the installation folder...";
        if (value.starts_with(L"Verify the installed"))
            return L"Verifying the installed game...";
        if (value.starts_with(L"Install the native PC executable"))
            return L"Installing the game executable...";
        if (value.starts_with(L"Finish the verified installation"))
            return L"Finishing installation...";
        return value;
    }
    void retain() {
        if (iso_edit)
            options.iso = text(iso_edit);
        if (destination_edit)
            options.destination = text(destination_edit);
        if (start_menu)
            options.start_menu = SendMessageW(start_menu, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (desktop)
            options.desktop = SendMessageW(desktop, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void advance() {
        try {
            if (finished) {
                if (success && launch && SendMessageW(launch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    auto result_ =
                        ShellExecuteW(window, L"open", (options.destination / L"lifeline.exe").c_str(),
                                      nullptr, options.destination.c_str(), SW_SHOWNORMAL);
                    require(INT_PTR(result_) > 32, "Cannot launch the installed game");
                }
                DestroyWindow(window);
                return;
            }
            retain();
            if (page == 0) {
                Disc validation(options.iso);
            } else if (page == 1)
                options.destination = validate_destination(options.destination, options.iso);
            else if (page == 2) {
                page = 3;
                started = std::chrono::steady_clock::now();
                SetTimer(window, clock_timer, 1000, nullptr);
                show();
                worker = std::thread([this] {
                    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    try {
                        Report report;
                        report.cancel = &cancel;
                        report.status = [this](const std::wstring &text_, int value) {
                            PostMessageW(window, status_message, WPARAM(value),
                                         reinterpret_cast<LPARAM>(new std::wstring(text_)));
                        };
                        result = perform_install(options, report);
                        success = true;
                    } catch (const std::exception &e) {
                        error = wide(e.what());
                    }
                    if (SUCCEEDED(com))
                        CoUninitialize();
                    PostMessageW(window, finished_message, 0, 0);
                });
                return;
            }
            ++page;
            show();
        } catch (const std::exception &e) {
            MessageBoxW(window, wide(e.what()).c_str(), L"Lifeline Setup", MB_OK | MB_ICONERROR);
        }
    }
    void close() {
        if (worker.joinable() && !finished) {
            if (publishing)
                return;
            if (MessageBoxW(window, L"Cancel installation? Your existing installation will be preserved.",
                            L"Cancel installation", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                cancel = true;
                EnableWindow(cancel_button, FALSE);
                SetWindowTextW(status, L"Cancelling installation...");
                invalidate_page();
            }
            return;
        }
        DestroyWindow(window);
    }

    // ---------------------------------------------------------------- painting
    void draw(HDC dc, const std::wstring &value, int x, int y, int width, int height, HFONT font, COLORREF value_colour,
              UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) const {
        ui::text(dc, value, box(x, y, width, height), font, value_colour, flags);
    }
    // Draws wrapped text and returns the logical height it occupied.
    int paragraph(HDC dc, const std::wstring &value, int x, int y, int width, HFONT font, COLORREF value_colour) const {
        auto height = ui::text_height(dc, value, font, px(width));
        auto area = box(x, y, width, 0);
        area.bottom = area.top + height;
        ui::text(dc, value, area, font, value_colour, DT_LEFT | DT_TOP | DT_WORDBREAK);
        return MulDiv(height, 96, dpi);
    }
    void panel(HDC dc, int y, int height, COLORREF inside, COLORREF edge) const {
        ui::smooth_rounded(dc, box(content_left, y, content_width, height), colour::window, inside, edge, px(8));
    }
    // A label/value line used by the panels; the dot marks state where it matters.
    void info(HDC dc, int y, const std::wstring &name, const std::wstring &value, COLORREF marker,
              COLORREF value_colour = colour::text) const {
        ui::smooth_rounded(dc, box(content_left + 16, y + 7, 7, 7), colour::window, marker, marker, px(4), 0);
        draw(dc, name, content_left + 32, y, 132, 20, body, colour::text_dim);
        draw(dc, value, content_left + 164, y, content_width - 180, 20, strong, value_colour);
    }
    void summary(HDC dc, int y, const std::wstring &name, const std::wstring &value) const {
        draw(dc, name, content_left + 16, y, 148, 22, body, colour::text_dim);
        draw(dc, value, content_left + 164, y, content_width - 180, 22, strong, colour::text,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
    }
    std::wstring shortcuts() const {
        if (options.start_menu && options.desktop)
            return L"Start menu and desktop";
        return options.start_menu ? L"Start menu" : options.desktop ? L"Desktop" : L"None";
    }
    void paint_header(HDC dc) {
        auto width = px(client_width), height = px(header_height);
        if (!header_art) {
            auto memory = CreateCompatibleDC(dc);
            header_art = CreateCompatibleBitmap(dc, width, height);
            auto previous = SelectObject(memory, header_art);
            ui::gradient(memory, RECT{0, 0, width, height}, RGB(4, 7, 12), RGB(12, 20, 33), false);
            unsigned seed = 0x4C1F3u;
            auto random = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
            for (int i = 0; i < 70; ++i) {
                int x = int(random() % unsigned(width)), y = int(random() % unsigned(height));
                int brightness = 30 + int(random() % 110);
                ui::fill(memory, RECT{x, y, x + 1, y + 1}, colour::mix(RGB(10, 16, 26), RGB(200, 225, 255), brightness));
            }
            // A low planet horizon on the right edge.
            const int cx = 800, cy = 420, radius = 340;
            auto planet = [&](int grow) {
                return box(cx - radius - grow, cy - radius - grow, 2 * (radius + grow), 2 * (radius + grow));
            };
            auto ellipse = [&](RECT area, COLORREF inside) {
                auto brush = CreateSolidBrush(inside);
                auto old_brush = SelectObject(memory, brush);
                auto old_pen = SelectObject(memory, GetStockObject(NULL_PEN));
                Ellipse(memory, area.left, area.top, area.right, area.bottom);
                SelectObject(memory, old_pen);
                SelectObject(memory, old_brush);
                DeleteObject(brush);
            };
            for (int grow : {16, 9, 4})
                ellipse(planet(grow), colour::mix(RGB(12, 20, 33), colour::accent_shade, 255 - grow * 12));
            auto inner = planet(0);
            OffsetRect(&inner, px(4), px(5));
            ellipse(planet(0), colour::mix(colour::accent_press, colour::window, 110));
            ellipse(inner, RGB(6, 10, 17));
            SelectObject(memory, previous);
            DeleteDC(memory);
        }
        auto memory = CreateCompatibleDC(dc);
        auto previous = SelectObject(memory, header_art);
        BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
        SelectObject(memory, previous);
        DeleteDC(memory);
        ui::spaced_text(dc, L"LIFELINE", box(28, 0, 400, header_height), brand, colour::text, px(4));
        ui::fill(dc, box(0, header_height, client_width, rule_height), colour::accent);
    }
    void paint_rail(HDC dc) const {
        ui::fill(dc, box(0, body_top, rail_width, footer_top - body_top), colour::rail);
        ui::fill(dc, box(rail_width - 1, body_top, 1, footer_top - body_top), colour::divider);
        const wchar_t *steps[] = {L"Game disc", L"Install location", L"Ready to install", L"Installing"};
        const wchar_t *hints[] = {L"Select your disc image", L"Folder and shortcuts", L"Review your choices",
                                  L"Copy and verify"};
        const int x = 26, marker = 26, row = 52;
        auto stopped = finished && !success;
        for (int i = 0; i < 4; ++i) {
            int y = body_top + 26 + i * row;
            bool done = i < page || (i == 3 && success), active = i == page && !done;
            auto area = box(x, y, marker, marker);
            if (i < 3)
                ui::fill(dc, box(x + marker / 2, y + marker + 3, 1, row - marker - 6),
                         done ? colour::accent_shade : colour::divider);
            if (done)
                ui::badge(dc, area, colour::rail, colour::accent_shade, colour::accent, ui::Icon::check, colour::accent);
            else if (active && stopped)
                ui::badge(dc, area, colour::rail, outcome_shade(), outcome(),
                          cancelled() ? ui::Icon::warning : ui::Icon::cross, outcome());
            else {
                ui::smooth_rounded(dc, area, colour::rail, active ? colour::accent : colour::rail,
                                   active ? colour::accent : colour::border, px(marker / 2));
                draw(dc, std::to_wstring(i + 1), x, y, marker, marker, active ? strong : body,
                     active ? colour::on_accent : colour::text_faint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            const int left = x + marker + 12, width = rail_width - left - 14;
            if (active) {
                draw(dc, steps[i], left, y - 1, width, 17, strong, colour::text);
                draw(dc, stopped ? (cancelled() ? L"Cancelled" : L"Stopped") : hints[i], left, y + 15, width, 17,
                     small_font, stopped ? outcome() : colour::text_faint);
            } else
                draw(dc, steps[i], left, y, width, marker, body, done ? colour::text_dim : colour::text_faint);
        }
    }
    void paint_footer(HDC dc) const {
        ui::fill(dc, box(0, footer_top, client_width, footer_height), colour::footer);
        ui::fill(dc, box(0, footer_top, client_width, 1), colour::divider);
        auto state = !finished     ? L"Step " + std::to_wstring(page + 1) + L" of 4"
                     : success     ? std::wstring(L"All steps complete")
                                   : std::wstring(L"Installation stopped");
        draw(dc, state, 28, footer_top, 300, footer_height, small_font, colour::text_faint);
    }
    void paint_path_panel(HDC dc, HWND edit, const wchar_t *caption) const {
        panel(dc, panel_top, panel_height, colour::window, colour::border);
        draw(dc, caption, content_left + 16, panel_top + 12, content_width - 32, 18, strong, colour::text);
        auto focused = edit && GetFocus() == edit;
        ui::smooth_rounded(dc, box(content_left + 16, field_top, field_right - content_left - 16, field_height),
                           colour::window, colour::field, focused ? colour::accent : colour::border, px(6));
    }
    void paint_content(HDC dc) const {
        ui::fill(dc, box(rail_width, body_top, client_width - rail_width, footer_top - body_top), colour::window);
        const int rows = field_top + field_height + 20;
        if (page == 0) {
            paint_path_panel(dc, iso_edit, L"Disc image");
            auto found = false;
            if (iso_edit) {
                std::error_code failure;
                found = fs::is_regular_file(fs::path(text(iso_edit)), failure);
            }
            info(dc, rows, L"Disc image", found ? L"Found" : L"No file selected",
                 found ? colour::success : colour::signal, found ? colour::text : colour::text_dim);
            info(dc, rows + 28, L"Required", L"North American release (SLUS-208.48)", colour::text_dim);
        } else if (page == 1) {
            paint_path_panel(dc, destination_edit, L"Destination folder");
            info(dc, rows, L"Space required", L"6 GB free during setup, about 5 GB installed", colour::text_dim);
            if (space_known)
                info(dc, rows + 28, L"Space available", space_note, space_ok ? colour::success : colour::danger,
                     space_ok ? colour::text : colour::danger);
            else
                info(dc, rows + 28, L"Space available", L"Checked when you continue", colour::text_faint,
                     colour::text_dim);
            draw(dc, L"Shortcuts", content_left, panel_top + panel_height + 20, content_width, 20, strong, colour::text);
        } else if (page == 2) {
            const int top = panel_top, height = 24 + 4 * row_height;
            panel(dc, top, height, colour::card, colour::border);
            int y = top + 14;
            summary(dc, y, L"Disc image", options.iso.wstring());
            summary(dc, y += row_height, L"Install folder", options.destination.wstring());
            summary(dc, y += row_height, L"Shortcuts", shortcuts());
            summary(dc, y += row_height, L"Disk space", L"6 GB free during setup, about 5 GB installed");
            paragraph(dc,
                      L"Setup checks your disc, installs the game and converts the original movies and audio. "
                      L"This takes a few minutes. Existing saves and settings are kept, and you can cancel until "
                      L"the final step.",
                      content_left, top + height + 16, content_width, small_font, colour::text_faint);
        } else if (!finished) {
            paint_progress(dc);
        } else {
            ui::badge(dc, box(content_left, content_top, 40, 40), colour::window, outcome_shade(), outcome(),
                      success ? ui::Icon::check : cancelled() ? ui::Icon::warning : ui::Icon::cross, outcome());
            const int top = panel_top;
            if (success) {
                const int height = 24 + 3 * row_height;
                panel(dc, top, height, colour::window, colour::mix(colour::border, colour::success, 90));
                int y = top + 14;
                summary(dc, y, L"Installed to", options.destination.wstring());
                summary(dc, y += row_height, L"Time taken", duration());
                summary(dc, y += row_height, L"Shortcuts", shortcuts());
                if (!warning.empty()) {
                    ui::icon(dc, ui::Icon::warning, box(content_left, top + height + 18, 18, 18), colour::window,
                             colour::signal);
                    paragraph(dc, warning, content_left + 28, top + height + 18, content_width - 28, small_font,
                              colour::signal);
                }
            } else
                panel(dc, top, error_height + 28, colour::card, colour::mix(colour::border, outcome(), 90));
        }
    }
    void paint_progress(HDC dc) const {
        const int y = content_top + 58;
        draw(dc, std::to_wstring(progress_value) + L"%", content_right - 90, y, 90, 22, strong, colour::accent,
             DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        ui::progress(dc, box(content_left, y + 32, content_width, 8), progress_value, colour::window);
        const wchar_t *phases[] = {L"Check disc", L"Install game data", L"Prepare movies and audio",
                                   L"Verify and finish"};
        int phase = progress_value < 3 ? 0 : progress_value < 75 ? 1 : progress_value < 85 ? 2 : 3;
        for (int i = 0; i < 4; ++i) {
            const int row = y + 64 + i * 26;
            bool done = i < phase, active = i == phase;
            auto marker = box(content_left + 2, row + 1, 18, 18);
            if (done)
                ui::badge(dc, marker, colour::window, colour::success_shade, colour::success, ui::Icon::check,
                          colour::success);
            else
                ui::smooth_rounded(dc, marker, colour::window, active ? colour::accent : colour::window,
                                   active ? colour::accent : colour::border, px(9));
            draw(dc, phases[i], content_left + 32, row, content_width - 32, 20, active ? strong : body,
                 active ? colour::text : done ? colour::text_dim : colour::text_faint);
        }
        if (publishing || cancel.load())
            draw(dc,
                 cancel.load() ? L"Setup will stop as soon as it safely can."
                               : L"Cancel is unavailable while setup finishes the installation.",
                 content_left, content_bottom - 48, content_width, 20, small_font, colour::signal);
        draw(dc, L"Elapsed " + duration(), content_left, content_bottom - 22, content_width, 22, small_font,
             colour::text_faint);
    }
    void paint(HDC target, RECT dirty) {
        ui::Buffer buffer(target, dirty);
        auto dc = buffer.dc();
        auto visible = [&](RECT area) {
            RECT overlap;
            return IntersectRect(&overlap, &area, &dirty) != FALSE;
        };
        if (visible(box(0, 0, client_width, body_top)))
            paint_header(dc);
        if (visible(box(0, body_top, rail_width, footer_top - body_top)))
            paint_rail(dc);
        if (visible(box(0, footer_top, client_width, footer_height)))
            paint_footer(dc);
        if (visible(box(rail_width, body_top, client_width - rail_width, footer_top - body_top)))
            paint_content(dc);
    }
    void invalidate_page() {
        auto area = box(rail_width, body_top, client_width - rail_width, footer_top - body_top);
        InvalidateRect(window, &area, FALSE);
    }
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_COMMAND: {
            auto source = reinterpret_cast<HWND>(lparam);
            if (source && (source == iso_edit || source == destination_edit)) {
                auto code = HIWORD(wparam);
                if (code == EN_CHANGE && source == destination_edit)
                    refresh_space();
                if (code == EN_CHANGE || code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    auto area = box(content_left, panel_top, content_width, panel_height);
                    InvalidateRect(window, &area, FALSE);
                }
                return 0;
            }
            switch (LOWORD(wparam)) {
            case next_id:
                advance();
                break;
            case back_id:
                retain();
                --page;
                show();
                break;
            case cancel_id:
                close();
                break;
            case iso_browse_id: {
                auto value = browse_iso(window, text(iso_edit));
                if (!value.empty())
                    SetWindowTextW(iso_edit, value.c_str());
                break;
            }
            case folder_browse_id:
                try {
                    auto value = browse_folder(window, text(destination_edit));
                    if (!value.empty())
                        SetWindowTextW(destination_edit, value.c_str());
                } catch (const std::exception &e) {
                    MessageBoxW(window, wide(e.what()).c_str(), L"Lifeline Setup", MB_OK | MB_ICONERROR);
                }
                break;
            }
            return 0;
        }
        case status_message: {
            std::unique_ptr<std::wstring> value(reinterpret_cast<std::wstring *>(lparam));
            progress_value = std::max(progress_value, int(wparam));
            if (!cancel.load())
                SetWindowTextW(status, friendly(*value).c_str());
            if (wparam >= 97) {
                publishing = true;
                EnableWindow(cancel_button, FALSE);
            }
            invalidate_page();
            return 0;
        }
        case finished_message:
            if (worker.joinable())
                worker.join();
            finished = true;
            KillTimer(window, clock_timer);
            if (success) {
                progress_value = 100;
                if (result.contains("warnings") && result["warnings"].is_array() && !result["warnings"].empty() &&
                    result["warnings"][0].is_string())
                    warning = wide(result["warnings"][0].get<std::string>());
            }
            show();
            return 0;
        case WM_TIMER:
            update_time();
            return 0;
        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(wparam);
            auto role = reinterpret_cast<LONG_PTR>(GetPropW(reinterpret_cast<HWND>(lparam), role_property));
            SetTextColor(dc, role & role_dim ? colour::text_dim : colour::text);
            SetBkColor(dc, role & role_card ? colour::card : colour::window);
            return reinterpret_cast<LRESULT>(role & role_card ? card_brush : window_brush);
        }
        case WM_CTLCOLORBTN:
            SetBkColor(reinterpret_cast<HDC>(wparam), colour::window);
            return reinterpret_cast<LRESULT>(window_brush);
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wparam), colour::text);
            SetBkColor(reinterpret_cast<HDC>(wparam), colour::field);
            return reinterpret_cast<LRESULT>(field_brush);
        case WM_DRAWITEM:
            ui::draw_button(*reinterpret_cast<DRAWITEMSTRUCT *>(lparam));
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint_data;
            auto dc = BeginPaint(window, &paint_data);
            if (!IsRectEmpty(&paint_data.rcPaint))
                paint(dc, paint_data.rcPaint);
            EndPaint(window, &paint_data);
            return 0;
        }
        case WM_CLOSE:
            close();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(success ? 0 : finished ? 1 : 0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    static LRESULT CALLBACK procedure(HWND window_, UINT message, WPARAM wparam, LPARAM lparam) {
        auto self = reinterpret_cast<Wizard *>(GetWindowLongPtrW(window_, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            self = static_cast<Wizard *>(create->lpCreateParams);
            self->window = window_;
            SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handle(message, wparam, lparam)
                    : DefWindowProcW(window_, message, wparam, lparam);
    }

  public:
    explicit Wizard(InstallOptions configuration) : options(std::move(configuration)) {}
    ~Wizard() {
        if (worker.joinable())
            worker.join();
        for (auto font : {body, heading, subheading, brand, small_font, strong})
            if (font)
                DeleteObject(font);
        if (header_art)
            DeleteObject(header_art);
        DeleteObject(window_brush);
        DeleteObject(card_brush);
        DeleteObject(field_brush);
    }
    int run(HINSTANCE module, int show_command) {
        instance = module;
        if (options.iso.empty())
            options.iso = find_iso();
        if (options.destination.empty())
            options.destination = default_destination();
        SetProcessDPIAware();
        dpi = int(GetDpiForSystem());
        body = ui::make_font(13, FW_NORMAL, dpi);
        strong = ui::make_font(13, FW_SEMIBOLD, dpi);
        small_font = ui::make_font(12, FW_NORMAL, dpi);
        subheading = ui::make_font(14, FW_NORMAL, dpi);
        heading = ui::make_font(27, FW_SEMIBOLD, dpi);
        brand = ui::make_font(30, FW_SEMIBOLD, dpi, ui::display_face());
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = instance;
        type.lpfnWndProc = procedure;
        type.lpszClassName = L"LifelineNativeSetup";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        type.hIconSm = type.hIcon;
        require(RegisterClassExW(&type), "Cannot register setup window");
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT rectangle{0, 0, px(client_width), px(client_height)};
        AdjustWindowRectEx(&rectangle, style, FALSE, WS_EX_CONTROLPARENT);
        auto width = int(rectangle.right - rectangle.left), height = int(rectangle.bottom - rectangle.top);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Lifeline Setup", style,
                                 work.left + std::max(0, int(work.right - work.left) - width) / 2,
                                 work.top + std::max(0, int(work.bottom - work.top) - height) / 2, width, height,
                                 nullptr, nullptr, instance, this);
        require(window, "Cannot create setup window");
        BOOL dark_title = TRUE;
        DwmSetWindowAttribute(window, 20, &dark_title, sizeof(dark_title));
        show();
        ShowWindow(window, show_command);
        UpdateWindow(window);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return int(message.wParam);
    }
};
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    fs::path report_path;
    bool unattended = false;
    bool movie_worker = false;
    int result_code = 0;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        require(arguments, "Cannot read setup arguments");
        struct Free {
            wchar_t **p;
            ~Free() { LocalFree(p); }
        } free{arguments};
        movie_worker = count > 1 && std::wstring_view(arguments[1]) == L"--convert-movie";
        if (movie_worker) {
            unattended = true;
            require(count == 7, "Incomplete movie worker arguments");
            if (SUCCEEDED(com))
                CoUninitialize();
            com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            require(SUCCEEDED(com), "Cannot initialize movie worker COM services");
            convert_movie(arguments[2], arguments[3], utf8(arguments[4]), utf8(arguments[5]), utf8(arguments[6]));
            if (SUCCEEDED(com))
                CoUninitialize();
            return 0;
        }
        InstallOptions options;
        bool validate = false, verify = false, check = false;
        fs::path verify_root;
        for (int i = 1; i < count; ++i) {
            std::wstring argument = arguments[i];
            auto value = [&] {
                require(i + 1 < count, "Incomplete setup argument");
                return fs::path(arguments[++i]);
            };
            if (argument == L"--install") {
                unattended = true;
            } else if (argument == L"--iso")
                options.iso = value();
            else if (argument == L"--destination")
                options.destination = value();
            else if (argument == L"--cache-data")
                options.cache_data = value();
            else if (argument == L"--desktop")
                options.desktop = true;
            else if (argument == L"--no-shortcuts")
                options.desktop = options.start_menu = false;
            else if (argument == L"--no-start-menu")
                options.start_menu = false;
            else if (argument == L"--report")
                report_path = value();
            else if (argument == L"--validate-iso") {
                validate = true;
                unattended = true;
            } else if (argument == L"--verify-installation") {
                verify = true;
                unattended = true;
                verify_root = value();
            } else if (argument == L"--check-package") {
                check = true;
                unattended = true;
            } else
                throw std::runtime_error("Unknown setup argument: " + utf8(argument));
        }
        Json result;
        if (validate) {
            require(!options.iso.empty(), "--validate-iso requires --iso");
            Disc disc(options.iso);
            result = disc.identity();
            result["game"] = "SLUS_208.48";
            result["archive_files"] = disc.files().size();
        } else if (verify)
            result = verify_installation(verify_root);
        else if (check) {
            auto package = Package::embedded();
            auto recipe = Json::parse(package.get("recipe.json"));
            for (const auto &file : package.files())
                package.get(file.name);
            result = {
                {"files", package.files().size()}, {"assets", recipe.at("assets").size()}, {"native", true}};
        } else if (unattended) {
            if (options.iso.empty())
                options.iso = find_iso();
            require(!options.iso.empty() && !options.destination.empty(),
                    "--install requires an ISO and --destination");
            result = perform_install(options, {});
        } else {
            Wizard application(std::move(options));
            result_code = application.run(instance, show);
        }
        if (unattended) {
            result["success"] = true;
            if (!report_path.empty())
                write_text(report_path, result.dump(2) + "\n");
        }
    } catch (const std::exception &error) {
        result_code = 1;
        if (movie_worker) {
            DWORD written = 0;
            auto message = std::string(error.what()) + "\n";
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), DWORD(message.size()), &written, nullptr);
        }
        if (!report_path.empty())
            try {
                write_text(report_path, Json{{"success", false}, {"error", error.what()}}.dump(2) + "\n");
            } catch (...) {
            }
        if (!unattended)
            MessageBoxW(nullptr, wide(error.what()).c_str(), L"Lifeline Setup", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return result_code;
}
