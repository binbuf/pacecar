#include "pacecar/metrics/PawnIODetection.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

namespace pacecar::metrics
{
namespace
{
constexpr wchar_t kDevicePath[] = L"\\\\.\\GLOBALROOT\\Device\\PawnIO";
constexpr wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO";
constexpr wchar_t kUninstallKeyWow[] =
    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO";

bool RegistryKeyExists(HKEY root, const wchar_t* subKey) noexcept
{
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(root, subKey, 0, KEY_READ, &key);
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
    return status == ERROR_SUCCESS;
}

class Win32PawnIOSource final : public IPawnIOSystemSource
{
  public:
    bool DevicePresent() override
    {
        // Opening the device can fail with access-denied in an unelevated process even though the
        // driver is installed; both outcomes prove presence. Any other error means absent.
        HANDLE device = CreateFileW(kDevicePath, GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr);
        if (device != INVALID_HANDLE_VALUE)
        {
            CloseHandle(device);
            return true;
        }
        const DWORD error = GetLastError();
        return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION;
    }

    bool UninstallKeyPresent() override
    {
        return RegistryKeyExists(HKEY_LOCAL_MACHINE, kUninstallKey) ||
               RegistryKeyExists(HKEY_LOCAL_MACHINE, kUninstallKeyWow);
    }
};
} // namespace

PawnIOStatus DetectPawnIO(IPawnIOSystemSource& source) noexcept
{
    if (source.DevicePresent() || source.UninstallKeyPresent())
    {
        return PawnIOStatus::Installed;
    }
    return PawnIOStatus::Absent;
}

std::unique_ptr<IPawnIOSystemSource> MakeWin32PawnIOSource()
{
    return std::make_unique<Win32PawnIOSource>();
}

const wchar_t* PawnIOStatusText(PawnIOStatus status) noexcept
{
    return status == PawnIOStatus::Installed ? L"installed" : L"not installed";
}

std::wstring PawnIOGuidance(PawnIOStatus status)
{
    if (status == PawnIOStatus::Installed)
    {
        return L"PawnIO detected; enable deep sensors to read package/board/fan sensors.";
    }
    return L"Deep sensors off - install PawnIO from pawnio.eu to read package/board/fan sensors.";
}
} // namespace pacecar::metrics