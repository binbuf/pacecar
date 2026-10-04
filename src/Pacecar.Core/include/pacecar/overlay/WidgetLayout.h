#pragma once

// Pure, headless widget geometry for the overlay (design refs 04-ui-ux.md, 05-performance.md).
//
// This header contains no Direct2D, DirectWrite, or Win32 types so tile layout and value geometry
// can be unit-tested without a device (08-project-layout-and-testing.md "Testing strategy"). The
// `Pacecar.Overlay/Widgets/` draw code consumes these results and only converts DIPs to pixels for
// the target. All coordinates are device-independent pixels (DIPs); the render target DPI does the
// physical scaling, so the same layout holds at 100%, 150%, and 200% (see `DipToPixels` in
// `OverlayPlacement.h`).
//
// Nothing here allocates: callers pass fixed-capacity output spans for point buffers.

#include <cstddef>
#include <cstdint>
#include <span>

namespace pacecar::overlay
{
// A rectangle in DIPs. `right`/`bottom` are exclusive.
struct RectF
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    [[nodiscard]] float Width() const noexcept
    {
        return right - left;
    }

    [[nodiscard]] float Height() const noexcept
    {
        return bottom - top;
    }

    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return Width() <= 0.0f || Height() <= 0.0f;
    }

    [[nodiscard]] bool operator==(const RectF&) const noexcept = default;
};

struct PointF
{
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] bool operator==(const PointF&) const noexcept = default;
};

// The metric families the overlay knows about, matching the legacy accent palette.
enum class MetricFamily : std::uint8_t
{
    Cpu,
    Ram,
    Gpu,
    Network,
    Disk,
    Ping,
    Fans,
    Mainboard,
    // Opt-in FPS / frame-time tile (task T17); shown only while a capture is active.
    Fps,
};

// Per-field visibility for one tile, mirroring `TileConfig` (config/Config.h). `visualization`
// is the gauge or sparkline block; `miniSparkline` is the compact history strip.
struct TileFieldVisibility
{
    bool label = true;
    bool primary = true;
    bool secondary = true;
    bool tertiary = true;
    bool visualization = true;
    bool miniSparkline = false;

    [[nodiscard]] bool operator==(const TileFieldVisibility&) const noexcept = default;
};

// Baseline DIP metrics for a tile. Kept as a value so T11 can tune density without touching the
// stacking algorithm.
struct TileLayoutMetrics
{
    float padding = 6.0f;
    float labelHeight = 11.0f;
    float primaryHeight = 22.0f;
    float lineHeight = 12.0f;
    float lineGap = 2.0f;
    float graphHeight = 44.0f;
    float miniSparklineHeight = 16.0f;
    float sectionGap = 4.0f;
    float minWidth = 56.0f;
};

// Result of `ComputeTileLayout`. Every rectangle is in the tile's local DIP space (origin at the
// tile's top-left). Hidden fields have empty rectangles and `visible.*` false.
struct TileLayout
{
    TileFieldVisibility visible{};
    RectF content{};
    RectF label{};
    RectF primary{};
    RectF secondary{};
    RectF tertiary{};
    RectF visualization{};
    RectF miniSparkline{};
    float measuredWidth = 0.0f;
    float measuredHeight = 0.0f;
};

struct TileSize
{
    float width = 0.0f;
    float height = 0.0f;
};

// Stacks the visible fields vertically inside `widthDip` x `heightDip`. Fields are laid out in the
// order label, primary, visualization, secondary, tertiary, mini-sparkline and clipped to the
// available height. When `visualization` is shown the primary rect overlays it (the gauge's centered
// value); otherwise primary is its own line.
[[nodiscard]] TileLayout ComputeTileLayout(float widthDip, float heightDip,
                                           const TileFieldVisibility& visibility,
                                           const TileLayoutMetrics& metrics = {}) noexcept;

// Minimum content size for the given visibility (used by T11's auto-fit). Width is the baseline
// minimum; height is the stacked height of the visible fields plus padding.
[[nodiscard]] TileSize MeasureTile(const TileFieldVisibility& visibility,
                                   const TileLayoutMetrics& metrics = {}) noexcept;

// --- Gauge geometry (fractions in 0..1) -------------------------------------------------------

inline constexpr float kPi = 3.14159265358979323846f;
// 270-degree sweep starting at the bottom-left, matching the legacy gauge.
inline constexpr float kGaugeStartAngle = 3.0f * kPi / 4.0f;
inline constexpr float kGaugeSweep = 3.0f * kPi / 2.0f;

// The sweep angle for a clamped value fraction.
[[nodiscard]] float GaugeSweepForValue(double fraction) noexcept;

// A point on a circle. Screen space (y grows downward), matching D2D.
[[nodiscard]] PointF ArcPoint(const PointF& center, float radius, float angle) noexcept;

// --- Sparkline geometry -----------------------------------------------------------------------

// Fixed history window for mini sparklines and gauges' companion graphs (1 minute at 1 Hz).
inline constexpr std::size_t kSparklineCapacity = 60;

// Maps `samples` (chronological, oldest first) into `box` and writes the polyline vertices to
// `out`. Values are scaled from `minValue`..`maxValue`; when the range is degenerate the line is
// centered. Returns the number of points written (0 for fewer than 1 sample or an empty span).
[[nodiscard]] std::size_t ComputeSparklinePoints(std::span<const float> samples, float minValue,
                                                 float maxValue, const RectF& box,
                                                 std::span<PointF> out) noexcept;
} // namespace pacecar::overlay