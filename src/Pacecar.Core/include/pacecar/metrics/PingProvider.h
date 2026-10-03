#pragma once

// Ping / ICMP RTT provider: unprivileged IPv4 `IcmpSendEcho`, matching the legacy app.
//
// Source (`docs/design/02-metrics.md`): `IcmpSendEcho` against the configured target, RTT in
// milliseconds. IPv4 only. A timeout or an ICMP error is surfaced as unavailable (`E_FAIL`) so the
// aggregator marks the ping domain stale; the last good RTT is retained rather than reported as 0.
//
// Bounded blocking: the single synchronous `IcmpSendEcho` call is given an explicit timeout (1 s by
// default, clamped to [1, 5000] ms), so a dead target cannot stall the sampler indefinitely.
//
// Target validation: the config string must be a dotted-decimal IPv4 literal (no DNS), exactly as
// the legacy app accepted. `ParseIpv4` is pure and unit-tested; an invalid target makes the provider
// report unavailable rather than crash.
//
// Testability: the ICMP call sits behind `IPingSource`; the parser and `ClassifyPingReply` are pure,
// so success/timeout/error classification is tested with injected replies and no network I/O.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
inline constexpr std::uint32_t kDefaultPingTimeoutMs = 1000;
inline constexpr std::uint32_t kMaxPingTimeoutMs = 5000;

enum class PingResultKind
{
    Success,
    Timeout,
    Error,
};

// Raw outcome of one `IcmpSendEcho` call. `replyCount` is the API's return value; when it is zero
// the source records `GetLastError()` in `status`.
struct PingRawReply
{
    std::uint32_t replyCount = 0;
    std::uint32_t status = 0;
    std::uint32_t roundTripMs = 0;
};

// Classifies a raw reply:
//   - Success when a reply arrived with `IP_SUCCESS`; `rttMs` is set from `roundTripMs`.
//   - Timeout when the status is `IP_REQ_TIMED_OUT`, or when no reply arrived and no error status
//     was recorded.
//   - Error for any other non-success status (for example host unreachable).
[[nodiscard]] PingResultKind ClassifyPingReply(const PingRawReply& reply, double& rttMs) noexcept;

// Parses a dotted-decimal IPv4 literal. On success, `address` is the host-order value of the
// network-byte-order address (what `IcmpSendEcho` expects in `DestinationAddress` on little-endian
// Windows). Rejects empty input, missing/extra octets, values above 255, and trailing characters.
[[nodiscard]] bool ParseIpv4(std::string_view text, std::uint32_t& address) noexcept;

class IPingSource
{
  public:
    virtual ~IPingSource() = default;

    // Runs one ICMP echo. Returns false only when the ICMP machinery itself failed (for example
    // `IcmpCreateFile`); otherwise `out` is filled and classified by the caller.
    virtual bool Send(std::uint32_t address, std::uint32_t timeoutMs, PingRawReply& out) = 0;
};

[[nodiscard]] std::unique_ptr<IPingSource> MakeIcmpPingSource();

class PingProvider final : public IMetricProvider
{
  public:
    // Uses the real `IcmpSendEcho` source, the default target `8.8.8.8`, and a 1 s timeout.
    PingProvider();

    // Test/embedding seam: callers supply the source, target string, and timeout. `timeoutMs` is
    // clamped to [1, kMaxPingTimeoutMs].
    PingProvider(std::unique_ptr<IPingSource> source,
                 std::string target,
                 std::uint32_t timeoutMs = kDefaultPingTimeoutMs);

    ~PingProvider() override;

    PingProvider(const PingProvider&) = delete;
    PingProvider& operator=(const PingProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] bool TargetValid() const noexcept
    {
        return targetValid_;
    }
    [[nodiscard]] const char* Target() const noexcept
    {
        return target_;
    }
    [[nodiscard]] std::uint32_t TimeoutMs() const noexcept
    {
        return timeoutMs_;
    }
    [[nodiscard]] std::uint32_t ConsecutiveFailures() const noexcept
    {
        return consecutiveFailures_;
    }

  private:
    std::unique_ptr<IPingSource> source_;
    char target_[kNameCapacity] = {};
    std::uint32_t address_ = 0;
    std::uint32_t timeoutMs_ = kDefaultPingTimeoutMs;
    std::uint32_t consecutiveFailures_ = 0;
    bool targetValid_ = false;
};
} // namespace pacecar::metrics