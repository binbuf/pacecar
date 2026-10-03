#include "WidgetScene.h"

#include <algorithm>
#include <cmath>

#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
namespace
{
constexpr float kCornerRadius = 6.0f;

// Placeholder values until T12 binds the aggregator. Labels and families are real so the layout and
// arrangement can be checked by eye.
struct DemoTile
{
    const wchar_t* label;
    const wchar_t* primary;
    const wchar_t* secondary;
    const wchar_t* tertiary;
    double gauge;
};

const DemoTile& DemoFor(TileId id) noexcept
{
    static constexpr DemoTile kCpu{L"CPU", L"42%", L"3.80 GHz", L"55\x00B0"
                                                                    L"C",
                                   0.42};
    static constexpr DemoTile kRam{L"RAM", L"63%", L"10.1/16 GB", L"42\x00B0"
                                                                  L"C",
                                   0.63};
    static constexpr DemoTile kGpu{L"GPU", L"28%", L"71\x00B0"
                                                      L"C",
                                   L"4.2/8 GB", 0.28};
    static constexpr DemoTile kNetwork{L"Network", L"1.4 MiB/s",
                                       L"\x2191"
                                       L" 200 KiB  \x2193"
                                       L" 1.2 MiB",
                                       L"", 0.35};
    static constexpr DemoTile kDisk{L"Disk", L"45 MiB/s", L"R: 30  W: 15", L"38\x00B0"
                                                                          L"C",
                                    0.55};
    static constexpr DemoTile kPing{L"Ping", L"12 ms", L"", L"", 0.12};
    static constexpr DemoTile kFans{L"Fans", L"1200 RPM", L"", L"", 0.4};
    static constexpr DemoTile kMainboard{L"Board", L"38\x00B0"
                                                  L"C",
                                         L"", L"", 0.38};
    switch (id)
    {
    case TileId::Cpu:
        return kCpu;
    case TileId::Ram:
        return kRam;
    case TileId::Gpu:
        return kGpu;
    case TileId::Network:
        return kNetwork;
    case TileId::Disk:
        return kDisk;
    case TileId::Ping:
        return kPing;
    case TileId::Fans:
        return kFans;
    case TileId::Mainboard:
    default:
        return kMainboard;
    }
}

float DemoWave(std::size_t index) noexcept
{
    const float t = static_cast<float>(index) / static_cast<float>(kSparklineCapacity);
    return 50.0f + 35.0f * std::sin(t * 6.2831853f);
}
} // namespace

void WidgetScene::EnsureInitialized(ID2D1RenderTarget* target)
{
    if (initialized_ || target == nullptr)
    {
        return;
    }
    if (FAILED(text_.Initialize()) || !text_.Ready())
    {
        return;
    }

    TextStyle headerTitle{};
    headerTitle.family = L"Segoe UI";
    headerTitle.sizeDip = 10.0f;
    headerTitle.weight = 700;
    headerTitle.align = TextAlign::Leading;
    styles_.headerTitle = text_.RegisterFormat(headerTitle);

    TextStyle headerStatus = headerTitle;
    headerStatus.sizeDip = 9.0f;
    headerStatus.weight = 400;
    headerStatus.align = TextAlign::Trailing;
    styles_.headerStatus = text_.RegisterFormat(headerStatus);

    TextStyle label{};
    label.family = L"Segoe UI";
    label.sizeDip = 9.0f;
    label.weight = 400;
    styles_.label = text_.RegisterFormat(label);

    TextStyle primary{};
    primary.family = L"Consolas";
    primary.sizeDip = 18.0f;
    primary.weight = 700;
    primary.tabular = true;
    primary.align = TextAlign::Center;
    styles_.primary = text_.RegisterFormat(primary);

    TextStyle secondary{};
    secondary.family = L"Consolas";
    secondary.sizeDip = 9.0f;
    secondary.weight = 400;
    secondary.tabular = true;
    styles_.secondary = text_.RegisterFormat(secondary);

    for (std::size_t i = 0; i < demoHistory_.size(); ++i)
    {
        demoHistory_[i] = DemoWave(i);
    }
    initialized_ = true;
}

void WidgetScene::Draw(ID2D1RenderTarget* target, const ResolvedTheme& theme)
{
    if (target == nullptr)
    {
        return;
    }
    const D2D1_SIZE_F size = target->GetSize();
    if (size.width <= 2.0f || size.height <= 2.0f)
    {
        return;
    }
    EnsureInitialized(target);
    if (!initialized_)
    {
        return;
    }

    const RectF bounds{0.0f, 0.0f, size.width, size.height};
    panel_.Draw(target, bounds, kCornerRadius, theme.palette.panelBackground,
                theme.palette.panelBorder);

    const RectF headerRect{settings_.panelPadding, settings_.panelPadding,
                           size.width - settings_.panelPadding,
                           settings_.panelPadding + settings_.headerHeight};
    header_.Draw(target, text_, styles_, headerRect, L"PACECAR", L"Live", theme);

    const LayoutResult layout = ComputeLayout(size.width, size.height, settings_);
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        const TilePlacement& placement = layout.tiles[i];
        const DemoTile& demo = DemoFor(placement.id);

        TileContent content{};
        content.family = placement.family;
        content.label = demo.label;
        content.primary = demo.primary;
        content.secondary = demo.secondary;
        content.tertiary = demo.tertiary;
        content.gaugeFraction = demo.gauge;
        content.visualizationIsSparkline =
            placement.visualization == pacecar::Visualization::Sparklines;
        content.sparkSamples = std::span<const float>(demoHistory_);
        content.sparkMin = 0.0f;
        content.sparkMax = 100.0f;

        tiles_[i].Draw(target, text_, styles_, placement.bounds, content, placement.fields, theme);
    }
}

void WidgetScene::Trim()
{
    text_.Trim();
}
} // namespace pacecar::overlay