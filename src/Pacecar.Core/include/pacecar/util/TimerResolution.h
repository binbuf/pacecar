#pragma once

// Platform timer-resolution probe (design ref 05-performance.md).
//
// Pacecar never raises the platform timer resolution: the sampler waits on a coarse timeout and the
// UI repaints on change, so no code calls `timeBeginPeriod(1)`. To prove that at runtime the task
// asserts the process-level timer resolution is unchanged by querying `NtQueryTimerResolution`
// before and after a run. `NtQueryTimerResolution` is loaded dynamically from `ntdll.dll` so no
// import library is required.
//
// All values are in 100 ns units, matching the API. `maximum` is the coarsest (worst/default)
// resolution; a process that has called `timeBeginPeriod` sees `current` become smaller than
// `maximum`.

#include <cstdint>

namespace pacecar
{
struct TimerResolution
{
    std::uint32_t minimum100ns = 0;
    std::uint32_t maximum100ns = 0;
    std::uint32_t current100ns = 0;
    bool valid = false;

    [[nodiscard]] double CurrentMs() const noexcept
    {
        return static_cast<double>(current100ns) / 10000.0;
    }
    [[nodiscard]] double MinimumMs() const noexcept
    {
        return static_cast<double>(minimum100ns) / 10000.0;
    }
    [[nodiscard]] double MaximumMs() const noexcept
    {
        return static_cast<double>(maximum100ns) / 10000.0;
    }
};

// Queries the current process's timer resolution. `valid` is false when the API is unavailable.
[[nodiscard]] TimerResolution QueryTimerResolution() noexcept;

// True when the resolution has not been raised above the platform default, i.e. `current` is the
// coarsest the system offers (`current == maximum`). Returns true for an unavailable API so a probe
// failure can never masquerade as a regression. Note: another process can raise the *system-wide*
// resolution, so the acceptance check compares a run's start and end (`SameTimerResolution`) rather
// than relying on this absolute test.
[[nodiscard]] bool IsDefaultTimerResolution(const TimerResolution& resolution) noexcept;

// True when both probes report the same current resolution, or either is unavailable. This is the
// "did this run change the timer resolution?" check.
[[nodiscard]] bool SameTimerResolution(const TimerResolution& before,
                                       const TimerResolution& after) noexcept;
} // namespace pacecar