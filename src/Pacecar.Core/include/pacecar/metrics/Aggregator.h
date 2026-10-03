#pragma once

// The Aggregator: the sampler-side assembly point for the metrics model.
//
// It owns the provider set and cadence schedule, polls due providers on the sampler thread, isolates
// per-provider failures, applies EMA smoothing to utilization/percentage values, records raw values
// into fixed-capacity history ring buffers, and publishes the latest snapshot.
//
// Publication model:
//   - Snapshots are built in an accumulator buffer and published by copying into a preallocated
//     slot from a small pool, then swapping a `shared_ptr<const MetricsSnapshot>` under a short
//     mutex. Consumers (`LatestSnapshot`) copy the pointer under the same lock and then read it
//     without holding any lock, so the render path never blocks the sampler.
//   - All snapshot slots, EMA state, and ring buffers are sized at construction. The steady-state
//     `Tick`/publish path performs no heap allocation (see the `NoAllocationInSteadyState` test),
//     which is why the pool and history capacities are fixed up front.
//
// The aggregator has no UI or hardware dependency and can be driven headless by fakes calling
// `Tick(nowMs)` directly.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/metrics/MetricsSnapshot.h"
#include "pacecar/util/RingBuffer.h"
#include "pacecar/util/Smoothing.h"

namespace pacecar::metrics
{
// Fixed-capacity raw-value history for every aggregate series the UI graphs. One sample is appended
// per aggregator tick regardless of the owning provider's cadence. Per-core series are intentionally
// not retained at full resolution here; the retention/downsampling strategy for long windows is
// documented in ADR 0009 and implemented by the sampling integration (task T12).
struct MetricHistory
{
    explicit MetricHistory(std::size_t capacity);

    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return capacity_;
    }

    void Push(const MetricsSnapshot& snapshot) noexcept;

    RingBuffer<double> cpuTotalUtilization;
    RingBuffer<double> cpuFrequencyMhz;
    RingBuffer<double> memoryUsedPercent;
    RingBuffer<double> gpuUtilization;
    RingBuffer<double> gpuTemperatureC;
    RingBuffer<double> networkUpBytesPerSecond;
    RingBuffer<double> networkDownBytesPerSecond;
    RingBuffer<double> diskReadBytesPerSecond;
    RingBuffer<double> diskWriteBytesPerSecond;
    RingBuffer<double> pingRttMs;
    RingBuffer<double> fps;

  private:
    std::size_t capacity_;
};

class Aggregator
{
  public:
    // Invoked after a snapshot is published, but only while `Visible()` is true. The UI wires this
    // to `PostMessage(hwnd, WM_APP_METRICS_UPDATED, ...)`.
    using WakeCallback = std::function<void()>;

    explicit Aggregator(std::size_t historyCapacity = 300,
                        std::size_t maxCpuCores = kDefaultMaxCpuCores,
                        std::size_t maxGpuEngines = kDefaultMaxGpuEngines,
                        std::size_t snapshotPoolSize = 4);

    Aggregator(const Aggregator&) = delete;
    Aggregator& operator=(const Aggregator&) = delete;

    // Preallocates provider storage so `AddProvider` during startup does not allocate.
    void ReserveProviders(std::size_t count);

    void AddProvider(std::shared_ptr<IMetricProvider> provider);

    void SetWakeCallback(WakeCallback callback);

    // Runs provider->Reset() on all providers and clears EMA/tick state. Does not clear history.
    void Reset();

    void SetVisible(bool visible) noexcept;
    [[nodiscard]] bool Visible() const noexcept;

    // Runs one aggregation tick at an explicit monotonic time in milliseconds. This is the
    // deterministic entry point for tests; the sampler thread calls the no-argument overload.
    // Returns true when a new snapshot was published.
    bool Tick(std::uint64_t nowMs);
    bool Tick();

    // Returns the most recently published snapshot, or nullptr before the first tick. The returned
    // pointer is immutable and safe to read without holding any lock.
    [[nodiscard]] std::shared_ptr<const MetricsSnapshot> LatestSnapshot() const noexcept;

    [[nodiscard]] const MetricHistory& History() const noexcept
    {
        return history_;
    }

    // Copies the raw history rings into `out` under the tick lock. `out` is resized to match. This is
    // the only safe way for the UI thread to read history (never touch `History()` directly): it is
    // intended for on-demand History-window refreshes, not the render path.
    void CopyHistory(MetricHistory& out) const;

    [[nodiscard]] std::uint64_t TickCount() const noexcept
    {
        return tickIndex_;
    }
    [[nodiscard]] std::uint64_t PublishCount() const noexcept
    {
        return publishCount_;
    }
    // Number of times the wake callback was invoked (only while visible).
    [[nodiscard]] std::uint64_t WakeCount() const noexcept
    {
        return wakeCount_;
    }
    [[nodiscard]] std::size_t ProviderCount() const noexcept
    {
        return providers_.size();
    }
    [[nodiscard]] std::uint64_t ProviderPollCount(std::size_t index) const;
    [[nodiscard]] std::uint64_t ProviderFailureCount(std::size_t index) const;

  private:
    static constexpr std::uint64_t kNeverPolled = std::numeric_limits<std::uint64_t>::max();

    struct ProviderEntry
    {
        std::shared_ptr<IMetricProvider> provider;
        std::uint32_t domains = 0;
        std::uint64_t lastPollMs = kNeverPolled;
        std::uint64_t pollCount = 0;
        std::uint64_t failureCount = 0;
    };

    std::shared_ptr<MetricsSnapshot> AcquireFreeSnapshotSlot();
    void ApplyDomainStatus(MetricsSnapshot& snapshot,
                           std::uint32_t domains,
                           bool success,
                           std::uint64_t tick) noexcept;
    void UpdateDerived(MetricsSnapshot& snapshot);
    bool Publish(const MetricsSnapshot& snapshot, std::uint64_t nowMs);

    std::size_t maxCpuCores_;
    std::size_t maxGpuEngines_;

    std::vector<std::shared_ptr<MetricsSnapshot>> pool_;
    std::shared_ptr<MetricsSnapshot> current_;
    std::shared_ptr<const MetricsSnapshot> published_;

    std::vector<ProviderEntry> providers_;

    MetricHistory history_;
    Ema cpuTotalEma_;
    std::vector<Ema> cpuCoreEmas_;
    Ema memoryUsedEma_;
    Ema gpuTotalEma_;
    std::vector<Ema> gpuEngineEmas_;

    mutable std::mutex mutex_;
    mutable std::mutex tickMutex_;
    std::function<void()> wake_;
    bool wakeSet_ = false;
    std::atomic<bool> visible_{false};

    std::uint64_t tickIndex_ = 0;
    std::uint64_t publishCount_ = 0;
    std::uint64_t publishSequence_ = 0;
    std::uint64_t wakeCount_ = 0;
    std::chrono::steady_clock::time_point startTime_{};
};
} // namespace pacecar::metrics