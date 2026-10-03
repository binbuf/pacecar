#include "pacecar/metrics/CpuTimes.h"

#include <algorithm>

namespace pacecar::metrics
{
std::size_t ParseProcessorPerformance(const void* buffer,
                                      std::size_t bufferBytes,
                                      std::size_t maxRecords,
                                      std::vector<ProcessorPerformanceInfo>& out)
{
    if (buffer == nullptr)
    {
        return 0;
    }

    constexpr std::size_t kRecordSize = sizeof(ProcessorPerformanceInfo);
    std::size_t available = bufferBytes / kRecordSize;
    available = std::min(available, maxRecords);

    out.resize(available);
    if (available != 0)
    {
        const auto* records = static_cast<const ProcessorPerformanceInfo*>(buffer);
        std::copy(records, records + available, out.begin());
    }
    return available;
}

CpuTimeCounters ToCounters(const ProcessorPerformanceInfo& info) noexcept
{
    CpuTimeCounters counters;
    counters.idle = static_cast<std::uint64_t>(info.idleTime);
    counters.kernel = static_cast<std::uint64_t>(info.kernelTime);
    counters.user = static_cast<std::uint64_t>(info.userTime);
    counters.dpc = static_cast<std::uint64_t>(info.dpcTime);
    counters.interrupt = static_cast<std::uint64_t>(info.interruptTime);
    return counters;
}

CpuTimeCounters SumCounters(const std::vector<CpuTimeCounters>& cores) noexcept
{
    CpuTimeCounters total;
    for (const CpuTimeCounters& core : cores)
    {
        total.idle += core.idle;
        total.kernel += core.kernel;
        total.user += core.user;
        total.dpc += core.dpc;
        total.interrupt += core.interrupt;
    }
    return total;
}

double ComputeBusyPercent(const CpuTimeCounters& previous, const CpuTimeCounters& current) noexcept
{
    if (current.kernel < previous.kernel || current.user < previous.user ||
        current.idle < previous.idle)
    {
        return 0.0;
    }

    const std::uint64_t totalDelta = (current.kernel - previous.kernel) + (current.user - previous.user);
    if (totalDelta == 0)
    {
        return 0.0;
    }

    const std::uint64_t idleDelta = current.idle - previous.idle;
    if (idleDelta >= totalDelta)
    {
        return 0.0;
    }

    const double busy = static_cast<double>(totalDelta - idleDelta);
    double percent = busy * 100.0 / static_cast<double>(totalDelta);
    if (percent < 0.0)
    {
        percent = 0.0;
    }
    else if (percent > 100.0)
    {
        percent = 100.0;
    }
    return percent;
}
} // namespace pacecar::metrics