#include "Tile.h"

#include <algorithm>

#include "pacecar/overlay/Theme.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
namespace
{
// Text drawn without a panel background gets a soft drop shadow so it stays readable over arbitrary
// desktop/game content. The shadow is a translucent near-black (works for the light-on-dark
// default) offset one DIP down-right.
constexpr float kShadowOffset = 1.0f;
const ColorF kShadowColor{0.0f, 0.0f, 0.0f, 0.7f};

// The gauge arc occupies the central 80% of the shorter side; keep the value inside it so the text
// reads as part of the diagram instead of floating in the full visualization band.
RectF GaugeInnerRect(const RectF& visualization) noexcept
{
    const float side = std::min(visualization.Width(), visualization.Height());
    const float inner = side * 0.82f;
    const float centerX = (visualization.left + visualization.right) * 0.5f;
    const float centerY = (visualization.top + visualization.bottom) * 0.5f;
    return RectF{centerX - inner * 0.5f, centerY - inner * 0.5f, centerX + inner * 0.5f,
                 centerY + inner * 0.5f};
}
} // namespace

void Tile::Draw(ID2D1RenderTarget* target, TextRenderer& text, const WidgetStyles& styles,
                const RectF& bounds, const TileContent& content,
                const TileFieldVisibility& visibility, const ResolvedTheme& theme)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }
    const TileLayout layout = ComputeTileLayout(bounds.Width(), bounds.Height(), visibility);
    if (layout.content.IsEmpty())
    {
        return;
    }

    // Local rectangles are relative to the tile origin.
    const auto absolute = [&bounds](const RectF& local) noexcept {
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

    ID2D1SolidColorBrush* shadow =
        content.shadow ? shadowBrush_.Get(target, kShadowColor) : nullptr;
    const auto drawText = [&](std::uint32_t style, std::wstring_view value, const RectF& rect,
                              ID2D1SolidColorBrush* brush) noexcept {
        if (shadow != nullptr)
        {
            const RectF shifted{rect.left + kShadowOffset, rect.top + kShadowOffset,
                                rect.right + kShadowOffset, rect.bottom + kShadowOffset};
            text.DrawText(target, style, value, shifted, shadow);
        }
        text.DrawText(target, style, value, rect, brush);
    };

    // When a gauge is shown the value is centered inside the arc; otherwise it occupies its own
    // band. Sparklines keep the full-width band so the value sits on the graph.
    RectF primaryRect = layout.primary;
    if (layout.visible.visualization && !layout.visualization.IsEmpty() &&
        !content.visualizationIsSparkline)
    {
        primaryRect = GaugeInnerRect(layout.visualization);
    }

    if (layout.visible.label && !content.label.empty() && !layout.label.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = labelBrush_.Get(target, theme.palette.textDim))
        {
            drawText(styles.label, content.label, absolute(layout.label), brush);
        }
    }
    if (layout.visible.primary && !content.primary.empty() && !primaryRect.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = primaryBrush_.Get(target, theme.palette.text))
        {
            drawText(styles.primary, content.primary, absolute(primaryRect), brush);
        }
    }
    if (layout.visible.secondary && !content.secondary.empty() && !layout.secondary.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = secondaryBrush_.Get(target, theme.palette.textDim))
        {
            drawText(styles.secondary, content.secondary, absolute(layout.secondary), brush);
        }
    }
    if (layout.visible.tertiary && !content.tertiary.empty() && !layout.tertiary.IsEmpty())
    {
        if (ID2D1SolidColorBrush* brush = secondaryBrush_.Get(target, theme.palette.textDim))
        {
            drawText(styles.secondary, content.tertiary, absolute(layout.tertiary), brush);
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