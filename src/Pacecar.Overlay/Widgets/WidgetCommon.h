#pragma once

// Shared widget plumbing: COM aliases and conversions from the pure Core geometry/theme types to
// Direct2D types. Widget draw methods take an `ID2D1RenderTarget*` (the base interface). Recipe A's
// WIC render target is only an `ID2D1RenderTarget`; `ID2D1DeviceContext` (Recipe B) derives from it,
// so one widget implementation serves both recipes without branching.

#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "pacecar/overlay/Theme.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

[[nodiscard]] inline D2D1_COLOR_F ToD2D(const ColorF& color) noexcept
{
    return D2D1::ColorF(color.r, color.g, color.b, color.a);
}

[[nodiscard]] inline D2D1_RECT_F ToD2D(const RectF& rect) noexcept
{
    return D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
}

[[nodiscard]] inline D2D1_POINT_2F ToD2D(const PointF& point) noexcept
{
    return D2D1::Point2F(point.x, point.y);
}

// A solid brush cached per render target and color. Recipe A recreates its WIC render target on
// resize, so brushes (unlike factory-owned geometry) must be rebuilt when the target changes. The
// steady-state call with the same target and color allocates nothing.
struct CachedBrush
{
    ComPtr<ID2D1SolidColorBrush> brush;
    ID2D1RenderTarget* target = nullptr;
    ColorF color{};
    bool bound = false;

    [[nodiscard]] ID2D1SolidColorBrush* Get(ID2D1RenderTarget* renderTarget,
                                            const ColorF& desired) noexcept
    {
        if (renderTarget == nullptr)
        {
            return nullptr;
        }
        if (bound && target == renderTarget && color == desired && brush != nullptr)
        {
            return brush.Get();
        }
        brush.Reset();
        if (FAILED(renderTarget->CreateSolidColorBrush(ToD2D(desired), brush.GetAddressOf())))
        {
            return nullptr;
        }
        target = renderTarget;
        color = desired;
        bound = true;
        return brush.Get();
    }
};
} // namespace pacecar::overlay