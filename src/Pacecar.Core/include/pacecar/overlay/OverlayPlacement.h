#pragma once

// Pure, headless overlay placement and DPI math.
//
// The overlay window's position and per-monitor DPI handling must be testable without an HWND, a
// monitor, or a message pump (design refs 03-overlay-rendering.md "Multi-monitor and DPI",
// 08-project-layout-and-testing.md "Testing strategy"). This header therefore contains no Win32
// types: callers convert `EnumDisplayMonitors`/`GetMonitorInfo` results into `MonitorWorkArea`
// values and convert the returned `IntRect` back into a `SetWindowPos` call.
//
// Coordinate convention: virtual-screen *physical* pixels. `dpi` values are the monitor DPI where
// 96 is 100%. All functions are total and never throw.

#include <cstddef>
#include <string>
#include <vector>

namespace pacecar::overlay
{
// A rectangle in virtual-screen physical pixels. `right`/`bottom` are exclusive.
struct IntRect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] int Width() const noexcept
    {
        return right - left;
    }

    [[nodiscard]] int Height() const noexcept
    {
        return bottom - top;
    }

    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return Width() <= 0 || Height() <= 0;
    }

    [[nodiscard]] bool operator==(const IntRect&) const noexcept = default;
};

// One monitor's usable area (work area, i.e. excluding the taskbar). `deviceName` is the stable
// GDI device name (`\\.\DISPLAY1`) used as the per-monitor persistence key.
struct MonitorWorkArea
{
    std::wstring deviceName{};
    IntRect workArea{};
    unsigned dpi = 96;
    bool primary = false;
};

// Default number of physical pixels of the overlay that must remain on a monitor for a saved
// position to be accepted without adjustment.
inline constexpr int kDefaultVisibleMargin = 32;

// Pixel area of the intersection of two rectangles (0 when they do not overlap).
[[nodiscard]] long long IntersectionArea(const IntRect& a, const IntRect& b) noexcept;

// Returns true when the rectangle has at least `margin` pixels of extent inside the monitor's work
// area on both axes (i.e. a grabbable sliver stays visible).
[[nodiscard]] bool IsSufficientlyVisible(const IntRect& rect,
                                         const MonitorWorkArea& monitor,
                                         int margin = kDefaultVisibleMargin) noexcept;

// True when at least `margin` pixels of `rect` are visible on *any* monitor.
[[nodiscard]] bool IsRectVisible(const IntRect& rect,
                                 const std::vector<MonitorWorkArea>& monitors,
                                 int margin = kDefaultVisibleMargin) noexcept;

// The monitor with the largest intersection with `rect` (ties broken by the earliest entry), or
// nullptr when the rectangle overlaps no monitor.
[[nodiscard]] const MonitorWorkArea* FindBestMonitor(
    const IntRect& rect, const std::vector<MonitorWorkArea>& monitors) noexcept;

// The monitor whose work-area center is closest to the rectangle's center. Used when the rectangle
// overlaps no monitor at all. Returns nullptr only for an empty monitor list.
[[nodiscard]] const MonitorWorkArea* FindNearestMonitor(
    const IntRect& rect, const std::vector<MonitorWorkArea>& monitors) noexcept;

// Clamps a saved rectangle onto a monitor:
//   - If the rectangle is already sufficiently visible on some monitor, it is returned unchanged
//     (so a deliberately edge-hugging placement is preserved).
//   - Otherwise the rectangle is re-anchored onto the best-overlapping monitor, or the nearest
//     monitor when it overlaps none, and constrained to that monitor's work area. The rectangle is
//     never enlarged; if it is larger than the work area only its top-left is aligned.
// With no monitors the rectangle is returned unchanged.
[[nodiscard]] IntRect ClampToWorkArea(
    const IntRect& rect,
    const std::vector<MonitorWorkArea>& monitors,
    int margin = kDefaultVisibleMargin) noexcept;

// The primary monitor, or the first entry when none is marked primary, or nullptr when empty.
[[nodiscard]] const MonitorWorkArea* PrimaryMonitor(
    const std::vector<MonitorWorkArea>& monitors) noexcept;

// --- DPI math ---------------------------------------------------------------------------------

inline constexpr unsigned kBaseDpi = 96;

// DPI scaled from a reference of 96. Non-positive/absurd DPI values fall back to 96.
[[nodiscard]] unsigned NormalizeDpi(unsigned dpi) noexcept;

// Device-independent pixels -> physical pixels, rounded to nearest.
[[nodiscard]] int DipToPixels(float dip, unsigned dpi) noexcept;

// Physical pixels -> device-independent pixels.
[[nodiscard]] float PixelsToDip(int pixels, unsigned dpi) noexcept;

// The layout scale factor (dpi / 96) as a float.
[[nodiscard]] float DpiScale(unsigned dpi) noexcept;

// Converts a rectangle drawn at `fromDpi` into the equivalent rectangle at `toDpi`, preserving its
// position and size in DIPs. Returns the input unchanged when the DPIs match or are invalid.
[[nodiscard]] IntRect ScaleRectForDpi(const IntRect& rect, unsigned fromDpi, unsigned toDpi) noexcept;
} // namespace pacecar::overlay