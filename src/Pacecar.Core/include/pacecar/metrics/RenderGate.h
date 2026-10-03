#pragma once

// Render-on-change gate.
//
// The aggregator wakes the UI (`WM_APP_METRICS_UPDATED`) on every publish; this gate decides
// whether that wake should actually cost a paint. It suppresses a repaint when the display
// fingerprint is unchanged and, independently, when the configured minimum interval has not yet
// elapsed since the last paint - so the overlay never repaints more than once per configured
// interval and never at all while the values are steady (design ref 05-performance.md).
//
// The gate is pure and headless: tests drive it with real snapshots from an `Aggregator` and count
// paint callbacks. Time is an explicit monotonic millisecond argument, never `steady_clock`, so
// scenarios are deterministic. The class is UI-thread-affine and needs no locking.

#include <chrono>
#include <cstdint>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
class RenderGate
{
  public:
    // `minInterval` is clamped to a 250 ms floor, matching the config's fastest refresh rate.
    explicit RenderGate(std::chrono::milliseconds minInterval =
                            std::chrono::milliseconds(250)) noexcept;

    // Returns true when the caller should repaint `snapshot` at monotonic time `nowMs`. A change is
    // only reported once per interval; a suppressed change is remembered so it paints on the next
    // eligible wake.
    [[nodiscard]] bool ShouldRepaint(const MetricsSnapshot& snapshot, std::uint64_t nowMs) noexcept;
    [[nodiscard]] bool ShouldRepaintFingerprint(std::uint64_t fingerprint,
                                                std::uint64_t nowMs) noexcept;

    // Forgets the last fingerprint and paint time (next call always paints). Keep the counters.
    void Reset() noexcept;

    [[nodiscard]] std::uint64_t Fingerprint() const noexcept
    {
        return lastFingerprint_;
    }
    [[nodiscard]] std::uint64_t PaintCount() const noexcept
    {
        return paintCount_;
    }
    [[nodiscard]] std::uint64_t SkipCount() const noexcept
    {
        return skipCount_;
    }
    [[nodiscard]] std::chrono::milliseconds Interval() const noexcept
    {
        return minInterval_;
    }

  private:
    std::chrono::milliseconds minInterval_;
    std::uint64_t lastFingerprint_ = 0;
    std::uint64_t lastPaintMs_ = 0;
    std::uint64_t paintCount_ = 0;
    std::uint64_t skipCount_ = 0;
    bool hasPainted_ = false;
};
} // namespace pacecar::metrics