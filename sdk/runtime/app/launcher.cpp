// The launcher, before the game starts: the settings the settings file keeps, and the game to play, from the assets
// folder or picked anywhere on the PC. Up at each start unless turned off; Shift held at the start, or no game found,
// brings it back. Painted like the game code window, and driven by the keyboard, the mouse or a controller.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <xinput.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/disc.h"

using Microsoft::WRL::ComPtr;

namespace app {

namespace {

namespace fs = std::filesystem;

// the game code window's colours
constexpr COLORREF kBackground = RGB(10, 11, 10);
constexpr COLORREF kText = RGB(227, 232, 219);
constexpr COLORREF kDim = RGB(145, 153, 138);
constexpr COLORREF kBarEmpty = RGB(28, 33, 26);
constexpr COLORREF kBarFull = RGB(145, 158, 130);

// a setting: what it is called, what it does, and the values it steps through
struct Row {
    const wchar_t* label;
    const wchar_t* hint;
    std::vector<std::wstring> names;
    std::vector<uint64_t> values;
    size_t pick = 0;
    uint64_t Value() const { return values[pick]; }
};

enum RowId : size_t {
    kRowGame,
    kRowDisplay,
    kRowResolution,
    kRowRender,
    kRowFps,
    kRowVsync,
    kRowAniso,
    kRowTextures,
    kRowLauncher,
    kRows
};

struct State {
    std::vector<Row> rows;
    std::vector<fs::path> games;  // the game row's values index this
    size_t focus = 0;
    bool play = false;
    bool browsing = false;  // the file dialog or its message is up: the timer still polls the controller
    HFONT font = nullptr;
    HFONT heading = nullptr;
};
State g;

std::wstring Widen(const char* s) { return std::wstring(s, s + std::char_traits<char>::length(s)); }

std::wstring Size(uint64_t w, uint64_t h) { return std::to_wstring(w) + L" \u00D7 " + std::to_wstring(h); }

// the values in order, with the current one added when it is none of them (set by hand in the settings file)
template <typename Name>
Row Choices(const wchar_t* label, const wchar_t* hint, std::vector<uint64_t> values, uint64_t current, Name name) {
    if (std::find(values.begin(), values.end(), current) == values.end()) {
        values.push_back(current);
        std::sort(values.begin(), values.end());
    }
    Row row{label, hint};
    for (uint64_t v : values) row.names.push_back(name(v));
    row.values = values;
    row.pick = size_t(std::find(values.begin(), values.end(), current) - values.begin());
    return row;
}

Row Toggle(const wchar_t* label, const wchar_t* hint, bool current) {
    return Choices(label, hint, {0, 1}, current, [](uint64_t v) { return std::wstring(v ? L"On" : L"Off"); });
}

std::wstring GameName(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec) ? path.filename().wstring() + L" folder" : path.filename().wstring();
}

void FillGameRow(size_t pick) {
    Row& row = g.rows[kRowGame];
    row.names.clear();
    row.values.clear();
    for (size_t i = 0; i < g.games.size(); i++) {
        row.names.push_back(GameName(g.games[i]));
        row.values.push_back(i);
    }
    if (g.games.empty()) {
        row.names.push_back(L"None found");
        row.values.push_back(0);
    }
    row.pick = std::min(pick, row.values.size() - 1);
}

// each width and height the primary monitor offers, the biggest first, after the desktop's own
std::vector<uint64_t> Resolutions() {
    std::vector<uint64_t> modes;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++)
        if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) modes.push_back(uint64_t(dm.dmPelsWidth) << 32 | dm.dmPelsHeight);
    const Settings& s = Get();
    if (s.width && s.height) modes.push_back(uint64_t(s.width) << 32 | s.height);
    std::sort(modes.rbegin(), modes.rend());
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    modes.insert(modes.begin(), 0);
    return modes;
}

void BuildRows() {
    const Settings& s = Get();
    g.rows.assign(kRows, Row{});
    g.rows[kRowGame] = {L"Game", L""};
    g.games = GameChoices();
    FillGameRow(0);
    const wchar_t* display_hint = L"Fullscreen switches the monitor to the resolution; borderless keeps the desktop's. "
                                  L"Alt+Enter in the game toggles windowed.";
    g.rows[kRowDisplay] = Choices(L"Display mode", display_hint, {0, 1, 2}, uint64_t(s.display_mode), [](uint64_t v) {
        const wchar_t* names[] = {L"Fullscreen", L"Borderless", L"Windowed"};
        return std::wstring(names[std::min<uint64_t>(v, 2)]);
    });
    Row resolution{L"Resolution", L"The size of the picture. Desktop is the monitor's own."};
    resolution.values = Resolutions();
    Mode desktop = Desktop();
    for (uint64_t v : resolution.values)
        resolution.names.push_back(v ? Size(v >> 32, v & 0xFFFFFFFF) : L"Desktop (" + Size(desktop.width, desktop.height) + L")");
    uint64_t current = s.width && s.height ? uint64_t(s.width) << 32 | s.height : 0;
    resolution.pick = size_t(std::find(resolution.values.begin(), resolution.values.end(), current) - resolution.values.begin());
    g.rows[kRowResolution] = resolution;
    g.rows[kRowRender] = Choices(L"Render scale", L"The 3D scene's resolution, as a share of the picture's height.",
                              {50, 75, 100, 125, 150, 200}, s.render_percent,
                              [](uint64_t v) { return std::to_wstring(v) + L"%"; });
    g.rows[kRowFps] = Choices(L"Frame rate cap", L"The most frames shown each second.", {0, kFpsCapRefresh, 30, 60, 120, 144, 240},
                           s.fps_cap, [](uint64_t v) {
                               if (!v) return std::wstring(L"Unlimited");
                               if (v == kFpsCapRefresh) return std::wstring(L"Monitor's refresh rate");
                               return std::to_wstring(v) + L" FPS";
                           });
    g.rows[kRowVsync] = Toggle(L"VSync", L"Presents with the monitor's refresh, so frames do not tear.", s.vsync);
    g.rows[kRowAniso] = Choices(L"Anisotropic filtering", L"Sharper textures seen at an angle.", {0, 2, 4, 8, 16}, s.aniso,
                             [](uint64_t v) { return v ? std::to_wstring(v) + L"\u00D7" : std::wstring(L"As the game asks"); });
    g.rows[kRowTextures] = Toggle(L"Custom textures", L"Images in textures\\replacement beside the game replace its own.",
                               s.custom_textures);
    g.rows[kRowLauncher] = Toggle(L"Show this launcher at start",
                                   L"Off starts the game at once. Hold Shift while starting it to see this again.", s.launcher);
}

// as the settings file keeps it: inside the exe's folder relative to it, so the folder can move, else whole
std::string Stored(const fs::path& path) {
    std::error_code ec;
    fs::path relative = fs::relative(path, ExeDir(), ec);
    bool inside = !ec && !relative.empty() && *relative.begin() != "..";
    std::u8string text = (inside ? relative : path).u8string();
    return std::string(text.begin(), text.end());
}

void Apply() {
    Settings& s = Get();
    s.game = Stored(g.games[g.rows[kRowGame].pick]);
    s.display_mode = DisplayMode(g.rows[kRowDisplay].Value());
    uint64_t size = g.rows[kRowResolution].Value();
    s.width = uint32_t(size >> 32);
    s.height = uint32_t(size);
    s.render_percent = uint32_t(g.rows[kRowRender].Value());
    s.fps_cap = uint32_t(g.rows[kRowFps].Value());
    s.vsync = g.rows[kRowVsync].Value();
    s.aniso = uint32_t(g.rows[kRowAniso].Value());
    s.custom_textures = g.rows[kRowTextures].Value();
    s.launcher = g.rows[kRowLauncher].Value();
    Save();
    LOGR("Launcher: playing %s", s.game.c_str());
}

// a disc image that holds the xbe, or the extracted files' folder for their xbe; empty with the reason otherwise
std::wstring CheckPick(fs::path& path) {
    std::wstring name = path.filename().wstring();
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    std::wstring xbe = Widen(IMAGE_XBE);
    if (lower == xbe) {
        path = path.parent_path();
        return {};
    }
    if (lower.ends_with(L".iso")) {
        kernel::Disc disc;
        std::string error;
        if (!disc.Open(path, error)) return name + L" is not an Xbox disc image.";
        if (!disc.Find(IMAGE_XBE)) return name + L" has no " + xbe + L" on it, so it is not this game's disc.";
        return {};
    }
    return L"Pick the game's disc image (.iso), or the " + xbe + L" among its extracted files.";
}

// the file dialog, from the assets folder; a good pick joins the game row and is chosen
void Pick(HWND hwnd) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    fs::path picked;
    ComPtr<IFileOpenDialog> dialog;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        std::wstring spec = L"*.iso;" + Widen(IMAGE_XBE);
        COMDLG_FILTERSPEC filters[] = {{L"The game (disc image or extracted files)", spec.c_str()}};
        dialog->SetFileTypes(1, filters);
        dialog->SetTitle(L"Choose the game");
        ComPtr<IShellItem> folder;
        if (fs::path assets = AssetsDir();
            !assets.empty() && SUCCEEDED(SHCreateItemFromParsingName(assets.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            dialog->SetDefaultFolder(folder.Get());
        ComPtr<IShellItem> item;
        PWSTR name = nullptr;
        if (SUCCEEDED(dialog->Show(hwnd)) && SUCCEEDED(dialog->GetResult(&item)) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            picked = name;
            CoTaskMemFree(name);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    if (picked.empty()) return;
    if (std::wstring problem = CheckPick(picked); !problem.empty()) {
        MessageBoxW(hwnd, problem.c_str(), kGame.short_title, MB_ICONWARNING);
        return;
    }
    std::error_code ec;
    size_t at = 0;
    while (at < g.games.size() && !fs::equivalent(g.games[at], picked, ec)) at++;
    if (at == g.games.size()) g.games.push_back(picked);
    FillGameRow(at);
    g.focus = kRowGame;
}

void Browse(HWND hwnd) {
    g.browsing = true;
    Pick(hwnd);
    g.browsing = false;
}

void Step(int delta) {
    Row& row = g.rows[g.focus];
    size_t n = row.values.size();
    row.pick = delta > 0 ? (row.pick + 1) % n : (row.pick + n - 1) % n;
}

void Close(HWND hwnd, bool play) {
    if (play && g.games.empty()) {
        Browse(hwnd);
        if (g.games.empty()) return;
    }
    g.play = play;
    if (play) Apply();
    DestroyWindow(hwnd);
}

int Unit(HWND hwnd) { return MulDiv(10, GetDpiForWindow(hwnd), 96); }

// top to bottom: the heading, a row each setting, the focused one's hint, then the buttons
struct Layout {
    RECT heading;
    RECT rows[kRows];
    RECT hint;
    RECT browse;
    RECT play;
    RECT exit;
};

Layout Arrange(const RECT& client, int unit) {
    Layout l;
    l.heading = {client.left + unit * 2, client.top + unit, client.right - unit * 2, client.top + unit * 5};
    LONG y = l.heading.bottom;
    for (RECT& row : l.rows) {
        row = {client.left + unit, y, client.right - unit, y + unit * 3};
        y = row.bottom;
    }
    l.hint = {client.left + unit * 2, y + unit, client.right - unit * 2, y + unit * 4};
    LONG top = l.hint.bottom + unit, bottom = top + unit * 3;
    l.exit = {client.right - unit * 2 - unit * 9, top, client.right - unit * 2, bottom};
    l.play = {l.exit.left - unit - unit * 10, top, l.exit.left - unit, bottom};
    l.browse = {client.left + unit * 2, top, client.left + unit * 2 + unit * 15, bottom};
    return l;
}

int ClientHeight(int unit) { return unit * (5 + 3 * int(kRows) + 1 + 3 + 1 + 3 + 2); }

void DrawLine(HDC dc, RECT box, const std::wstring& text, COLORREF color, UINT align) {
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), int(text.size()), &box, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

void FillBox(HDC dc, const RECT& box, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &box, brush);
    DeleteObject(brush);
}

// the value right aligned, between arrows on the focused row
void PaintRow(HDC dc, const Row& row, RECT box, bool focused, int unit) {
    if (focused) {
        FillBox(dc, box, kBarEmpty);
        FillBox(dc, {box.left, box.top, box.left + unit / 3, box.bottom}, kBarFull);
    }
    RECT label = box;
    label.left += unit;
    DrawLine(dc, label, row.label, focused ? kText : kDim, DT_LEFT);
    RECT value = box;
    value.right -= unit * 3;
    value.left = (box.left + box.right) / 2;
    const std::wstring& name = row.names[row.pick];
    DrawLine(dc, value, name, kText, DT_RIGHT | DT_END_ELLIPSIS);
    if (!focused || row.values.size() < 2) return;
    SIZE extent{};
    GetTextExtentPoint32W(dc, name.c_str(), int(name.size()), &extent);
    LONG left = std::max(value.left, value.right - extent.cx) - unit * 2;
    HGDIOBJ old = SelectObject(dc, g.heading);
    DrawLine(dc, {left, box.top, left + unit, box.bottom}, L"\u2039", kBarFull, DT_CENTER);
    DrawLine(dc, {box.right - unit * 2, box.top, box.right - unit, box.bottom}, L"\u203A", kBarFull, DT_CENTER);
    SelectObject(dc, old);
}

std::wstring Hint() {
    if (g.focus != kRowGame) return g.rows[g.focus].hint;
    if (g.games.empty()) return L"No game in the assets folder. Browse for its disc image (.iso) or " + Widen(IMAGE_XBE) + L".";
    return g.games[g.rows[kRowGame].pick].wstring();
}

void Paint(HDC dc, const RECT& client, int unit) {
    Layout l = Arrange(client, unit);
    FillBox(dc, client, kBackground);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old = SelectObject(dc, g.heading);
    DrawLine(dc, l.heading, kGame.title, kText, DT_LEFT);
    SelectObject(dc, old);
    for (size_t i = 0; i < kRows; i++) PaintRow(dc, g.rows[i], l.rows[i], i == g.focus, unit);
    // a path on one line, shortened in its middle; a hint wraps
    RECT hint = l.hint;
    std::wstring text = Hint();
    bool path = g.focus == kRowGame && !g.games.empty();
    SetTextColor(dc, kDim);
    DrawTextW(dc, text.c_str(), int(text.size()), &hint,
              DT_LEFT | DT_NOPREFIX | (path ? DT_SINGLELINE | DT_PATH_ELLIPSIS : DT_WORDBREAK));
    FillBox(dc, l.browse, kBarEmpty);
    DrawLine(dc, l.browse, L"Browse for the game (X)", kText, DT_CENTER);
    FillBox(dc, l.play, kBarFull);
    DrawLine(dc, l.play, L"Play (A)", kBackground, DT_CENTER);
    FillBox(dc, l.exit, kBarEmpty);
    DrawLine(dc, l.exit, L"Exit (B)", kText, DT_CENTER);
}

// painted off screen, then copied, so it does not flicker
void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
    HGDIOBJ old_font = SelectObject(memory, g.font);
    Paint(memory, client, Unit(hwnd));
    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_font);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd, &ps);
}

// a row picks it and steps its value, back for the right button; the game's value browses
void OnClick(HWND hwnd, LPARAM lp, int delta) {
    RECT client;
    GetClientRect(hwnd, &client);
    Layout l = Arrange(client, Unit(hwnd));
    POINT at{short(LOWORD(lp)), short(HIWORD(lp))};
    if (delta > 0 && PtInRect(&l.play, at)) return Close(hwnd, true);
    if (delta > 0 && PtInRect(&l.exit, at)) return Close(hwnd, false);
    if (delta > 0 && PtInRect(&l.browse, at)) return Browse(hwnd);
    for (size_t i = 0; i < kRows; i++) {
        if (!PtInRect(&l.rows[i], at)) continue;
        bool on_value = at.x >= (l.rows[i].left + l.rows[i].right) / 2;
        bool was_focused = g.focus == i;
        g.focus = i;
        if (i == kRowGame && on_value && delta > 0) Browse(hwnd);
        else if (on_value || was_focused) Step(delta);
    }
}

// the d-pad moves and steps, A plays, B leaves, X browses; on the press, not the hold
void PollController(HWND hwnd) {
    static uint16_t held = 0;
    uint16_t now = input::HeldOnAnyController();
    uint16_t pressed = now & ~held;
    held = now;
    if (g.browsing) return;
    if (pressed & XINPUT_GAMEPAD_DPAD_UP) g.focus = (g.focus + kRows - 1) % kRows;
    if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) g.focus = (g.focus + 1) % kRows;
    if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) Step(-1);
    if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) Step(1);
    if (pressed & XINPUT_GAMEPAD_X) Browse(hwnd);
    else if (pressed & XINPUT_GAMEPAD_A) Close(hwnd, true);
    else if (pressed & XINPUT_GAMEPAD_B) Close(hwnd, false);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintWindow(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            PollController(hwnd);
            if (IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            OnClick(hwnd, lp, msg == WM_LBUTTONUP ? 1 : -1);
            if (IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_UP:
                    g.focus = (g.focus + kRows - 1) % kRows;
                    break;
                case VK_DOWN:
                case VK_TAB:
                    g.focus = (g.focus + 1) % kRows;
                    break;
                case VK_LEFT:
                    Step(-1);
                    break;
                case VK_RIGHT:
                    Step(1);
                    break;
                case VK_SPACE:
                    Browse(hwnd);
                    break;
                case VK_RETURN:
                case VK_ESCAPE:
                    Close(hwnd, wp == VK_RETURN);
                    return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            Close(hwnd, false);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// centred on the primary monitor, sized for its DPI, in the message font
HWND OpenWindow() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    std::wstring class_name = std::wstring(kGame.name) + L"Launcher";
    wc.lpszClassName = class_name.c_str();
    RegisterClassExW(&wc);
    UINT dpi = GetDpiForSystem();
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, MulDiv(560, dpi, 96), ClientHeight(MulDiv(10, dpi, 96))};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, kGame.title, style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    g.font = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW heading = metrics.lfMessageFont;
    heading.lfHeight = heading.lfHeight * 3 / 2;
    heading.lfWeight = FW_SEMIBOLD;
    g.heading = CreateFontIndirectW(&heading);
    SetTimer(hwnd, 1, 50, nullptr);  // the controller is read with each repaint
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    return hwnd;
}

}  // namespace

bool Launch() {
    bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    if (!Get().launcher && !shift && !GameChoices().empty()) return true;
    BuildRows();
    input::Init();
    HWND hwnd = OpenWindow();
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(g.font);
    DeleteObject(g.heading);
    return g.play;
}

}  // namespace app
