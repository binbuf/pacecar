#include "Panel.h"

namespace pacecar::overlay
{
void Panel::Draw(ID2D1RenderTarget* target, const RectF& bounds, float cornerRadiusDip,
                 const ColorF& background, const ColorF& border)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }
    // Inset half a pixel so the border stroke stays inside the surface.
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(
        D2D1::RectF(bounds.left + 0.5f, bounds.top + 0.5f, bounds.right - 0.5f, bounds.bottom - 0.5f),
        cornerRadiusDip, cornerRadiusDip);

    if (ID2D1SolidColorBrush* fill = background_.Get(target, background))
    {
        target->FillRoundedRectangle(rounded, fill);
    }
    if (ID2D1SolidColorBrush* stroke = border_.Get(target, border))
    {
        target->DrawRoundedRectangle(rounded, stroke, 1.0f);
    }
}
} // namespace pacecar::overlay