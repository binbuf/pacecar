#include "WidgetScene.h"

#include <algorithm>
#include <cmath>

#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
namespace
{
constexpr float kOuterPadding = 8.0f;
constexpr float kHeaderHeight = 18.0f;
constexpr float kHeaderBottomGap = 6.0f;
constexpr float kTileGap = 6.0f;
constexpr float kCornerRadius = 6.0f;
constexpr int kColumns = 3;
constexpr int kRows = 2;

struct DemoTile
{
    MetricFamily family;
    const wchar_t* label;
    const wchar_t* primary;
    const wchar_t* secondary;
    const wchar_t* tertiary;
    double gauge;
};

constexpr DemoTile kDemoTiles[kColumns * kRows] = {
    {MetricFamily::Cpu, L"CPU", L"42%", L"3.80 GHz", L"55.0\x00B0"
                                                       L"C",
     0.42},
    {MetricFamily::Ram, L"RAM", L"63%", L"10.1/16 GB", L"42.0\x00B0"
                                                         L"C",
     0.63},
    {MetricFamily::Gpu, L"GPU", L"28%", L"71.0\x00B0"
                                        L"C",
     L"4.2/8 GB", 0.28},
    {MetricFamily::Network, L"Network", L"1.4 MiB/s", L"\x2191"
                                                      L" 200 KiB  \x2193"
                                                      L" 1.2 MiB",
     L"", 0.35},
    {MetricFamily::Disk, L"Disk", L"45 MiB/s", L"R: 30  W: 15", L"38.0\x00B0"
                                                                 L"C",
     0.55},
    {MetricFamily::Ping, L"Ping", L"12 ms", L"", L"", 0.12},
};

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

    const RectF headerRect{kOuterPadding, kOuterPadding, size.width - kOuterPadding,
                           kOuterPadding + kHeaderHeight};
    header_.Draw(target, text_, styles_, headerRect, L"PACECAR", L"Live", theme);

    const float gridTop = headerRect.bottom + kHeaderBottomGap;
    const float gridLeft = kOuterPadding;
    const float gridRight = size.width - kOuterPadding;
    const float gridBottom = size.height - kOuterPadding;
    const float gridWidth = gridRight - gridLeft;
    const float gridHeight = gridBottom - gridTop;
    if (gridWidth <= kTileGap || gridHeight <= kTileGap)
    {
        return;
    }
    const float tileWidth =
        (gridWidth - kTileGap * static_cast<float>(kColumns - 1)) / static_cast<float>(kColumns);
    const float tileHeight =
        (gridHeight - kTileGap * static_cast<float>(kRows - 1)) / static_cast<float>(kRows);

    TileFieldVisibility visibility{};
    visibility.miniSparkline = true;

    for (int i = 0; i < kColumns * kRows; ++i)
    {
        const int column = i % kColumns;
        const int row = i / kColumns;
        const RectF tileRect{gridLeft + static_cast<float>(column) * (tileWidth + kTileGap),
                             gridTop + static_cast<float>(row) * (tileHeight + kTileGap),
                             gridLeft + static_cast<float>(column) * (tileWidth + kTileGap) +
                                 tileWidth,
                             gridTop + static_cast<float>(row) * (tileHeight + kTileGap) +
                                 tileHeight};

        const DemoTile& demo = kDemoTiles[i];
        TileContent content{};
        content.family = demo.family;
        content.label = demo.label;
        content.primary = demo.primary;
        content.secondary = demo.secondary;
        content.tertiary = demo.tertiary;
        content.gaugeFraction = demo.gauge;
        content.visualizationIsSparkline = false;
        content.sparkSamples = std::span<const float>(demoHistory_);
        content.sparkMin = 0.0f;
        content.sparkMax = 100.0f;

        tiles_[static_cast<std::size_t>(i)].Draw(target, text_, styles_, tileRect, content,
                                                 visibility, theme);
    }
}

void WidgetScene::Trim()
{
    text_.Trim();
}
} // namespace pacecar::overlay