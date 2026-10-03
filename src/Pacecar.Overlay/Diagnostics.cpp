#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "Diagnostics.h"

#include <cstdio>

#include <windows.h>
#include <psapi.h>

namespace pacecar::overlay
{
ProcessUsage QueryProcessUsage() noexcept
{
    ProcessUsage usage{};
    const HANDLE process = GetCurrentProcess();

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)))
    {
        usage.privateWorkingSetBytes = memory.PrivateUsage;
    }

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user))
    {
        const auto toUint64 = [](const FILETIME& time)
        {
            ULARGE_INTEGER value{};
            value.LowPart = time.dwLowDateTime;
            value.HighPart = time.dwHighDateTime;
            return value.QuadPart;
        };
        usage.cpuTime100ns = toUint64(kernel) + toUint64(user);
    }

    DWORD handleCount = 0;
    if (GetProcessHandleCount(process, &handleCount))
    {
        usage.handleCount = handleCount;
    }
    usage.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    usage.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    usage.threadCount = 0;
    return usage;
}

std::wstring FormatProcessUsage(const ProcessUsage& usage)
{
    const double mebibyte = 1024.0 * 1024.0;
    wchar_t buffer[256] = {};
    swprintf_s(buffer,
               L"privateWorkingSet=%.1f MiB cpuTime=%.0f ms handles=%lu gdi=%lu user=%lu",
               static_cast<double>(usage.privateWorkingSetBytes) / mebibyte,
               static_cast<double>(usage.cpuTime100ns) / 10000.0,
               static_cast<unsigned long>(usage.handleCount),
               static_cast<unsigned long>(usage.gdiObjects),
               static_cast<unsigned long>(usage.userObjects));
    return buffer;
}
} // namespace pacecar::overlay