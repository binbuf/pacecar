#include "pacecar/overlay/Theme.h"

#include <algorithm>

namespace pacecar::overlay
{
namespace
{
ColorF Rgb(float r, float g, float b) noexcept
{
    return ColorF{r, g, b, 1.0f};
}
} // namespace

AccentPalette DarkPalette() noexcept
{
    AccentPalette palette{};
    palette.cpu = Rgb(100.0f / 255.0f, 160.0f / 255.0f, 255.0f / 255.0f);     // bright blue
    palette.ram = Rgb(80.0f / 255.0f, 210.0f / 255.0f, 130.0f / 255.0f);      // emerald green
    palette.gpu = Rgb(240.0f / 255.0f, 90.0f / 255.0f, 90.0f / 255.0f);       // soft red
    palette.network = Rgb(255.0f / 255.0f, 175.0f / 255.0f, 50.0f / 255.0f);  // warm orange
    palette.disk = Rgb(180.0f / 255.0f, 140.0f / 255.0f, 240.0f / 255.0f);    // lavender purple
    palette.ping = Rgb(80.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f);     // teal
    palette.fans = Rgb(230.0f / 255.0f, 160.0f / 255.0f, 180.0f / 255.0f);    // soft pink
    palette.mainboard = Rgb(200.0f / 255.0f, 180.0f / 255.0f, 120.0f / 255.0f); // warm gold
    palette.text = Rgb(0.92f, 0.94f, 0.98f);
    palette.textDim = ColorF{0.62f, 0.66f, 0.72f, 0.90f};
    palette.panelBackground = ColorF{0.05f, 0.06f, 0.08f, 1.0f};
    palette.panelBorder = ColorF{0.35f, 0.40f, 0.50f, 0.55f};
    palette.track = ColorF{0.25f, 0.27f, 0.32f, 0.80f};
    return palette;
}

AccentPalette LightPalette() noexcept
{
    AccentPalette palette{};
    // Darkened curves of the dark-theme accents so they stay legible on the light panel.
    palette.cpu = Rgb(30.0f / 255.0f, 90.0f / 255.0f, 190.0f / 255.0f);      // deep blue
    palette.ram = Rgb(20.0f / 255.0f, 130.0f / 255.0f, 60.0f / 255.0f);      // forest green
    palette.gpu = Rgb(190.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f);      // strong red
    palette.network = Rgb(200.0f / 255.0f, 110.0f / 255.0f, 0.0f / 255.0f);  // amber orange
    palette.disk = Rgb(110.0f / 255.0f, 70.0f / 255.0f, 180.0f / 255.0f);    // violet
    palette.ping = Rgb(0.0f / 255.0f, 130.0f / 255.0f, 130.0f / 255.0f);     // teal
    palette.fans = Rgb(180.0f / 255.0f, 80.0f / 255.0f, 120.0f / 255.0f);    // magenta
    palette.mainboard = Rgb(150.0f / 255.0f, 110.0f / 255.0f, 20.0f / 255.0f); // bronze
    palette.text = Rgb(0.10f, 0.11f, 0.14f);
    palette.textDim = ColorF{0.34f, 0.36f, 0.40f, 0.95f};
    palette.panelBackground = ColorF{0.96f, 0.97f, 0.99f, 1.0f};
    palette.panelBorder = ColorF{0.45f, 0.48f, 0.54f, 0.65f};
    palette.track = ColorF{0.72f, 0.75f, 0.80f, 0.90f};
    return palette;
}

AccentPalette HighContrastPalette(bool lightBackground) noexcept
{
    const ColorF foreground = lightBackground ? Rgb(0.0f, 0.0f, 0.0f) : Rgb(1.0f, 1.0f, 1.0f);
    const ColorF background = lightBackground ? Rgb(1.0f, 1.0f, 1.0f) : Rgb(0.0f, 0.0f, 0.0f);

    AccentPalette palette{};
    palette.cpu = foreground;
    palette.ram = foreground;
    palette.gpu = foreground;
    palette.network = foreground;
    palette.disk = foreground;
    palette.ping = foreground;
    palette.fans = foreground;
    palette.mainboard = foreground;
    palette.text = foreground;
    palette.textDim = foreground;
    palette.panelBackground = background;
    palette.panelBorder = foreground;
    palette.track = foreground;
    return palette;
}

ColorF ApplyOpacity(const ColorF& color, double opacity) noexcept
{
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    ColorF result = color;
    result.a = static_cast<float>(clamped);
    return result;
}

ColorF AccentFor(const AccentPalette& palette, MetricFamily family) noexcept
{
    switch (family)
    {
    case MetricFamily::Cpu:
        return palette.cpu;
    case MetricFamily::Ram:
        return palette.ram;
    case MetricFamily::Gpu:
        return palette.gpu;
    case MetricFamily::Network:
        return palette.network;
    case MetricFamily::Disk:
        return palette.disk;
    case MetricFamily::Ping:
        return palette.ping;
    case MetricFamily::Fans:
        return palette.fans;
    case MetricFamily::Mainboard:
        return palette.mainboard;
    }
    return palette.text;
}

ResolvedTheme ResolveTheme(const ThemeInputs& inputs) noexcept
{
    ResolvedTheme resolved{};
    const bool highContrast = inputs.highContrastActive || inputs.theme == pacecar::Theme::HighContrast;
    if (highContrast)
    {
        resolved.palette = HighContrastPalette(/*lightBackground=*/inputs.theme == pacecar::Theme::Light);
        resolved.highContrast = true;
        resolved.requireNonColorCue = true;
        // Accessibility: the High Contrast panel stays fully opaque.
        resolved.palette.panelBackground.a = 1.0f;
        return resolved;
    }

    resolved.palette = inputs.theme == pacecar::Theme::Light ? LightPalette() : DarkPalette();
    resolved.highContrast = false;
    resolved.requireNonColorCue = false;
    resolved.palette.panelBackground = ApplyOpacity(resolved.palette.panelBackground, inputs.opacity);
    return resolved;
}
} // namespace pacecar::overlay