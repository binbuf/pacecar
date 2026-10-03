#pragma once

// GPU PDH "GPU Engine" provider: the cross-vendor, unprivileged GPU baseline.
//
// Source (`docs/design/02-metrics.md`): the undocumented PDH "GPU Engine" counter set, counters
// `Utilization Percentage` and `Running Time`. Each instance name encodes
//   pid_<n>_luid_0x<high>_0x<low>_phys_<n>_eng_<n>_engtype_<type>
// and `type` may contain spaces (for example `high priority 3d`), so it is parsed defensively as the
// remainder of the string. The counter set is effectively undocumented and may change; parsing is
// unit-tested against real and malformed names, and a missing counter set marks PDH unavailable so
// the provider falls back to D3DKMT.
//
// Aggregation rule (the one explicitly documented choice): instances are **never summed**. For each
// normalized engine type the reported value is the maximum over the matching instances, and the
// whole-GPU value is the `3d` type when present, otherwise the maximum across engine types. Because
// every term is a single-instance [0, 100] value, the aggregate cannot exceed 100 - unlike a naive
// sum of the overlapping engine instances.
//
// Scope: "all processes" (default) or "selected PID only" (`SetTargetPid`). The selected PID is not
// a config key; the sampler (T12) sets it from the foreground process.
//
// Testability: the PDH query sits behind `IGpuEngineSource`, adapter enumeration behind
// `IGpuAdapterEnumerator`, and the parser/aggregation are pure functions. Adapter selection is
// cached once at construction and on a new provider (config change).

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pacecar/metrics/GpuD3dKmtProvider.h"
#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
// One parsed `GPU Engine` instance. `engineType` is normalized (lowercase; interior whitespace runs
// become single underscores) so `high priority 3d` and `high priority 3d` aggregate together and
// `graphics_1` stays distinct.
struct GpuEngineInstance
{
    std::uint32_t pid = 0;
    GpuLuid luid{};
    std::uint32_t physicalAdapter = 0;
    std::uint32_t engine = 0;
    char engineType[kEngineTypeCapacity] = {};
};

// Parses one instance name. Handles the trailing `#<n>` PDH duplicate-index suffix and tolerates an
// optional surrounding `\GPU Engine(...)` wrapper. Returns false (leaving `out` unspecified) for
// any malformed name; `engineType` is normalized as described above.
[[nodiscard]] bool ParseGpuEngineInstance(std::string_view instanceName,
                                          GpuEngineInstance& out) noexcept;

// One instance's utilization, already filtered to the chosen adapter/PID scope.
struct GpuEngineReading
{
    GpuEngineInstance instance{};
    double utilizationPercent = 0.0;
};

// Aggregates readings by normalized engine type using the documented maximum rule, writing
// `GpuEngineMetrics` entries (name + percent) into `out`. `out` is cleared first; entries are
// ordered by first appearance.
void AggregateGpuEnginesByType(const std::vector<GpuEngineReading>& readings,
                               std::vector<GpuEngineMetrics>& out);

// The documented whole-GPU value: the `3d` engine type when present, else the maximum across all
// engine types, else 0. Never exceeds 100 because every input is a maximum of [0, 100] values.
[[nodiscard]] double PrimaryGpuPercent(const std::vector<GpuEngineMetrics>& engines) noexcept;

// Raw sample of the `GPU Engine` counter set for one instance, as read by the source. The source is
// responsible for resolving `utilizationPercent` (using PDH's own filter, falling back to a
// running-time delta), so the provider only parses and aggregates.
struct GpuEngineCounterSample
{
    char instanceName[128] = {};
    double utilizationPercent = 0.0;
    bool utilizationValid = false;
};

// Test seam over the PDH query. `IsAvailable` is the capability check: false when the counter set is
// absent or cannot be read.
class IGpuEngineSource
{
  public:
    virtual ~IGpuEngineSource() = default;

    [[nodiscard]] virtual bool IsAvailable() = 0;
    virtual bool Read(std::vector<GpuEngineCounterSample>& out) = 0;
};

[[nodiscard]] std::unique_ptr<IGpuEngineSource> MakePdhEngineSource();

// The single GPU provider T12 adds. It owns the PDH engine source and, when the counter set is
// absent (or later becomes unreadable), delegates to a D3DKMT fallback. This is the "fallback
// selection" required by the task: capability is decided once at construction and recorded in
// `ActiveBackend()`.
class GpuPdhProvider final : public IMetricProvider
{
  public:
    enum class Backend
    {
        Unavailable,
        Pdh,
        D3dKmt,
    };

    // Uses the real PDH source, DXGI enumeration, D3DKMT fallback, and QPC clock.
    GpuPdhProvider();

    // Test/embedding seam: callers supply every source. `fallbackSource`/`fallbackClock` may be
    // null to test the "PDH only" or "PDH unavailable and no fallback" paths.
    GpuPdhProvider(std::unique_ptr<IGpuEngineSource> engineSource,
                   std::unique_ptr<IGpuAdapterEnumerator> adapters,
                   std::unique_ptr<IGpuD3dKmtSource> fallbackSource,
                   std::unique_ptr<IGpuElapsedClock> fallbackClock,
                   std::string selection);

    ~GpuPdhProvider() override;

    GpuPdhProvider(const GpuPdhProvider&) = delete;
    GpuPdhProvider& operator=(const GpuPdhProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    // 0 means "all processes system-wide"; any other value filters to that PID.
    void SetTargetPid(std::uint32_t pid) noexcept
    {
        targetPid_ = pid;
    }
    [[nodiscard]] std::uint32_t TargetPid() const noexcept
    {
        return targetPid_;
    }
    [[nodiscard]] bool PdhAvailable() const noexcept
    {
        return pdhAvailable_;
    }
    [[nodiscard]] Backend ActiveBackend() const noexcept
    {
        return backend_;
    }
    [[nodiscard]] const char* SelectedAdapterName() const noexcept
    {
        return selected_.name;
    }

  private:
    void EnumerateAndSelect(const std::string& selectionText);
    HRESULT PollPdh(MetricsSnapshot& snapshot);
    HRESULT PollFallback(MetricsSnapshot& snapshot);

    std::unique_ptr<IGpuEngineSource> engineSource_;
    std::unique_ptr<IGpuAdapterEnumerator> adapters_;
    std::unique_ptr<GpuD3dKmtProvider> fallback_;
    std::vector<GpuAdapterInfo> adapterList_;
    GpuAdapterInfo selected_{};
    bool pdhAvailable_ = false;
    bool pdhFirstRead_ = true;
    std::uint32_t targetPid_ = 0;
    Backend backend_ = Backend::Unavailable;

    // Scratch buffers reused across polls so the steady state does not allocate.
    std::vector<GpuEngineCounterSample> samples_;
    std::vector<GpuEngineReading> readings_;
    std::vector<GpuEngineMetrics> aggregated_;
};
} // namespace pacecar::metrics