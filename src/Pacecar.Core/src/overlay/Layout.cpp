#include "pacecar/overlay/Layout.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace pacecar::overlay
{
namespace
{
constexpr int kCompactColumns = 3;

std::size_t CollectVisible(const LayoutSettings& settings,
                           std::array<std::size_t, kMaxTiles>& ids) noexcept
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        if (settings.tiles[i].visible)
        {
            ids[count++] = i;
        }
    }
    return count;
}

float ContentTop(const LayoutSettings& settings) noexcept
{
    return settings.panelPadding +
           (settings.drawHeader ? settings.headerHeight + settings.headerGap : 0.0f);
}

RectF ContentArea(float widthDip, float heightDip, const LayoutSettings& settings) noexcept
{
    return RectF{settings.panelPadding, ContentTop(settings), widthDip - settings.panelPadding,
                 heightDip - settings.panelPadding};
}

void FillPlacement(TilePlacement& placement, std::size_t index, const RectF& bounds,
                   const LayoutSettings& settings) noexcept
{
    placement.id = static_cast<TileId>(index);
    placement.family = FamilyFor(placement.id);
    placement.bounds = bounds;
    placement.fields = settings.tiles[index].fields;
    placement.visualization = settings.tiles[index].visualization;
}

bool AllCustomValid(const std::array<std::size_t, kMaxTiles>& ids, std::size_t count,
                    const LayoutSettings& settings) noexcept
{
    for (std::size_t k = 0; k < count; ++k)
    {
        const TileSettings& tile = settings.tiles[ids[k]];
        if (!tile.customValid || tile.custom.Width() <= 0.0f || tile.custom.Height() <= 0.0f)
        {
            return false;
        }
    }
    return count > 0;
}

// Stretches tiles to fill the whole content area in a `columns` x ceil(n/columns) grid.
void PlaceStretchedGrid(const RectF& area, const std::array<std::size_t, kMaxTiles>& ids,
                        std::size_t count, int columns, LayoutResult& result,
                        const LayoutSettings& settings) noexcept
{
    const int rows = (static_cast<int>(count) + columns - 1) / columns;
    const float cellWidth = (area.Width() - settings.tileGap * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
    const float cellHeight = (area.Height() - settings.tileGap * static_cast<float>(rows - 1)) /
                             static_cast<float>(rows);
    if (cellWidth <= 0.0f || cellHeight <= 0.0f)
    {
        return;
    }
    for (std::size_t k = 0; k < count; ++k)
    {
        const int column = static_cast<int>(k) % columns;
        const int row = static_cast<int>(k) / columns;
        const float left = area.left + static_cast<float>(column) * (cellWidth + settings.tileGap);
        const float top = area.top + static_cast<float>(row) * (cellHeight + settings.tileGap);
        FillPlacement(result.tiles[result.count], ids[k],
                      RectF{left, top, left + cellWidth, top + cellHeight}, settings);
        ++result.count;
    }
}

// Packs tiles at their measured natural size in a grid; reports the grid extent.
void PlaceNaturalGrid(const RectF& area, const std::array<std::size_t, kMaxTiles>& ids,
                      std::size_t count, int columns, LayoutResult& result,
                      const LayoutSettings& settings, float& gridWidth, float& gridHeight) noexcept
{
    const int rows = (static_cast<int>(count) + columns - 1) / columns;
    std::array<float, kMaxTiles> columnWidths{};
    std::array<float, kMaxTiles> rowHeights{};
    for (std::size_t k = 0; k < count; ++k)
    {
        const TileSize measured = MeasureTile(settings.tiles[ids[k]].fields, settings.tileMetrics);
        const int column = static_cast<int>(k) % columns;
        const int row = static_cast<int>(k) / columns;
        columnWidths[static_cast<std::size_t>(column)] =
            std::max(columnWidths[static_cast<std::size_t>(column)], measured.width);
        rowHeights[static_cast<std::size_t>(row)] =
            std::max(rowHeights[static_cast<std::size_t>(row)], measured.height);
    }

    std::array<float, kMaxTiles> columnX{};
    float x = area.left;
    for (int c = 0; c < columns; ++c)
    {
        columnX[static_cast<std::size_t>(c)] = x;
        x += columnWidths[static_cast<std::size_t>(c)] + settings.tileGap;
    }
    gridWidth = x - settings.tileGap - area.left;

    std::array<float, kMaxTiles> rowY{};
    float y = area.top;
    for (int r = 0; r < rows; ++r)
    {
        rowY[static_cast<std::size_t>(r)] = y;
        y += rowHeights[static_cast<std::size_t>(r)] + settings.tileGap;
    }
    gridHeight = y - settings.tileGap - area.top;

    for (std::size_t k = 0; k < count; ++k)
    {
        const int column = static_cast<int>(k) % columns;
        const int row = static_cast<int>(k) / columns;
        const float left = columnX[static_cast<std::size_t>(column)];
        const float top = rowY[static_cast<std::size_t>(row)];
        FillPlacement(result.tiles[result.count], ids[k],
                      RectF{left, top, left + columnWidths[static_cast<std::size_t>(column)],
                            top + rowHeights[static_cast<std::size_t>(row)]},
                      settings);
        ++result.count;
    }
}

// Places tiles at their persisted Custom geometry; reports the content extent (relative to origin).
void PlaceCustomGrid(const RectF& area, const std::array<std::size_t, kMaxTiles>& ids,
                     std::size_t count, LayoutResult& result, const LayoutSettings& settings,
                     float& contentWidth, float& contentHeight) noexcept
{
    contentWidth = 0.0f;
    contentHeight = 0.0f;
    for (std::size_t k = 0; k < count; ++k)
    {
        const TileSettings& tile = settings.tiles[ids[k]];
        const float left = area.left + tile.custom.left;
        const float top = area.top + tile.custom.top;
        const float right = area.left + tile.custom.right;
        const float bottom = area.top + tile.custom.bottom;
        FillPlacement(result.tiles[result.count], ids[k], RectF{left, top, right, bottom},
                      settings);
        ++result.count;
        contentWidth = std::max(contentWidth, tile.custom.right);
        contentHeight = std::max(contentHeight, tile.custom.bottom);
    }
}

bool TileIdFromConfigKey(std::string_view key, TileId& out) noexcept
{
    char buffer[16] = {};
    std::size_t length = 0;
    for (const char c : key)
    {
        if (c == ' ' || c == '_' || c == '-' || c == '\t')
        {
            continue;
        }
        if (length + 1 >= sizeof(buffer))
        {
            return false;
        }
        buffer[length++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const std::string_view normalized(buffer, length);
    constexpr std::array<std::pair<std::string_view, TileId>, 9> kKeys{{
        {"cpu", TileId::Cpu},
        {"ram", TileId::Ram},
        {"gpu", TileId::Gpu},
        {"network", TileId::Network},
        {"disk", TileId::Disk},
        {"ping", TileId::Ping},
        {"fans", TileId::Fans},
        {"mainboard", TileId::Mainboard},
        {"fps", TileId::Fps},
    }};
    for (const auto& [name, id] : kKeys)
    {
        if (name == normalized)
        {
            out = id;
            return true;
        }
    }
    return false;
}
} // namespace

MetricFamily FamilyFor(TileId id) noexcept
{
    switch (id)
    {
    case TileId::Cpu:
        return MetricFamily::Cpu;
    case TileId::Ram:
        return MetricFamily::Ram;
    case TileId::Gpu:
        return MetricFamily::Gpu;
    case TileId::Network:
        return MetricFamily::Network;
    case TileId::Disk:
        return MetricFamily::Disk;
    case TileId::Ping:
        return MetricFamily::Ping;
    case TileId::Fans:
        return MetricFamily::Fans;
    case TileId::Mainboard:
        return MetricFamily::Mainboard;
    case TileId::Fps:
    default:
        return MetricFamily::Fps;
    }
}

const wchar_t* TileKey(TileId id) noexcept
{
    switch (id)
    {
    case TileId::Cpu:
        return L"cpu";
    case TileId::Ram:
        return L"ram";
    case TileId::Gpu:
        return L"gpu";
    case TileId::Network:
        return L"network";
    case TileId::Disk:
        return L"disk";
    case TileId::Ping:
        return L"ping";
    case TileId::Fans:
        return L"fans";
    case TileId::Mainboard:
        return L"mainboard";
    case TileId::Fps:
    default:
        return L"fps";
    }
}

LayoutSettings DefaultLayoutSettings() noexcept
{
    LayoutSettings settings{};
    for (std::size_t i = 0; i < kMaxTiles; ++i)
    {
        settings.tiles[i].visible = i < 6;
    }
    return settings;
}

void SetFrameCaptureTileVisible(LayoutSettings& settings, bool active) noexcept
{
    settings.tiles[static_cast<std::size_t>(TileId::Fps)].visible = active;
}

void ApplyViewMode(LayoutSettings& settings, pacecar::ViewMode view) noexcept
{
    settings.view = view;
    switch (view)
    {
    case pacecar::ViewMode::LargeVisuals: {
        // Bigger gauges/graphs with room for the value line inside the diagram. The chosen layout
        // preset (3x3 vs 1x6) still decides the arrangement.
        settings.tileMetrics.graphHeight = 72.0f;
        settings.tileMetrics.primaryHeight = 26.0f;
        settings.tileMetrics.lineHeight = 12.0f;
        settings.tileMetrics.labelHeight = 11.0f;
        settings.tileMetrics.minWidth = 96.0f;
        settings.tileMetrics.padding = 7.0f;
        settings.tileGap = 8.0f;
        for (TileSettings& tile : settings.tiles)
        {
            if (!tile.visible)
            {
                continue;
            }
            tile.fields.visualization = true;
            tile.fields.primary = true;
        }
        break;
    }
    case pacecar::ViewMode::SmallText: {
        // Compact text readout: pack each metric as label + value + one detail line, no chrome.
        settings.preset = pacecar::LayoutPreset::AutoFit;
        settings.tileMetrics.labelHeight = 9.0f;
        settings.tileMetrics.primaryHeight = 15.0f;
        settings.tileMetrics.lineHeight = 10.0f;
        settings.tileMetrics.minWidth = 54.0f;
        settings.tileMetrics.padding = 4.0f;
        settings.tileGap = 4.0f;
        for (TileSettings& tile : settings.tiles)
        {
            tile.fields.label = true;
            tile.fields.primary = true;
            tile.fields.secondary = true;
            tile.fields.tertiary = false;
            tile.fields.visualization = false;
            tile.fields.miniSparkline = false;
        }
        break;
    }
    case pacecar::ViewMode::StatRows: {
        // The simplest readout: one plain text line per visible stat. The scene draws the list
        // itself (WidgetScene::DrawStatRows); the layout engine only needs the visibility flags.
        // The panel background is forced off so the view is just text labels floating over the
        // desktop/game with a soft shadow.
        settings.drawBackground = false;
        settings.preset = pacecar::LayoutPreset::AutoFit;
        settings.tileMetrics.labelHeight = 9.0f;
        settings.tileMetrics.primaryHeight = 14.0f;
        settings.tileMetrics.lineHeight = 10.0f;
        settings.tileMetrics.minWidth = 60.0f;
        settings.tileMetrics.padding = 3.0f;
        settings.tileGap = 2.0f;
        for (TileSettings& tile : settings.tiles)
        {
            tile.fields.label = true;
            tile.fields.primary = true;
            tile.fields.secondary = false;
            tile.fields.tertiary = false;
            tile.fields.visualization = false;
            tile.fields.miniSparkline = false;
        }
        break;
    }
    case pacecar::ViewMode::FpsText: {
        // A StatRows-style plain text list restricted to the FPS / frame-time readout. Ignore the
        // per-tile visibility config: only FPS is listed (the scene shows it once frames flow).
        settings.drawBackground = false;
        settings.preset = pacecar::LayoutPreset::AutoFit;
        settings.tileMetrics.labelHeight = 9.0f;
        settings.tileMetrics.primaryHeight = 14.0f;
        settings.tileMetrics.lineHeight = 10.0f;
        settings.tileMetrics.minWidth = 60.0f;
        settings.tileMetrics.padding = 3.0f;
        settings.tileGap = 2.0f;
        for (TileSettings& tile : settings.tiles)
        {
            tile.visible = false;
            tile.fields.label = true;
            tile.fields.primary = true;
            tile.fields.secondary = false;
            tile.fields.tertiary = false;
            tile.fields.visualization = false;
            tile.fields.miniSparkline = false;
        }
        settings.tiles[static_cast<std::size_t>(TileId::Fps)].visible = true;
        break;
    }
    case pacecar::ViewMode::FpsOnly: {
        // Just the frame-time readout. Ignore the per-tile config: only FPS is shown.
        settings.preset = pacecar::LayoutPreset::Compact3x3;
        for (TileSettings& tile : settings.tiles)
        {
            tile.visible = false;
        }
        TileSettings& fps = settings.tiles[static_cast<std::size_t>(TileId::Fps)];
        fps.visible = true;
        fps.fields.label = false;
        fps.fields.primary = true;
        fps.fields.secondary = true;
        fps.fields.tertiary = true;
        fps.fields.visualization = false;
        fps.fields.miniSparkline = false;
        settings.tileMetrics.primaryHeight = 34.0f;
        settings.tileMetrics.lineHeight = 16.0f;
        settings.tileMetrics.labelHeight = 0.0f;
        settings.tileMetrics.padding = 6.0f;
        break;
    }
    case pacecar::ViewMode::Full:
    default:
        break;
    }
}

LayoutSettings LayoutSettingsFromConfig(const pacecar::Config& config) noexcept
{
    LayoutSettings settings = DefaultLayoutSettings();
    settings.preset = config.general.layout;
    settings.statTextSize = static_cast<float>(config.general.stat_text_size);
    settings.drawBackground = !config.general.transparent_background;
    settings.drawHeader =
        settings.drawBackground && (config.general.view == pacecar::ViewMode::Full ||
                                    config.general.view == pacecar::ViewMode::LargeVisuals);

    const std::array<std::pair<TileId, const pacecar::TileConfig*>, 7> source{{
        {TileId::Cpu, &config.tiles.cpu},
        {TileId::Ram, &config.tiles.ram},
        {TileId::Gpu, &config.tiles.gpu},
        {TileId::Network, &config.tiles.network},
        {TileId::Disk, &config.tiles.disk},
        {TileId::Ping, &config.tiles.ping},
        {TileId::Fps, &config.tiles.fps},
    }};
    for (const auto& [id, tilePtr] : source)
    {
        const pacecar::TileConfig& tile = *tilePtr;
        TileSettings& target = settings.tiles[static_cast<std::size_t>(id)];
        target.visible = tile.visible;
        target.fields.label = true;
        target.fields.primary = tile.show_primary;
        target.fields.secondary = tile.show_secondary;
        target.fields.tertiary = tile.show_tertiary;
        target.fields.visualization = tile.show_visualization;
        target.fields.miniSparkline = tile.mini_sparklines;
        target.visualization = tile.visualization;
    }

    // The FPS tile has no per-tile toggle in Settings. It is shown whenever FPS capture is enabled
    // (the explicit opt-in via the hotkey, menus, or an FPS view), even before frames start flowing,
    // so the readout confirms the opt-in instead of silently hiding.
    settings.tiles[static_cast<std::size_t>(TileId::Fps)].visible = config.sensors.fps_capture;

    for (const pacecar::CustomTilePlacement& placement : config.layout.custom_tiles)
    {
        TileId id{};
        if (!TileIdFromConfigKey(placement.tile, id))
        {
            continue;
        }
        TileSettings& target = settings.tiles[static_cast<std::size_t>(id)];
        target.customValid = placement.valid && placement.width > 0.0 && placement.height > 0.0;
        target.custom = RectF{static_cast<float>(placement.x), static_cast<float>(placement.y),
                              static_cast<float>(placement.x + placement.width),
                              static_cast<float>(placement.y + placement.height)};
    }

    ApplyViewMode(settings, config.general.view);
    return settings;
}

TileSize MeasureLayout(const LayoutSettings& settings) noexcept
{
    std::array<std::size_t, kMaxTiles> ids{};
    const std::size_t count = CollectVisible(settings, ids);
    const float width = settings.panelPadding * 2.0f;
    const float contentTop = ContentTop(settings);
    if (count == 0)
    {
        return TileSize{width, contentTop + settings.panelPadding};
    }

    if (settings.preset == pacecar::LayoutPreset::Custom && AllCustomValid(ids, count, settings))
    {
        float customWidth = 0.0f;
        float customHeight = 0.0f;
        for (std::size_t k = 0; k < count; ++k)
        {
            const TileSettings& tile = settings.tiles[ids[k]];
            customWidth = std::max(customWidth, tile.custom.right);
            customHeight = std::max(customHeight, tile.custom.bottom);
        }
        return TileSize{width + customWidth, contentTop + customHeight + settings.panelPadding};
    }

    const int columns = settings.preset == pacecar::LayoutPreset::Vertical1x6
                            ? 1
                            : std::min(kCompactColumns, static_cast<int>(count));
    const int rows = (static_cast<int>(count) + columns - 1) / columns;

    std::array<float, kMaxTiles> columnWidths{};
    std::array<float, kMaxTiles> rowHeights{};
    for (std::size_t k = 0; k < count; ++k)
    {
        const TileSize measured = MeasureTile(settings.tiles[ids[k]].fields, settings.tileMetrics);
        const std::size_t column = k % static_cast<std::size_t>(columns);
        const std::size_t row = k / static_cast<std::size_t>(columns);
        columnWidths[column] = std::max(columnWidths[column], measured.width);
        rowHeights[row] = std::max(rowHeights[row], measured.height);
    }
    float gridWidth = settings.tileGap * static_cast<float>(columns - 1);
    for (int c = 0; c < columns; ++c)
    {
        gridWidth += columnWidths[static_cast<std::size_t>(c)];
    }
    float gridHeight = settings.tileGap * static_cast<float>(rows - 1);
    for (int r = 0; r < rows; ++r)
    {
        gridHeight += rowHeights[static_cast<std::size_t>(r)];
    }
    return TileSize{width + gridWidth, contentTop + gridHeight + settings.panelPadding};
}

LayoutResult ComputeLayout(float widthDip, float heightDip, const LayoutSettings& settings) noexcept
{
    LayoutResult result{};
    result.preset = settings.preset;
    result.content = ContentArea(widthDip, heightDip, settings);

    std::array<std::size_t, kMaxTiles> ids{};
    const std::size_t count = CollectVisible(settings, ids);
    if (count == 0 || widthDip <= 0.0f || heightDip <= 0.0f || result.content.IsEmpty())
    {
        result.contentSize = MeasureLayout(settings);
        return result;
    }

    switch (settings.preset)
    {
    case pacecar::LayoutPreset::Vertical1x6:
        PlaceStretchedGrid(result.content, ids, count, 1, result, settings);
        break;
    case pacecar::LayoutPreset::Custom:
        if (AllCustomValid(ids, count, settings))
        {
            float contentWidth = 0.0f;
            float contentHeight = 0.0f;
            PlaceCustomGrid(result.content, ids, count, result, settings, contentWidth,
                            contentHeight);
            result.content =
                RectF{result.content.left, result.content.top, result.content.left + contentWidth,
                      result.content.top + contentHeight};
        }
        else
        {
            PlaceStretchedGrid(result.content, ids, count, kCompactColumns, result, settings);
        }
        break;
    case pacecar::LayoutPreset::AutoFit: {
        float gridWidth = 0.0f;
        float gridHeight = 0.0f;
        PlaceNaturalGrid(result.content, ids, count,
                         std::min(kCompactColumns, static_cast<int>(count)), result, settings,
                         gridWidth, gridHeight);
        result.content = RectF{result.content.left, result.content.top,
                               result.content.left + gridWidth, result.content.top + gridHeight};
        break;
    }
    case pacecar::LayoutPreset::Compact3x3:
    default:
        PlaceStretchedGrid(result.content, ids, count,
                           std::min(kCompactColumns, static_cast<int>(count)), result, settings);
        break;
    }

    result.contentSize = MeasureLayout(settings);
    return result;
}
} // namespace pacecar::overlay