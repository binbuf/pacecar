#pragma once

// `IMetricProvider` that turns the helper's streamed present events into the `frame` domain of the
// snapshot (task T17). The helper owns the ETW session (never the UI thread and never this process);
// this provider only processes the decoded events on the sampler thread.
//
// It shares the `SensorHelperClient` with `SensorHelperProvider`: both pump the same pipe, and a
// second `Pump` in a tick is a cheap no-op because there are no new bytes. Availability of the
// frame domain is independent of the deep-sensor domain, so the capture can run with deep sensors
// off (the sampler enables the client whenever either feature is requested).

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "pacecar/metrics/FrameTimeProcessor.h"
#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/metrics/SensorHelperClient.h"

namespace pacecar::metrics
{
class FrameTimeProvider final : public IMetricProvider
{
  public:
    using Clock = std::function<std::uint64_t()>;

    explicit FrameTimeProvider(std::shared_ptr<SensorHelperClient> client);
    FrameTimeProvider(std::shared_ptr<SensorHelperClient> client, Clock clock);

    ~FrameTimeProvider() override = default;

    FrameTimeProvider(const FrameTimeProvider&) = delete;
    FrameTimeProvider& operator=(const FrameTimeProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] FrameCaptureState CaptureState() const noexcept;

    // The last computed FPS (0 when not capturing), for the header/status text.
    [[nodiscard]] double LastFps() const noexcept;

    // Human-readable status for the UI: the active state line, or an explanation when the capture
    // has not produced frames yet.
    [[nodiscard]] std::wstring StatusLine() const;

  private:
    std::shared_ptr<SensorHelperClient> client_;
    Clock clock_;
    FrameTimeProcessor processor_;
    std::vector<ipc::FrameEventPayload> eventScratch_;
    mutable std::mutex mutex_;
    FrameCaptureState state_ = FrameCaptureState::NotCapturing;
    double lastFps_ = 0.0;
};
} // namespace pacecar::metrics