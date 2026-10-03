#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>

#include "allocation_probe.h"
#include "pacecar/overlay/OverlayPlacement.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace
{
using pacecar::overlay::ArcPoint;
using pacecar::overlay::ComputeSparklinePoints;
using pacecar::overlay::ComputeTileLayout;
using pacecar::overlay::GaugeSweepForValue;
using pacecar::overlay::kGaugeSweep;
using pacecar::overlay::kSparklineCapacity;
using pacecar::overlay::MeasureTile;
using pacecar::overlay::PointF;
using pacecar::overlay::RectF;
using pacecar::overlay::TileFieldVisibility;

constexpr float kWidth = 120.0f;
constexpr float kHeight = 200.0f;

TEST(WidgetLayout, AllFieldsStackTopToBottom)
{
    TileFieldVisibility visibility{};
    visibility.miniSparkline = true;
    const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);

    EXPECT_FALSE(layout.label.IsEmpty());
    EXPECT_FALSE(layout.primary.IsEmpty());
    EXPECT_FALSE(layout.visualization.IsEmpty());
    EXPECT_FALSE(layout.secondary.IsEmpty());
    EXPECT_FALSE(layout.tertiary.IsEmpty());
    EXPECT_FALSE(layout.miniSparkline.IsEmpty());

    EXPECT_LE(layout.label.bottom, layout.visualization.top + 0.001f);
    EXPECT_LE(layout.visualization.bottom, layout.secondary.top + 0.001f);
    EXPECT_LE(layout.secondary.bottom, layout.tertiary.top + 0.001f);
    EXPECT_LE(layout.tertiary.bottom, layout.miniSparkline.top + 0.001f);
}

TEST(WidgetLayout, PrimaryOverlaysVisualization)
{
    TileFieldVisibility visibility{};
    const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);
    EXPECT_EQ(layout.primary, layout.visualization);
}

TEST(WidgetLayout, PrimaryHasOwnBandWithoutVisualization)
{
    TileFieldVisibility visibility{};
    visibility.visualization = false;
    const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);
    EXPECT_TRUE(layout.visualization.IsEmpty());
    EXPECT_FALSE(layout.primary.IsEmpty());
    EXPECT_NE(layout.primary, layout.visualization);
}

TEST(WidgetLayout, HiddenFieldsProduceEmptyRects)
{
    TileFieldVisibility visibility{};
    visibility.label = false;
    visibility.secondary = false;
    visibility.tertiary = false;
    const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);

    EXPECT_TRUE(layout.label.IsEmpty());
    EXPECT_TRUE(layout.secondary.IsEmpty());
    EXPECT_TRUE(layout.tertiary.IsEmpty());
    EXPECT_FALSE(layout.primary.IsEmpty());
    EXPECT_FALSE(layout.visualization.IsEmpty());
}

TEST(WidgetLayout, VisibilityFlagsAreMirrored)
{
    TileFieldVisibility visibility{};
    visibility.label = false;
    visibility.miniSparkline = true;
    const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);
    EXPECT_FALSE(layout.visible.label);
    EXPECT_TRUE(layout.visible.miniSparkline);
    EXPECT_EQ(layout.visible, visibility);
}

TEST(WidgetLayout, DegenerateSizeProducesEmptyLayout)
{
    const auto layout = ComputeTileLayout(0.0f, 100.0f, TileFieldVisibility{});
    EXPECT_TRUE(layout.content.IsEmpty());
    EXPECT_TRUE(layout.label.IsEmpty());
    EXPECT_TRUE(layout.primary.IsEmpty());
}

TEST(WidgetLayout, ContentIsInsetByPadding)
{
    const auto metrics = pacecar::overlay::TileLayoutMetrics{};
    const auto layout = ComputeTileLayout(kWidth, kHeight, TileFieldVisibility{}, metrics);
    EXPECT_FLOAT_EQ(layout.content.left, metrics.padding);
    EXPECT_FLOAT_EQ(layout.content.top, metrics.padding);
    EXPECT_FLOAT_EQ(layout.content.right, kWidth - metrics.padding);
    EXPECT_FLOAT_EQ(layout.content.bottom, kHeight - metrics.padding);
}

TEST(WidgetLayout, MeasureTileHonorsVisibility)
{
    TileFieldVisibility everything{};
    everything.miniSparkline = true;
    TileFieldVisibility minimal{};
    minimal.label = false;
    minimal.primary = true;
    minimal.secondary = false;
    minimal.tertiary = false;
    minimal.visualization = false;

    const auto full = MeasureTile(everything);
    const auto small = MeasureTile(minimal);
    EXPECT_GT(full.height, small.height);
    EXPECT_FLOAT_EQ(full.width, small.width);
    EXPECT_GT(full.height, 0.0f);
}

TEST(WidgetLayout, GaugeSweepClampsFraction)
{
    EXPECT_FLOAT_EQ(GaugeSweepForValue(-1.0), 0.0f);
    EXPECT_FLOAT_EQ(GaugeSweepForValue(0.0), 0.0f);
    EXPECT_FLOAT_EQ(GaugeSweepForValue(1.0), kGaugeSweep);
    EXPECT_FLOAT_EQ(GaugeSweepForValue(0.5), kGaugeSweep * 0.5f);
    EXPECT_FLOAT_EQ(GaugeSweepForValue(2.0), kGaugeSweep);
}

TEST(WidgetLayout, ArcPointStaysOnCircle)
{
    const PointF center{50.0f, 50.0f};
    const PointF point = ArcPoint(center, 20.0f, 0.0f);
    EXPECT_NEAR(point.x, 70.0f, 1e-3f);
    EXPECT_NEAR(point.y, 50.0f, 1e-3f);
}

TEST(WidgetLayout, SparklineMapsRangeToBox)
{
    const std::array<float, 3> samples{0.0f, 50.0f, 100.0f};
    const RectF box{0.0f, 0.0f, 100.0f, 40.0f};
    std::array<PointF, kSparklineCapacity> points{};

    const std::size_t count =
        ComputeSparklinePoints(samples, 0.0f, 100.0f, box, std::span<PointF>(points));
    ASSERT_EQ(count, 3u);
    EXPECT_NEAR(points[0].x, 0.0f, 1e-3f);
    EXPECT_NEAR(points[0].y, 40.0f, 1e-3f);
    EXPECT_NEAR(points[1].x, 50.0f, 1e-3f);
    EXPECT_NEAR(points[1].y, 20.0f, 1e-3f);
    EXPECT_NEAR(points[2].x, 100.0f, 1e-3f);
    EXPECT_NEAR(points[2].y, 0.0f, 1e-3f);
}

TEST(WidgetLayout, SparklineDegenerateRangeCenters)
{
    const std::array<float, 2> samples{42.0f, 42.0f};
    const RectF box{0.0f, 0.0f, 100.0f, 40.0f};
    std::array<PointF, kSparklineCapacity> points{};
    const std::size_t count =
        ComputeSparklinePoints(samples, 42.0f, 42.0f, box, std::span<PointF>(points));
    ASSERT_EQ(count, 2u);
    EXPECT_NEAR(points[0].y, 20.0f, 1e-3f);
    EXPECT_NEAR(points[1].y, 20.0f, 1e-3f);
}

TEST(WidgetLayout, SparklineNeverExceedsCapacity)
{
    std::array<float, 200> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<float>(i % 100);
    }
    const RectF box{0.0f, 0.0f, 100.0f, 40.0f};
    std::array<PointF, kSparklineCapacity> points{};
    const std::size_t count =
        ComputeSparklinePoints(samples, 0.0f, 100.0f, box, std::span<PointF>(points));
    EXPECT_EQ(count, kSparklineCapacity);
}

TEST(WidgetLayout, EmptySparklineProducesNoPoints)
{
    std::array<PointF, kSparklineCapacity> points{};
    EXPECT_EQ(ComputeSparklinePoints({}, 0.0f, 100.0f, RectF{0.0f, 0.0f, 10.0f, 10.0f},
                                     std::span<PointF>(points)),
              0u);
}

TEST(WidgetLayout, LayoutIsDpiIndependentInDips)
{
    // The same DIP layout must hold at every scale; only the physical pixel size changes.
    const auto at96 = ComputeTileLayout(kWidth, kHeight, TileFieldVisibility{});
    const auto at144 = ComputeTileLayout(kWidth, kHeight, TileFieldVisibility{});
    EXPECT_EQ(at96.primary, at144.primary);

    EXPECT_EQ(pacecar::overlay::DipToPixels(kWidth, 96), 120);
    EXPECT_EQ(pacecar::overlay::DipToPixels(kWidth, 144), 180);
    EXPECT_EQ(pacecar::overlay::DipToPixels(kWidth, 192), 240);
}

TEST(WidgetLayout, HotPathPerformsZeroHeapAllocations)
{
    TileFieldVisibility visibility{};
    visibility.miniSparkline = true;
    std::array<float, kSparklineCapacity> samples{};
    std::array<PointF, kSparklineCapacity> points{};
    const RectF box{0.0f, 0.0f, 100.0f, 40.0f};

    // Warm up (any lazy init is outside the measured window).
    for (int i = 0; i < 4; ++i)
    {
        const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);
        static_cast<void>(layout);
        static_cast<void>(
            ComputeSparklinePoints(samples, 0.0f, 100.0f, box, std::span<PointF>(points)));
    }

    pacecar::test::ResetAllocationCount();
    std::size_t sink = 0;
    for (int i = 0; i < 10000; ++i)
    {
        samples[static_cast<std::size_t>(i) % kSparklineCapacity] = static_cast<float>(i % 100);
        const auto layout = ComputeTileLayout(kWidth, kHeight, visibility);
        sink += static_cast<std::size_t>(layout.primary.Width());
        sink += ComputeSparklinePoints(samples, 0.0f, 100.0f, box, std::span<PointF>(points));
    }

    EXPECT_GT(sink, 0u);
    EXPECT_EQ(pacecar::test::AllocationCount(), 0u);
}
} // namespace