#include <gtest/gtest.h>

#include "pacecar/config/Config.h"
#include "pacecar/overlay/Theme.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace
{
using pacecar::overlay::AccentFor;
using pacecar::overlay::ApplyOpacity;
using pacecar::overlay::ColorF;
using pacecar::overlay::DarkPalette;
using pacecar::overlay::HighContrastPalette;
using pacecar::overlay::LightPalette;
using pacecar::overlay::MetricFamily;
using pacecar::overlay::ResolveTheme;
using pacecar::overlay::ThemeInputs;

TEST(Theme, DarkPaletteHasDistinctFamilyAccents)
{
    const auto palette = DarkPalette();
    EXPECT_NE(palette.cpu, palette.ram);
    EXPECT_NE(palette.ram, palette.gpu);
    EXPECT_NE(palette.gpu, palette.network);
    EXPECT_NE(palette.network, palette.disk);
    EXPECT_FLOAT_EQ(AccentFor(palette, MetricFamily::Cpu).r, palette.cpu.r);
    EXPECT_FLOAT_EQ(AccentFor(palette, MetricFamily::Disk).b, palette.disk.b);
}

TEST(Theme, LightThemeResolvesToLightPalette)
{
    ThemeInputs inputs{};
    inputs.theme = pacecar::Theme::Light;
    inputs.opacity = 0.8;
    const auto resolved = ResolveTheme(inputs);
    EXPECT_FALSE(resolved.highContrast);
    // Light chrome: near-white panel and dark text, distinct from the dark palette.
    EXPECT_GT(resolved.palette.panelBackground.r, 0.5f);
    EXPECT_LT(resolved.palette.text.r, 0.5f);
    EXPECT_NE(resolved.palette.cpu, DarkPalette().cpu);
    EXPECT_FLOAT_EQ(resolved.palette.panelBackground.a, 0.8f);
    // Family accents stay distinguishable.
    EXPECT_NE(resolved.palette.cpu, resolved.palette.ram);
    EXPECT_NE(resolved.palette.ram, resolved.palette.gpu);
}

TEST(Theme, OpacityScalesAlphaOnly)
{
    const ColorF base{0.2f, 0.4f, 0.6f, 1.0f};
    const ColorF dimmed = ApplyOpacity(base, 0.25);
    EXPECT_FLOAT_EQ(dimmed.r, base.r);
    EXPECT_FLOAT_EQ(dimmed.g, base.g);
    EXPECT_FLOAT_EQ(dimmed.b, base.b);
    EXPECT_FLOAT_EQ(dimmed.a, 0.25f);
    EXPECT_FLOAT_EQ(ApplyOpacity(base, -1.0).a, 0.0f);
    EXPECT_FLOAT_EQ(ApplyOpacity(base, 5.0).a, 1.0f);
}

TEST(Theme, ConfigOpacityReachesPanelBackground)
{
    ThemeInputs inputs{};
    inputs.theme = pacecar::Theme::Dark;
    inputs.opacity = 0.4;
    const auto resolved = ResolveTheme(inputs);
    EXPECT_FLOAT_EQ(resolved.palette.panelBackground.a, 0.4f);
    EXPECT_FALSE(resolved.highContrast);
}

TEST(Theme, HighContrastReplacesAccentsAndRequiresNonColorCue)
{
    ThemeInputs inputs{};
    inputs.highContrastActive = true;
    inputs.opacity = 0.3;
    const auto resolved = ResolveTheme(inputs);

    EXPECT_TRUE(resolved.highContrast);
    EXPECT_TRUE(resolved.requireNonColorCue);
    // Every accent is the same accessible foreground; color no longer distinguishes families.
    EXPECT_EQ(resolved.palette.cpu, resolved.palette.gpu);
    EXPECT_EQ(resolved.palette.network, resolved.palette.ram);
    // Accessibility panel stays opaque regardless of config opacity.
    EXPECT_FLOAT_EQ(resolved.palette.panelBackground.a, 1.0f);
}

TEST(Theme, HighContrastThemeEnumAlsoTriggersSafePalette)
{
    ThemeInputs inputs{};
    inputs.theme = pacecar::Theme::HighContrast;
    const auto resolved = ResolveTheme(inputs);
    EXPECT_TRUE(resolved.highContrast);
    EXPECT_TRUE(resolved.requireNonColorCue);
}

TEST(Theme, HighContrastLightUsesBlackForeground)
{
    const auto palette = HighContrastPalette(/*lightBackground=*/true);
    EXPECT_FLOAT_EQ(palette.text.r, 0.0f);
    EXPECT_FLOAT_EQ(palette.panelBackground.r, 1.0f);
}
} // namespace