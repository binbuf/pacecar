#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "Hdr.h"

#include <cstring>
#include <cwchar>
#include <vector>

namespace pacecar::overlay
{
namespace
{
bool DeviceNameMatches(const wchar_t* gdiName, const wchar_t* monitorName) noexcept
{
    return gdiName != nullptr && monitorName != nullptr &&
           std::wcscmp(gdiName, monitorName) == 0;
}
} // namespace

const wchar_t* AdvancedColorState::Summary() const noexcept
{
    if (!querySucceeded)
    {
        return L"HDR query unavailable";
    }
    if (advancedColorEnabled)
    {
        return wideColorEnforced ? L"HDR/advanced color: enabled (wide color forced)"
                                 : L"HDR/advanced color: enabled";
    }
    if (advancedColorSupported)
    {
        return L"HDR/advanced color: supported but disabled (SDR)";
    }
    return L"HDR/advanced color: unsupported (SDR)";
}

AdvancedColorState QueryAdvancedColor(HMONITOR monitor) noexcept
{
    AdvancedColorState state{};
    if (monitor == nullptr)
    {
        return state;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
    {
        return state;
    }

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
    {
        return state;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS)
    {
        return state;
    }
    paths.resize(pathCount);

    for (const DISPLAYCONFIG_PATH_INFO& path : paths)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = path.sourceInfo.adapterId;
        sourceName.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS)
        {
            continue;
        }
        if (!DeviceNameMatches(sourceName.viewGdiDeviceName, monitorInfo.szDevice))
        {
            continue;
        }

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(colorInfo);
        colorInfo.header.adapterId = path.targetInfo.adapterId;
        colorInfo.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&colorInfo.header) != ERROR_SUCCESS)
        {
            return state;
        }
        state.querySucceeded = true;
        state.advancedColorSupported = colorInfo.advancedColorSupported != 0;
        state.advancedColorEnabled = colorInfo.advancedColorEnabled != 0;
        state.wideColorEnforced = colorInfo.wideColorEnforced != 0;
        return state;
    }
    return state;
}
} // namespace pacecar::overlay