#pragma once

// The immutable unit the UI actually renders: one published snapshot plus a small, fixed-length
// tail of each history series for the tile sparklines.
//
// The aggregator publishes snapshots from the sampler thread; ring buffers are also written there.
// Reading a ring from the UI thread while the sampler pushes would be a data race, so the sampler
// captures the display-relevant tail into a `DisplayFrame` at publish time and hands the UI an
// immutable `shared_ptr<const DisplayFrame>`. The frame copy is allocation-free (fixed arrays), and
// a frame's `fingerprint` is the snapshot fingerprint the render gate compares.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
// Sparkline tail length for the tile visualizations. Matches `overlay::kSparklineCapacity`.
inline constexpr std::size_t kDisplaySparklineSamples = 60;

struct MetricSparkline
{
    float samples[kDisplaySparklineSamples] = {};
    std::size_t count = 0;

    [[nodiscard]] bool Empty() const noexcept
    {
        return count == 0;
    }
};

struct DisplayFrame
{
    std::shared_ptr<const MetricsSnapshot> snapshot{};
    std::uint64_t fingerprint = 0;
    MetricSparkline cpu{};
    MetricSparkline memory{};
    MetricSparkline gpu{};
    MetricSparkline network{};
    MetricSparkline disk{};
    MetricSparkline ping{};
    // FPS tail (task T17). Populated only while a capture is active; the FPS tile is drawn only
    // then, so an empty tail is never shown.
    MetricSparkline fps{};
};

// Copies the newest up to `kDisplaySparklineSamples` samples of each history series (oldest first)
// into `out`. Never allocates. `out.count` is 0 when the series is empty.
void CaptureSparkline(const pacecar::RingBuffer<double>& series, MetricSparkline& out) noexcept;

// Builds a frame from a snapshot and the sampler's history. `snapshot` may be null.
[[nodiscard]] DisplayFrame BuildDisplayFrame(std::shared_ptr<const MetricsSnapshot> snapshot,
                                             const MetricHistory& history) noexcept;
} // namespace pacecar::metrics