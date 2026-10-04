#pragma once

// Pure, headless overlay layout engine (design refs 04-ui-ux.md, 05-performance.md).
//
// The layout engine turns the config-driven preset and per-tile/per-field toggles into a fixed list
// of tile rectangles and field flags that the Direct2D widget scene consumes. It contains no
// Direct2D, DirectWrite, or Win32 types so presets and toggles can be unit-tested without a device
// (08-project-layout-and-testing.md "Testing strategy"). All coordinates are device-independent
// pixels (DIPs); the render target DPI does the physical scaling (`DipToPixels`).
//
// Nothing here allocates: the result is a fixed-capacity `std::array`, so the engine is safe to
// call from the render path every frame.

#include <array>
#include <cstddef>
#include <cstdint>

#include "pacecar/config/Config.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
// The maximum number of tiles the engine places. The first six map to `TilesConfig`; `Fans` and
// `Mainboard` are reserved for later sensor work and default hidden. `Fps` (task T17) is shown
// only while a frame-time capture is active.
inline constexpr std::size_t kMaxTiles = 9;

inline constexpr float kLayoutPanelPadding = 8.0f;
inline constexpr float kLayoutHeaderHeight = 18.0f;
inline constexpr float kLayoutHeaderGap = 6.0f;
inline constexpr float kLayoutTileGap = 6.0f;

// Canonical tile order, matching `TilesConfig` field order followed by the reserved families.
enum class TileId : std::uint8_t
{
    Cpu = 0,
    Ram,
    Gpu,
    Network,
    Disk,
    Ping,
    Fans,
    Mainboard,
    Fps,
};

// The accent family for a tile (mirrors `MetricFamily`).
[[nodiscard]] MetricFamily FamilyFor(TileId id) noexcept;

// The canonical config key for a tile ("cpu", "ram", ... "mainboard").
[[nodiscard]] const wchar_t* TileKey(TileId id) noexcept;

// One tile's visibility, field flags, and (for Custom) geometry, in DIPs relative to the content
// origin. `custom` is only read when `preset == Custom` and `customValid` is true.
struct TileSettings
{
    bool visible = true;
    TileFieldVisibility fields{};
    pacecar::Visualization visualization = pacecar::Visualization::Gauges;
    bool customValid = false;
    RectF custom{};
};

// Everything the engine needs to arrange the panel.
struct LayoutSettings
{
    pacecar::LayoutPreset preset = pacecar::LayoutPreset::Compact3x3;
    // Presentation view. Full/LargeVisuals honor `preset`; the text views imply their own compact
    // arrangement. `ApplyViewMode` (below) resolves the per-view visibility, fields, and metrics.
    pacecar::ViewMode view = pacecar::ViewMode::Full;
    std::array<TileSettings, kMaxTiles> tiles{};
    TileLayoutMetrics tileMetrics{};
    float panelPadding = kLayoutPanelPadding;
    float headerHeight = kLayoutHeaderHeight;
    float headerGap = kLayoutHeaderGap;
    float tileGap = kLayoutTileGap;
    // Whether the panel fill/border is drawn. False makes the overlay text/graphics only.
    bool drawBackground = true;
    // Whether the "PACECAR" header band is reserved and drawn.
    bool drawHeader = true;
    // Value font size (DIPs) for the text views, notably StatRows. The scene re-registers its text
    // formats when this changes so the effect is visible live from Settings. Matches the config
    // default (`GeneralConfig::stat_text_size`) so an unmapped scene reads the same as a loaded one.
    float statTextSize = 9.0f;
};

// A placed tile: its identity, window-space rectangle (DIPs), and the resolved fields to draw.
struct TilePlacement
{
    TileId id = TileId::Cpu;
    MetricFamily family = MetricFamily::Cpu;
    RectF bounds{};
    TileFieldVisibility fields{};
    pacecar::Visualization visualization = pacecar::Visualization::Gauges;
};

// The engine output. `count` entries of `tiles` are filled (visible tiles, in reading order); the
// rest are left default. `content` is the area available to tiles; `contentSize` is the exact
// window DIP size required to fit the content with no extra margin (Auto-fit / Specs).
struct LayoutResult
{
    std::array<TilePlacement, kMaxTiles> tiles{};
    std::size_t count = 0;
    RectF content{};
    TileSize contentSize{};
    pacecar::LayoutPreset preset = pacecar::LayoutPreset::Compact3x3;
};

// Arranges the visible tiles inside `widthDip` x `heightDip` per the preset. Fields and per-tile
// visibility are honored; hidden tiles are skipped and the remaining tiles pack in reading order.
[[nodiscard]] LayoutResult ComputeLayout(float widthDip, float heightDip,
                                         const LayoutSettings& settings) noexcept;

// The exact window DIP size that fits the visible content for the preset, no extra margin.
[[nodiscard]] TileSize MeasureLayout(const LayoutSettings& settings) noexcept;

// Default settings: the six metric tiles visible with default fields; reserved families hidden.
[[nodiscard]] LayoutSettings DefaultLayoutSettings() noexcept;

// Forces the FPS tile's visibility to `active` (task T17). The FPS tile is capture-only, so the
// overlay calls this every frame; the config's `tiles.fps.visible` is intentionally ignored.
void SetFrameCaptureTileVisible(LayoutSettings& settings, bool active) noexcept;

// Applies a view's presentation policy to `settings` in place: which tiles are visible, each
// tile's field flags and metrics, and the arrangement used by the text views. Called by
// `LayoutSettingsFromConfig` after the config toggles are mapped, so an explicit view overrides the
// per-tile config for the duration of that view.
void ApplyViewMode(LayoutSettings& settings, pacecar::ViewMode view) noexcept;

// Maps a `Config` (general preset/view/background, tiles toggles, custom geometry) into engine
// settings. The active `ViewMode` is applied last.
[[nodiscard]] LayoutSettings LayoutSettingsFromConfig(const pacecar::Config& config) noexcept;
} // namespace pacecar::overlay