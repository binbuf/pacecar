#include "Gauge.h"

#include "pacecar/overlay/WidgetLayout.h"

#include <algorithm>
#include <cmath>

namespace pacecar::overlay
{
namespace
{
constexpr float kRadiusFraction = 0.40f;
constexpr float kStrokeFraction = 0.08f;
constexpr float kMinRadius = 1.0f;

D2D1_ARC_SIZE ArcSizeForSweep(float sweep) noexcept
{
    return std::abs(sweep) > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
}

HRESULT StreamArc(ID2D1PathGeometry* geometry, const PointF& center, float radius, float startAngle,
                  float sweep)
{
    if (geometry == nullptr || std::abs(sweep) < 1e-4f)
    {
        return E_INVALIDARG;
    }
    ComPtr<ID2D1GeometrySink> sink;
    HRESULT hr = geometry->Open(sink.GetAddressOf());
    if (FAILED(hr))
    {
        return hr;
    }
    const PointF start = ArcPoint(center, radius, startAngle);
    const PointF end = ArcPoint(center, radius, startAngle + sweep);
    sink->BeginFigure(D2D1::Point2F(start.x, start.y), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(end.x, end.y), D2D1::SizeF(radius, radius), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, ArcSizeForSweep(sweep)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    return sink->Close();
}
} // namespace

bool Gauge::EnsureResources(ID2D1RenderTarget* target, const PointF& center, float radius)
{
    if (target == nullptr)
    {
        return false;
    }
    if (!trackGeometry_ || !valueGeometry_)
    {
        ComPtr<ID2D1Factory> factory;
        target->GetFactory(factory.GetAddressOf());
        if (!factory)
        {
            return false;
        }
        if (FAILED(factory->CreatePathGeometry(trackGeometry_.GetAddressOf())) ||
            FAILED(factory->CreatePathGeometry(valueGeometry_.GetAddressOf())))
        {
            return false;
        }
        cachedRadius_ = -1.0f;
    }
    const bool moved = std::abs(radius - cachedRadius_) > 0.01f ||
                       std::abs(center.x - cachedCenter_.x) > 0.01f ||
                       std::abs(center.y - cachedCenter_.y) > 0.01f;
    if (moved)
    {
        if (FAILED(StreamArc(trackGeometry_.Get(), center, radius, kGaugeStartAngle, kGaugeSweep)))
        {
            return false;
        }
        cachedRadius_ = radius;
        cachedCenter_ = center;
    }
    return true;
}

void Gauge::StreamValueArc(const PointF& center, float radius, float sweep)
{
    if (valueGeometry_ == nullptr || sweep <= 0.0f)
    {
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(valueGeometry_->Open(sink.GetAddressOf())))
    {
        return;
    }
    const PointF start = ArcPoint(center, radius, kGaugeStartAngle);
    const PointF end = ArcPoint(center, radius, kGaugeStartAngle + sweep);
    sink->BeginFigure(D2D1::Point2F(start.x, start.y), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(end.x, end.y), D2D1::SizeF(radius, radius), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, ArcSizeForSweep(sweep)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
}

void Gauge::Draw(ID2D1RenderTarget* target, const RectF& bounds, double fraction,
                 const ColorF& accent, const ColorF& track)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }
    const float diameter = std::min(bounds.Width(), bounds.Height());
    if (diameter <= 2.0f)
    {
        return;
    }
    const PointF center{bounds.left + bounds.Width() * 0.5f, bounds.top + bounds.Height() * 0.5f};
    const float radius = std::max(kMinRadius, diameter * kRadiusFraction);
    const float strokeWidth = std::max(1.0f, diameter * kStrokeFraction);

    if (!EnsureResources(target, center, radius))
    {
        return;
    }

    if (ID2D1SolidColorBrush* trackBrush = trackBrush_.Get(target, track))
    {
        target->DrawGeometry(trackGeometry_.Get(), trackBrush, strokeWidth);
    }

    const float sweep = GaugeSweepForValue(fraction);
    if (sweep > 0.0f)
    {
        if (ID2D1SolidColorBrush* accentBrush = accentBrush_.Get(target, accent))
        {
            StreamValueArc(center, radius, sweep);
            target->DrawGeometry(valueGeometry_.Get(), accentBrush, strokeWidth);
        }
    }
}
} // namespace pacecar::overlay