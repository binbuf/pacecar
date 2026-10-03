#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "OverlayWindow.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include <shellscalingapi.h>

#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kWindowClassName[] = L"PacecarOverlayWindow";

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
    area.workArea = IntRect{info.rcWork.left, info.rcWork.top, info.rcWork.right,
                            info.rcWork.bottom};
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
                     [](const MonitorWorkArea& a, const MonitorWorkArea& b)
                     {
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
        const bool clearStyle = recipe == OverlayRecipe::Layered ||
                                options_.compositionClickThrough ==
                                    CompositionClickThrough::TransparentExStyle;
        if (clearStyle)
        {
            exStyle |= WS_EX_TRANSPARENT;
        }
    }
    hwnd_ = CreateWindowExW(exStyle, kWindowClassName, L"Pacecar Overlay", WS_POPUP, CW_USEDEFAULT, 0,
                            CW_USEDEFAULT, 0, nullptr, nullptr, instance_, this);
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
}

void OverlayWindow::SetClickThrough(bool enabled)
{
    if (hwnd_ == nullptr || clickThrough_ == enabled)
    {
        return;
    }
    clickThrough_ = enabled;

    const bool useExStyle =
        renderer_ != nullptr && renderer_->Recipe() == OverlayRecipe::Layered;
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

void OverlayWindow::Invalidate()
{
    if (renderer_ == nullptr)
    {
        return;
    }
    renderer_->Invalidate();
    if (hwnd_ != nullptr && IsWindowVisible(hwnd_))
    {
        renderer_->Present();
    }
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

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1; // The renderer owns every pixel; never let USER paint the background.
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd_, &paint);
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // Never steal focus from the app underneath.
    case WM_NCHITTEST:
        if (renderer_ != nullptr && renderer_->Recipe() == OverlayRecipe::Composition &&
            clickThrough_ &&
            options_.compositionClickThrough == CompositionClickThrough::HitTestTransparent)
        {
            return HTTRANSPARENT;
        }
        break;
    case WM_DPICHANGED:
        HandleDpiChanged(wParam, lParam);
        return 0;
    case WM_DISPLAYCHANGE:
        OnDisplayChange();
        return 0;
    case WM_EXITSIZEMOVE:
    case WM_ENDSESSION:
        PublishPlacement();
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        if (closeCallback_)
        {
            closeCallback_();
        }
        else
        {
            PostQuitMessage(0);
        }
        return 0;
    case WM_NCDESTROY:
    {
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
        const auto it = std::find_if(monitors_.begin(), monitors_.end(),
                                     [monitor](const MonitorWorkArea& candidate)
                                     { return &candidate == monitor; });
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

pacecar::MonitorRect OverlayWindow::PlacementFor(
    const std::optional<pacecar::MonitorRect>& saved, unsigned dpi)
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
        desired = IntRect{left + margin, top + margin, left + margin + width, top + margin + height};
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
        const auto it = std::find_if(monitors_.begin(), monitors_.end(),
                                     [monitor](const MonitorWorkArea& candidate)
                                     { return &candidate == monitor; });
        placement.monitor_id = static_cast<int>(std::distance(monitors_.begin(), it));
    }
    else if (const MonitorWorkArea* primary = PrimaryMonitor(monitors_))
    {
        const auto it = std::find_if(monitors_.begin(), monitors_.end(),
                                     [primary](const MonitorWorkArea& candidate)
                                     { return &candidate == primary; });
        placement.monitor_id = static_cast<int>(std::distance(monitors_.begin(), it));
    }
    return placement;
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