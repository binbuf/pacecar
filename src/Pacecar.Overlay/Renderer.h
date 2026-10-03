#pragma once

// Overlay presenter abstraction.
//
// `OverlayWindow` owns the HWND, input mode, DPI, and placement; a `Renderer` owns the graphics
// device and decides *how* pixels reach the screen. Two recipes exist (design ref 03):
//
//   - Recipe A, `LayeredRenderer`: Direct2D draws into a premultiplied WIC bitmap which is
//     published with `UpdateLayeredWindow(ULW_ALPHA)`. This is the v1 shipping path and the required
//     CPU/software fallback.
//   - Recipe B, `CompositionRenderer` (prototype): Direct3D 11 + a composition swapchain bound to a
//     DirectComposition visual on a `WS_EX_NOREDIRECTIONBITMAP` window. Never combined with
//     `WS_EX_LAYERED`.
//
// Later tasks (widgets, data) render through `IRenderer` and never branch on the recipe.

#include <memory>
#include <string>
#include <string_view>

#include <windows.h>

#include "pacecar/overlay/Theme.h"

namespace pacecar::overlay
{
enum class OverlayRecipe
{
    Layered,     // Recipe A - primary
    Composition, // Recipe B - prototype
};

struct PresentResult
{
    HRESULT hr = S_OK;
    // False when nothing was dirty and the presentation call was skipped (render-on-change).
    bool presented = false;
};

class IRenderer
{
  public:
    IRenderer() = default;
    virtual ~IRenderer() = default;

    IRenderer(const IRenderer&) = delete;
    IRenderer& operator=(const IRenderer&) = delete;

    [[nodiscard]] virtual OverlayRecipe Recipe() const noexcept = 0;
    [[nodiscard]] virtual std::wstring_view Name() const noexcept = 0;

    // One-line human-readable summary of recipe-specific prototype findings (FLIP_DISCARD result,
    // swapchain color space, device type) for the decision record and `--diagnostics`.
    [[nodiscard]] virtual std::wstring Describe() const = 0;

    // Creates device resources and the first surface at the given physical-pixel size and DPI.
    virtual HRESULT Initialize(HWND hwnd, int widthPx, int heightPx, unsigned dpi) = 0;

    // Recreates the surface for a new physical size / DPI. Keeps the device where possible.
    virtual HRESULT Resize(int widthPx, int heightPx, unsigned dpi) = 0;

    // Sets the resolved color/opacity theme used by the widget scene (from config + High Contrast).
    virtual void SetTheme(const ResolvedTheme& theme) = 0;

    // Marks the content dirty so the next `Present` re-renders and publishes.
    virtual void Invalidate() noexcept = 0;

    // Renders and publishes only when dirty; a clean renderer returns `presented == false`.
    virtual PresentResult Present() = 0;

    // Releases the backing surface while keeping the device (occlusion / session lock).
    virtual void Trim() noexcept = 0;
};

// Factory for Recipe A. Never fails to construct; `Initialize` may still fail.
[[nodiscard]] std::unique_ptr<IRenderer> CreateLayeredRenderer() noexcept;

// Factory for Recipe B. `Initialize` reports hardware/API failure so the caller can fall back to
// Recipe A; a software/WARP composition chain is never attempted.
[[nodiscard]] std::unique_ptr<IRenderer> CreateCompositionRenderer() noexcept;

// Convenience factory dispatching on `recipe`.
[[nodiscard]] std::unique_ptr<IRenderer> CreateRenderer(OverlayRecipe recipe) noexcept;
} // namespace pacecar::overlay