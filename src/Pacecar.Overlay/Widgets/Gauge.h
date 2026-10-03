#pragma once

// Circular arc gauge (design ref 04-ui-ux.md "circular arc gauge"). The track path geometry is
// cached and rebuilt only when the radius changes; the value arc reuses one path geometry object and
// re-streams its figure each draw, so no geometry or brush is allocated per frame. The arc sweep is
// computed by the pure `GaugeSweepForValue` in WidgetLayout.h.

#include "WidgetCommon.h"

namespace pacecar::overlay
{
class Gauge
{
  public:
    // Draws a 270-degree track arc plus a value arc for `fraction` (0..1), centered in `bounds`.
    void Draw(ID2D1RenderTarget* target, const RectF& bounds, double fraction, const ColorF& accent,
              const ColorF& track);

  private:
    bool EnsureResources(ID2D1RenderTarget* target, const PointF& center, float radius);
    void StreamValueArc(const PointF& center, float radius, float sweep);

    ComPtr<ID2D1PathGeometry> trackGeometry_;
    ComPtr<ID2D1PathGeometry> valueGeometry_;
    CachedBrush accentBrush_;
    CachedBrush trackBrush_;
    float cachedRadius_ = -1.0f;
    PointF cachedCenter_{};
};
} // namespace pacecar::overlay