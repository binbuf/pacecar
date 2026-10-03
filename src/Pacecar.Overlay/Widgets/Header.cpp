#include "Header.h"

#include <algorithm>

namespace pacecar::overlay
{
namespace
{
constexpr float kSeparatorThickness = 1.0f;
} // namespace

void Header::Draw(ID2D1RenderTarget* target, TextRenderer& text, const WidgetStyles& styles,
                  const RectF& bounds, std::wstring_view title, std::wstring_view status,
                  const ResolvedTheme& theme)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }

    const float textHeight = std::max(0.0f, bounds.Height() - kSeparatorThickness - 2.0f);
    const RectF titleRect{bounds.left, bounds.top, bounds.left + bounds.Width() * 0.5f,
                          bounds.top + textHeight};
    const RectF statusRect{bounds.left + bounds.Width() * 0.5f, bounds.top, bounds.right,
                           bounds.top + textHeight};

    if (ID2D1SolidColorBrush* titleBrush = titleBrush_.Get(target, theme.palette.text))
    {
        text.DrawText(target, styles.headerTitle, title, titleRect, titleBrush);
    }
    if (ID2D1SolidColorBrush* statusBrush = statusBrush_.Get(target, theme.palette.textDim))
    {
        text.DrawText(target, styles.headerStatus, status, statusRect, statusBrush);
    }

    if (ID2D1SolidColorBrush* separator = separatorBrush_.Get(target, theme.palette.panelBorder))
    {
        const float y = bounds.bottom - kSeparatorThickness * 0.5f;
        target->DrawLine(D2D1::Point2F(bounds.left, y), D2D1::Point2F(bounds.right, y), separator,
                         kSeparatorThickness);
    }
}
} // namespace pacecar::overlay