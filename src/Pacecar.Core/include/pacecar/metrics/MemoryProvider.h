#pragma once

// Memory metric provider: physical used/total, commit charge, and system cache.
//
// Sources (both in-box and require no elevation, per `docs/design/02-metrics.md`):
//   - Physical usage: `GlobalMemoryStatusEx` gives `ullTotalPhys` and `ullAvailPhys`. "Used" is
//     derived as `total - available`, so the reported percentage is always computed from the same
//     two numbers the snapshot displays.
//   - Commit charge / cache: `GetPerformanceInfo` gives committed bytes, the commit limit, and
//     `SystemCache`. These are reported distinctly from physical usage; they are NOT part of the
//     physical percentage.
//
// Semantics caveat (documented here and in the snapshot): `GlobalMemoryStatusEx`'s "available"
// includes standby and cached pages, so "used" is not the same as Task Manager's "In use". The UI
// labels this value "in use" and treats "available" as including standby/cached memory.
//
// Unlike the CPU provider this metric needs no delta: the first `Poll` after start already returns
// a valid sample (no `E_PENDING`).
//
// Testability: the OS queries sit behind `IMemorySystemSource` and the percentage/clamping math is
// pure (`ComputeUsedBytes` / `ComputeUsedPercent`), so both can be exercised without hardware.

#include <chrono>
#include <cstdint>
#include <memory>

#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
// Raw byte values read from the OS, before any derivation. Kept separate from the snapshot so the
// pure math can be tested with injected values, including impossible ones (available > total).
struct MemoryRawSample
{
    std::uint64_t totalBytes = 0;
    std::uint64_t availableBytes = 0;
    std::uint64_t commitBytes = 0;
    std::uint64_t commitLimitBytes = 0;
    std::uint64_t cacheBytes = 0;
};

// Abstracts the two unprivileged OS memory queries so the provider can be driven by fakes in tests.
class IMemorySystemSource
{
  public:
    virtual ~IMemorySystemSource() = default;

    // `GlobalMemoryStatusEx`: total and available physical bytes. `availableBytes` includes
    // standby/cached memory. Returns false when the API fails.
    virtual bool ReadPhysical(std::uint64_t& totalBytes, std::uint64_t& availableBytes) = 0;

    // `GetPerformanceInfo`: commit charge, commit limit, and system cache, all converted to bytes
    // from the API's page counts. Returns false when the API fails.
    virtual bool ReadPerformance(std::uint64_t& commitBytes,
                                 std::uint64_t& commitLimitBytes,
                                 std::uint64_t& cacheBytes) = 0;
};

// Used physical bytes derived from the same total/available the snapshot shows. Clamps to zero when
// `availableBytes >= totalBytes` (degenerate or inconsistent input) so the result can never
// underflow or go negative.
[[nodiscard]] std::uint64_t ComputeUsedBytes(std::uint64_t totalBytes,
                                             std::uint64_t availableBytes) noexcept;

// `usedBytes / totalBytes` as a percentage in [0, 100]. Returns 0 when `totalBytes` is 0 (no
// divide-by-zero) and clamps values above 100 when the inputs are inconsistent.
[[nodiscard]] double ComputeUsedPercent(std::uint64_t usedBytes, std::uint64_t totalBytes) noexcept;

class MemoryProvider final : public IMetricProvider
{
  public:
    // Uses the real `GlobalMemoryStatusEx` / `GetPerformanceInfo` source.
    MemoryProvider();

    // Test/embedding seam: callers supply the raw-value source.
    explicit MemoryProvider(std::unique_ptr<IMemorySystemSource> source);

    ~MemoryProvider() override;

    MemoryProvider(const MemoryProvider&) = delete;
    MemoryProvider& operator=(const MemoryProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    // True when the last `Poll` read commit/cache from `GetPerformanceInfo`. Exposed for
    // diagnostics; the physical values are still valid when it is false.
    [[nodiscard]] bool PerformanceAvailable() const noexcept
    {
        return performanceAvailable_;
    }

  private:
    std::unique_ptr<IMemorySystemSource> source_;
    bool performanceAvailable_ = false;
};
} // namespace pacecar::metrics