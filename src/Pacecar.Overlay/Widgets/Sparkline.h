#pragma once

// Fixed-window polyline sparkline (design ref 04-ui-ux.md "sparklines over a fixed 60-sample
// window"). Vertices are computed into a fixed `std::array` by the pure `ComputeSparklinePoints`,
// then streamed into one reused path geometry, so drawing allocates nothing and never grows a
// per-frame `Vec`.

#include <array>

#include "WidgetCommon.h"

namespace pacecar::overlay
{
class Sparkline
{
  public:
    // Draws `samples` (oldest first) scaled from `minValue`..`maxValue` inside `bounds`.
    void Draw(ID2D1RenderTarget* target, const RectF& bounds, std::span<const float> samples,
              float minValue, float maxValue, const ColorF& accent);

  private:
    std::array<PointF, kSparklineCapacity> points_{};
    std::array<D2D1_POINT_2F, kSparklineCapacity> d2dPoints_{};
    ComPtr<ID2D1PathGeometry> geometry_;
    CachedBrush accentBrush_;
};
} // namespace pacecar::overlay