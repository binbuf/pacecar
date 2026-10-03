#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "../targetver.h"
#include "SystemTheme.h"

#include <windows.h>

namespace pacecar::overlay
{
bool IsHighContrastActive() noexcept
{
    HIGHCONTRASTW highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
    {
        return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
    return false;
}

ResolvedTheme ResolveSystemTheme(pacecar::Theme theme, double opacity) noexcept
{
    ThemeInputs inputs{};
    inputs.theme = theme;
    inputs.opacity = opacity;
    inputs.highContrastActive = IsHighContrastActive();
    return ResolveTheme(inputs);
}
} // namespace pacecar::overlay