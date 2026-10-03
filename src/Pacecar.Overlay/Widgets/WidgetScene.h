#pragma once

// The draw-only widget scene: panel + header + a grid of tiles. This task owns geometry and drawing
// only; T11 replaces the demo arrangement with the real layout engine and T12 feeds live values.
// The scene holds all caches (text layouts, brushes, gauge/sparkline geometry) so a steady-state
// redraw of unchanged content performs no heap allocation.

#include <array>

#include "Header.h"
#include "Panel.h"
#include "Tile.h"
#include "WidgetStyles.h"
#include "pacecar/overlay/Theme.h"

namespace pacecar::overlay
{
class WidgetScene
{
  public:
    // Draws the panel/header/tile demo at the target's current DIP size with `theme`.
    void Draw(ID2D1RenderTarget* target, const ResolvedTheme& theme);

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
    std::array<Tile, 6> tiles_{};
    std::array<float, kSparklineCapacity> demoHistory_{};
};
} // namespace pacecar::overlay