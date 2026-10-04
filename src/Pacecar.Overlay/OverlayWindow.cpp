#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "OverlayWindow.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include "Widgets/SystemTheme.h"
#include "pacecar/overlay/Layout.h"
#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kWindowClassName[] = L"PacecarOverlayWindow";

// Hit-test border for interactive edge/corner resizing, in DIPs.
constexpr float kResizeBorderDip = 6.0f;

ATOM g_windowClass = 0;

BOOL CALLBACK EnumMonitorProc(HMONITOR monitor, HDC /*hdc*/, LPRECT /*rect*/, LPARAM data)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorWorkArea>*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
    {
        return TRUE;
    }
    MonitorWorkArea area;
    area.deviceName = info.szDevice;
    area.workArea =
        IntRect{info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom};
    area.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0)
    {
        area.dpi = dpiX;
    }
    monitors->push_back(std::move(area));
    return TRUE;
}

bool EnsureWindowClass(HINSTANCE instance)
{
    if (g_windowClass != 0)
    {
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
    windowClass.lpfnWndProc = OverlayWindow::StaticWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;
    g_windowClass = RegisterClassExW(&windowClass);
    return g_windowClass != 0;
}
} // namespace

std::vector<MonitorWorkArea> EnumerateMonitorWorkAreas()
{
    std::vector<MonitorWorkArea> monitors;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc, reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(monitors.begin(), monitors.end(),
                     [](const MonitorWorkArea& a, const MonitorWorkArea& b) {
                         if (a.primary != b.primary)
                         {
                             return a.primary;
                         }
                         return a.deviceName < b.deviceName;
                     });
    return monitors;
}

OverlayWindow::OverlayWindow() = default;

OverlayWindow::~OverlayWindow()
{
    renderer_.reset();
    if (hwnd_ != nullptr)
    {
        if (sessionNotificationsRegistered_)
        {
            WTSUnRegisterSessionNotification(hwnd_);
            sessionNotificationsRegistered_ = false;
        }
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK OverlayWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                                 LPARAM lParam)
{
    OverlayWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr)
    {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool OverlayWindow::Create(HINSTANCE instance, const OverlayOptions& options,
                           const std::optional<pacecar::MonitorRect>& savedRect)
{
    instance_ = instance;
    options_ = options;
    clickThrough_ = options.clickThrough;
    monitors_ = EnumerateMonitorWorkAreas();

    if (!EnsureWindowClass(instance))
    {
        return false;
    }

    OverlayRecipe recipe = options_.recipe;
    for (;;)
    {
        if (!CreateWindowForRecipe(recipe))
        {
            return false;
        }
        if (hwnd_ == nullptr)
        {
            return false;
        }

        const unsigned dpi = WindowDpi();
        const pacecar::MonitorRect placement = PlacementFor(savedRect, dpi);
        SetWindowPos(hwnd_, HWND_TOPMOST, placement.x, placement.y, placement.width,
                     placement.height, SWP_NOACTIVATE | SWP_FRAMECHANGED);

        if (InitializeRenderer(recipe, placement))
        {
            RegisterSessionNotifications();
            return true;
        }

        renderer_.reset();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        if (recipe == OverlayRecipe::Composition)
        {
            LogWarn(L"Composition renderer unavailable; falling back to the layered recipe");
            recipe = OverlayRecipe::Layered;
            continue;
        }
        return false;
    }
}

bool OverlayWindow::CreateWindowForRecipe(OverlayRecipe recipe)
{
    DWORD exStyle = RecipeExtendedStyle(recipe);
    if (clickThrough_)
    {
        const bool clearStyle =
            recipe == OverlayRecipe::Layered ||
            options_.compositionClickThrough == CompositionClickThrough::TransparentExStyle;
        if (clearStyle)
        {
            exStyle |= WS_EX_TRANSPARENT;
        }
    }
    // WS_THICKFRAME enables the native sizing loop for the WM_NCHITTEST edge codes; WM_NCCALCSIZE
    // removes the drawn frame so the panel still fills the whole window.
    hwnd_ = CreateWindowExW(exStyle, kWindowClassName, L"Pacecar Overlay", WS_POPUP | WS_THICKFRAME,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

bool OverlayWindow::InitializeRenderer(OverlayRecipe recipe, const pacecar::MonitorRect& placement)
{
    renderer_ = CreateRenderer(recipe);
    if (!renderer_)
    {
        return false;
    }

    const unsigned dpi = WindowDpi();
    const HRESULT hr = renderer_->Initialize(hwnd_, placement.width, placement.height, dpi);
    if (FAILED(hr))
    {
        return false;
    }
    renderer_->SetTheme(ResolveSystemTheme(options_.theme, options_.panelOpacity));
    renderer_->Invalidate();
    const PresentResult presented = renderer_->Present();
    if (FAILED(presented.hr))
    {
        return false;
    }

    if (options_.captureExclusion)
    {
        ApplyCaptureExclusion(true);
    }

    diagnostics_.clear();
    diagnostics_ += L"renderer=";
    diagnostics_ += renderer_->Name();
    diagnostics_ += L"; ";
    diagnostics_ += renderer_->Describe();
    diagnostics_ += L"; ";
    diagnostics_ += QueryAdvancedColor(CurrentMonitor()).Summary();
    diagnostics_ += L"; captureExclusion=";
    diagnostics_ += captureExclusionApplied_ ? L"enabled" : L"off/unsupported";
    return true;
}

void OverlayWindow::Show(int cmdShow)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    ShowWindow(hwnd_, cmdShow);
    ReassertTopmost();
    Invalidate();
    RefreshSuspension(L"show");
    NotifyVisibilityChanged();
}

void OverlayWindow::SetVisible(bool visible)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    if (visible)
    {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        ReassertTopmost();
        Invalidate();
        RefreshSuspension(L"show");
        NotifyVisibilityChanged();
    }
    else
    {
        ShowWindow(hwnd_, SW_HIDE);
        RefreshSuspension(L"hide");
        NotifyVisibilityChanged();
    }
}

void OverlayWindow::ToggleVisibility()
{
    SetVisible(!IsVisible());
}

bool OverlayWindow::IsVisible() const noexcept
{
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void OverlayWindow::Quit()
{
    // Let the message loop exit and the destructor destroy the window (the same shutdown path the
    // measure mode uses). Destroying the window mid-loop while the renderer is still alive is
    // avoided on purpose.
    quitRequested_ = true;
    PostQuitMessage(0);
}

void OverlayWindow::SetClickThrough(bool enabled)
{
    if (hwnd_ == nullptr || clickThrough_ == enabled)
    {
        return;
    }
    clickThrough_ = enabled;

    const bool useExStyle = renderer_ != nullptr && renderer_->Recipe() == OverlayRecipe::Layered;
    const bool compositionExStyle =
        renderer_ != nullptr && renderer_->Recipe() == OverlayRecipe::Composition &&
        options_.compositionClickThrough == CompositionClickThrough::TransparentExStyle;
    if (useExStyle || compositionExStyle)
    {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if (enabled)
        {
            exStyle |= WS_EX_TRANSPARENT;
        }
        else
        {
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        }
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void OverlayWindow::ToggleClickThrough()
{
    SetClickThrough(!clickThrough_);
}

void OverlayWindow::ReassertTopmost()
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    const HWND insertAfter = options_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(hwnd_, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayWindow::ApplyCaptureExclusion(bool enabled)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    if (enabled)
    {
        captureExclusionApplied_ = SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    }
    else
    {
        SetWindowDisplayAffinity(hwnd_, WDA_NONE);
        captureExclusionApplied_ = false;
    }
}

void OverlayWindow::ApplyConfig(const pacecar::Config& config)
{
    options_.panelOpacity = config.general.opacity;
    options_.theme = config.general.theme;
    options_.alwaysOnTop = config.overlay.always_on_top;
    transparentBackground_ = config.general.transparent_background;
    if (renderer_ == nullptr)
    {
        return;
    }
    renderer_->SetTheme(ResolveSystemTheme(options_.theme, options_.panelOpacity));
    renderer_->SetLayout(LayoutSettingsFromConfig(config));
    SetClickThrough(config.overlay.mode == pacecar::OverlayMode::ClickThrough);
    ReassertTopmost();
    ApplyCaptureExclusion(config.overlay.capture_exclusion);
    Invalidate();
}

void OverlayWindow::SetFrame(std::shared_ptr<const pacecar::metrics::DisplayFrame> frame)
{
    if (renderer_ == nullptr)
    {
        return;
    }
    renderer_->SetFrame(std::move(frame));
}

void OverlayWindow::Invalidate()
{
    if (renderer_ == nullptr || renderingSuspended_)
    {
        return;
    }
    renderer_->Invalidate();
    if (hwnd_ != nullptr && IsWindowVisible(hwnd_))
    {
        renderer_->Present();
    }
}

bool OverlayWindow::SetRenderingSuspended(bool suspended) noexcept
{
    if (renderingSuspended_ == suspended)
    {
        return false;
    }
    renderingSuspended_ = suspended;
    if (renderer_ != nullptr)
    {
        if (suspended)
        {
            renderer_->Trim();
        }
        else
        {
            renderer_->Invalidate();
            if (hwnd_ != nullptr && IsWindowVisible(hwnd_))
            {
                renderer_->Present();
            }
        }
    }
    NotifyVisibilityChanged();
    return true;
}

bool OverlayWindow::EffectivelyVisible() const noexcept
{
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) && !renderingSuspended_;
}

void OverlayWindow::SetVisibilityChangedCallback(VisibilityChangedCallback callback)
{
    visibilityChanged_ = std::move(callback);
}

void OverlayWindow::OnDisplayChange()
{
    monitors_ = EnumerateMonitorWorkAreas();
    if (hwnd_ == nullptr)
    {
        return;
    }
    ReassertTopmost();

    RECT currentRect{};
    if (GetWindowRect(hwnd_, &currentRect))
    {
        const IntRect current{currentRect.left, currentRect.top, currentRect.right,
                              currentRect.bottom};
        const IntRect clamped = ClampToWorkArea(current, monitors_);
        if (clamped != current)
        {
            SetWindowPos(hwnd_, HWND_TOPMOST, clamped.left, clamped.top, clamped.Width(),
                         clamped.Height(), SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ResizeRendererToWindow();
        }
    }
    PublishPlacement();
}

void OverlayWindow::SetPositionChangedCallback(PositionChangedCallback callback)
{
    positionChanged_ = std::move(callback);
}

void OverlayWindow::SetCloseCallback(CloseCallback callback)
{
    closeCallback_ = std::move(callback);
}

void OverlayWindow::SetSessionEndingCallback(SessionEndingCallback callback)
{
    sessionEndingCallback_ = std::move(callback);
}

void OverlayWindow::SetCommandCallback(CommandCallback callback)
{
    commandCallback_ = std::move(callback);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1; // The renderer owns every pixel; never let USER paint the background.
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd_, &paint);
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // Never steal focus from the app underneath.
    case WM_NCCALCSIZE:
        // Remove the WS_THICKFRAME frame; the client area stays the whole window.
        if (wParam == TRUE)
        {
            return 0;
        }
        break;
    case WM_NCHITTEST:
        if (renderer_ != nullptr && renderer_->Recipe() == OverlayRecipe::Composition &&
            clickThrough_ &&
            options_.compositionClickThrough == CompositionClickThrough::HitTestTransparent)
        {
            return HTTRANSPARENT;
        }
        if (!clickThrough_)
        {
            return HitTestBorder(lParam);
        }
        break;
    case WM_NCRBUTTONUP:
        if (!clickThrough_ && wParam == HTCAPTION)
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ShowContextMenu(point);
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        if (!clickThrough_)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1)
            {
                RECT window{};
                if (GetWindowRect(hwnd_, &window))
                {
                    point.x = window.left + 8;
                    point.y = window.top + 8;
                }
            }
            ShowContextMenu(point);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0 && IsOverlayCommand(LOWORD(wParam)))
        {
            ExecuteCommand(static_cast<OverlayCommand>(LOWORD(wParam)));
            return 0;
        }
        break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            SetRenderingSuspended(true);
        }
        else
        {
            RefreshSuspension(L"size");
            ResizeRendererToWindow();
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        RefreshSuspension(L"windowpos");
        break;
    case WM_SHOWWINDOW:
        RefreshSuspension(L"showwindow");
        break;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK)
        {
            RefreshSuspension(L"session-lock");
        }
        else if (wParam == WTS_SESSION_UNLOCK)
        {
            RefreshSuspension(L"session-unlock");
        }
        return 0;
    case WM_DPICHANGED:
        HandleDpiChanged(wParam, lParam);
        return 0;
    case WM_DISPLAYCHANGE:
        OnDisplayChange();
        return 0;
    case WM_EXITSIZEMOVE:
        PublishPlacement();
        break;
    case WM_ENDSESSION:
        if (sessionEndingCallback_)
        {
            sessionEndingCallback_();
        }
        PublishPlacement();
        break;
    case WM_CLOSE:
        // Ordinary close hides to the tray; an explicit Quit() destroys the window. This keeps the
        // tray icon available until the user picks Exit.
        if (quitRequested_)
        {
            DestroyWindow(hwnd_);
        }
        else
        {
            ShowWindow(hwnd_, SW_HIDE);
            RefreshSuspension(L"close-hide");
            NotifyVisibilityChanged();
        }
        return 0;
    case WM_DESTROY:
        if (sessionNotificationsRegistered_ && hwnd_ != nullptr)
        {
            WTSUnRegisterSessionNotification(hwnd_);
            sessionNotificationsRegistered_ = false;
        }
        if (closeCallback_)
        {
            closeCallback_();
        }
        else
        {
            PostQuitMessage(0);
        }
        return 0;
    case WM_NCDESTROY: {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void OverlayWindow::HandleDpiChanged(WPARAM /*wParam*/, LPARAM lParam)
{
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    if (suggested == nullptr)
    {
        return;
    }
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ResizeRendererToWindow();
    ReassertTopmost();
    PublishPlacement();
}

void OverlayWindow::ResizeRendererToWindow()
{
    if (renderer_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd_, &rect))
    {
        return;
    }
    const int width = static_cast<int>(std::max<LONG>(1, rect.right - rect.left));
    const int height = static_cast<int>(std::max<LONG>(1, rect.bottom - rect.top));
    if (SUCCEEDED(renderer_->Resize(width, height, WindowDpi())))
    {
        renderer_->Invalidate();
        renderer_->Present();
    }
}

void OverlayWindow::PublishPlacement()
{
    if (positionChanged_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd_, &rect))
    {
        return;
    }
    pacecar::MonitorRect placement{};
    placement.x = rect.left;
    placement.y = rect.top;
    placement.width = rect.right - rect.left;
    placement.height = rect.bottom - rect.top;
    placement.valid = true;

    const IntRect current{rect.left, rect.top, rect.right, rect.bottom};
    const MonitorWorkArea* monitor = FindBestMonitor(current, monitors_);
    if (monitor != nullptr)
    {
        const auto it = std::find_if(
            monitors_.begin(), monitors_.end(),
            [monitor](const MonitorWorkArea& candidate) { return &candidate == monitor; });
        placement.monitor_id = static_cast<int>(std::distance(monitors_.begin(), it));
    }
    positionChanged_(placement);
}

unsigned OverlayWindow::WindowDpi() const
{
    UINT dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 0;
    if (dpi == 0)
    {
        dpi = GetDpiForSystem();
    }
    return NormalizeDpi(dpi);
}

HMONITOR OverlayWindow::CurrentMonitor() const
{
    return MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
}

pacecar::MonitorRect OverlayWindow::PlacementFor(const std::optional<pacecar::MonitorRect>& saved,
                                                 unsigned dpi)
{
    IntRect desired{};
    if (saved.has_value() && saved->valid && saved->width > 0 && saved->height > 0)
    {
        desired = IntRect{saved->x, saved->y, saved->x + saved->width, saved->y + saved->height};
    }
    else
    {
        const MonitorWorkArea* primary = PrimaryMonitor(monitors_);
        const int width = DipToPixels(options_.defaultWidthDip, dpi);
        const int height = DipToPixels(options_.defaultHeightDip, dpi);
        const int margin = DipToPixels(24.0f, dpi);
        const int left = primary != nullptr ? primary->workArea.left : 0;
        const int top = primary != nullptr ? primary->workArea.top : 0;
        desired =
            IntRect{left + margin, top + margin, left + margin + width, top + margin + height};
    }

    const IntRect clamped = ClampToWorkArea(desired, monitors_);
    pacecar::MonitorRect placement{};
    placement.x = clamped.left;
    placement.y = clamped.top;
    placement.width = clamped.Width();
    placement.height = clamped.Height();
    placement.valid = true;

    const MonitorWorkArea* monitor = FindBestMonitor(clamped, monitors_);
    if (monitor != nullptr)
    {
        const auto it = std::find_if(
            monitors_.begin(), monitors_.end(),
            [monitor](const MonitorWorkArea& candidate) { return &candidate == monitor; });
        placement.monitor_id = static_cast<int>(std::distance(monitors_.begin(), it));
    }
    else if (const MonitorWorkArea* primary = PrimaryMonitor(monitors_))
    {
        const auto it = std::find_if(
            monitors_.begin(), monitors_.end(),
            [primary](const MonitorWorkArea& candidate) { return &candidate == primary; });
        placement.monitor_id = static_cast<int>(std::distance(monitors_.begin(), it));
    }
    return placement;
}

LRESULT OverlayWindow::HitTestBorder(LPARAM lParam) const
{
    if (hwnd_ == nullptr)
    {
        return HTCLIENT;
    }
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT window{};
    if (!GetWindowRect(hwnd_, &window))
    {
        return HTCLIENT;
    }
    const int border = DipToPixels(kResizeBorderDip, WindowDpi());
    const bool left = point.x < window.left + border;
    const bool right = point.x >= window.right - border;
    const bool top = point.y < window.top + border;
    const bool bottom = point.y >= window.bottom - border;

    if (top && left)
    {
        return HTTOPLEFT;
    }
    if (top && right)
    {
        return HTTOPRIGHT;
    }
    if (bottom && left)
    {
        return HTBOTTOMLEFT;
    }
    if (bottom && right)
    {
        return HTBOTTOMRIGHT;
    }
    if (left)
    {
        return HTLEFT;
    }
    if (right)
    {
        return HTRIGHT;
    }
    if (top)
    {
        return HTTOP;
    }
    if (bottom)
    {
        return HTBOTTOM;
    }
    // Interior: native caption drag (no polling, no SetForegroundWindow).
    return HTCAPTION;
}

void OverlayWindow::ShowContextMenu(POINT screenPoint)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }
    for (const OverlayCommand command : kContextMenuCommands)
    {
        UINT flags = MF_STRING;
        if (command == OverlayCommand::Mode && clickThrough_)
        {
            flags |= MF_CHECKED;
        }
        if (command == OverlayCommand::ToggleBackground && transparentBackground_)
        {
            flags |= MF_CHECKED;
        }
        if (AppendMenuW(menu, flags, static_cast<UINT_PTR>(command), CommandLabel(command)) ==
            FALSE)
        {
            DestroyMenu(menu);
            return;
        }
    }

    const UINT chosen =
        static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                         screenPoint.x, screenPoint.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (chosen != 0 && IsOverlayCommand(chosen))
    {
        ExecuteCommand(static_cast<OverlayCommand>(chosen));
    }
}

void OverlayWindow::ExecuteCommand(OverlayCommand command)
{
    switch (command)
    {
    case OverlayCommand::Mode:
        ToggleClickThrough();
        break;
    case OverlayCommand::Hide:
        SetVisible(false);
        break;
    case OverlayCommand::ToggleVisibility:
        ToggleVisibility();
        break;
    case OverlayCommand::Exit:
        Quit();
        break;
    case OverlayCommand::CopySystemInfo:
    case OverlayCommand::About:
    case OverlayCommand::ToggleFrameCapture:
    case OverlayCommand::CycleView:
    case OverlayCommand::ToggleBackground:
        if (commandCallback_)
        {
            commandCallback_(command);
        }
        break;
    case OverlayCommand::Settings:
    case OverlayCommand::History:
    case OverlayCommand::Specs:
        if (commandCallback_)
        {
            commandCallback_(command);
        }
        else
        {
            LogWarn(L"overlay: Settings/History/Specs windows are not available yet");
        }
        break;
    case OverlayCommand::None:
    default:
        break;
    }
}

void OverlayWindow::RegisterSessionNotifications()
{
    if (hwnd_ == nullptr || sessionNotificationsRegistered_)
    {
        return;
    }
    sessionNotificationsRegistered_ =
        WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
}

void OverlayWindow::RefreshSuspension(const wchar_t* reason)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    const bool shouldSuspend = !IsWindowVisible(hwnd_) || IsOccludedOrMinimized();
    if (SetRenderingSuspended(shouldSuspend) && reason != nullptr)
    {
        LogDebug(shouldSuspend ? L"overlay: rendering suspended" : L"overlay: rendering resumed");
        static_cast<void>(reason);
    }
}

bool OverlayWindow::IsOccludedOrMinimized() const noexcept
{
    if (hwnd_ == nullptr)
    {
        return true;
    }
    if (IsIconic(hwnd_))
    {
        return true;
    }
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd_, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0)
    {
        // A cloaked window is on another virtual desktop / not composed.
        return true;
    }
    return false;
}

void OverlayWindow::NotifyVisibilityChanged()
{
    if (visibilityChanged_)
    {
        visibilityChanged_(EffectivelyVisible());
    }
}

DWORD OverlayWindow::RecipeExtendedStyle(OverlayRecipe recipe) const
{
    DWORD style = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (options_.alwaysOnTop)
    {
        style |= WS_EX_TOPMOST;
    }
    if (recipe == OverlayRecipe::Layered)
    {
        style |= WS_EX_LAYERED;
    }
    else
    {
        style |= WS_EX_NOREDIRECTIONBITMAP;
    }
    return style;
}
} // namespace pacecar::overlay