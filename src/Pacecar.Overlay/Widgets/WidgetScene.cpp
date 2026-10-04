#include "WidgetScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>

#include "pacecar/overlay/WidgetLayout.h"
#include "pacecar/util/Format.h"

namespace pacecar::overlay
{
namespace
{
constexpr float kCornerRadius = 6.0f;

// Neutral placeholders shown before the first sample (cold start) or when a metric is unavailable.
// Labels and families are real so the layout and arrangement can be checked by eye.
struct DemoTile
{
    const wchar_t* primary;
    const wchar_t* secondary;
    const wchar_t* tertiary;
    double gauge;
};

const DemoTile& DemoFor(TileId id) noexcept
{
    static constexpr DemoTile kCpu{L"--%", L"-- GHz", L"--\x00B0"
                                             L"C",
                                   0.0};
    static constexpr DemoTile kRam{L"--%", L"-- / -- GB", L"", 0.0};
    static constexpr DemoTile kGpu{L"--%", L"--\x00B0"
                                            L"C",
                                   L"-- / -- GB", 0.0};
    static constexpr DemoTile kNetwork{L"-- /s", L"\x2191 --  \x2193 --", L"", 0.0};
    static constexpr DemoTile kDisk{L"-- /s", L"R: --  W: --", L"--\x00B0"
                                                               L"C",
                                    0.0};
    static constexpr DemoTile kPing{L"-- ms", L"", L"", 0.0};
    static constexpr DemoTile kFans{L"-- RPM", L"", L"", 0.0};
    static constexpr DemoTile kMainboard{L"--\x00B0"
                                         L"C",
                                         L"", L"", 0.0};
    static constexpr DemoTile kFps{L"-- FPS", L"-- ms", L"", 0.0};
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
        return kMainboard;
    case TileId::Fps:
    default:
        return kFps;
    }
}

const wchar_t* LabelFor(TileId id) noexcept
{
    switch (id)
    {
    case TileId::Cpu:
        return L"CPU";
    case TileId::Ram:
        return L"RAM";
    case TileId::Gpu:
        return L"GPU";
    case TileId::Network:
        return L"Network";
    case TileId::Disk:
        return L"Disk";
    case TileId::Ping:
        return L"Ping";
    case TileId::Fans:
        return L"Fans";
    case TileId::Mainboard:
        return L"Board";
    case TileId::Fps:
    default:
        return L"FPS";
    }
}

float DemoWave(std::size_t index) noexcept
{
    const float t = static_cast<float>(index) / static_cast<float>(kSparklineCapacity);
    return 50.0f + 35.0f * std::sin(t * 6.2831853f);
}

// Sparkline autoscale for a rate series: 0 at the bottom, the observed maximum (with a small head
// room floor) at the top. Percentage series keep their fixed 0..100 range.
float SeriesMaximum(std::span<const float> samples) noexcept
{
    float maximum = 0.0f;
    for (const float value : samples)
    {
        maximum = std::max(maximum, value);
    }
    return maximum > 0.0f ? maximum : 1.0f;
}

const pacecar::metrics::MetricSparkline* SparklineFor(const pacecar::metrics::DisplayFrame& frame,
                                                      TileId id) noexcept
{
    switch (id)
    {
    case TileId::Cpu:
        return &frame.cpu;
    case TileId::Ram:
        return &frame.memory;
    case TileId::Gpu:
        return &frame.gpu;
    case TileId::Network:
        return &frame.network;
    case TileId::Disk:
        return &frame.disk;
    case TileId::Ping:
        return &frame.ping;
    case TileId::Fps:
        return &frame.fps;
    default:
        return nullptr;
    }
}

bool Unavailable(const pacecar::metrics::MetricStatus& status) noexcept
{
    return !status.available;
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
    const bool haveFrame = frame_ && frame_->snapshot;
    // The FPS tile is drawn only while a capture is actually producing frames, and disappears
    // again as soon as the capture stops (T17). Config cannot leave it visible otherwise.
    const bool captureActive =
        haveFrame && !Unavailable(frame_->snapshot->frame.status) &&
        frame_->snapshot->frame.fps > 0.0;
    SetFrameCaptureTileVisible(settings_, captureActive);

    const wchar_t* status = L"Live";
    if (captureActive)
    {
        status = L"FPS capture";
    }
    else if (haveFrame && !frame_->snapshot->deepSensors.available)
    {
        status = L"Deep sensors off";
    }
    header_.Draw(target, text_, styles_, headerRect, L"PACECAR", status, theme);

    const LayoutResult layout = ComputeLayout(size.width, size.height, settings_);
    const bool live = frame_ && frame_->snapshot;
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        if (live)
        {
            DrawLiveTile(target, layout.tiles[i], i, theme);
        }
        else
        {
            DrawDemoTile(target, layout.tiles[i], i, theme);
        }
    }
}

void WidgetScene::DrawDemoTile(ID2D1RenderTarget* target, const TilePlacement& placement,
                               std::size_t index, const ResolvedTheme& theme)
{
    const DemoTile& demo = DemoFor(placement.id);

    TileContent content{};
    content.family = placement.family;
    content.label = LabelFor(placement.id);
    content.primary = demo.primary;
    content.secondary = demo.secondary;
    content.tertiary = demo.tertiary;
    content.gaugeFraction = demo.gauge;
    content.visualizationIsSparkline = placement.visualization == pacecar::Visualization::Sparklines;
    content.sparkSamples = std::span<const float>(demoHistory_);
    content.sparkMin = 0.0f;
    content.sparkMax = 100.0f;

    tiles_[index].Draw(target, text_, styles_, placement.bounds, content, placement.fields, theme);
}

void WidgetScene::DrawLiveTile(ID2D1RenderTarget* target, const TilePlacement& placement,
                               std::size_t index, const ResolvedTheme& theme)
{
    const pacecar::metrics::MetricsSnapshot& snapshot = *frame_->snapshot;
    auto& primary = primaryBuffers_[index];
    auto& secondary = secondaryBuffers_[index];
    auto& tertiary = tertiaryBuffers_[index];
    std::wstring_view primaryView;
    std::wstring_view secondaryView;
    std::wstring_view tertiaryView;
    double gauge = 0.0;
    bool gaugeValid = true;

    const auto percent = [](const pacecar::metrics::MetricStatus& status, double value) {
        return Unavailable(status) ? 0.0 : std::clamp(value, 0.0, 100.0);
    };

    switch (placement.id)
    {
    case TileId::Cpu:
    {
        const double util = percent(snapshot.cpu.status, snapshot.cpu.totalUtilizationPercent);
        primaryView = pacecar::FormatPercent(primary, util);
        if (snapshot.cpu.totalFrequencyMhz > 0.0)
        {
            secondaryView = pacecar::FormatFrequency(secondary, snapshot.cpu.totalFrequencyMhz * 1.0e6);
        }
        if (!Unavailable(snapshot.cpu.temperatureStatus) && snapshot.cpu.packageTemperatureC > 0.0)
        {
            tertiaryView = pacecar::FormatTemperature(tertiary, snapshot.cpu.packageTemperatureC);
        }
        gauge = util / 100.0;
        break;
    }
    case TileId::Ram:
    {
        primaryView = pacecar::FormatPercent(primary, percent(snapshot.memory.status,
                                                               snapshot.memory.usedPercent));
        wchar_t usedW[32]{};
        wchar_t totalW[32]{};
        static_cast<void>(pacecar::FormatBytes(usedW, snapshot.memory.usedBytes));
        static_cast<void>(pacecar::FormatBytes(totalW, snapshot.memory.totalBytes));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"%s / %s", usedW, totalW);
        secondaryView = std::wstring_view(secondary.data());
        gauge = percent(snapshot.memory.status, snapshot.memory.usedPercent) / 100.0;
        break;
    }
    case TileId::Gpu:
    {
        const double util = percent(snapshot.gpu.status, snapshot.gpu.utilizationPercent);
        primaryView = pacecar::FormatPercent(primary, util);
        if (!Unavailable(snapshot.gpu.temperatureStatus) && snapshot.gpu.temperatureC > 0.0)
        {
            secondaryView = pacecar::FormatTemperature(secondary, snapshot.gpu.temperatureC);
        }
        if (snapshot.gpu.vramTotalBytes > 0)
        {
            wchar_t usedW[32]{};
            wchar_t totalW[32]{};
            static_cast<void>(pacecar::FormatBytes(usedW, snapshot.gpu.vramUsedBytes));
            static_cast<void>(pacecar::FormatBytes(totalW, snapshot.gpu.vramTotalBytes));
            _snwprintf_s(tertiary.data(), tertiary.size(), _TRUNCATE, L"%s / %s", usedW, totalW);
            tertiaryView = std::wstring_view(tertiary.data());
        }
        gauge = util / 100.0;
        break;
    }
    case TileId::Network:
    {
        const double down = Unavailable(snapshot.network.status)
                                ? 0.0
                                : snapshot.network.downBytesPerSecond;
        primaryView = pacecar::FormatRate(primary, down);
        wchar_t upW[32]{};
        wchar_t downW[32]{};
        static_cast<void>(pacecar::FormatRate(upW, snapshot.network.upBytesPerSecond));
        static_cast<void>(pacecar::FormatRate(downW, down));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"\x2191 %s  \x2193 %s", upW,
                     downW);
        secondaryView = std::wstring_view(secondary.data());
        gaugeValid = false;
        break;
    }
    case TileId::Disk:
    {
        const double read =
            Unavailable(snapshot.disk.status) ? 0.0 : snapshot.disk.readBytesPerSecond;
        const double write =
            Unavailable(snapshot.disk.status) ? 0.0 : snapshot.disk.writeBytesPerSecond;
        primaryView = pacecar::FormatRate(primary, read + write);
        wchar_t readW[32]{};
        wchar_t writeW[32]{};
        static_cast<void>(pacecar::FormatRate(readW, read));
        static_cast<void>(pacecar::FormatRate(writeW, write));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"R: %s  W: %s", readW,
                     writeW);
        secondaryView = std::wstring_view(secondary.data());
        if (!Unavailable(snapshot.disk.temperatureStatus) && snapshot.disk.temperatureC > 0.0)
        {
            tertiaryView = pacecar::FormatTemperature(tertiary, snapshot.disk.temperatureC);
        }
        gaugeValid = false;
        break;
    }
    case TileId::Ping:
    {
        if (Unavailable(snapshot.ping.status))
        {
            primaryView = L"-- ms";
        }
        else
        {
            _snwprintf_s(primary.data(), primary.size(), _TRUNCATE, L"%.0f ms",
                         snapshot.ping.rttMs);
            primaryView = std::wstring_view(primary.data());
        }
        gaugeValid = false;
        break;
    }
    case TileId::Fps:
    {
        if (!Unavailable(snapshot.frame.status) && snapshot.frame.fps > 0.0)
        {
            _snwprintf_s(primary.data(), primary.size(), _TRUNCATE, L"%.0f FPS",
                         snapshot.frame.fps);
            primaryView = std::wstring_view(primary.data());
            _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"%.1f ms",
                         snapshot.frame.frameTimeMs);
            secondaryView = std::wstring_view(secondary.data());
            if (snapshot.frame.gpuTimeMs > 0.0)
            {
                _snwprintf_s(tertiary.data(), tertiary.size(), _TRUNCATE, L"GPU %.1f ms",
                             snapshot.frame.gpuTimeMs);
                tertiaryView = std::wstring_view(tertiary.data());
            }
            gauge = std::clamp(snapshot.frame.fps / 240.0, 0.0, 1.0);
        }
        else
        {
            primaryView = L"-- FPS";
            gaugeValid = false;
        }
        break;
    }
    case TileId::Fans:
    {
        if (!Unavailable(snapshot.fan.status) && snapshot.fan.highestRpm > 0)
        {
            _snwprintf_s(primary.data(), primary.size(), _TRUNCATE, L"%d RPM",
                         snapshot.fan.highestRpm);
            primaryView = std::wstring_view(primary.data());
        }
        else
        {
            primaryView = L"-- RPM";
        }
        gaugeValid = false;
        break;
    }
    case TileId::Mainboard:
    default:
    {
        if (!Unavailable(snapshot.board.status) && snapshot.board.mainboardTemperatureC > 0.0)
        {
            primaryView = pacecar::FormatTemperature(primary, snapshot.board.mainboardTemperatureC);
        }
        gaugeValid = false;
        break;
    }
    }

    const pacecar::metrics::MetricSparkline* spark = SparklineFor(*frame_, placement.id);
    std::span<const float> sparkSamples;
    float sparkMax = 100.0f;
    if (spark != nullptr && spark->count > 0)
    {
        sparkSamples = std::span<const float>(spark->samples, spark->count);
        if (placement.id == TileId::Network || placement.id == TileId::Disk ||
            placement.id == TileId::Ping || placement.id == TileId::Fps)
        {
            sparkMax = SeriesMaximum(sparkSamples);
        }
    }
    else
    {
        sparkSamples = std::span<const float>(demoHistory_);
    }

    TileContent content{};
    content.family = placement.family;
    content.label = LabelFor(placement.id);
    content.primary = primaryView;
    content.secondary = secondaryView;
    content.tertiary = tertiaryView;
    content.gaugeFraction = gaugeValid ? gauge : 0.0;
    content.visualizationIsSparkline = placement.visualization == pacecar::Visualization::Sparklines;
    content.sparkSamples = sparkSamples;
    content.sparkMin = 0.0f;
    content.sparkMax = sparkMax;

    tiles_[index].Draw(target, text_, styles_, placement.bounds, content, placement.fields, theme);
}

void WidgetScene::Trim()
{
    text_.Trim();
}
} // namespace pacecar::overlay