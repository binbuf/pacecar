#pragma once

// Advanced-color (HDR) detection for the overlay (design ref 03 "HDR and color management").
//
// The overlay content is SDR; on an advanced-color display DWM/DirectComposition color management
// can render SDR content dim, washed out, or oversaturated. Phase 0 records what is detected so the
// rendering path and any color-space decision (Recipe B's `SetColorSpace1`) have evidence behind
// them and so no single hardcoded color assumption is relied on across HDR and SDR.

#include <windows.h>

namespace pacecar::overlay
{
struct AdvancedColorState
{
    // True when `QueryDisplayConfig`/`DisplayConfigGetDeviceInfo` answered for this monitor.
    bool querySucceeded = false;
    bool advancedColorSupported = false;
    bool advancedColorEnabled = false;
    bool wideColorEnforced = false;

    // A human-readable summary for the decision record and `--diagnostics`.
    [[nodiscard]] const wchar_t* Summary() const noexcept;
};

// Queries HDR/advanced-color state for the monitor that hosts `monitor`.
[[nodiscard]] AdvancedColorState QueryAdvancedColor(HMONITOR monitor) noexcept;
} // namespace pacecar::overlay