#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cwchar>

#include "allocation_probe.h"
#include "pacecar/config/Config.h"
#include "pacecar/overlay/Layout.h"
#include "pacecar/overlay/OverlayCommands.h"

namespace
{
using pacecar::LayoutPreset;
using pacecar::ViewMode;
using pacecar::overlay::ComputeLayout;
using pacecar::overlay::DefaultLayoutSettings;
using pacecar::overlay::IsOverlayCommand;
using pacecar::overlay::kContextMenuCommands;
using pacecar::overlay::kMaxTiles;
using pacecar::overlay::LayoutResult;
using pacecar::overlay::LayoutSettings;
using pacecar::overlay::LayoutSettingsFromConfig;
using pacecar::overlay::MeasureLayout;
using pacecar::overlay::OverlayCommand;
using pacecar::overlay::RectF;
using pacecar::overlay::TileId;
using pacecar::overlay::TileSize;

constexpr float kWidth = 360.0f;
constexpr float kHeight = 220.0f;

LayoutSettings MakeSettings(LayoutPreset preset, std::size_t visibleCount = 6)
{
    LayoutSettings settings = DefaultLayoutSettings();
    settings.preset = preset;
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        settings.tiles[i].visible = i < visibleCount;
    }
    return settings;
}

bool Overlaps(const RectF& a, const RectF& b)
{
    constexpr float kEpsilon = 1e-3f;
    return a.left < b.right - kEpsilon && b.left < a.right - kEpsilon &&
           a.top < b.bottom - kEpsilon && b.top < a.bottom - kEpsilon;
}

void ExpectNoOverlapAndContained(const LayoutResult& result, float width, float height)
{
    for (std::size_t i = 0; i < result.count; ++i)
    {
        const RectF& bounds = result.tiles[i].bounds;
        EXPECT_GT(bounds.Width(), 0.0f) << "tile " << i;
        EXPECT_GT(bounds.Height(), 0.0f) << "tile " << i;
        EXPECT_GE(bounds.left, -1e-3f) << "tile " << i;
        EXPECT_GE(bounds.top, -1e-3f) << "tile " << i;
        EXPECT_LE(bounds.right, width + 1e-3f) << "tile " << i;
        EXPECT_LE(bounds.bottom, height + 1e-3f) << "tile " << i;
        for (std::size_t j = i + 1; j < result.count; ++j)
        {
            EXPECT_FALSE(Overlaps(bounds, result.tiles[j].bounds))
                << "tiles " << i << " and " << j << " overlap";
        }
    }
}

TEST(Layout, Compact3x3PlacesTopThenBottomRow)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::Compact3x3, 6);
    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);

    ASSERT_EQ(result.count, 6u);
    EXPECT_EQ(result.tiles[0].id, TileId::Cpu);
    EXPECT_EQ(result.tiles[1].id, TileId::Ram);
    EXPECT_EQ(result.tiles[2].id, TileId::Gpu);
    EXPECT_EQ(result.tiles[3].id, TileId::Network);
    EXPECT_EQ(result.tiles[4].id, TileId::Disk);
    EXPECT_EQ(result.tiles[5].id, TileId::Ping);

    // Top row shares a top edge; the bottom row starts lower.
    EXPECT_NEAR(result.tiles[0].bounds.top, result.tiles[2].bounds.top, 1e-3f);
    EXPECT_LT(result.tiles[0].bounds.top, result.tiles[3].bounds.top);
    // Column 0 aligns across rows.
    EXPECT_NEAR(result.tiles[0].bounds.left, result.tiles[3].bounds.left, 1e-3f);
    EXPECT_LT(result.tiles[0].bounds.left, result.tiles[1].bounds.left);
    EXPECT_LT(result.tiles[1].bounds.left, result.tiles[2].bounds.left);

    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, Vertical1x6StacksOneColumn)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::Vertical1x6, 6);
    const LayoutResult result = ComputeLayout(180.0f, 420.0f, settings);

    ASSERT_EQ(result.count, 6u);
    for (std::size_t i = 0; i < result.count; ++i)
    {
        EXPECT_NEAR(result.tiles[i].bounds.left, result.tiles[0].bounds.left, 1e-3f);
        EXPECT_NEAR(result.tiles[i].bounds.Width(), result.tiles[0].bounds.Width(), 1e-3f);
        if (i > 0)
        {
            EXPECT_LT(result.tiles[i - 1].bounds.top, result.tiles[i].bounds.top);
        }
    }
    ExpectNoOverlapAndContained(result, 180.0f, 420.0f);
}

TEST(Layout, AutoFitMeasureMatchesPackedContent)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::AutoFit, 6);
    const TileSize measured = MeasureLayout(settings);
    EXPECT_GT(measured.width, 0.0f);
    EXPECT_GT(measured.height, 0.0f);

    const LayoutResult result = ComputeLayout(measured.width, measured.height, settings);
    ASSERT_EQ(result.count, 6u);
    EXPECT_NEAR(result.contentSize.width, measured.width, 1e-3f);
    EXPECT_NEAR(result.contentSize.height, measured.height, 1e-3f);

    // The content bounds never exceed the exact measured size.
    for (std::size_t i = 0; i < result.count; ++i)
    {
        EXPECT_LE(result.tiles[i].bounds.right, measured.width + 1e-3f);
        EXPECT_LE(result.tiles[i].bounds.bottom, measured.height + 1e-3f);
    }
    ExpectNoOverlapAndContained(result, measured.width, measured.height);
}

TEST(Layout, AutoFitGrowsWithVisibleFields)
{
    LayoutSettings minimal = MakeSettings(LayoutPreset::AutoFit, 6);
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        minimal.tiles[i].fields.label = false;
        minimal.tiles[i].fields.secondary = false;
        minimal.tiles[i].fields.tertiary = false;
        minimal.tiles[i].fields.visualization = false;
        minimal.tiles[i].fields.miniSparkline = false;
    }

    LayoutSettings full = MakeSettings(LayoutPreset::AutoFit, 6);
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        full.tiles[i].fields.miniSparkline = true;
    }

    EXPECT_GT(MeasureLayout(full).height, MeasureLayout(minimal).height);
}

TEST(Layout, CustomGeometryIsHonored)
{
    LayoutSettings settings = MakeSettings(LayoutPreset::Custom, 6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        settings.tiles[i].customValid = true;
        settings.tiles[i].custom = RectF{static_cast<float>(i) * 50.0f, 0.0f,
                                         static_cast<float>(i) * 50.0f + 40.0f, 30.0f};
    }
    const LayoutResult result = ComputeLayout(400.0f, 200.0f, settings);

    ASSERT_EQ(result.count, 6u);
    EXPECT_NEAR(result.tiles[0].bounds.left, settings.panelPadding, 1e-3f);
    EXPECT_NEAR(result.tiles[1].bounds.left, settings.panelPadding + 50.0f, 1e-3f);
    EXPECT_NEAR(result.tiles[0].bounds.Width(), 40.0f, 1e-3f);
}

TEST(Layout, CustomFallsBackWhenGeometryMissing)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::Custom, 6);
    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
    ASSERT_EQ(result.count, 6u);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, AllTilesHiddenProducesNoPlacements)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::Compact3x3, 0);
    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
    EXPECT_EQ(result.count, 0u);
}

TEST(Layout, OnlyPrimaryFieldsStillProducesValidLayout)
{
    LayoutSettings settings = MakeSettings(LayoutPreset::Compact3x3, 6);
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        settings.tiles[i].fields = pacecar::overlay::TileFieldVisibility{};
        settings.tiles[i].fields.label = false;
        settings.tiles[i].fields.primary = true;
        settings.tiles[i].fields.secondary = false;
        settings.tiles[i].fields.tertiary = false;
        settings.tiles[i].fields.visualization = false;
        settings.tiles[i].fields.miniSparkline = false;
    }
    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);

    ASSERT_EQ(result.count, 6u);
    EXPECT_TRUE(result.tiles[0].fields.primary);
    EXPECT_FALSE(result.tiles[0].fields.visualization);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, HidingATileCollapsesTheGrid)
{
    const LayoutSettings full = MakeSettings(LayoutPreset::Compact3x3, 6);
    LayoutSettings reduced = full;
    reduced.tiles[static_cast<std::size_t>(TileId::Ping)].visible = false;

    const LayoutResult result = ComputeLayout(kWidth, kHeight, reduced);
    ASSERT_EQ(result.count, 5u);
    EXPECT_EQ(result.tiles[4].id, TileId::Disk);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, DegenerateSizeProducesContentSizeOnly)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::Compact3x3, 6);
    const LayoutResult result = ComputeLayout(0.0f, kHeight, settings);
    EXPECT_EQ(result.count, 0u);
    EXPECT_GT(result.contentSize.height, 0.0f);
}

TEST(Layout, ExplicitWindowSwitchChangesArrangement)
{
    const LayoutResult compact =
        ComputeLayout(kWidth, kHeight, MakeSettings(LayoutPreset::Compact3x3, 6));
    const LayoutResult vertical =
        ComputeLayout(kWidth, kHeight, MakeSettings(LayoutPreset::Vertical1x6, 6));
    ASSERT_EQ(compact.count, 6u);
    ASSERT_EQ(vertical.count, 6u);
    // Same window, different arrangement.
    EXPECT_GT(vertical.tiles[1].bounds.top, compact.tiles[1].bounds.top);
}

TEST(Layout, HotPathPerformsZeroHeapAllocations)
{
    const LayoutSettings settings = MakeSettings(LayoutPreset::AutoFit, 6);
    for (int i = 0; i < 4; ++i)
    {
        static_cast<void>(ComputeLayout(kWidth, kHeight, settings));
        static_cast<void>(MeasureLayout(settings));
    }

    pacecar::test::ResetAllocationCount();
    std::size_t sink = 0;
    for (int i = 0; i < 10000; ++i)
    {
        const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
        sink += result.count;
        sink += static_cast<std::size_t>(MeasureLayout(settings).height);
    }
    EXPECT_GT(sink, 0u);
    EXPECT_EQ(pacecar::test::AllocationCount(), 0u);
}

TEST(Layout, SettingsFromConfigMapsTogglesAndGeometry)
{
    pacecar::Config config = pacecar::Config::Defaults();
    config.general.layout = LayoutPreset::Vertical1x6;
    config.tiles.cpu.show_secondary = false;
    config.tiles.ram.visible = false;
    config.tiles.gpu.mini_sparklines = true;
    config.tiles.disk.show_visualization = false;
    config.layout.custom_tiles.push_back(
        pacecar::CustomTilePlacement{"cpu", 10.0, 20.0, 100.0, 40.0, true});

    const LayoutSettings settings = LayoutSettingsFromConfig(config);
    EXPECT_EQ(settings.preset, LayoutPreset::Vertical1x6);
    EXPECT_FALSE(settings.tiles[0].fields.secondary);
    EXPECT_FALSE(settings.tiles[1].visible);
    EXPECT_TRUE(settings.tiles[2].fields.miniSparkline);
    EXPECT_FALSE(settings.tiles[4].fields.visualization);
    EXPECT_TRUE(settings.tiles[0].customValid);
    EXPECT_NEAR(settings.tiles[0].custom.Width(), 100.0f, 1e-3f);
    EXPECT_EQ(settings.tiles[6].visible, false); // reserved family stays hidden
}

TEST(Layout, FpsOnlyViewShowsOnlyFpsWithoutVisualization)
{
    pacecar::Config config = pacecar::Config::Defaults();
    config.general.view = ViewMode::FpsOnly;
    const LayoutSettings settings = LayoutSettingsFromConfig(config);

    EXPECT_FALSE(settings.drawHeader);
    const pacecar::overlay::TileSettings& fps =
        settings.tiles[static_cast<std::size_t>(TileId::Fps)];
    EXPECT_TRUE(fps.visible);
    EXPECT_FALSE(fps.fields.visualization);
    EXPECT_FALSE(settings.tiles[static_cast<std::size_t>(TileId::Cpu)].visible);

    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
    ASSERT_EQ(result.count, 1u);
    EXPECT_EQ(result.tiles[0].id, TileId::Fps);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, SmallTextViewIsTextOnlyAndCompact)
{
    pacecar::Config config = pacecar::Config::Defaults();
    config.general.view = ViewMode::SmallText;
    const LayoutSettings settings = LayoutSettingsFromConfig(config);

    EXPECT_FALSE(settings.drawHeader);
    EXPECT_EQ(settings.preset, LayoutPreset::AutoFit);
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        EXPECT_FALSE(settings.tiles[i].fields.visualization) << "tile " << i;
        EXPECT_TRUE(settings.tiles[i].fields.primary) << "tile " << i;
        EXPECT_FALSE(settings.tiles[i].fields.tertiary) << "tile " << i;
    }

    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
    ASSERT_EQ(result.count, 6u);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, LargeVisualsViewForcesVisualizationAndKeepsHeader)
{
    pacecar::Config config = pacecar::Config::Defaults();
    config.general.view = ViewMode::LargeVisuals;
    config.tiles.cpu.show_visualization = false;
    const LayoutSettings settings = LayoutSettingsFromConfig(config);

    EXPECT_TRUE(settings.drawHeader);
    EXPECT_TRUE(settings.drawBackground);
    const pacecar::overlay::TileFieldVisibility& cpu =
        settings.tiles[static_cast<std::size_t>(TileId::Cpu)].fields;
    EXPECT_TRUE(cpu.visualization);
    EXPECT_GT(settings.tileMetrics.graphHeight, pacecar::overlay::TileLayoutMetrics{}.graphHeight);

    const LayoutResult result = ComputeLayout(kWidth, kHeight, settings);
    ExpectNoOverlapAndContained(result, kWidth, kHeight);
}

TEST(Layout, TransparentBackgroundHidesPanelAndHeader)
{
    pacecar::Config config = pacecar::Config::Defaults();
    config.general.transparent_background = true;
    const LayoutSettings settings = LayoutSettingsFromConfig(config);

    EXPECT_FALSE(settings.drawBackground);
    EXPECT_FALSE(settings.drawHeader);

    // Without the header the first tile starts higher than in the default (header) layout.
    const LayoutSettings withHeader = LayoutSettingsFromConfig(pacecar::Config::Defaults());
    const LayoutResult transparent = ComputeLayout(kWidth, kHeight, settings);
    const LayoutResult headed = ComputeLayout(kWidth, kHeight, withHeader);
    ASSERT_GT(transparent.count, 0u);
    ASSERT_GT(headed.count, 0u);
    EXPECT_LT(transparent.tiles[0].bounds.top, headed.tiles[0].bounds.top);
}

TEST(OverlayCommands, MenuExposesAllRequiredCommandsInOrder)
{
    const std::array<OverlayCommand, 9> expected{OverlayCommand::CycleView,
                                                 OverlayCommand::ToggleBackground,
                                                 OverlayCommand::Mode,
                                                 OverlayCommand::Settings,
                                                 OverlayCommand::History,
                                                 OverlayCommand::Specs,
                                                 OverlayCommand::ToggleFrameCapture,
                                                 OverlayCommand::Hide,
                                                 OverlayCommand::Exit};
    EXPECT_EQ(kContextMenuCommands, expected);
    for (const OverlayCommand command : kContextMenuCommands)
    {
        EXPECT_GT(std::wcslen(pacecar::overlay::CommandLabel(command)), 0u);
        EXPECT_TRUE(IsOverlayCommand(static_cast<unsigned>(command)));
    }
    EXPECT_FALSE(IsOverlayCommand(0));
    EXPECT_FALSE(IsOverlayCommand(99));
}
} // namespace