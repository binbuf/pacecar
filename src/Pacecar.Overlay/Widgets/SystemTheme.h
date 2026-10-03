#pragma once

// System-level theme queries and resolution for the overlay. The pure palette math lives in
// `pacecar/overlay/Theme.h`; this layer only reads the Windows High Contrast flag.

#include "pacecar/config/Config.h"
#include "pacecar/overlay/Theme.h"

namespace pacecar::overlay
{
// True when Windows High Contrast is active (`SystemParametersInfoW(SPI_GETHIGHCONTRAST)`).
[[nodiscard]] bool IsHighContrastActive() noexcept;

// Resolves the config theme/opacity together with the live High Contrast state.
[[nodiscard]] ResolvedTheme ResolveSystemTheme(pacecar::Theme theme, double opacity) noexcept;
} // namespace pacecar::overlay