#include "pacecar/overlay/WidgetLayout.h"

#include <algorithm>
#include <cmath>

namespace pacecar::overlay
{
namespace
{
float SumVisibleHeight(const TileFieldVisibility& visibility, const TileLayoutMetrics& metrics,
                       int reservedLines) noexcept
{
    float height = 0.0f;
    int lines = 0;
    if (visibility.label)
    {
        height += metrics.labelHeight;
        ++lines;
    }
    if (visibility.primary && !visibility.visualization)
    {
        height += metrics.primaryHeight;
        ++lines;
    }
    if (visibility.visualization)
    {
        height += metrics.graphHeight;
        ++lines;
    }
    if (visibility.secondary)
    {
        height += metrics.lineHeight;
        ++lines;
    }
    if (visibility.tertiary)
    {
        height += metrics.lineHeight;
        ++lines;
    }
    if (visibility.miniSparkline)
    {
        height += metrics.miniSparklineHeight;
        ++lines;
    }
    if (lines > 1)
    {
        height += metrics.lineGap * static_cast<float>(lines - 1);
    }
    static_cast<void>(reservedLines);
    return height;
}
} // namespace

TileLayout ComputeTileLayout(float widthDip, float heightDip,
                             const TileFieldVisibility& visibility,
                             const TileLayoutMetrics& metrics) noexcept
{
    TileLayout layout{};
    layout.visible = visibility;
    layout.measuredWidth = std::max(0.0f, widthDip);
    layout.measuredHeight = std::max(0.0f, heightDip);

    if (widthDip <= 0.0f || heightDip <= 0.0f)
    {
        return layout;
    }

    const RectF content{metrics.padding, metrics.padding, widthDip - metrics.padding,
                        heightDip - metrics.padding};
    layout.content = content;
    if (content.IsEmpty())
    {
        return layout;
    }

    const float left = content.left;
    const float right = content.right;
    const float bottom = content.bottom;
    float y = content.top;

    const auto nextBand = [&](float bandHeight, float gapAfter) noexcept -> RectF
    {
        const float top = y;
        const float bandBottom = std::min(bottom, top + bandHeight);
        y = std::min(bottom, top + bandHeight + gapAfter);
        return RectF{left, top, right, bandBottom};
    };

    if (visibility.label)
    {
        layout.label = nextBand(metrics.labelHeight, metrics.lineGap);
    }
    if (visibility.visualization)
    {
        layout.visualization = nextBand(metrics.graphHeight, metrics.sectionGap);
        if (visibility.primary)
        {
            // The primary value is drawn centered inside the gauge/sparkline block.
            layout.primary = layout.visualization;
        }
    }
    else if (visibility.primary)
    {
        layout.primary = nextBand(metrics.primaryHeight, metrics.lineGap);
    }
    if (visibility.secondary)
    {
        layout.secondary = nextBand(metrics.lineHeight, metrics.lineGap);
    }
    if (visibility.tertiary)
    {
        layout.tertiary = nextBand(metrics.lineHeight, metrics.lineGap);
    }
    if (visibility.miniSparkline)
    {
        layout.miniSparkline = nextBand(metrics.miniSparklineHeight, 0.0f);
    }

    return layout;
}

TileSize MeasureTile(const TileFieldVisibility& visibility, const TileLayoutMetrics& metrics) noexcept
{
    TileSize size{};
    size.width = metrics.minWidth;
    size.height = metrics.padding * 2.0f + SumVisibleHeight(visibility, metrics, 0);
    return size;
}

float GaugeSweepForValue(double fraction) noexcept
{
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return kGaugeSweep * static_cast<float>(clamped);
}

PointF ArcPoint(const PointF& center, float radius, float angle) noexcept
{
    return PointF{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

std::size_t ComputeSparklinePoints(std::span<const float> samples, float minValue, float maxValue,
                                   const RectF& box, std::span<PointF> out) noexcept
{
    if (samples.empty() || out.empty() || box.IsEmpty())
    {
        return 0;
    }

    const std::size_t count = std::min(samples.size(), out.size());
    const float width = box.Width();
    const float height = box.Height();
    const float range = maxValue - minValue;
    const bool degenerate = std::abs(range) < 1e-9f;

    for (std::size_t i = 0; i < count; ++i)
    {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
        const float x = box.left + t * width;
        float y = box.top + height * 0.5f;
        if (!degenerate)
        {
            const float normalized = (samples[i] - minValue) / range;
            y = box.bottom - normalized * height;
        }
        out[i] = PointF{x, y};
    }
    return count;
}
} // namespace pacecar::overlay