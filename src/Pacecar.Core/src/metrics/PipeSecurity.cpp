#include "pacecar/metrics/PipeSecurity.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <sddl.h>

namespace pacecar::metrics
{
namespace
{
// A SID string is only safe to splice into an SDDL when it starts with "S-1-".
bool IsPlausibleSidString(const std::wstring& sid) noexcept
{
    return sid.size() >= 4 && sid.compare(0, 4, L"S-1-") == 0;
}
} // namespace

std::wstring BuildPipeSddl(const std::wstring& userSid)
{
    if (!IsPlausibleSidString(userSid))
    {
        return {};
    }

    std::wstring sddl = L"D:P(D;;GA;;;AN)(D;;GA;;;NU)(A;;GA;;;";
    sddl += userSid;
    sddl += L")(A;;GA;;;BA)";

    // Validate the assembled SDDL by parsing it; reject anything the OS cannot turn into an SD.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const BOOL ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
    if (descriptor != nullptr)
    {
        LocalFree(descriptor);
    }
    if (ok == FALSE)
    {
        return {};
    }
    return sddl;
}
} // namespace pacecar::metrics