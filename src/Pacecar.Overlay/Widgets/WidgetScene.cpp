#include "WidgetScene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <span>
#include <string_view>

#include "pacecar/overlay/WidgetLayout.h"
#include "pacecar/util/Format.h"

namespace pacecar::overlay
{
namespace
{
constexpr float kCornerRadius = 6.0f;

// The StatRows view draws plain text with a soft drop shadow when the panel background is off, so
// the list stays legible over arbitrary desktop/game content (mirrors Tile.cpp).
constexpr float kStatShadowOffset = 1.0f;
const ColorF kStatShadowColor{0.0f, 0.0f, 0.0f, 0.7f};

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
    static constexpr DemoTile kCpu{L"--%", L"-- GHz",
                                   L"--\x00B0"
                                   L"C",
                                   0.0};
    static constexpr DemoTile kRam{L"--%", L"-- / -- GB", L"", 0.0};
    static constexpr DemoTile kGpu{L"--%",
                                   L"--\x00B0"
                                   L"C",
                                   L"-- / -- GB", 0.0};
    static constexpr DemoTile kNetwork{L"-- /s", L"\x2191 --  \x2193 --", L"", 0.0};
    static constexpr DemoTile kDisk{L"-- /s", L"R: --  W: --",
                                    L"--\x00B0"
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

// Shown in place of an FPS value so a stalled capture explains itself instead of a bare "--".
// Values mirror `pacecar::metrics::FrameCaptureState` (IpcProtocol.h).
const wchar_t* FrameUnavailableLabel(std::uint32_t captureState) noexcept
{
    switch (captureState)
    {
    case 1:
        return L"waiting..."; // capturing, no interval measured yet
    case 2:
        return L"ETW busy"; // another tool owns the session
    case 3:
        return L"need admin"; // helper not elevated
    case 4:
        return L"no ETW provider";
    case 5:
        return L"no target"; // no foreground process to capture
    default:
        return L"-- FPS";
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

// The StatRows header glyph for one stat, drawn from "Segoe MDL2 Assets" (code points in the font's
// private-use area). Kept as fixed-width glyphs so the value column lines up flush.
wchar_t IconFor(TileId id) noexcept
{
    switch (id)
    {
    case TileId::Cpu:
        return L'\xE950'; // Component
    case TileId::Ram:
        return L'\xE964'; // SmartcardVirtual
    case TileId::Gpu:
        return L'\xE7FC'; // Game
    case TileId::Network:
        return L'\xE968'; // Network
    case TileId::Disk:
        return L'\xEDA2'; // HardDrive
    case TileId::Ping:
        return L'\xE916'; // Stopwatch
    case TileId::Fans:
        return L'\xE72C'; // Refresh (spinning fan)
    case TileId::Mainboard:
        return L'\xE957'; // Sensor
    case TileId::Fps:
    default:
        return L'\xEC4A'; // SpeedHigh
    }
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

// Smoothed scale for a rate series: snap up to a new observed maximum, then decay slowly so a
// short spike does not permanently flatten the rest of the history. `scale` is persisted per tile.
float SmoothedSeriesMaximum(float& scale, std::span<const float> samples) noexcept
{
    const float observed = SeriesMaximum(samples);
    scale = observed >= scale ? observed : std::max(observed, scale * 0.92f);
    return std::max(scale, 1.0f);
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

// Appends `piece` to the null-terminated `buffer`, separated from any prior piece by two spaces.
void AppendStatPiece(wchar_t* buffer, std::size_t size, const wchar_t* piece) noexcept
{
    if (piece == nullptr || piece[0] == L'\0')
    {
        return;
    }
    if (buffer[0] != L'\0')
    {
        wcscat_s(buffer, size, L"  ");
    }
    wcscat_s(buffer, size, piece);
}

// Builds the single-line value shown for one stat in the StatRows view. Writes into `buffer` and
// returns it as a view; an unavailable metric yields "--". `buffer` must hold at least 128 wchars.
std::wstring_view FormatStatRow(TileId id, const pacecar::metrics::MetricsSnapshot& snapshot,
                                wchar_t* buffer, std::size_t size) noexcept
{
    buffer[0] = L'\0';
    wchar_t piece[48] = {};
    std::span<wchar_t> slot(piece);

    switch (id)
    {
    case TileId::Cpu: {
        if (Unavailable(snapshot.cpu.status))
        {
            break;
        }
        static_cast<void>(pacecar::FormatPercent(slot, std::clamp(snapshot.cpu.totalUtilizationPercent, 0.0, 100.0)));
        AppendStatPiece(buffer, size, piece);
        if (snapshot.cpu.totalFrequencyMhz > 0.0)
        {
            static_cast<void>(pacecar::FormatFrequency(slot, snapshot.cpu.totalFrequencyMhz * 1.0e6));
            AppendStatPiece(buffer, size, piece);
        }
        if (!Unavailable(snapshot.cpu.temperatureStatus) && snapshot.cpu.packageTemperatureC > 0.0)
        {
            static_cast<void>(pacecar::FormatTemperature(slot, snapshot.cpu.packageTemperatureC));
            AppendStatPiece(buffer, size, piece);
        }
        break;
    }
    case TileId::Ram: {
        if (Unavailable(snapshot.memory.status))
        {
            break;
        }
        static_cast<void>(pacecar::FormatPercent(slot, std::clamp(snapshot.memory.usedPercent, 0.0, 100.0)));
        AppendStatPiece(buffer, size, piece);
        wchar_t used[32] = {};
        wchar_t total[32] = {};
        static_cast<void>(pacecar::FormatBytes(std::span<wchar_t>(used), snapshot.memory.usedBytes));
        static_cast<void>(
            pacecar::FormatBytes(std::span<wchar_t>(total), snapshot.memory.totalBytes));
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%s / %s", used, total);
        AppendStatPiece(buffer, size, piece);
        break;
    }
    case TileId::Gpu: {
        if (Unavailable(snapshot.gpu.status))
        {
            break;
        }
        static_cast<void>(pacecar::FormatPercent(slot, std::clamp(snapshot.gpu.utilizationPercent, 0.0, 100.0)));
        AppendStatPiece(buffer, size, piece);
        if (!Unavailable(snapshot.gpu.temperatureStatus) && snapshot.gpu.temperatureC > 0.0)
        {
            static_cast<void>(pacecar::FormatTemperature(slot, snapshot.gpu.temperatureC));
            AppendStatPiece(buffer, size, piece);
        }
        if (snapshot.gpu.vramTotalBytes > 0)
        {
            wchar_t used[32] = {};
            wchar_t total[32] = {};
            static_cast<void>(
                pacecar::FormatBytes(std::span<wchar_t>(used), snapshot.gpu.vramUsedBytes));
            static_cast<void>(
                pacecar::FormatBytes(std::span<wchar_t>(total), snapshot.gpu.vramTotalBytes));
            _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%s / %s", used, total);
            AppendStatPiece(buffer, size, piece);
        }
        break;
    }
    case TileId::Network: {
        if (Unavailable(snapshot.network.status))
        {
            break;
        }
        wchar_t down[32] = {};
        wchar_t up[32] = {};
        static_cast<void>(pacecar::FormatRate(std::span<wchar_t>(down),
                                              snapshot.network.downBytesPerSecond));
        static_cast<void>(
            pacecar::FormatRate(std::span<wchar_t>(up), snapshot.network.upBytesPerSecond));
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"\x2193 %s", down);
        AppendStatPiece(buffer, size, piece);
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"\x2191 %s", up);
        AppendStatPiece(buffer, size, piece);
        break;
    }
    case TileId::Disk: {
        if (Unavailable(snapshot.disk.status))
        {
            break;
        }
        wchar_t read[32] = {};
        wchar_t write[32] = {};
        static_cast<void>(
            pacecar::FormatRate(std::span<wchar_t>(read), snapshot.disk.readBytesPerSecond));
        static_cast<void>(
            pacecar::FormatRate(std::span<wchar_t>(write), snapshot.disk.writeBytesPerSecond));
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"R %s", read);
        AppendStatPiece(buffer, size, piece);
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"W %s", write);
        AppendStatPiece(buffer, size, piece);
        if (!Unavailable(snapshot.disk.temperatureStatus) && snapshot.disk.temperatureC > 0.0)
        {
            static_cast<void>(pacecar::FormatTemperature(slot, snapshot.disk.temperatureC));
            AppendStatPiece(buffer, size, piece);
        }
        break;
    }
    case TileId::Ping: {
        if (Unavailable(snapshot.ping.status))
        {
            break;
        }
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%.0f ms", snapshot.ping.rttMs);
        AppendStatPiece(buffer, size, piece);
        break;
    }
    case TileId::Fps: {
        if (Unavailable(snapshot.frame.status) || snapshot.frame.fps <= 0.0)
        {
            AppendStatPiece(buffer, size, FrameUnavailableLabel(snapshot.frame.captureState));
            break;
        }
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%.0f FPS", snapshot.frame.fps);
        AppendStatPiece(buffer, size, piece);
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%.1f ms", snapshot.frame.frameTimeMs);
        AppendStatPiece(buffer, size, piece);
        break;
    }
    case TileId::Fans: {
        if (Unavailable(snapshot.fan.status) || snapshot.fan.highestRpm <= 0)
        {
            break;
        }
        _snwprintf_s(piece, std::size(piece), _TRUNCATE, L"%d RPM", snapshot.fan.highestRpm);
        AppendStatPiece(buffer, size, piece);
        break;
    }
    case TileId::Mainboard:
    default: {
        if (Unavailable(snapshot.board.status) || snapshot.board.mainboardTemperatureC <= 0.0)
        {
            break;
        }
        static_cast<void>(pacecar::FormatTemperature(slot, snapshot.board.mainboardTemperatureC));
        AppendStatPiece(buffer, size, piece);
        break;
    }
    }

    if (buffer[0] == L'\0')
    {
        wcscpy_s(buffer, size, L"--");
    }
    return std::wstring_view(buffer);
}
} // namespace

void WidgetScene::EnsureInitialized(ID2D1RenderTarget* target, float statTextSizeDip)
{
    if (target == nullptr)
    {
        return;
    }
    if (!initialized_)
    {
        if (FAILED(text_.Initialize()) || !text_.Ready())
        {
            return;
        }
        initialized_ = true;
        RegisterStyles(statTextSizeDip);
        return;
    }
    if (statTextSizeDip != registeredStatTextSize_)
    {
        RegisterStyles(statTextSizeDip);
    }
}

void WidgetScene::RegisterStyles(float statTextSizeDip)
{
    // Re-registering invalidates every format id held in `styles_`, so register them all again.
    text_.Reset();
    registeredStatTextSize_ = statTextSizeDip;

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
    // Sized to sit inside a default (Compact 3x3) gauge without spilling over the arc.
    primary.sizeDip = 14.0f;
    primary.weight = 700;
    primary.tabular = true;
    primary.align = TextAlign::Center;
    styles_.primary = text_.RegisterFormat(primary);

    TextStyle primarySmall = primary;
    primarySmall.sizeDip = 13.0f;
    styles_.primarySmall = text_.RegisterFormat(primarySmall);

    TextStyle primaryLarge = primary;
    primaryLarge.sizeDip = 24.0f;
    styles_.primaryLarge = text_.RegisterFormat(primaryLarge);

    TextStyle primaryXl = primary;
    primaryXl.sizeDip = 32.0f;
    styles_.primaryXl = text_.RegisterFormat(primaryXl);

    TextStyle secondary{};
    secondary.family = L"Consolas";
    secondary.sizeDip = 9.0f;
    secondary.weight = 400;
    secondary.tabular = true;
    styles_.secondary = text_.RegisterFormat(secondary);

    TextStyle secondaryLarge = secondary;
    secondaryLarge.sizeDip = 11.0f;
    styles_.secondaryLarge = text_.RegisterFormat(secondaryLarge);

    // The StatRows view: a tabular value and a slightly smaller label, sized from config so the
    // user can tune it live. Registered last so their ids change on every size change.
    TextStyle statValue{};
    statValue.family = L"Consolas";
    statValue.sizeDip = statTextSizeDip;
    statValue.weight = 600;
    statValue.tabular = true;
    // Values are left-aligned: the whole row is pushed against the right edge instead, so a change
    // in digit count never shifts the value's starting position.
    statValue.align = TextAlign::Leading;
    styles_.statValue = text_.RegisterFormat(statValue);

    TextStyle statLabel = statValue;
    statLabel.family = L"Segoe UI";
    statLabel.sizeDip = std::max(7.0f, statTextSizeDip * 0.9f);
    statLabel.weight = 600;
    statLabel.tabular = false;
    statLabel.align = TextAlign::Leading;
    styles_.statLabel = text_.RegisterFormat(statLabel);

    // The StatRows "header" column is an icon from the Windows symbol font instead of a text label.
    // Every MDL2 glyph shares one fixed advance width, so a single icon cell keeps the value columns
    // flush regardless of which metric is shown.
    TextStyle statIcon{};
    statIcon.family = L"Segoe MDL2 Assets";
    statIcon.sizeDip = std::max(8.0f, statTextSizeDip * 1.1f);
    statIcon.weight = 400;
    statIcon.align = TextAlign::Leading;
    styles_.statIcon = text_.RegisterFormat(statIcon);
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
    EnsureInitialized(target, settings_.statTextSize);
    if (!initialized_)
    {
        return;
    }

    // High contrast must never rely on whatever is behind the text, so force an opaque panel even
    // for views (StatRows) that normally omit it.
    const bool paintBackground = settings_.drawBackground || theme.highContrast;
    const RectF bounds{0.0f, 0.0f, size.width, size.height};
    if (paintBackground)
    {
        panel_.Draw(target, bounds, kCornerRadius, theme.palette.panelBackground,
                    theme.palette.panelBorder);
    }

    const bool haveFrame = frame_ && frame_->snapshot;
    // Used only for the header status label; FPS visibility is decided by the layout (the capture
    // opt-in and the active view), not by whether frames are currently flowing.
    const bool captureActive = haveFrame && !Unavailable(frame_->snapshot->frame.status) &&
                               frame_->snapshot->frame.fps > 0.0;

    if (settings_.view == pacecar::ViewMode::StatRows ||
        settings_.view == pacecar::ViewMode::FpsText)
    {
        DrawStatRows(target, theme, !paintBackground);
        return;
    }

    if (settings_.drawHeader)
    {
        const RectF headerRect{settings_.panelPadding, settings_.panelPadding,
                               size.width - settings_.panelPadding,
                               settings_.panelPadding + settings_.headerHeight};
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
    }

    // Pick density-appropriate value styles for the active view.
    WidgetStyles viewStyles = styles_;
    switch (settings_.view)
    {
    case pacecar::ViewMode::LargeVisuals:
        viewStyles.primary = styles_.primaryLarge;
        viewStyles.secondary = styles_.secondaryLarge;
        break;
    case pacecar::ViewMode::SmallText:
        viewStyles.primary = styles_.primarySmall;
        break;
    case pacecar::ViewMode::FpsOnly:
        viewStyles.primary = styles_.primaryXl;
        viewStyles.secondary = styles_.secondaryLarge;
        break;
    case pacecar::ViewMode::Full:
    default:
        break;
    }

    const LayoutResult layout = ComputeLayout(size.width, size.height, settings_);
    const bool live = frame_ && frame_->snapshot;
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        if (live)
        {
            DrawLiveTile(target, layout.tiles[i], i, viewStyles, theme);
        }
        else
        {
            DrawDemoTile(target, layout.tiles[i], i, viewStyles, theme);
        }
    }
}

void WidgetScene::DrawStatRows(ID2D1RenderTarget* target, const ResolvedTheme& theme, bool shadow)
{
    const D2D1_SIZE_F size = target->GetSize();
    const float padding = settings_.panelPadding;
    const float textSize = settings_.statTextSize > 1.0f ? settings_.statTextSize : 11.0f;
    const float rowHeight = textSize * 1.7f;
    const float left = padding;
    const float right = size.width - padding;
    const float bottom = size.height - padding * 0.5f;
    if (right <= left || bottom <= padding)
    {
        return;
    }

    // Two columns per row: a fixed-width icon cell and a left-aligned value. Every icon shares one
    // advance width, so the value column starts at a single x and all detail columns sit flush. The
    // block is pushed as close to the right edge as it fits, hugging the top-right corner.
    const bool haveFrame = frame_ && frame_->snapshot;
    wchar_t buffer[128] = {};

    float iconWidth = 0.0f;
    float valueWidth = 0.0f;
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        if (!settings_.tiles[i].visible)
        {
            continue;
        }
        const TileId id = static_cast<TileId>(i);
        const wchar_t icon[2] = {IconFor(id), L'\0'};
        iconWidth = std::max(iconWidth, text_.MeasureWidth(styles_.statIcon, icon));
        const std::wstring_view value =
            haveFrame ? FormatStatRow(id, *frame_->snapshot, buffer, std::size(buffer))
                      : std::wstring_view(DemoFor(id).primary);
        valueWidth = std::max(valueWidth, text_.MeasureWidth(styles_.statValue, value));
    }

    const float columnGap = 2.0f;
    const float blockWidth = iconWidth + columnGap + valueWidth;
    const float blockLeft = std::max(left, right - blockWidth);
    const float valueLeft = blockLeft + iconWidth + columnGap;

    ID2D1SolidColorBrush* shadowBrush =
        shadow ? statShadowBrush_.Get(target, kStatShadowColor) : nullptr;

    const auto drawRowText = [&](std::uint32_t style, std::wstring_view text, const RectF& rect,
                                 ID2D1SolidColorBrush* brush) noexcept {
        if (shadowBrush != nullptr)
        {
            const RectF shifted{rect.left + kStatShadowOffset, rect.top + kStatShadowOffset,
                                rect.right + kStatShadowOffset, rect.bottom + kStatShadowOffset};
            text_.DrawText(target, style, text, shifted, shadowBrush);
        }
        text_.DrawText(target, style, text, rect, brush);
    };

    float y = padding;
    bool truncated = false;
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        if (!settings_.tiles[i].visible)
        {
            continue;
        }
        if (y + rowHeight > bottom)
        {
            truncated = true;
            break;
        }
        const TileId id = static_cast<TileId>(i);
        const std::wstring_view value = haveFrame
                                            ? FormatStatRow(id, *frame_->snapshot, buffer,
                                                            std::size(buffer))
                                            : std::wstring_view(DemoFor(id).primary);

        const wchar_t icon[2] = {IconFor(id), L'\0'};
        const RectF iconRect{blockLeft, y, blockLeft + iconWidth, y + rowHeight};
        const RectF valueRect{valueLeft, y, right, y + rowHeight};
        if (ID2D1SolidColorBrush* brush = statLabelBrush_.Get(target, theme.palette.textDim))
        {
            drawRowText(styles_.statIcon, icon, iconRect, brush);
        }
        if (ID2D1SolidColorBrush* brush = statValueBrush_.Get(target, theme.palette.text))
        {
            drawRowText(styles_.statValue, value, valueRect, brush);
        }
        y += rowHeight;
    }

    // Signal that more stats exist than the current height can show, instead of dropping them
    // silently. Drawn in the value column just below the last row that fit.
    if (truncated && y + textSize <= bottom + textSize)
    {
        const RectF moreRect{valueLeft, y, right, bottom};
        if (ID2D1SolidColorBrush* brush = statLabelBrush_.Get(target, theme.palette.textDim))
        {
            drawRowText(styles_.statLabel, L"\x2026", moreRect, brush);
        }
    }
}

void WidgetScene::DrawDemoTile(ID2D1RenderTarget* target, const TilePlacement& placement,
                               std::size_t index, const WidgetStyles& styles,
                               const ResolvedTheme& theme)
{
    const DemoTile& demo = DemoFor(placement.id);

    TileContent content{};
    content.family = placement.family;
    content.label = LabelFor(placement.id);
    content.primary = demo.primary;
    content.secondary = demo.secondary;
    content.tertiary = demo.tertiary;
    content.gaugeFraction = demo.gauge;
    content.visualizationIsSparkline =
        placement.visualization == pacecar::Visualization::Sparklines;
    // No sparkline before the first sample: a fabricated curve would look like live data.
    content.sparkMin = 0.0f;
    content.sparkMax = 100.0f;
    content.shadow = !settings_.drawBackground;

    tiles_[index].Draw(target, text_, styles, placement.bounds, content, placement.fields, theme);
}

void WidgetScene::DrawLiveTile(ID2D1RenderTarget* target, const TilePlacement& placement,
                               std::size_t index, const WidgetStyles& styles,
                               const ResolvedTheme& theme)
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
    case TileId::Cpu: {
        const double util = percent(snapshot.cpu.status, snapshot.cpu.totalUtilizationPercent);
        primaryView = pacecar::FormatPercent(primary, util);
        if (snapshot.cpu.totalFrequencyMhz > 0.0)
        {
            secondaryView =
                pacecar::FormatFrequency(secondary, snapshot.cpu.totalFrequencyMhz * 1.0e6);
        }
        if (!Unavailable(snapshot.cpu.temperatureStatus) && snapshot.cpu.packageTemperatureC > 0.0)
        {
            tertiaryView = pacecar::FormatTemperature(tertiary, snapshot.cpu.packageTemperatureC);
        }
        gauge = util / 100.0;
        break;
    }
    case TileId::Ram: {
        primaryView = pacecar::FormatPercent(
            primary, percent(snapshot.memory.status, snapshot.memory.usedPercent));
        wchar_t usedW[32]{};
        wchar_t totalW[32]{};
        static_cast<void>(pacecar::FormatBytes(usedW, snapshot.memory.usedBytes));
        static_cast<void>(pacecar::FormatBytes(totalW, snapshot.memory.totalBytes));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"%s / %s", usedW, totalW);
        secondaryView = std::wstring_view(secondary.data());
        gauge = percent(snapshot.memory.status, snapshot.memory.usedPercent) / 100.0;
        break;
    }
    case TileId::Gpu: {
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
    case TileId::Network: {
        const double down =
            Unavailable(snapshot.network.status) ? 0.0 : snapshot.network.downBytesPerSecond;
        primaryView = pacecar::FormatRate(primary, down);
        wchar_t upW[32]{};
        wchar_t downW[32]{};
        static_cast<void>(pacecar::FormatRate(upW, snapshot.network.upBytesPerSecond));
        static_cast<void>(pacecar::FormatRate(downW, down));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"\x2191 %s  \x2193 %s", upW,
                     downW);
        secondaryView = std::wstring_view(secondary.data());
        // Ping is folded into the network readout (the standalone Ping tile remains optional).
        if (Unavailable(snapshot.ping.status))
        {
            tertiaryView = L"ping --";
        }
        else
        {
            _snwprintf_s(tertiary.data(), tertiary.size(), _TRUNCATE, L"ping %.0f ms",
                         snapshot.ping.rttMs);
            tertiaryView = std::wstring_view(tertiary.data());
        }
        gaugeValid = false;
        break;
    }
    case TileId::Disk: {
        const double read =
            Unavailable(snapshot.disk.status) ? 0.0 : snapshot.disk.readBytesPerSecond;
        const double write =
            Unavailable(snapshot.disk.status) ? 0.0 : snapshot.disk.writeBytesPerSecond;
        primaryView = pacecar::FormatRate(primary, read + write);
        wchar_t readW[32]{};
        wchar_t writeW[32]{};
        static_cast<void>(pacecar::FormatRate(readW, read));
        static_cast<void>(pacecar::FormatRate(writeW, write));
        _snwprintf_s(secondary.data(), secondary.size(), _TRUNCATE, L"R: %s  W: %s", readW, writeW);
        secondaryView = std::wstring_view(secondary.data());
        if (!Unavailable(snapshot.disk.temperatureStatus) && snapshot.disk.temperatureC > 0.0)
        {
            tertiaryView = pacecar::FormatTemperature(tertiary, snapshot.disk.temperatureC);
        }
        gaugeValid = false;
        break;
    }
    case TileId::Ping: {
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
    case TileId::Fps: {
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
            gauge = std::clamp(snapshot.frame.fps / targetFps_, 0.0, 1.0);
        }
        else
        {
            primaryView = FrameUnavailableLabel(snapshot.frame.captureState);
            gaugeValid = false;
        }
        break;
    }
    case TileId::Fans: {
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
    default: {
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
            sparkMax = SmoothedSeriesMaximum(sparkScale_[index], sparkSamples);
        }
    }
    // No samples: leave the sparkline empty rather than drawing fabricated activity.

    TileContent content{};
    content.family = placement.family;
    content.label = LabelFor(placement.id);
    content.primary = primaryView;
    content.secondary = secondaryView;
    content.tertiary = tertiaryView;
    content.gaugeFraction = gaugeValid ? gauge : 0.0;
    content.visualizationIsSparkline =
        placement.visualization == pacecar::Visualization::Sparklines;
    content.sparkSamples = sparkSamples;
    content.sparkMin = 0.0f;
    content.sparkMax = sparkMax;
    content.shadow = !settings_.drawBackground;

    tiles_[index].Draw(target, text_, styles, placement.bounds, content, placement.fields, theme);
}

void WidgetScene::Trim()
{
    text_.Trim();
}
} // namespace pacecar::overlay