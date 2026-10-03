#pragma once

// The metrics sampling thread owned by the overlay process (design refs 01-architecture.md
// "Threading", 05-performance.md).
//
// `Sampler` builds the provider set from config, runs the `Aggregator` on a dedicated below-normal +
// EcoQoS thread, captures a fixed, allocation-free `DisplayFrame` per tick, and posts
// `WM_APP_METRICS_UPDATED` to the UI window only while the overlay is visible. The UI thread never
// touches provider state or the history rings: it reads an immutable frame pointer.
//
// The process is never blanket-throttled. Only the sampling thread gets EcoQoS, and the process
// sets `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION` so sampling does not raise the platform
// timer resolution. `timeBeginPeriod` is never called.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "pacecar/config/Config.h"
#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/DisplayFrame.h"
#include "pacecar/metrics/GpuPdhProvider.h"

namespace pacecar::overlay
{
// Posted to the overlay window (no parameters) when a new frame is published and the overlay is
// visible. Defined next to the window that consumes it in OverlayWindow.h; re-declared here for
// clarity of the sampler/UI contract.
class Sampler
{
  public:
    Sampler();
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // Builds the providers, sizes history from the retention config, configures QoS, and starts the
    // thread. `wakeWindow` receives `WM_APP_METRICS_UPDATED` while visible; it may be null for a
    // headless run. Running already returns true if a sampler is active.
    bool Start(const pacecar::Config& config, HWND wakeWindow);

    // Signals the thread and joins it. Safe to call more than once or before `Start`.
    void Stop();

    void SetVisible(bool visible) noexcept;
    [[nodiscard]] bool Visible() const noexcept;
    void SetInterval(std::chrono::milliseconds interval) noexcept;
    [[nodiscard]] std::chrono::milliseconds Interval() const noexcept;
    void SetForegroundPid(std::uint32_t pid) noexcept;

    [[nodiscard]] std::shared_ptr<const pacecar::metrics::DisplayFrame> LatestFrame() const noexcept;

    // Convenience for the app's diagnostics and cache writer.
    [[nodiscard]] std::shared_ptr<const pacecar::metrics::MetricsSnapshot> LatestSnapshot() const;

    // Copies the raw history rings for the History window. Thread-safe: the aggregator snapshots the
    // rings under its tick lock. Returns false when the sampler is not running. The caller supplies
    // an `out` sized to at least the planned raw capacity; it is resized to match the live rings.
    bool CopyHistory(pacecar::metrics::MetricHistory& out) const;

    [[nodiscard]] bool Running() const noexcept;

    // One-line human-readable status (provider count, cadence, history capacity, QoS outcomes).
    [[nodiscard]] std::wstring Diagnostics() const;

  private:
    void ThreadMain();
    void TickOnce();
    void PublishFrame(std::shared_ptr<const pacecar::metrics::MetricsSnapshot> snapshot);
    void BuildProviders(const pacecar::Config& config);

    std::unique_ptr<pacecar::metrics::Aggregator> aggregator_;
    std::shared_ptr<pacecar::metrics::GpuPdhProvider> gpuProvider_;
    std::thread thread_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> visible_{false};
    std::atomic<std::int64_t> intervalMs_{1000};

    HWND wakeWindow_ = nullptr;
    std::atomic<std::uint32_t> foregroundPid_{0};

    std::vector<std::shared_ptr<pacecar::metrics::DisplayFrame>> framePool_;
    mutable std::mutex frameMutex_;
    std::shared_ptr<const pacecar::metrics::DisplayFrame> publishedFrame_;

    std::size_t historyCapacity_ = 0;
    std::size_t providerCount_ = 0;
    std::atomic<bool> ecoQosApplied_{false};
    std::atomic<bool> ignoreTimerResolutionApplied_{false};
};
} // namespace pacecar::overlay