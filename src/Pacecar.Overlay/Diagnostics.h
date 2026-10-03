#pragma once

// Phase 0 overhead measurement helpers (design ref 05 "Memory budget"): private working set, CPU
// time, and handle/GDI/USER object counts for the prototype process. Kept separate from the
// renderer so the same measurement is available to the app's `--measure` mode and the decision
// record.

#include <cstdint>
#include <string>

namespace pacecar::overlay
{
struct ProcessUsage
{
    std::uint64_t privateWorkingSetBytes = 0;
    // Kernel + user CPU time in 100 ns units.
    std::uint64_t cpuTime100ns = 0;
    std::uint32_t handleCount = 0;
    std::uint32_t gdiObjects = 0;
    std::uint32_t userObjects = 0;
    std::uint32_t threadCount = 0;
};

[[nodiscard]] ProcessUsage QueryProcessUsage() noexcept;

// e.g. "privateWorkingSet=9.4 MiB cpuTime=120 ms handles=312 gdi=48 user=57 threads=6".
[[nodiscard]] std::wstring FormatProcessUsage(const ProcessUsage& usage);
} // namespace pacecar::overlay