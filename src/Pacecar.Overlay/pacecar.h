#pragma once

#include "Resource.h"

#include <windows.h>

namespace pacecar::overlay
{
// Loads the shared application icon for use as a window class icon. `useSmallIcon` selects the
// small system icon size (title bars, task switcher) instead of the large one.
inline HICON LoadAppIcon(HINSTANCE instance, bool useSmallIcon)
{
    const int cx = GetSystemMetrics(useSmallIcon ? SM_CXSMICON : SM_CXICON);
    const int cy = GetSystemMetrics(useSmallIcon ? SM_CYSMICON : SM_CYICON);
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PACECAR), IMAGE_ICON, cx,
                                         cy, LR_DEFAULTCOLOR));
}
} // namespace pacecar::overlay
