#include "SettingsWindow.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

#include <commctrl.h>

#include "OverlayWindow.h"
#include "pacecar/app/HotkeySpec.h"
#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kSettingsClassName[] = L"PacecarSettingsWindow";
constexpr wchar_t kHotkeyClassName[] = L"PacecarHotkeyCapture";
constexpr int kBottomStrip = 42;

// Control ids. Ranges are laid out so the owning field is easy to recover from the id.
constexpr int kIdTab = 1000;
constexpr int kIdReset = 1001;
constexpr int kIdClose = 1002;

constexpr int kIdRefreshCombo = 1010;
constexpr int kIdOpacitySlider = 1011;
constexpr int kIdThemeCombo = 1013;
constexpr int kIdLayoutCombo = 1014;
constexpr int kIdStartWindows = 1015;
constexpr int kIdStartHidden = 1016;

constexpr int kIdModeCombo = 1020;
constexpr int kIdAlwaysOnTop = 1021;
constexpr int kIdMonitorCombo = 1022;
constexpr int kIdCaptureExclusion = 1023;

constexpr int kIdTileVisibleBase = 1100;
constexpr int kIdTilePrimaryBase = 1120;
constexpr int kIdTileSecondaryBase = 1140;
constexpr int kIdTileTertiaryBase = 1160;
constexpr int kIdTileGraphBase = 1180;
constexpr int kIdTileMiniBase = 1200;
constexpr int kIdTileVisComboBase = 1220;

constexpr int kIdSensorToggleBase = 1300;
constexpr int kIdDeepSensorsToggle = 1306; // after the six per-sensor toggles (1300..1305)
constexpr int kIdDeviceComboBase = 1320;
constexpr int kIdDiskTempCombo = 1330;
constexpr int kIdFanModeCombo = 1331;
constexpr int kIdMainboardModeCombo = 1332;
constexpr int kIdPingEdit = 1333;

constexpr int kIdRetentionCombo = 1400;

constexpr int kIdOverlayHotkey = 1410;
constexpr int kIdClickThroughHotkey = 1411;

constexpr int kIdAboutText = 1420;
constexpr int kIdCopyInfo = 1421;

ATOM g_class = 0;
ATOM g_hotkeyClass = 0;
HFONT g_font = nullptr;

constexpr int kPageGeneral = 0;
constexpr int kPageOverlay = 1;
constexpr int kPageTiles = 2;
constexpr int kPageSensors = 3;
constexpr int kPageHistory = 4;
constexpr int kPageHotkeys = 5;
constexpr int kPageAbout = 6;
constexpr int kPageCount = 7;

void EnsureFont()
{
    if (g_font != nullptr)
    {
        return;
    }
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
    {
        g_font = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (g_font == nullptr)
    {
        g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
}

std::wstring WideFromUtf8(std::string_view text)
{
    std::wstring out;
    out.reserve(text.size());
    for (const char c : text)
    {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string Utf8FromWide(std::wstring_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const wchar_t c : text)
    {
        out.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
    }
    return out;
}

void SetControlFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

// The key-capture control: a focusable, edit-like child that turns the next key chord into a
// canonical hotkey string and notifies its parent. Never a raw text box, so the stored value is
// always a parseable binding.
LRESULT CALLBACK HotkeyCaptureProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_CHAR:
        return 0; // Swallow the beep; the binding is built from WM_KEYDOWN.
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const std::uint32_t vk = static_cast<std::uint32_t>(wParam);
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN)
        {
            return 0; // A modifier alone is not a binding.
        }
        if (vk == VK_DELETE || vk == VK_BACK)
        {
            SetWindowTextW(hwnd, L"");
            PostMessageW(GetParent(hwnd), kWmHotkeyCaptured,
                         static_cast<WPARAM>(GetDlgCtrlID(hwnd)), 0);
            return 0;
        }

        pacecar::app::HotkeyBinding binding{};
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            binding.modifiers |= pacecar::app::kHotkeyControl;
        }
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            binding.modifiers |= pacecar::app::kHotkeyShift;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        {
            binding.modifiers |= pacecar::app::kHotkeyAlt;
        }
        if (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0)
        {
            binding.modifiers |= pacecar::app::kHotkeyWin;
        }
        binding.virtualKey = vk;

        const std::wstring text = pacecar::app::FormatHotkey(binding);
        if (text.empty())
        {
            return 0; // Unsupported key: leave the previous value in place.
        }
        SetWindowTextW(hwnd, text.c_str());
        PostMessageW(GetParent(hwnd), kWmHotkeyCaptured,
                     static_cast<WPARAM>(GetDlgCtrlID(hwnd)), 0);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        HBRUSH background = GetSysColorBrush(COLOR_WINDOW);
        FillRect(dc, &rect, background);
        FrameRect(dc, &rect, GetSysColorBrush(COLOR_WINDOWFRAME));

        wchar_t text[128] = {};
        GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ oldFont = SelectObject(dc, g_font);
        RECT textRect = rect;
        textRect.left += 6;
        textRect.right -= 4;
        DrawTextW(dc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);

        if (GetFocus() == hwnd)
        {
            RECT focus = rect;
            InflateRect(&focus, -2, -2);
            DrawFocusRect(dc, &focus);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsureClasses(HINSTANCE instance)
{
    if (g_class == 0)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = SettingsWindow::StaticWndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        windowClass.lpszClassName = kSettingsClassName;
        g_class = RegisterClassExW(&windowClass);
    }
    if (g_hotkeyClass == 0)
    {
        WNDCLASSEXW captureClass{};
        captureClass.cbSize = sizeof(captureClass);
        captureClass.style = CS_HREDRAW | CS_VREDRAW;
        captureClass.lpfnWndProc = HotkeyCaptureProc;
        captureClass.hInstance = instance;
        captureClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        captureClass.hbrBackground = nullptr;
        captureClass.lpszClassName = kHotkeyClassName;
        g_hotkeyClass = RegisterClassExW(&captureClass);
    }
    return g_class != 0 && g_hotkeyClass != 0;
}
} // namespace

SettingsWindow::~SettingsWindow()
{
    Close();
}

bool SettingsWindow::Open(const SettingsWindowHooks& hooks)
{
    hooks_ = hooks;
    if (hwnd_ == nullptr)
    {
        if (!Create(hooks.instance))
        {
            return false;
        }
    }
    RefreshFromConfig();
    if (hwnd_ == nullptr)
    {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void SettingsWindow::Close()
{
    if (hwnd_ != nullptr)
    {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(hwnd);
    }
}

bool SettingsWindow::Create(HINSTANCE instance)
{
    instance_ = instance;
    EnsureFont();
    if (!EnsureClasses(instance))
    {
        return false;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, kSettingsClassName, L"Pacecar Settings",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 540,
                            470, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr)
    {
        return false;
    }

    if (hooks_.config != nullptr)
    {
        binding_ = std::make_unique<SettingsBinding>(*hooks_.config, [this] { ApplyLive(); });
    }
    BuildPages();
    ShowPage(kPageGeneral);
    return true;
}

void SettingsWindow::AddControl(int page, HWND control)
{
    if (control == nullptr)
    {
        return;
    }
    SetControlFont(control);
    if (page >= 0 && page < static_cast<int>(pages_.size()))
    {
        pages_[page].controls.push_back(control);
    }
}

void SettingsWindow::BuildPages()
{
    pages_.clear();
    pages_.resize(kPageCount);
    pages_[kPageGeneral].title = L"General";
    pages_[kPageOverlay].title = L"Overlay";
    pages_[kPageTiles].title = L"Tiles";
    pages_[kPageSensors].title = L"Sensors";
    pages_[kPageHistory].title = L"History";
    pages_[kPageHotkeys].title = L"Hotkeys";
    pages_[kPageAbout].title = L"About";

    tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           8, 8, 500, 380, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab)), instance_, nullptr);
    SetControlFont(tab_);
    for (int i = 0; i < kPageCount; ++i)
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(pages_[i].title.c_str());
        TabCtrl_InsertItem(tab_, i, &item);
    }

    BuildGeneralPage();
    BuildOverlayPage();
    BuildTilesPage();
    BuildSensorsPage();
    BuildHistoryPage();
    BuildHotkeysPage();
    BuildAboutPage();

    // Always-visible footer.
    HWND reset = CreateWindowExW(0, L"BUTTON", L"Reset to defaults",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 8, 400, 130, 26,
                                 hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdReset)),
                                 instance_, nullptr);
    SetControlFont(reset);
    HWND close = CreateWindowExW(0, L"BUTTON", L"Close",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 430, 400, 80,
                                 26, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClose)), instance_,
                                 nullptr);
    SetControlFont(close);
}

namespace
{
HWND MakeLabel(HWND parent, HINSTANCE instance, const wchar_t* text, int x, int y, int w, int h = 18)
{
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h,
                                 parent, nullptr, instance, nullptr);
    SetControlFont(label);
    return label;
}
} // namespace

void SettingsWindow::BuildGeneralPage()
{
    const int page = kPageGeneral;
    AddControl(page, MakeLabel(hwnd_, instance_, L"Refresh rate (ms):", 16, 48, 120));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Opacity:", 16, 80, 120));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Theme:", 16, 112, 120));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Layout preset:", 16, 144, 120));

    HWND refresh = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                             CBS_DROPDOWNLIST | WS_VSCROLL, 150, 45,
                                   140, 200, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRefreshCombo)),
                                   instance_, nullptr);
    AddControl(page, refresh);
    for (const int value : kRefreshOptions)
    {
        SendMessageW(refresh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(value).c_str()));
    }

    HWND opacity = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 150,
                                   78, 180, 24, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpacitySlider)),
                                   instance_, nullptr);
    AddControl(page, opacity);
    SendMessageW(opacity, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    AddControl(page, MakeLabel(hwnd_, instance_, L"100%", 336, 80, 60));

    HWND theme = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                           CBS_DROPDOWNLIST | WS_VSCROLL, 150, 109, 140,
                                 200, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdThemeCombo)),
                                 instance_, nullptr);
    AddControl(page, theme);
    const std::array<const wchar_t*, 3> themeNames{L"Dark", L"Light", L"High Contrast"};
    for (const wchar_t* name : themeNames)
    {
        SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }

    HWND layout = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                            CBS_DROPDOWNLIST | WS_VSCROLL, 150, 141, 180,
                                  200, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLayoutCombo)),
                                  instance_, nullptr);
    AddControl(page, layout);
    const std::array<const wchar_t*, 4> layoutNames{L"Compact 3x3", L"Vertical 1x6", L"Auto-fit",
                                                    L"Custom"};
    for (const wchar_t* name : layoutNames)
    {
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }

    AddControl(page, CreateWindowExW(0, L"BUTTON", L"Start with Windows",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 178, 220,
                                     22, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStartWindows)),
                                     instance_, nullptr));
    AddControl(page, CreateWindowExW(0, L"BUTTON", L"Start hidden (tray only)",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 204, 260,
                                     22, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStartHidden)),
                                     instance_, nullptr));
}

void SettingsWindow::BuildOverlayPage()
{
    const int page = kPageOverlay;
    AddControl(page, MakeLabel(hwnd_, instance_, L"Mode:", 16, 48, 120));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Monitor:", 16, 80, 120));

    HWND mode = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                         CBS_DROPDOWNLIST | WS_VSCROLL, 150, 45, 160,
                                 200, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdModeCombo)), instance_,
                                 nullptr);
    AddControl(page, mode);
    SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Interactive"));
    SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Click-through"));

    HWND monitor = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                            CBS_DROPDOWNLIST | WS_VSCROLL, 150, 77, 200,
                                  200, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMonitorCombo)),
                                  instance_, nullptr);
    AddControl(page, monitor);
    const std::vector<MonitorWorkArea> monitors = EnumerateMonitorWorkAreas();
    for (std::size_t i = 0; i < monitors.size(); ++i)
    {
        std::wstring text = L"Monitor " + std::to_wstring(i + 1);
        if (monitors[i].primary)
        {
            text += L" (primary)";
        }
        SendMessageW(monitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    AddControl(page, CreateWindowExW(0, L"BUTTON", L"Always on top",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 116, 220,
                                     22, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAlwaysOnTop)),
                                     instance_, nullptr));
    AddControl(page, CreateWindowExW(0, L"BUTTON", L"Exclude from screen capture",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 142, 300,
                                     22, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCaptureExclusion)),
                                     instance_, nullptr));
    AddControl(page, MakeLabel(hwnd_, instance_,
                               L"Enhanced fullscreen is reserved and is not available in this build.",
                               16, 176, 420, 18));
}

void SettingsWindow::BuildTilesPage()
{
    const int page = kPageTiles;
    const std::array<const wchar_t*, 6> tileNames{L"CPU", L"RAM", L"GPU", L"Network", L"Disk", L"Ping"};
    const std::array<const wchar_t*, 5> headers{L"Visible", L"Primary", L"Secondary", L"Tertiary",
                                                L"Graph"};
    const int columnX[5] = {80, 160, 236, 312, 388};

    for (int c = 0; c < 5; ++c)
    {
        AddControl(page, MakeLabel(hwnd_, instance_, headers[c], columnX[c], 42, 76));
    }
    AddControl(page, MakeLabel(hwnd_, instance_, L"Mini", 80, 60, 76));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Spark", 160, 60, 76));

    for (int t = 0; t < 6; ++t)
    {
        const int y = 82 + t * 26;
        AddControl(page, MakeLabel(hwnd_, instance_, tileNames[t], 16, y, 60));

        const auto makeCheck = [&](int id, int x, const wchar_t* text)
        {
            HWND box = CreateWindowExW(
                0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y - 2,
                76, 22, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
            AddControl(page, box);
        };
        makeCheck(kIdTileVisibleBase + t, columnX[0], L"");
        makeCheck(kIdTilePrimaryBase + t, columnX[1], L"");
        makeCheck(kIdTileSecondaryBase + t, columnX[2], L"");
        makeCheck(kIdTileTertiaryBase + t, columnX[3], L"");
        makeCheck(kIdTileGraphBase + t, columnX[4], L"");
        makeCheck(kIdTileMiniBase + t, 84, L"");
        makeCheck(kIdTileVisComboBase + t, 160, L"Sparklines");
    }
}

void SettingsWindow::BuildSensorsPage()
{
    const int page = kPageSensors;
    const int labelX = 16;
    const int comboX = 150;
    AddControl(page, MakeLabel(hwnd_, instance_, L"GPU selection:", labelX, 44, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"CPU selection:", labelX, 72, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"NIC selection:", labelX, 100, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Disk selection:", labelX, 128, 130));

    const std::array<DeviceKind, 4> kinds{DeviceKind::Gpu, DeviceKind::Cpu, DeviceKind::Nic,
                                          DeviceKind::Disk};
    for (int i = 0; i < 4; ++i)
    {
        HWND combo = CreateWindowExW(
            0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL,
            comboX, 41 + i * 28, 180, 200, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDeviceComboBase + i)), instance_, nullptr);
        AddControl(page, combo);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"auto"));
        (void)kinds;
    }

    const std::array<const wchar_t*, 6> toggleNames{
        L"CPU temperature", L"GPU temperature", L"Disk temperature",
        L"Fan speed",       L"RAM temperature", L"Mainboard temperature"};
    for (int i = 0; i < 6; ++i)
    {
AddControl(page, CreateWindowExW(
                              0, L"BUTTON", toggleNames[i],
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 176 + i * 24,
                             300, 22, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSensorToggleBase + i)),
                             instance_, nullptr));
    }

    AddControl(page, CreateWindowExW(
                          0, L"BUTTON",
                          L"Deep sensors (temps/fans; requires PawnIO + elevation)",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, 176 + 6 * 24, 340,
                          22, hwnd_,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDeepSensorsToggle)), instance_,
                          nullptr));

    AddControl(page, MakeLabel(hwnd_, instance_, L"Disk temp mode:", 340, 44, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Fan mode:", 340, 72, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Mainboard mode:", 340, 100, 130));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Ping target:", 340, 128, 130));

    HWND diskTemp = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                              CBS_DROPDOWNLIST | WS_VSCROLL, 340, 62,
                                     170, 200, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDiskTempCombo)),
                                     instance_, nullptr);
    AddControl(page, diskTemp);
    SendMessageW(diskTemp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Selected disk"));
    SendMessageW(diskTemp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Highest"));
    SendMessageW(diskTemp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Average"));

    HWND fanMode = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                             CBS_DROPDOWNLIST | WS_VSCROLL, 340, 90,
                                   170, 200, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFanModeCombo)),
                                   instance_, nullptr);
    AddControl(page, fanMode);
    SendMessageW(fanMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Highest"));
    SendMessageW(fanMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Average"));

    HWND boardMode = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                               CBS_DROPDOWNLIST | WS_VSCROLL, 340, 118,
                                     170, 200, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMainboardModeCombo)),
                                     instance_, nullptr);
    AddControl(page, boardMode);
    SendMessageW(boardMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Highest"));
    SendMessageW(boardMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Average"));

    HWND ping = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                                                      ES_AUTOHSCROLL, 340, 146, 170, 22, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPingEdit)),
                                 instance_, nullptr);
    AddControl(page, ping);
}

void SettingsWindow::BuildHistoryPage()
{
    const int page = kPageHistory;
    AddControl(page, MakeLabel(hwnd_, instance_, L"Retention (minutes):", 16, 48, 160));
    HWND combo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                           CBS_DROPDOWNLIST | WS_VSCROLL, 180, 45, 140,
                                 200, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRetentionCombo)),
                                 instance_, nullptr);
    AddControl(page, combo);
    for (const int value : kRetentionOptions)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(value).c_str()));
    }
}

void SettingsWindow::BuildHotkeysPage()
{
    const int page = kPageHotkeys;
    AddControl(page, MakeLabel(hwnd_, instance_, L"Toggle overlay:", 16, 56, 160));
    AddControl(page, MakeLabel(hwnd_, instance_, L"Toggle click-through:", 16, 96, 160));
    AddControl(page, MakeLabel(hwnd_, instance_,
                               L"Click the box, then press a key combination. Delete clears it.",
                               16, 132, 440, 20));

    HWND overlayKey = CreateWindowExW(0, kHotkeyClassName, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      190, 52, 160, 24, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOverlayHotkey)),
                                      instance_, nullptr);
    AddControl(page, overlayKey);
    HWND clickKey = CreateWindowExW(0, kHotkeyClassName, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    190, 92, 160, 24, hwnd_,
                                    reinterpret_cast<HMENU>(
                                        static_cast<INT_PTR>(kIdClickThroughHotkey)),
                                    instance_, nullptr);
    AddControl(page, clickKey);
}

void SettingsWindow::BuildAboutPage()
{
    const int page = kPageAbout;
    HWND text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                                    ES_READONLY | ES_AUTOVSCROLL,
                                16, 44, 460, 250, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAboutText)), instance_,
                                nullptr);
    AddControl(page, text);
    AddControl(page, CreateWindowExW(0, L"BUTTON", L"Copy info",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 16, 304, 110,
                                     26, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCopyInfo)),
                                     instance_, nullptr));
}

void SettingsWindow::ShowPage(int index)
{
    if (index < 0 || index >= kPageCount)
    {
        return;
    }
    activePage_ = index;
    for (int p = 0; p < kPageCount; ++p)
    {
        const int show = p == index ? SW_SHOW : SW_HIDE;
        for (HWND control : pages_[p].controls)
        {
            ShowWindow(control, show);
        }
    }
    TabCtrl_SetCurSel(tab_, index);
}

void SettingsWindow::RefreshFromConfig()
{
    if (binding_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    loading_ = true;

    const auto setCombo = [this](int id, int selection)
    {
        HWND combo = GetDlgItem(hwnd_, id);
        if (combo != nullptr)
        {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
        }
    };
    const auto setCheck = [this](int id, bool checked)
    {
        HWND box = GetDlgItem(hwnd_, id);
        if (box != nullptr)
        {
            SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    };

    setCombo(kIdRefreshCombo, binding_->RefreshIndex());
    setCombo(kIdThemeCombo, binding_->ThemeIndex());
    setCombo(kIdLayoutCombo, binding_->LayoutIndex());
    setCheck(kIdStartWindows, binding_->StartWithWindows());
    setCheck(kIdStartHidden, binding_->StartHidden());

    HWND opacity = GetDlgItem(hwnd_, kIdOpacitySlider);
    if (opacity != nullptr)
    {
        SendMessageW(opacity, TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(static_cast<int>(binding_->Opacity() * 100.0 + 0.5)));
    }

    setCombo(kIdModeCombo, binding_->OverlayModeIndex());
    setCombo(kIdMonitorCombo, binding_->MonitorIndex());
    setCheck(kIdAlwaysOnTop, binding_->AlwaysOnTop());
    setCheck(kIdCaptureExclusion, binding_->CaptureExclusion());

    for (int t = 0; t < 6; ++t)
    {
        setCheck(kIdTileVisibleBase + t, binding_->TileFlag(t, TileField::Visible));
        setCheck(kIdTilePrimaryBase + t, binding_->TileFlag(t, TileField::Primary));
        setCheck(kIdTileSecondaryBase + t, binding_->TileFlag(t, TileField::Secondary));
        setCheck(kIdTileTertiaryBase + t, binding_->TileFlag(t, TileField::Tertiary));
        setCheck(kIdTileGraphBase + t, binding_->TileFlag(t, TileField::Visualization));
        setCheck(kIdTileMiniBase + t, binding_->TileFlag(t, TileField::MiniSparkline));
        setCheck(kIdTileVisComboBase + t,
                 binding_->TileVisualizationIndex(t) == static_cast<int>(OptionIndex(
                                                        kVisualizationOptions, Visualization::Sparklines)));
    }

    const std::array<SensorToggle, 6> toggles{
        SensorToggle::CpuTemperature, SensorToggle::GpuTemperature, SensorToggle::DiskTemperature,
        SensorToggle::FanSpeed,       SensorToggle::RamTemperature,  SensorToggle::MainboardTemperature};
    for (int i = 0; i < 6; ++i)
    {
        setCheck(kIdSensorToggleBase + i, binding_->SensorEnabled(toggles[i]));
    }
    setCheck(kIdDeepSensorsToggle, binding_->DeepSensorsEnabled());

    const std::array<DeviceKind, 4> kinds{DeviceKind::Gpu, DeviceKind::Cpu, DeviceKind::Nic,
                                          DeviceKind::Disk};
    for (int i = 0; i < 4; ++i)
    {
        HWND combo = GetDlgItem(hwnd_, kIdDeviceComboBase + i);
        if (combo != nullptr)
        {
            const std::wstring value = WideFromUtf8(binding_->Selection(kinds[i]));
            SetWindowTextW(combo, value.c_str());
        }
    }

    setCombo(kIdDiskTempCombo, binding_->DiskTempModeIndex());
    setCombo(kIdFanModeCombo, binding_->FanModeIndex());
    setCombo(kIdMainboardModeCombo, binding_->MainboardModeIndex());
    if (HWND ping = GetDlgItem(hwnd_, kIdPingEdit))
    {
        SetWindowTextW(ping, WideFromUtf8(binding_->PingTarget()).c_str());
    }

    setCombo(kIdRetentionCombo, binding_->RetentionIndex());

    if (HWND overlayKey = GetDlgItem(hwnd_, kIdOverlayHotkey))
    {
        SetWindowTextW(overlayKey, FormatHotkeyForDisplay(binding_->ToggleOverlayHotkey()).c_str());
    }
    if (HWND clickKey = GetDlgItem(hwnd_, kIdClickThroughHotkey))
    {
        SetWindowTextW(clickKey,
                       FormatHotkeyForDisplay(binding_->ToggleClickThroughHotkey()).c_str());
    }

    if (HWND about = GetDlgItem(hwnd_, kIdAboutText))
    {
        std::wstring text = L"Pacecar 2.0\n\n";
        if (hooks_.aboutText)
        {
            text += hooks_.aboutText();
        }
        SetWindowTextW(about, text.c_str());
    }

    loading_ = false;
}

void SettingsWindow::ApplyLive()
{
    if (loading_)
    {
        return;
    }
    if (hooks_.onChanged)
    {
        hooks_.onChanged();
    }
}

void SettingsWindow::PersistStartup()
{
    if (hooks_.onStartupSettingChanged)
    {
        hooks_.onStartupSettingChanged();
    }
}

void SettingsWindow::OnHotkeyCaptured(int controlId)
{
    if (binding_ == nullptr)
    {
        return;
    }
    HWND control = GetDlgItem(hwnd_, controlId);
    if (control == nullptr)
    {
        return;
    }
    wchar_t raw[128] = {};
    GetWindowTextW(control, raw, static_cast<int>(std::size(raw)));
    const std::string captured = Utf8FromWide(raw);

    if (controlId == kIdOverlayHotkey)
    {
        if (!binding_->SetToggleOverlayHotkey(captured))
        {
            SetWindowTextW(control, FormatHotkeyForDisplay(binding_->ToggleOverlayHotkey()).c_str());
        }
    }
    else if (controlId == kIdClickThroughHotkey)
    {
        if (!binding_->SetToggleClickThroughHotkey(captured))
        {
            SetWindowTextW(control,
                           FormatHotkeyForDisplay(binding_->ToggleClickThroughHotkey()).c_str());
        }
    }
}

void SettingsWindow::OnCommand(int controlId, int notifyCode, HWND /*control*/)
{
    if (binding_ == nullptr)
    {
        return;
    }

    const auto comboSelection = [this](int id) -> int
    {
        HWND combo = GetDlgItem(hwnd_, id);
        const LRESULT result = combo != nullptr ? SendMessageW(combo, CB_GETCURSEL, 0, 0) : CB_ERR;
        return result == CB_ERR ? 0 : static_cast<int>(result);
    };
    const auto isChecked = [this](int id) -> bool
    {
        HWND box = GetDlgItem(hwnd_, id);
        return box != nullptr && SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };

    switch (controlId)
    {
    case kIdReset:
        binding_->ResetToDefaults();
        RefreshFromConfig();
        PersistStartup();
        return;
    case kIdClose:
        ShowWindow(hwnd_, SW_HIDE);
        return;
    case kIdCopyInfo:
    {
        if (!OpenClipboard(hwnd_))
        {
            return;
        }
        EmptyClipboard();
        std::wstring text = L"Pacecar 2.0\n\n";
        if (hooks_.aboutText)
        {
            text += hooks_.aboutText();
        }
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* destination = GlobalLock(memory))
            {
                memcpy(destination, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
                {
                    GlobalFree(memory);
                }
            }
            else
            {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
        return;
    }
    case kIdStartWindows:
        binding_->SetStartWithWindows(isChecked(kIdStartWindows));
        PersistStartup();
        return;
    case kIdStartHidden:
        binding_->SetStartHidden(isChecked(kIdStartHidden));
        PersistStartup();
        return;
    case kIdAlwaysOnTop:
        binding_->SetAlwaysOnTop(isChecked(kIdAlwaysOnTop));
        return;
    case kIdCaptureExclusion:
        binding_->SetCaptureExclusion(isChecked(kIdCaptureExclusion));
        return;
    default:
        break;
    }

    if (controlId == kIdRefreshCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetRefreshIndex(comboSelection(kIdRefreshCombo));
        return;
    }
    if (controlId == kIdThemeCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetThemeIndex(comboSelection(kIdThemeCombo));
        return;
    }
    if (controlId == kIdLayoutCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetLayoutIndex(comboSelection(kIdLayoutCombo));
        return;
    }
    if (controlId == kIdModeCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetOverlayModeIndex(comboSelection(kIdModeCombo));
        return;
    }
    if (controlId == kIdMonitorCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetMonitorIndex(comboSelection(kIdMonitorCombo));
        return;
    }
    if (controlId == kIdDiskTempCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetDiskTempModeIndex(comboSelection(kIdDiskTempCombo));
        return;
    }
    if (controlId == kIdFanModeCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetFanModeIndex(comboSelection(kIdFanModeCombo));
        return;
    }
    if (controlId == kIdMainboardModeCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetMainboardModeIndex(comboSelection(kIdMainboardModeCombo));
        return;
    }
    if (controlId == kIdRetentionCombo && notifyCode == CBN_SELCHANGE)
    {
        binding_->SetRetentionIndex(comboSelection(kIdRetentionCombo));
        return;
    }

    if (controlId >= kIdDeviceComboBase && controlId < kIdDeviceComboBase + 4 &&
        (notifyCode == CBN_SELCHANGE || notifyCode == CBN_KILLFOCUS))
    {
        const int index = controlId - kIdDeviceComboBase;
        const std::array<DeviceKind, 4> kinds{DeviceKind::Gpu, DeviceKind::Cpu, DeviceKind::Nic,
                                              DeviceKind::Disk};
        HWND combo = GetDlgItem(hwnd_, controlId);
        wchar_t text[256] = {};
        if (combo != nullptr)
        {
            GetWindowTextW(combo, text, static_cast<int>(std::size(text)));
        }
        binding_->SetSelection(kinds[index], Utf8FromWide(text));
        return;
    }

    if (controlId == kIdPingEdit && notifyCode == EN_KILLFOCUS)
    {
        wchar_t text[256] = {};
        GetDlgItemTextW(hwnd_, kIdPingEdit, text, static_cast<int>(std::size(text)));
        binding_->SetPingTarget(Utf8FromWide(text));
        return;
    }

    if (controlId >= kIdSensorToggleBase && controlId < kIdSensorToggleBase + 6)
    {
        const int index = controlId - kIdSensorToggleBase;
        const std::array<SensorToggle, 6> toggles{
            SensorToggle::CpuTemperature, SensorToggle::GpuTemperature,
            SensorToggle::DiskTemperature, SensorToggle::FanSpeed,
            SensorToggle::RamTemperature, SensorToggle::MainboardTemperature};
        binding_->SetSensorEnabled(toggles[index], isChecked(controlId));
        return;
    }

    if (controlId == kIdDeepSensorsToggle)
    {
        binding_->SetDeepSensorsEnabled(isChecked(controlId));
        return;
    }

    if (controlId >= kIdTileVisibleBase && controlId < kIdTileVisibleBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTileVisibleBase),
                              TileField::Visible, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTilePrimaryBase && controlId < kIdTilePrimaryBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTilePrimaryBase),
                              TileField::Primary, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTileSecondaryBase && controlId < kIdTileSecondaryBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTileSecondaryBase),
                              TileField::Secondary, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTileTertiaryBase && controlId < kIdTileTertiaryBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTileTertiaryBase),
                              TileField::Tertiary, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTileGraphBase && controlId < kIdTileGraphBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTileGraphBase),
                              TileField::Visualization, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTileMiniBase && controlId < kIdTileMiniBase + 6)
    {
        binding_->SetTileFlag(static_cast<std::size_t>(controlId - kIdTileMiniBase),
                              TileField::MiniSparkline, isChecked(controlId));
        return;
    }
    if (controlId >= kIdTileVisComboBase && controlId < kIdTileVisComboBase + 6)
    {
        binding_->SetTileVisualizationIndex(
            static_cast<std::size_t>(controlId - kIdTileVisComboBase),
            isChecked(controlId) ? static_cast<int>(OptionIndex(kVisualizationOptions,
                                                                Visualization::Sparklines))
                                 : static_cast<int>(OptionIndex(kVisualizationOptions,
                                                                Visualization::Gauges)));
        return;
    }
}

LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr)
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 480;
        info->ptMinTrackSize.y = 420;
        return 0;
    }
    case WM_SIZE:
        if (tab_ != nullptr)
        {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            SetWindowPos(tab_, nullptr, 8, 8, std::max(width - 16, 0),
                         std::max(height - kBottomStrip - 16, 0), SWP_NOZORDER);
            if (HWND reset = GetDlgItem(hwnd_, kIdReset))
            {
                SetWindowPos(reset, nullptr, 8, height - 34, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
            if (HWND close = GetDlgItem(hwnd_, kIdClose))
            {
                SetWindowPos(close, nullptr, width - 96, height - 34, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
        }
        return 0;
    case WM_COMMAND:
    {
        const int controlId = LOWORD(wParam);
        const int notify = HIWORD(wParam);
        if (controlId == kIdTab && notify == 0)
        {
            return 0;
        }
        OnCommand(controlId, notify, reinterpret_cast<HWND>(lParam));
        return 0;
    }
    case WM_HSCROLL:
        if (binding_ != nullptr && reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd_, kIdOpacitySlider))
        {
            const LRESULT position = SendMessageW(GetDlgItem(hwnd_, kIdOpacitySlider), TBM_GETPOS, 0, 0);
            binding_->SetOpacity(static_cast<double>(position) / 100.0);
            return 0;
        }
        break;
    case WM_NOTIFY:
    {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header != nullptr && header->code == TCN_SELCHANGE && header->hwndFrom == tab_)
        {
            ShowPage(TabCtrl_GetCurSel(tab_));
            return 0;
        }
        break;
    }
    case kWmHotkeyCaptured:
        OnHotkeyCaptured(static_cast<int>(wParam));
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace pacecar::overlay