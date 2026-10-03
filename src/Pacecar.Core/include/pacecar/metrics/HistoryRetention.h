#pragma once

// Retention and downsampling policy for the metric history rings (design refs 05, 09; task T12).
//
// The aggregator keeps one raw ring buffer per *aggregate* series (total CPU, GPU, RAM, network,
// disk, ping, FPS) - never per-core or per-engine samples. At the fastest cadence and the longest
// retention that is 120 min * 4 Hz = 28 800 samples per series; holding every one of them for every
// series would still fit the 15 MB budget, but the design asks for an explicit scheme rather than
// an unbounded ring.
//
// Scheme (documented here and in ADR 0017):
//   1. A series ring keeps at most `kMaxRawHistorySamples` full-rate samples. That is 4096 samples,
//      about 68 minutes at 1 Hz or 17 minutes at 250 ms.
//   2. When the requested retention would exceed the cap, the policy records a `downsampleStride`:
//      the number of raw samples that a long-window History view folds into one bucket. The raw
//      ring still holds the recent window at full resolution (sparklines read its tail); the
//      History window (T14) calls `BucketAverage` to compress the whole retention into the buckets.
//   3. The steady-state cost is therefore `rawCapacity * series * 8 B`, bounded by
//      `kHistoryBudgetBytes` (1 MiB) for every combination of the config's retention and cadence.
//      There is no growth over time: the rings are sized once when the sampler starts.
//
// `BucketAverage` averages whole buckets (a partial trailing bucket is averaged over what it has),
// so a minute-bucket series shows a mean, not a single sampled instant.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pacecar/util/RingBuffer.h"

namespace pacecar::metrics
{
// Number of aggregate series `MetricHistory` maintains (kept in sync with that struct).
inline constexpr std::size_t kHistorySeriesCount = 11;
// Hard cap on full-rate samples retained per series.
inline constexpr std::size_t kMaxRawHistorySamples = 4096;
// Steady-state ceiling for all raw history rings together.
inline constexpr std::size_t kHistoryBudgetBytes = 1024u * 1024u;

struct HistoryRetention
{
    // Capacity to pass to `Aggregator`/`MetricHistory`.
    std::size_t rawCapacity = 0;
    // Full-rate samples per minute at the configured cadence.
    std::size_t samplesPerMinute = 0;
    // Samples the raw, uncapped retention would have wanted.
    std::size_t requestedSamples = 0;
    // Raw samples folded into one long-window bucket when retention exceeds the cap (always >= 1).
    std::size_t downsampleStride = 1;
    // Estimated steady-state bytes for all series at `rawCapacity`.
    std::size_t estimatedBytes = 0;
    // True when the request exceeded the cap and a History view must downsample.
    bool downsampled = false;
};

// Computes the retention policy for a config choice. `retentionMinutes` is clamped to [1, 120] and
// `cadence` to at least 1 ms. Never throws and never allocates.
[[nodiscard]] HistoryRetention PlanHistoryRetention(int retentionMinutes,
                                                    std::chrono::milliseconds cadence) noexcept;

// Averages consecutive runs of `stride` samples (oldest first) into `out` and returns the number of
// buckets written. A partial trailing run is averaged over the samples it contains. Returns 0 for an
// empty source or output, or a `stride` of 0.
[[nodiscard]] std::size_t BucketAverage(const pacecar::RingBuffer<double>& source,
                                        std::size_t stride,
                                        std::span<double> out) noexcept;
} // namespace pacecar::metrics