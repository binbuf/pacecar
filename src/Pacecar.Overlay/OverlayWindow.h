#pragma once

// The overlay HWND: input mode, topmost, DPI, placement, capture exclusion, and HDR reporting.
//
// `OverlayWindow` owns the window and delegates all pixel work to an `IRenderer` (Recipe A or B).
// The window is created with `WS_POPUP` + `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST`
// always, plus recipe-specific extended styles:
//   - Recipe A adds `WS_EX_LAYERED` (and `WS_EX_TRANSPARENT` while click-through is on).
//   - Recipe B adds `WS_EX_NOREDIRECTIONBITMAP` and never `WS_EX_LAYERED`.
//
// Input toggling and DPI handling live here; device/resource code lives in `Renderer.cpp`.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "Hdr.h"
#include "Renderer.h"
#include "pacecar/config/Config.h"
#include "pacecar/overlay/OverlayPlacement.h"

namespace pacecar::overlay
{
// How Recipe B attempts cross-process click-through. Recipe A does not need this: a layered window
// with `WS_EX_TRANSPARENT` passes the whole window through by documented behavior.
enum class CompositionClickThrough
{
    TransparentExStyle, // (a) WS_EX_TRANSPARENT alone
    HitTestTransparent, // (b) WM_NCHITTEST -> HTTRANSPARENT
};

struct OverlayOptions
{
    OverlayRecipe recipe = OverlayRecipe::Layered;
    bool clickThrough = true;
    bool captureExclusion = true;
    bool alwaysOnTop = true;
    // Recipe B's click-through strategy; ignored for Recipe A.
    CompositionClickThrough compositionClickThrough = CompositionClickThrough::TransparentExStyle;
    // DIP size used when there is no valid saved rectangle.
    float defaultWidthDip = 320.0f;
    float defaultHeightDip = 200.0f;
};

class OverlayWindow
{
  public:
    using PositionChangedCallback = std::function<void(const pacecar::MonitorRect&)>;
    using CloseCallback = std::function<void()>;

    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // Creates the window and renderer. `savedRect` (may be nullopt or invalid) is clamped onto a
    // monitor at startup. Returns false only when even the Recipe A fallback cannot be created.
    bool Create(HINSTANCE instance, const OverlayOptions& options,
                const std::optional<pacecar::MonitorRect>& savedRect);

    void Show(int cmdShow);

    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] bool ClickThrough() const noexcept
    {
        return clickThrough_;
    }

    // Toggles whole-window input pass-through. Never calls SetForegroundWindow.
    void SetClickThrough(bool enabled);
    void ToggleClickThrough();

    // Pushes the window back above other topmost windows after monitor/fullscreen changes.
    void ReassertTopmost();

    // Best-effort capture exclusion; a no-op when unsupported. Records the outcome.
    void ApplyCaptureExclusion(bool enabled);

    // Marks the renderer dirty and presents immediately if the window is visible.
    void Invalidate();

    // Handles a display configuration change: re-assert topmost and re-clamp the placement.
    void OnDisplayChange();

    void SetPositionChangedCallback(PositionChangedCallback callback);

    // Invoked when the window is destroyed (defaults to posting WM_QUIT).
    void SetCloseCallback(CloseCallback callback);

    [[nodiscard]] IRenderer* Renderer() const noexcept
    {
        return renderer_.get();
    }

    [[nodiscard]] const std::vector<MonitorWorkArea>& Monitors() const noexcept
    {
        return monitors_;
    }

    [[nodiscard]] const std::wstring& Diagnostics() const noexcept
    {
        return diagnostics_;
    }

    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

  private:
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateWindowForRecipe(OverlayRecipe recipe);
    bool InitializeRenderer(OverlayRecipe recipe, const pacecar::MonitorRect& placement);
    void HandleDpiChanged(WPARAM wParam, LPARAM lParam);
    void ResizeRendererToWindow();
    void PublishPlacement();
    [[nodiscard]] unsigned WindowDpi() const;
    [[nodiscard]] HMONITOR CurrentMonitor() const;
    [[nodiscard]] pacecar::MonitorRect PlacementFor(const std::optional<pacecar::MonitorRect>& saved,
                                                    unsigned dpi);
    [[nodiscard]] DWORD RecipeExtendedStyle(OverlayRecipe recipe) const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    OverlayOptions options_{};
    std::unique_ptr<IRenderer> renderer_;
    bool clickThrough_ = true;
    bool captureExclusionApplied_ = false;
    std::vector<MonitorWorkArea> monitors_;
    PositionChangedCallback positionChanged_;
    CloseCallback closeCallback_;
    std::wstring diagnostics_;
};

// Enumerates monitors in a stable order (primary first) as placement work areas. Exposed so the app
// can convert the persisted configuration the same way the window does.
[[nodiscard]] std::vector<MonitorWorkArea> EnumerateMonitorWorkAreas();
} // namespace pacecar::overlay