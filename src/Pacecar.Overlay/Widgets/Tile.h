#pragma once

// A metric tile: optional label, primary value, secondary/tertiary lines, an optional gauge or
// sparkline, and an optional mini-sparkline (design refs 04-ui-ux.md, 05-performance.md). The
// layout is the pure `ComputeTileLayout` from WidgetLayout.h; this class only converts DIPs to
// draw calls and reuses the gauge/sparkline sub-widgets.

#include <span>
#include <string_view>

#include "Gauge.h"
#include "Sparkline.h"
#include "Text.h"
#include "WidgetStyles.h"

namespace pacecar::overlay
{
struct TileContent
{
    MetricFamily family = MetricFamily::Cpu;
    std::wstring_view label{};
    std::wstring_view primary{};
    std::wstring_view secondary{};
    std::wstring_view tertiary{};
    // Gauge fraction in 0..1 when the visualization is a gauge.
    double gaugeFraction = 0.0;
    // When true the main visualization is a sparkline instead of a gauge.
    bool visualizationIsSparkline = false;
    // Optional history for the main visualization and/or the mini-sparkline.
    std::span<const float> sparkSamples{};
    float sparkMin = 0.0f;
    float sparkMax = 100.0f;
};

class Tile
{
  public:
    void Draw(ID2D1RenderTarget* target, TextRenderer& text, const WidgetStyles& styles,
              const RectF& bounds, const TileContent& content,
              const TileFieldVisibility& visibility, const ResolvedTheme& theme);

  private:
    Gauge gauge_;
    Sparkline sparkline_;
    CachedBrush labelBrush_;
    CachedBrush primaryBrush_;
    CachedBrush secondaryBrush_;
};
} // namespace pacecar::overlay