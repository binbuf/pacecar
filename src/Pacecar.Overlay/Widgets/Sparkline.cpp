#include "Sparkline.h"

#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
void Sparkline::Draw(ID2D1RenderTarget* target, const RectF& bounds, std::span<const float> samples,
                     float minValue, float maxValue, const ColorF& accent)
{
    if (target == nullptr || bounds.IsEmpty())
    {
        return;
    }
    const std::size_t count = ComputeSparklinePoints(
        samples, minValue, maxValue, bounds, std::span<PointF>(points_.data(), points_.size()));
    if (count < 2)
    {
        return;
    }
    if (geometry_ == nullptr)
    {
        ComPtr<ID2D1Factory> factory;
        target->GetFactory(factory.GetAddressOf());
        if (!factory)
        {
            return;
        }
        if (FAILED(factory->CreatePathGeometry(geometry_.GetAddressOf())))
        {
            return;
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        d2dPoints_[i] = ToD2D(points_[i]);
    }

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry_->Open(sink.GetAddressOf())))
    {
        return;
    }
    sink->BeginFigure(d2dPoints_[0], D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(d2dPoints_.data() + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    if (ID2D1SolidColorBrush* brush = accentBrush_.Get(target, accent))
    {
        target->DrawGeometry(geometry_.Get(), brush, 1.25f);
    }
}
} // namespace pacecar::overlay