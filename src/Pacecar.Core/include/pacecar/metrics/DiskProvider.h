#pragma once

// Disk throughput provider: unprivileged PDH `PhysicalDisk` rate counters.
//
// Source (`docs/design/02-metrics.md`): the PDH `PhysicalDisk` counter set's
// `Disk Read Bytes/sec` and `Disk Write Bytes/sec`. PDH computes the rate from two collections
// internally, so the provider keeps one persistent query and discards the first sample. Crucially
// this never opens a physical-drive handle (which can require elevation); PDH is unprivileged.
//
// Selection: `sensors.disk_selection` is a loose string (see `DeviceSelection.h`). "auto"/"all" sums
// every physical disk except the `_Total` instance (falling back to `_Total` when it is the only
// instance); an index selects the nth non-total instance in enumeration order; a name is matched
// case-insensitively against the instance name (for example `0 C:`).
//
// Testability: PDH sits behind `IDiskSource` and the filter/sum assembly is the pure
// `AggregateDiskRates`, so the rate assembly is exercised with injected raw counters and no PDH.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pacecar/metrics/DeviceSelection.h"
#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
// One `PhysicalDisk` instance's formatted rates as read by the source.
struct DiskCounterSample
{
    char instanceName[128] = {};
    double readBytesPerSecond = 0.0;
    double writeBytesPerSecond = 0.0;
    bool readValid = false;
    bool writeValid = false;
};

// Test seam over the PDH query. `IsAvailable` is false when the `PhysicalDisk` counters cannot be
// opened. `Read` returns false only when the collection fails outright; on the very first
// collection PDH has no baseline and the formatted values may be invalid, which `Read` reports by
// returning true with `readValid`/`writeValid` false.
class IDiskSource
{
  public:
    virtual ~IDiskSource() = default;

    [[nodiscard]] virtual bool IsAvailable() = 0;
    virtual bool Read(std::vector<DiskCounterSample>& out) = 0;
};

[[nodiscard]] std::unique_ptr<IDiskSource> MakePdhDiskSource();

// The aggregate selected by one read.
struct DiskRate
{
    char name[kNameCapacity] = {};
    double readBytesPerSecond = 0.0;
    double writeBytesPerSecond = 0.0;
    std::size_t instanceCount = 0;
};

// Filters `samples` by `selection` and sums the valid read/write rates. Never allocates. `out` is
// zeroed first; `out.name` is the instance name for a single match, "All disks" for an aggregate,
// or empty when nothing matched.
void AggregateDiskRates(const std::vector<DiskCounterSample>& samples,
                        const DeviceSelection& selection,
                        DiskRate& out) noexcept;

class DiskProvider final : public IMetricProvider
{
  public:
    // Uses the real PDH `PhysicalDisk` source and the "auto" selection.
    DiskProvider();

    // Test/embedding seam: callers supply the source and selection string.
    DiskProvider(std::unique_ptr<IDiskSource> source, std::string selection);

    ~DiskProvider() override;

    DiskProvider(const DiskProvider&) = delete;
    DiskProvider& operator=(const DiskProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] bool PdhAvailable() const noexcept
    {
        return source_ && source_->IsAvailable();
    }

  private:
    std::unique_ptr<IDiskSource> source_;
    DeviceSelection selection_{};

    std::vector<DiskCounterSample> samples_; // reused across polls
    DiskRate current_{};
    bool firstRead_ = true;
};
} // namespace pacecar::metrics