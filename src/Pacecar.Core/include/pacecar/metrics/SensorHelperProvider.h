#pragma once

// Thin `IMetricProvider` adapter over `SensorHelperClient` (design refs 02-metrics.md,
// 01-architecture.md "Data flow").
//
// The aggregator polls this once per second. `Poll` advances the client (connect/backoff/read) and
// either applies the latest readings or clears the helper-owned statuses. The overall `DeepSensors`
// domain availability is owned by the aggregator: `Poll` returns `S_OK` only when the helper is
// connected and has streamed a snapshot, so a crash/disconnect downgrades the deep-sensor domain
// without disturbing any other provider.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/metrics/SensorHelperClient.h"

namespace pacecar::metrics
{
class SensorHelperProvider final : public IMetricProvider
{
  public:
    using Clock = std::function<std::uint64_t()>;

    // Uses a steady monotonic clock (milliseconds) by default.
    explicit SensorHelperProvider(std::shared_ptr<SensorHelperClient> client);

    // Test seam: inject the monotonic clock the client is pumped with.
    SensorHelperProvider(std::shared_ptr<SensorHelperClient> client, Clock clock);

    ~SensorHelperProvider() override = default;

    SensorHelperProvider(const SensorHelperProvider&) = delete;
    SensorHelperProvider& operator=(const SensorHelperProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] const SensorHelperClient& Client() const noexcept
    {
        return *client_;
    }

    // Controls whether the provider actually writes deep-sensor readings. The sampler can keep the
    // helper connection alive for frame-time capture (T17) while deep sensors are off; in that case
    // the provider still pumps the pipe but reports the deep-sensor domain unavailable.
    void SetSensorsEnabled(bool enabled) noexcept;
    [[nodiscard]] bool SensorsEnabled() const noexcept;

  private:
    std::shared_ptr<SensorHelperClient> client_;
    Clock clock_;
    bool sensorsEnabled_ = true;
};
} // namespace pacecar::metrics