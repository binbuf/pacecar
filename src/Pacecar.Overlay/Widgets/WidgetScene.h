#pragma once

// The draw-only widget scene: panel + header + the configured tile layout. The layout engine
// (T11) decides the rectangles and field flags; this class only draws them. T12 feeds live values
// through `SetContent`. The scene holds all caches (text layouts, brushes, gauge/sparkline geometry)
// so a steady-state redraw of unchanged content performs no heap allocation.

#include <array>

#include "Header.h"
#include "Panel.h"
#include "Tile.h"
#include "WidgetStyles.h"
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
};
} // namespace pacecar::overlay