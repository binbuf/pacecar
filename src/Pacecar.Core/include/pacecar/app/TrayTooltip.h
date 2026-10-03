#pragma once

// One-line tray tooltip summarising the live metrics (design ref 04-ui-ux.md "Tray": "CPU 42% |
// GPU 71C"). Kept in the headless core so the text is unit-testable; the tray just copies it into
// `NOTIFYICONDATA.szTip` (127 characters + NUL).

#include <string>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::app
{
// Builds "CPU 42% | GPU 71C | RAM 63%", omitting metrics whose source is unavailable. Returns
// "Pacecar" when nothing is available. The result never exceeds kTrayTooltipCapacity characters.
[[nodiscard]] std::wstring FormatTrayTooltip(const pacecar::metrics::MetricsSnapshot& snapshot);

// `NOTIFYICONDATAW.szTip` holds 128 wchar_t including the terminator.
inline constexpr std::size_t kTrayTooltipCapacity = 127;
} // namespace pacecar::app