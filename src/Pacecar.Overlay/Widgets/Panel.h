#pragma once

// The rounded, translucent panel background (design ref 04-ui-ux.md "Rounded panel, subtle border,
// configurable opacity"). Geometry is immediate (a rounded-rect draw call), and the two solid
// brushes are cached per render target so drawing allocates nothing.

#include "WidgetCommon.h"

namespace pacecar::overlay
{
class Panel
{
  public:
    // Fills a rounded rectangle with `background` and outlines it with `border`.
    void Draw(ID2D1RenderTarget* target, const RectF& bounds, float cornerRadiusDip,
              const ColorF& background, const ColorF& border);

  private:
    CachedBrush background_;
    CachedBrush border_;
};
} // namespace pacecar::overlay