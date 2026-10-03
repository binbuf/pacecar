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
#include "pacecar/overlay/OverlayCommands.h"
#include "pacecar/overlay/OverlayPlacement.h"

namespace pacecar::overlay
{
// Posted by the sampler to the overlay window (no parameters) when a new metrics frame is published
// and the overlay is visible. The UI formats the frame and repaints only if the render gate says
// the visible values changed. Part of the sampler/UI contract; see Sampler.h.
inline constexpr UINT WM_APP_METRICS_UPDATED = WM_APP + 1;

// Posted by the background cache-load thread once the last snapshot has been read (or the read
// failed). The UI then applies the cached frame if one was loaded.
inline constexpr UINT WM_APP_CACHE_READY = WM_APP + 2;

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
    // Panel background opacity and theme from config; High Contrast is OR-ed in at create time.
    double panelOpacity = 0.65;
    pacecar::Theme theme = pacecar::Theme::Dark;
};

class OverlayWindow
{
  public:
    using PositionChangedCallback = std::function<void(const pacecar::MonitorRect&)>;
    using CloseCallback = std::function<void()>;
    using CommandCallback = std::function<void(OverlayCommand)>;
    // Invoked before placement is published on WM_ENDSESSION so the app can flush its config.
    using SessionEndingCallback = std::function<void()>;
    // Reports the effective "should the sampler wake us?" visibility: the window is shown and not
    // suspended for occlusion/session lock. The app forwards this to `Sampler::SetVisible`.
    using VisibilityChangedCallback = std::function<void(bool visible)>;

    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // Creates the window and renderer. `savedRect` (may be nullopt or invalid) is clamped onto a
    // monitor at startup. Returns false only when even the Recipe A fallback cannot be created.
    bool Create(HINSTANCE instance, const OverlayOptions& options,
                const std::optional<pacecar::MonitorRect>& savedRect);

    void Show(int cmdShow);

    // Shows or hides the overlay without destroying it (tray Show/Hide and the toggle hotkey).
    void SetVisible(bool visible);
    void ToggleVisibility();
    [[nodiscard]] bool IsVisible() const noexcept;

    // Requests a clean shutdown: the next WM_CLOSE (or an immediate one) destroys the window and
    // posts WM_QUIT. Ordinary WM_CLOSE hides the overlay so the tray stays available.
    void Quit();

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

    // Applies config-driven theme and layout to the live renderer without recreating the window, so
// preset/toggle/theme changes take effect immediately (design ref 04-ui-ux.md).
    void ApplyConfig(const pacecar::Config& config);

    // Publishes a new metrics frame to the renderer (no-op when no renderer exists). The frame is held
// until the next one; a null frame restores the placeholders.
    void SetFrame(std::shared_ptr<const pacecar::metrics::DisplayFrame> frame);

    // Marks the renderer dirty and presents immediately if the window is visible and not suspended.
    void Invalidate();

    // Suspends presentation while the overlay is occluded or the session is locked (the design's
    // "stop rendering while occluded" rule). While suspended `Invalidate` is a no-op; clearing the
    // flag trims back in with a fresh present. Returns true when the state actually changed.
    bool SetRenderingSuspended(bool suspended) noexcept;
    [[nodiscard]] bool RenderingSuspended() const noexcept
    {
        return renderingSuspended_;
    }

    // Effective visibility: shown and not occluded/locked. The sampler wakes the UI only while this
    // is true.
    [[nodiscard]] bool EffectivelyVisible() const noexcept;

    void SetVisibilityChangedCallback(VisibilityChangedCallback callback);

    // Handles a display configuration change: re-assert topmost and re-clamp the placement.
    void OnDisplayChange();

    void SetPositionChangedCallback(PositionChangedCallback callback);

    // Invoked when the window is destroyed (defaults to posting WM_QUIT).
    void SetCloseCallback(CloseCallback callback);

    // Invoked on WM_ENDSESSION before the placement is published, so the app can persist config.
    void SetSessionEndingCallback(SessionEndingCallback callback);

    // Invoked for context-menu commands the window does not own (Settings/History/Specs).
    void SetCommandCallback(CommandCallback callback);

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
    [[nodiscard]] LRESULT HitTestBorder(LPARAM lParam) const;
    void ShowContextMenu(POINT screenPoint);
    void ExecuteCommand(OverlayCommand command);
    void RegisterSessionNotifications();
    void RefreshSuspension(const wchar_t* reason);
    [[nodiscard]] bool IsOccludedOrMinimized() const noexcept;
    void NotifyVisibilityChanged();
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
    CommandCallback commandCallback_;
    SessionEndingCallback sessionEndingCallback_;
    VisibilityChangedCallback visibilityChanged_;
    bool renderingSuspended_ = false;
    bool quitRequested_ = false;
    bool sessionNotificationsRegistered_ = false;
    std::wstring diagnostics_;
};

// Enumerates monitors in a stable order (primary first) as placement work areas. Exposed so the app
// can convert the persisted configuration the same way the window does.
[[nodiscard]] std::vector<MonitorWorkArea> EnumerateMonitorWorkAreas();
} // namespace pacecar::overlay