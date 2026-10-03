#pragma once

// Metric provider contract.
//
// Every metric source (CPU, memory, GPU, network, disk, ping, frame time, the privileged helper)
// implements `IMetricProvider`. The `Aggregator` knows only this interface, so it can be driven by
// fakes with no hardware, drivers, or elevation - the main testability seam of the project (the
// C++ analogue of the legacy `mockall` design).
//
// Contract:
//   - `Domains()` returns a bitmask of `MetricDomain` values the provider writes.
//   - `Cadence()` is the provider's preferred poll interval. The aggregator polls a provider only
//     when its cadence has elapsed, so different providers run at different rates within one tick.
//   - `Poll()` writes raw values into the snapshot. It returns `S_OK` on success. On failure it may
//     return any failing `HRESULT` or throw; the aggregator catches both and marks the provider's
//     domains stale/unavailable without disturbing the rest of the snapshot.
//   - Providers never touch `MetricStatus` for their top-level domain; the aggregator owns that so
//     availability is reported consistently. Providers may set sub-statuses (for example GPU
//     temperature vs. GPU utilization) where a domain has finer-grained sources.

#include <chrono>
#include <cstdint>

#include <winerror.h>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
// Logical metric domains used to attribute a provider's success/failure to the right part of the
// snapshot. Values are bit flags.
enum class MetricDomain : std::uint32_t
{
    None = 0,
    Cpu = 1u << 0,
    Memory = 1u << 1,
    Gpu = 1u << 2,
    Network = 1u << 3,
    Disk = 1u << 4,
    Ping = 1u << 5,
    FrameTime = 1u << 6,
    Board = 1u << 7,
    Fan = 1u << 8,
    DeepSensors = 1u << 9,
};

[[nodiscard]] inline constexpr bool HasDomain(std::uint32_t mask, MetricDomain domain) noexcept
{
    return (mask & static_cast<std::uint32_t>(domain)) != 0u;
}

class IMetricProvider
{
  public:
    virtual ~IMetricProvider() = default;

    IMetricProvider(const IMetricProvider&) = delete;
    IMetricProvider& operator=(const IMetricProvider&) = delete;
    IMetricProvider(IMetricProvider&&) = delete;
    IMetricProvider& operator=(IMetricProvider&&) = delete;

    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    [[nodiscard]] virtual std::uint32_t Domains() const noexcept = 0;

    [[nodiscard]] virtual std::chrono::milliseconds Cadence() const noexcept = 0;

    // Reads the provider's metrics into `snapshot`. Not `noexcept` on purpose: the aggregator
    // isolates throwing providers with a try/catch.
    virtual HRESULT Poll(MetricsSnapshot& snapshot) = 0;

    // Drops any delta baselines so the next `Poll` starts fresh. Called by the aggregator on
    // (re)start.
    virtual void Reset() noexcept {}

  protected:
    IMetricProvider() = default;
};
} // namespace pacecar::metrics