#pragma once

// Named-pipe client for the optional elevated `Pacecar.Sensors` helper (design refs
// 01-architecture.md "Process model", 06-security-distribution.md).
//
// The client is deliberately *pump-based* rather than owning a reader thread: the sampler calls
// `Pump(nowMs)` once per second (the helper's cadence), which
//   - attempts a non-blocking connect when the pipe is down and the reconnect backoff has elapsed,
//   - drains any complete IPC messages from the pipe,
//   - and updates a small store of the newest sensor readings.
// A caller then maps the store into a `MetricsSnapshot` with `ApplyTo` (or clears the helper-owned
// statuses with `MarkUnavailable`). Keeping the state machine deterministic makes reconnect/backoff
// and unavailable transitions unit-testable without threads or hardware.
//
// The client treats all helper data as untrusted: every frame is validated by `IpcProtocol.h`
// (magic/version/kind/length) and any malformed, short, or oversized frame drops the connection
// rather than risking an out-of-bounds read. It never calls into the UI or hardware.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "pacecar/metrics/FrameTimeProcessor.h"
#include "pacecar/metrics/IpcProtocol.h"
#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
enum class HelperState
{
    Disconnected,
    Connected,
};

class SensorHelperClient
{
  public:
    // Reconnect backoff, visible for diagnostics and tests.
    static constexpr std::uint64_t kInitialBackoffMs = 1000;
    static constexpr std::uint64_t kMaxBackoffMs = 30000;

    // A pipe name unique to the user SID, e.g. `\\.\pipe\Pacecar.Sensors.S-1-5-21-...`. An empty
    // suffix yields the unsuffixed `\\.\pipe\Pacecar.Sensors`.
    [[nodiscard]] static std::wstring DefaultPipeName(const std::wstring& userSid);

    explicit SensorHelperClient(std::wstring pipeName);
    ~SensorHelperClient();

    SensorHelperClient(const SensorHelperClient&) = delete;
    SensorHelperClient& operator=(const SensorHelperClient&) = delete;

    // Advances the connection state machine at monotonic time `nowMs`. Returns true when the pipe
    // is connected afterwards.
    bool Pump(std::uint64_t nowMs);

    // Enables/disables the helper path without recreating the client. While disabled, `Pump` never
    // connects and any live connection is torn down, so the deep-sensor domain stays unavailable.
    // Re-enabling schedules an immediate connect attempt. Defaults to enabled so a client created
    // directly keeps its pre-existing behavior.
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool Enabled() const noexcept;

    // Tears the connection down (used at shutdown).
    void Disconnect() noexcept;

    [[nodiscard]] HelperState State() const noexcept;
    [[nodiscard]] bool Connected() const noexcept;

    // True once at least one valid snapshot has been decoded for the current connection.
    [[nodiscard]] bool HasFreshData() const noexcept;

    // Monotonic time of the next reconnect attempt; 0 before the first attempt.
    [[nodiscard]] std::uint64_t NextAttemptMs() const noexcept;

    // Valid snapshots decoded since construction (diagnostics/tests).
    [[nodiscard]] std::uint64_t SnapshotCount() const noexcept;

    // The capability bitmask from the helper's HelloAck (0 when unknown).
    [[nodiscard]] std::uint32_t Capabilities() const noexcept;

    // ---- Frame-time capture (task T17) --------------------------------------------------------
    // Requests/stop an opt-in ETW capture for `pid`. Thread-safe: the desired state is latched and
    // sent to the helper on the next `Pump` (and resent after every reconnect, because a fresh
    // helper starts with no capture). Calling with `enabled=false` also clears the frame store.
    void SetCaptureTarget(std::uint32_t pid, bool enabled);

    [[nodiscard]] FrameCaptureState CaptureState() const noexcept;
    [[nodiscard]] std::uint32_t CaptureTargetPid() const noexcept;
    [[nodiscard]] bool CaptureRequested() const noexcept;

    // True once at least one frame-time stats message has been decoded for the current connection.
    [[nodiscard]] bool HasFreshFrameData() const noexcept;
    [[nodiscard]] std::uint64_t FrameStatsCount() const noexcept;

    // Moves all present events accumulated since the last call into `events` (swapping the internal
// buffer) and reports the capture metadata. Returns the accumulated events even when several stats
// messages arrived between polls, so no presents are dropped.
    void TakeFrameData(std::vector<ipc::FrameEventPayload>& events,
                       FrameCaptureState& state,
                       std::uint32_t& targetPid,
                       std::uint64_t& qpcFrequency,
                       std::uint64_t& timestampMs);

    // Maps the latest readings into the snapshot. Only touches the fields the helper owns; leaves
    // the unprivileged baseline (for example the ACPI CPU temperature) intact when the helper has
    // no reading for it.
    void ApplyTo(MetricsSnapshot& snapshot) const;

    // Clears the helper-owned statuses when the helper is absent/disconnected so a previously
    // available value cannot survive in a reused snapshot slot.
    void MarkUnavailable(MetricsSnapshot& snapshot) const;

    // One-line status for diagnostics ("connected", "unavailable", ...).
    [[nodiscard]] std::wstring Status() const;

  private:
    // All three run with `mutex_` held.
    bool TryConnectInternal();
    bool DrainReceiveBuffer();
    bool ParseFrames();
    bool HandleMessage(const ipc::DecodedMessage& message);
    void SendHello();
    void SendPendingCaptureCommand();
    bool SendCaptureCommand(std::uint32_t command, std::uint32_t pid);
    void ClosePipeInternal();
    void ScheduleReconnectInternal(std::uint64_t nowMs) noexcept;
    bool ReadBytesInternal(std::size_t capacity);

    std::wstring pipeName_;
    void* pipe_ = nullptr;
    bool enabled_ = true;
    HelperState state_ = HelperState::Disconnected;
    std::uint64_t nextAttemptMs_ = 0;
    std::uint64_t backoffMs_ = kInitialBackoffMs;
    std::uint64_t snapshotCount_ = 0;
    std::uint32_t capabilities_ = 0;
    std::uint32_t helloSequence_ = 0;
    mutable bool haveData_ = false;

    std::vector<std::uint8_t> rx_;
    std::vector<ipc::SensorReading> readings_;

    // Frame-time capture state (task T17).
    bool captureEnabled_ = false;
    bool capturePending_ = false;
    std::uint32_t capturePid_ = 0;
    std::uint32_t commandSequence_ = 0;
    FrameCaptureState frameState_ = FrameCaptureState::NotCapturing;
    std::uint32_t frameTargetPid_ = 0;
    std::uint64_t frameQpcFrequency_ = 0;
    std::uint64_t frameTimestampMs_ = 0;
    std::vector<ipc::FrameEventPayload> frameEvents_;
    bool haveFrameData_ = false;
    std::uint64_t frameStatsCount_ = 0;

    mutable std::mutex mutex_;
};
} // namespace pacecar::metrics