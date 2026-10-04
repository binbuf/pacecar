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

    // Drops cached text layouts (device loss / occlusion).
    void Trim();

    [[nodiscard]] std::size_t TextLayoutCount() const noexcept
    {
        return text_.LayoutCount();
    }

  private:
    void EnsureInitialized(ID2D1RenderTarget* target);

    bool initialized_ = false;
    TextRenderer text_;
    WidgetStyles styles_{};
    Panel panel_;
    Header header_;
    LayoutSettings settings_ = DefaultLayoutSettings();
    std::array<Tile, kMaxTiles> tiles_{};
    std::array<float, kSparklineCapacity> demoHistory_{};
    std::shared_ptr<const pacecar::metrics::DisplayFrame> frame_{};
    // Scratch buffers reused for each formatted tile so a steady-state draw allocates nothing.
    std::array<std::array<wchar_t, 32>, kMaxTiles> primaryBuffers_{};
    std::array<std::array<wchar_t, 64>, kMaxTiles> secondaryBuffers_{};
    std::array<std::array<wchar_t, 32>, kMaxTiles> tertiaryBuffers_{};

    void DrawDemoTile(ID2D1RenderTarget* target, const TilePlacement& placement, std::size_t index,
                      const WidgetStyles& styles, const ResolvedTheme& theme);
    void DrawLiveTile(ID2D1RenderTarget* target, const TilePlacement& placement, std::size_t index,
                      const WidgetStyles& styles, const ResolvedTheme& theme);
};
} // namespace pacecar::overlay