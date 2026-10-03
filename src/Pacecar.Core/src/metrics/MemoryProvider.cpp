#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "pacecar/metrics/MemoryProvider.h"

#include <windows.h>

#include <psapi.h>

#include <cstdint>

#pragma comment(lib, "Psapi.lib")

namespace pacecar::metrics
{
namespace
{
// Real source over the two in-box queries. Both are documented, long-stable, and require no
// elevation. `GetPerformanceInfo` reports sizes in pages, so each count is scaled by the page size
// returned in the same call.
class WinMemorySystemSource final : public IMemorySystemSource
{
  public:
    bool ReadPhysical(std::uint64_t& totalBytes, std::uint64_t& availableBytes) override
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!::GlobalMemoryStatusEx(&status))
        {
            return false;
        }
        totalBytes = static_cast<std::uint64_t>(status.ullTotalPhys);
        availableBytes = static_cast<std::uint64_t>(status.ullAvailPhys);
        return true;
    }

    bool ReadPerformance(std::uint64_t& commitBytes,
                         std::uint64_t& commitLimitBytes,
                         std::uint64_t& cacheBytes) override
    {
        PERFORMANCE_INFORMATION info{};
        if (!::GetPerformanceInfo(&info, sizeof(info)))
        {
            return false;
        }
        const auto pageSize = static_cast<std::uint64_t>(info.PageSize);
        commitBytes = static_cast<std::uint64_t>(info.CommitTotal) * pageSize;
        commitLimitBytes = static_cast<std::uint64_t>(info.CommitLimit) * pageSize;
        cacheBytes = static_cast<std::uint64_t>(info.SystemCache) * pageSize;
        return true;
    }
};
} // namespace

std::uint64_t ComputeUsedBytes(std::uint64_t totalBytes, std::uint64_t availableBytes) noexcept
{
    return totalBytes > availableBytes ? totalBytes - availableBytes : 0;
}

double ComputeUsedPercent(std::uint64_t usedBytes, std::uint64_t totalBytes) noexcept
{
    if (totalBytes == 0)
    {
        return 0.0;
    }
    const double used = static_cast<double>(usedBytes);
    const double total = static_cast<double>(totalBytes);
    const double percent = (used / total) * 100.0;
    if (!(percent > 0.0)) // also rejects NaN; a negative is impossible from unsigned inputs
    {
        return 0.0;
    }
    if (percent > 100.0)
    {
        return 100.0;
    }
    return percent;
}

MemoryProvider::MemoryProvider() : MemoryProvider(std::make_unique<WinMemorySystemSource>())
{
}

MemoryProvider::MemoryProvider(std::unique_ptr<IMemorySystemSource> source)
    : source_(std::move(source))
{
}

MemoryProvider::~MemoryProvider() = default;

const char* MemoryProvider::Name() const noexcept
{
    return "memory";
}

std::uint32_t MemoryProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Memory);
}

std::chrono::milliseconds MemoryProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT MemoryProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!source_)
    {
        return E_FAIL;
    }

    std::uint64_t total = 0;
    std::uint64_t available = 0;
    if (!source_->ReadPhysical(total, available))
    {
        // Physical usage is the primary metric; without it the domain has nothing to publish.
        performanceAvailable_ = false;
        return E_FAIL;
    }

    const std::uint64_t used = ComputeUsedBytes(total, available);
    snapshot.memory.totalBytes = total;
    snapshot.memory.availableBytes = available;
    snapshot.memory.usedBytes = used;
    snapshot.memory.usedPercent = ComputeUsedPercent(used, total);

    std::uint64_t commit = 0;
    std::uint64_t commitLimit = 0;
    std::uint64_t cache = 0;
    performanceAvailable_ = source_->ReadPerformance(commit, commitLimit, cache);
    if (performanceAvailable_)
    {
        snapshot.memory.commitBytes = commit;
        snapshot.memory.commitLimitBytes = commitLimit;
        snapshot.memory.cacheBytes = cache;
    }
    else
    {
        // Commit/cache are secondary; report them as zero rather than failing the physical sample.
        snapshot.memory.commitBytes = 0;
        snapshot.memory.commitLimitBytes = 0;
        snapshot.memory.cacheBytes = 0;
    }

    return S_OK;
}

void MemoryProvider::Reset() noexcept
{
    performanceAvailable_ = false;
}
} // namespace pacecar::metrics