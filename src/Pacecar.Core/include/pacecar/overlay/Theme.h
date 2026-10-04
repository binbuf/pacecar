#pragma once

// The overlay's color system: a dark default with one accent per metric family plus a
// High-Contrast-safe mode (design refs 04-ui-ux.md "Visual style" and "Accessibility").
//
// Color resolution is pure and device-free so it can be unit-tested: the caller passes the config
// theme/opacity and whether Windows High Contrast is active (queried in the Overlay layer with
// `SystemParametersInfo`). In High Contrast every accent collapses to the accessible foreground and
// `requireNonColorCue` is set, because state must never be encoded in color alone.

#include <cstdint>

#include "pacecar/config/Config.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
// Linear 0..1 RGBA, converted to `D2D1_COLOR_F` at the draw boundary.
struct ColorF
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    [[nodiscard]] bool operator==(const ColorF&) const noexcept = default;
};

// One accent per metric family plus the shared chrome colors.
struct AccentPalette
{
    ColorF cpu{};
    ColorF ram{};
    ColorF gpu{};
    ColorF network{};
    ColorF disk{};
    ColorF ping{};
    ColorF fans{};
    ColorF mainboard{};
    ColorF text{};
    ColorF textDim{};
    ColorF panelBackground{};
    ColorF panelBorder{};
    ColorF track{};
};

// The legacy dark palette (04-ui-ux.md): CPU blue, RAM green, GPU red, network orange, disk purple,
// ping teal, fans pink, mainboard gold. Panel background is a translucent near-black.
[[nodiscard]] AccentPalette DarkPalette() noexcept;

// The light palette: the same per-family accents darkened for contrast on a near-white panel, with
// near-black text. Panel background is a translucent near-white.
[[nodiscard]] AccentPalette LightPalette() noexcept;

// High-Contrast palette. `lightBackground` selects black-on-white (true) or white-on-black (false)
// chrome; every accent becomes the single accessible foreground so color never carries meaning.
[[nodiscard]] AccentPalette HighContrastPalette(bool lightBackground) noexcept;

// Scales a color's alpha by `opacity` (clamped to 0..1); RGB is preserved.
[[nodiscard]] ColorF ApplyOpacity(const ColorF& color, double opacity) noexcept;

// The accent for a metric family.
[[nodiscard]] ColorF AccentFor(const AccentPalette& palette, MetricFamily family) noexcept;

struct ThemeInputs
{
    pacecar::Theme theme = pacecar::Theme::Dark;
    double opacity = 0.65;
    // True when Windows High Contrast is active regardless of the config theme.
    bool highContrastActive = false;
};

struct ResolvedTheme
{
    AccentPalette palette{};
    bool highContrast = false;
    // True when the theme forbids conveying state through color alone (High Contrast). Widgets then
    // always render text labels/values so meaning survives a monochrome palette.
    bool requireNonColorCue = false;
};

// Resolves the config theme + system High Contrast flag into a concrete palette. High Contrast
// wins over the config theme and forces an opaque panel.
[[nodiscard]] ResolvedTheme ResolveTheme(const ThemeInputs& inputs) noexcept;
} // namespace pacecar::overlay