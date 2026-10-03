#include "pacecar/util/TimerResolution.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pacecar
{
namespace
{
using NtQueryTimerResolutionFn = LONG(NTAPI*)(PULONG minimum, PULONG maximum, PULONG current);
} // namespace

TimerResolution QueryTimerResolution() noexcept
{
    TimerResolution resolution{};

    static NtQueryTimerResolutionFn query = nullptr;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            ntdll = LoadLibraryW(L"ntdll.dll");
        }
        if (ntdll != nullptr)
        {
            query = reinterpret_cast<NtQueryTimerResolutionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQueryTimerResolution")));
        }
    }
    if (query == nullptr)
    {
        return resolution;
    }

    // `NtQueryTimerResolution` fills the coarsest resolution first, then the finest, then the
    // current. Naming the outputs by meaning keeps the struct intuitive.
    ULONG maximum = 0;
    ULONG minimum = 0;
    ULONG current = 0;
    if (query(&maximum, &minimum, &current) < 0)
    {
        return resolution;
    }

    resolution.minimum100ns = minimum;
    resolution.maximum100ns = maximum;
    resolution.current100ns = current;
    resolution.valid = true;
    return resolution;
}

bool IsDefaultTimerResolution(const TimerResolution& resolution) noexcept
{
    if (!resolution.valid)
    {
        return true;
    }
    return resolution.current100ns >= resolution.maximum100ns;
}

bool SameTimerResolution(const TimerResolution& before, const TimerResolution& after) noexcept
{
    if (!before.valid || !after.valid)
    {
        return true;
    }
    return before.current100ns == after.current100ns;
}
} // namespace pacecar