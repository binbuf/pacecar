#include "Tile.h"

#include "pacecar/overlay/Theme.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
void Tile::Draw(ID2D1RenderTarget* target, TextRenderer& text, const WidgetStyles& styles,
                const RectF& bounds, const TileContent& content,
                const TileFieldVisibility& visibility, const ResolvedTheme& theme)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }
    const TileLayout layout =
        ComputeTileLayout(bounds.Width(), bounds.Height(), visibility);
    if (layout.content.IsEmpty())
    {
        return;
    }

    // Local rectangles are relative to the tile origin.
    const auto absolute = [&bounds](const RectF& local) noexcept
    {
        return RectF{bounds.left + local.left, bounds.top + local.top, bounds.left + local.right,
                     bounds.top + local.bottom};
    };

    const ColorF accent = AccentFor(theme.palette, content.family);

    // Visualization sits behind the primary value.
    if (layout.visible.visualization && !layout.visualization.IsEmpty())
    {
        const RectF rect = absolute(layout.visualization);
        if (content.visualizationIsSparkline && !content.sparkSamples.empty())
        {
            sparkline_.Draw(target, rect, content.sparkSamples, content.sparkMin, content.sparkMax,
                            accent);
        }
        else
        {
            gauge_.Draw(target, rect, content.gaugeFraction, accent, theme.palette.track);
        }
    }

    if (layout.visible.label && !content.label.empty() && !layout.label.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = labelBrush_.Get(target, theme.palette.textDim))
        {
            text.DrawText(target, styles.label, content.label, absolute(layout.label), brush);
        }
    }
    if (layout.visible.primary && !content.primary.empty() && !layout.primary.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = primaryBrush_.Get(target, theme.palette.text))
        {
            text.DrawText(target, styles.primary, content.primary, absolute(layout.primary), brush);
        }
    }
    if (layout.visible.secondary && !content.secondary.empty() && !layout.secondary.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = secondaryBrush_.Get(target, theme.palette.textDim))
        {
            text.DrawText(target, styles.secondary, content.secondary, absolute(layout.secondary),
                          brush);
        }
    }
    if (layout.visible.tertiary && !content.tertiary.empty() && !layout.tertiary.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = secondaryBrush_.Get(target, theme.palette.textDim))
        {
            text.DrawText(target, styles.secondary, content.tertiary, absolute(layout.tertiary),
                          brush);
        }
    }
    if (layout.visible.miniSparkline && !layout.miniSparkline.IsEmpty() &&
        !content.sparkSamples.empty())
    {
        sparkline_.Draw(target, absolute(layout.miniSparkline), content.sparkSamples,
                        content.sparkMin, content.sparkMax, accent);
    }
}
} // namespace pacecar::overlay