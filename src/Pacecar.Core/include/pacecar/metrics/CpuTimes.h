#pragma once

// CPU time-sampling primitives: the isolated NT structure, defensive parsing, and the pure
// delta-to-percent math. This header deliberately has no provider, aggregator, PDH, or scheduler
// state so it can be unit-tested with synthetic buffers and never touches hardware.
//
// The design (`docs/design/02-metrics.md`) calls for per-core utilization from
// `NtQuerySystemInformation(SystemProcessorPerformanceInformation)`. That structure is undocumented
// but stable, so it is defined once here (rather than relying on `winternl.h`, whose declaration
// names DpcTime/InterruptTime as `Reserved1[2]`) and parsed defensively.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pacecar::metrics
{
// `SYSTEM_INFORMATION_CLASS.SystemProcessorPerformanceInformation`. Isolated alongside the struct.
inline constexpr std::uint32_t kSystemProcessorPerformanceInformation = 8;

// Layout of one SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION record. Times are cumulative and expressed
// in 100 ns units. `kernelTime` includes idle, DPC, and interrupt time; `idleTime` is the portion of
// `kernelTime` spent idle.
struct ProcessorPerformanceInfo
{
    std::int64_t idleTime = 0;
    std::int64_t kernelTime = 0;
    std::int64_t userTime = 0;
    std::int64_t dpcTime = 0;
    std::int64_t interruptTime = 0;
    std::uint32_t interruptCount = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(ProcessorPerformanceInfo) == 48,
              "SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION is five LARGE_INTEGERs (40) plus a ULONG and "
              "padding (8)");

// Parses a raw NtQuerySystemInformation buffer of per-processor records into `out`. Reads at most
// `min(bufferBytes / sizeof(record), maxRecords)` whole records; a trailing partial record is
// ignored and nothing past `buffer` is ever read. `out` is resized only when it needs to grow.
// Returns the number of records parsed.
std::size_t ParseProcessorPerformance(const void* buffer,
                                      std::size_t bufferBytes,
                                      std::size_t maxRecords,
                                      std::vector<ProcessorPerformanceInfo>& out);

// Cumulative CPU times with the same fields as the NT record, decoupled from the wire struct so
// both the NT and `GetSystemTimes` paths share one delta/percent implementation.
struct CpuTimeCounters
{
    std::uint64_t idle = 0;
    std::uint64_t kernel = 0;
    std::uint64_t user = 0;
    std::uint64_t dpc = 0;
    std::uint64_t interrupt = 0;
};

[[nodiscard]] CpuTimeCounters ToCounters(const ProcessorPerformanceInfo& info) noexcept;

// Sums a set of per-core counters into one system-wide total (the NT path has no total row, so the
// total is derived by summing the logical processors).
[[nodiscard]] CpuTimeCounters SumCounters(const std::vector<CpuTimeCounters>& cores) noexcept;

// Busy percentage between two cumulative samples. `kernel` already includes idle, DPC, and
// interrupt time, so total elapsed time is `kernel + user` and busy time is `total - idle`.
// Returns 0 when no time elapsed or the counters moved backwards (for example a reset/reload).
[[nodiscard]] double ComputeBusyPercent(const CpuTimeCounters& previous,
                                        const CpuTimeCounters& current) noexcept;
} // namespace pacecar::metrics