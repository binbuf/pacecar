#include <gtest/gtest.h>

#include <vector>

#include "pacecar/overlay/OverlayPlacement.h"

namespace
{
using pacecar::overlay::ClampToWorkArea;
using pacecar::overlay::DipToPixels;
using pacecar::overlay::DpiScale;
using pacecar::overlay::FindBestMonitor;
using pacecar::overlay::FindNearestMonitor;
using pacecar::overlay::IntRect;
using pacecar::overlay::IntersectionArea;
using pacecar::overlay::IsRectVisible;
using pacecar::overlay::IsSufficientlyVisible;
using pacecar::overlay::kDefaultVisibleMargin;
using pacecar::overlay::MonitorWorkArea;
using pacecar::overlay::NormalizeDpi;
using pacecar::overlay::PixelsToDip;
using pacecar::overlay::PrimaryMonitor;
using pacecar::overlay::ScaleRectForDpi;

// A two-monitor layout: a 1920x1080 primary at the origin and a 2560x1440 secondary to its right.
// Work areas exclude a 40 px taskbar on the primary.
std::vector<MonitorWorkArea> TwoMonitorLayout()
{
    return {
        MonitorWorkArea{L"\\\\.\\DISPLAY1", IntRect{0, 0, 1920, 1040}, 96, true},
        MonitorWorkArea{L"\\\\.\\DISPLAY2", IntRect{1920, 0, 4480, 1440}, 144, false},
    };
}

std::vector<MonitorWorkArea> SingleMonitorLayout()
{
    return {MonitorWorkArea{L"\\\\.\\DISPLAY1", IntRect{0, 0, 1920, 1040}, 96, true}};
}

TEST(OverlayPlacement, IntersectionAreaHandlesOverlapContainmentAndDisjoint)
{
    EXPECT_EQ(IntersectionArea(IntRect{0, 0, 100, 100}, IntRect{50, 50, 150, 150}), 2500);
    EXPECT_EQ(IntersectionArea(IntRect{0, 0, 100, 100}, IntRect{10, 10, 20, 20}), 100);
    EXPECT_EQ(IntersectionArea(IntRect{0, 0, 100, 100}, IntRect{100, 0, 200, 100}), 0);
    EXPECT_EQ(IntersectionArea(IntRect{0, 0, 100, 100}, IntRect{-50, -50, 0, 0}), 0);
}

TEST(OverlayPlacement, SufficientVisibilityRequiresMarginOnBothAxes)
{
    const MonitorWorkArea monitor{L"\\\\.\\DISPLAY1", IntRect{0, 0, 1920, 1040}, 96, true};
    EXPECT_TRUE(IsSufficientlyVisible(IntRect{100, 100, 400, 300}, monitor));
    // Only 20 px of width is inside -> below the 32 px margin.
    EXPECT_FALSE(IsSufficientlyVisible(IntRect{-380, 100, 20, 300}, monitor));
    // A rectangle smaller than the margin only needs to be fully visible.
    EXPECT_TRUE(IsSufficientlyVisible(IntRect{0, 0, 10, 10}, monitor, kDefaultVisibleMargin));
    EXPECT_FALSE(IsSufficientlyVisible(IntRect{-5, 0, 5, 10}, monitor, kDefaultVisibleMargin));
}

TEST(OverlayPlacement, FindBestMonitorPicksLargestOverlap)
{
    const auto monitors = TwoMonitorLayout();
    const MonitorWorkArea* best = FindBestMonitor(IntRect{1700, 100, 2100, 300}, monitors);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->deviceName, L"\\\\.\\DISPLAY1");

    best = FindBestMonitor(IntRect{1900, 100, 2400, 300}, monitors);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->deviceName, L"\\\\.\\DISPLAY2");
}

TEST(OverlayPlacement, ClampLeavesSufficientlyVisibleRectangleUntouched)
{
    const auto monitors = TwoMonitorLayout();
    const IntRect rect{2000, 200, 2400, 500};
    EXPECT_EQ(ClampToWorkArea(rect, monitors), rect);
    EXPECT_TRUE(IsRectVisible(rect, monitors));
}

TEST(OverlayPlacement, ClampPullsOffscreenRectangleBackWithFullExtent)
{
    const auto monitors = SingleMonitorLayout();
    const IntRect rect{-500, -500, -100, -100};
    EXPECT_EQ(ClampToWorkArea(rect, monitors), IntRect(0, 0, 400, 400));

    // A window hanging off the right/bottom edge but keeping a full margin visible is an
    // intentional placement and must be preserved, not yanked fully on-screen.
    const IntRect overflow{1800, 900, 2200, 1300};
    EXPECT_EQ(ClampToWorkArea(overflow, monitors), overflow);
}

TEST(OverlayPlacement, ClampRepairsABarelyVisibleSliver)
{
    const auto monitors = TwoMonitorLayout();
    const IntRect sliver{1890, 100, 1930, 300};
    EXPECT_FALSE(IsSufficientlyVisible(sliver, monitors.front(), kDefaultVisibleMargin));
    const IntRect clamped = ClampToWorkArea(sliver, monitors);
    // Re-anchored onto the primary (larger overlap) with its 40 px width fully inside.
    EXPECT_EQ(clamped, IntRect(1880, 100, 1920, 300));
}

TEST(OverlayPlacement, ClampKeepsRectangleOnTheMonitorItMostlyOccupies)
{
    const auto monitors = TwoMonitorLayout();
    const IntRect rect{2000, 100, 2400, 400};
    EXPECT_EQ(ClampToWorkArea(rect, monitors), rect);
}

TEST(OverlayPlacement, ClampUsesNearestMonitorWhenThereIsNoOverlap)
{
    const auto monitors = TwoMonitorLayout();
    // Far to the right of both monitors: the center is closest to DISPLAY2's center.
    const IntRect right{5000, 100, 5400, 400};
    EXPECT_EQ(ClampToWorkArea(right, monitors), IntRect(4080, 100, 4480, 400));

    // Far to the left: closest to the primary.
    const IntRect left{-3000, 100, -2600, 400};
    EXPECT_EQ(ClampToWorkArea(left, monitors), IntRect(0, 100, 400, 400));
}

TEST(OverlayPlacement, ClampIsIdentityWithoutMonitorsOrForEmptyRect)
{
    const IntRect rect{100, 100, 300, 300};
    EXPECT_EQ(ClampToWorkArea(rect, {}), rect);
    EXPECT_EQ(ClampToWorkArea(IntRect{}, SingleMonitorLayout()), IntRect{});
}

TEST(OverlayPlacement, ClampPullsInARectangleLargerThanTheWorkArea)
{
    const auto monitors = SingleMonitorLayout();
    // A 2000x1600 window whose top-left quarter sits on the primary; only 20 px of its left edge
    // overlaps, below the margin, so it is re-anchored and capped to the work area.
    const IntRect mostlyOff{1900, -100, 3900, 1500};
    const IntRect clamped = ClampToWorkArea(mostlyOff, monitors);
    EXPECT_EQ(clamped, IntRect(0, 0, 1920, 1040));
}

TEST(OverlayPlacement, NearestMonitorTieAndPrimarySelection)
{
    const auto monitors = TwoMonitorLayout();
    EXPECT_EQ(FindNearestMonitor(IntRect{0, 0, 10, 10}, monitors)->deviceName, L"\\\\.\\DISPLAY1");
    ASSERT_NE(PrimaryMonitor(monitors), nullptr);
    EXPECT_EQ(PrimaryMonitor(monitors)->deviceName, L"\\\\.\\DISPLAY1");

    const std::vector<MonitorWorkArea> noPrimary{
        MonitorWorkArea{L"\\\\.\\DISPLAY1", IntRect{0, 0, 100, 100}, 96, false},
        MonitorWorkArea{L"\\\\.\\DISPLAY2", IntRect{200, 0, 300, 100}, 96, false},
    };
    EXPECT_EQ(PrimaryMonitor(noPrimary)->deviceName, L"\\\\.\\DISPLAY1");
    EXPECT_EQ(PrimaryMonitor({}), nullptr);
}

TEST(OverlayPlacement, NormalizeDpiRejectsAbsurdValues)
{
    EXPECT_EQ(NormalizeDpi(96), 96u);
    EXPECT_EQ(NormalizeDpi(144), 144u);
    EXPECT_EQ(NormalizeDpi(0), 96u);
    EXPECT_EQ(NormalizeDpi(1000), 96u);
}

TEST(OverlayPlacement, DpiScaleIsRatioOverNinetySix)
{
    EXPECT_FLOAT_EQ(DpiScale(96), 1.0f);
    EXPECT_FLOAT_EQ(DpiScale(144), 1.5f);
    EXPECT_FLOAT_EQ(DpiScale(192), 2.0f);
    // Invalid DPI collapses to the 100% scale rather than dividing by zero.
    EXPECT_FLOAT_EQ(DpiScale(0), 1.0f);
}

TEST(OverlayPlacement, DipPixelConversionRoundTrips)
{
    EXPECT_EQ(DipToPixels(96.0f, 96), 96);
    EXPECT_EQ(DipToPixels(96.0f, 192), 192);
    EXPECT_EQ(DipToPixels(100.0f, 144), 150);
    EXPECT_FLOAT_EQ(PixelsToDip(192, 192), 96.0f);
    EXPECT_FLOAT_EQ(PixelsToDip(96, 96), 96.0f);
    EXPECT_EQ(DipToPixels(PixelsToDip(137, 144), 144), 137);
}

TEST(OverlayPlacement, ScaleRectForDpiPreservesDipGeometry)
{
    EXPECT_EQ(ScaleRectForDpi(IntRect{0, 0, 100, 100}, 96, 192), IntRect(0, 0, 200, 200));
    EXPECT_EQ(ScaleRectForDpi(IntRect{10, 20, 110, 120}, 192, 96), IntRect(5, 10, 55, 60));
    EXPECT_EQ(ScaleRectForDpi(IntRect{10, 20, 110, 120}, 96, 144), IntRect(15, 30, 165, 180));
    // Matching DPIs are a no-op.
    EXPECT_EQ(ScaleRectForDpi(IntRect{10, 20, 110, 120}, 144, 144), IntRect(10, 20, 110, 120));
}
} // namespace