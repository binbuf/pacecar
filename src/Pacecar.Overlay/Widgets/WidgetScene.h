#pragma once

// The draw-only widget scene: panel + header + the configured tile layout. The layout engine
// (T11) decides the rectangles and field flags; this class only draws them. T12 feeds live values
// through `SetContent`. The scene holds all caches (text layouts, brushes, gauge/sparkline
// geometry) so a steady-state redraw of unchanged content performs no heap allocation.

#include <array>
#include <memory>

#include "Header.h"
#include "Panel.h"
#include "Tile.h"
#include "WidgetStyles.h"
#include "pacecar/metrics/DisplayFrame.h"
#include "pacecar/overlay/Layout.h"
#include "pacecar/overlay/Theme.h"

namespace pacecar::overlay
{
class WidgetScene
{
  public:
    // Draws the panel/header/tiles at the target's current DIP size with `theme`.
    void Draw(ID2D1RenderTarget* target, const ResolvedTheme& theme);

    // Applies a new config-driven layout (preset + per-tile/per-field toggles). Cheap and
    // allocation-free; the next Draw reflects it immediately.
    void SetLayout(const LayoutSettings& settings) noexcept
    {
        settings_ = settings;
    }

    // Publishes the latest metrics frame (snapshot + sparkline tails). The scene keeps the shared
    // pointer and formats it during Draw; a null frame shows the neutral placeholders used before
    // the first sample or on a cold start.
    void SetFrame(std::shared_ptr<const pacecar::metrics::DisplayFrame> frame) noexcept
    {
        frame_ = std::move(frame);
    }

    // Sets the display refresh rate used to scale the FPS gauge (frames-per-refresh). Values below
    // 1 are ignored; the default keeps the gauge meaningful before a monitor is known.
    void SetTargetFps(double targetFps) noexcept
    {
        if (targetFps >= 1.0)
        {
            targetFps_ = targetFps;
        }
    }

    // Drops cached text layouts (device loss / occlusion).
    void Trim();

    [[nodiscard]] std::size_t TextLayoutCount() const noexcept
    {
        return text_.LayoutCount();
    }

  private:
    void EnsureInitialized(ID2D1RenderTarget* target, float statTextSizeDip);
    // Registers (or re-registers) every text format. `statTextSizeDip` sizes the StatRows value.
    void RegisterStyles(float statTextSizeDip);

    void DrawDemoTile(ID2D1RenderTarget* target, const TilePlacement& placement, std::size_t index,
                      const WidgetStyles& styles, const ResolvedTheme& theme);
    void DrawLiveTile(ID2D1RenderTarget* target, const TilePlacement& placement, std::size_t index,
                      const WidgetStyles& styles, const ResolvedTheme& theme);
    // The StatRows view: one icon + value line per visible stat, stacked top to bottom. `shadow`
    // adds a drop shadow when the panel background is not drawn.
    void DrawStatRows(ID2D1RenderTarget* target, const ResolvedTheme& theme, bool shadow);

    bool initialized_ = false;
    float registeredStatTextSize_ = 0.0f;
    TextRenderer text_;
    WidgetStyles styles_{};
    Panel panel_;
    Header header_;
    LayoutSettings settings_ = DefaultLayoutSettings();
    std::array<Tile, kMaxTiles> tiles_{};
    // Smoothed autoscale per tile for rate sparklines (network/disk/ping/FPS), so a one-frame spike
    // does not rescale the whole history. Grows immediately, decays slowly.
    std::array<float, kMaxTiles> sparkScale_{};
    double targetFps_ = 120.0;
    std::shared_ptr<const pacecar::metrics::DisplayFrame> frame_{};
    // Scratch buffers reused for each formatted tile so a steady-state draw allocates nothing.
    std::array<std::array<wchar_t, 32>, kMaxTiles> primaryBuffers_{};
    std::array<std::array<wchar_t, 64>, kMaxTiles> secondaryBuffers_{};
    std::array<std::array<wchar_t, 32>, kMaxTiles> tertiaryBuffers_{};
    CachedBrush statLabelBrush_;
    CachedBrush statValueBrush_;
    CachedBrush statShadowBrush_;
};
} // namespace pacecar::overlay