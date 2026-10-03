#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "pacecar/metrics/PingProvider.h"

#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>
// `IcmpCreateFile` / `IcmpSendEcho` / `IcmpCloseHandle` are declared here, not in `iphlpapi.h`
// (which only declares the legacy IP Helper APIs).
#include <IcmpAPI.h>

#include <array>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "iphlpapi.lib")

namespace pacecar::metrics
{
namespace
{
// `IP_SUCCESS` / `IP_REQ_TIMED_OUT` from `ipexport.h`, repeated here so classification has no header
// dependency (and can be unit-tested in isolation).
constexpr std::uint32_t kIpSuccess = 0;
constexpr std::uint32_t kIpReqTimedOut = 11010;

void CopyName(char (&destination)[kNameCapacity], const char* source) noexcept
{
    if (source == nullptr)
    {
        destination[0] = '\0';
        return;
    }
    const std::size_t length = std::strlen(source);
    const std::size_t count = length < kNameCapacity - 1 ? length : kNameCapacity - 1;
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

// Real `IcmpSendEcho` source. One synchronous call bounded by the caller's timeout; the handle is
// closed on every path.
class IcmpPingSource final : public IPingSource
{
  public:
    bool Send(std::uint32_t address, std::uint32_t timeoutMs, PingRawReply& out) override
    {
        out = PingRawReply{};

        HANDLE handle = ::IcmpCreateFile();
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const char payload[] = "pacecar";
        constexpr std::size_t kReplyBytes = sizeof(ICMP_ECHO_REPLY) + 32 + 8;
        std::array<unsigned char, kReplyBytes> buffer{};

        const DWORD replies = ::IcmpSendEcho(
            handle, static_cast<IPAddr>(address), const_cast<char*>(payload),
            static_cast<WORD>(sizeof(payload) - 1), nullptr, buffer.data(),
            static_cast<DWORD>(buffer.size()), timeoutMs);

        if (replies > 0)
        {
            const auto* reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(buffer.data());
            out.replyCount = replies;
            out.status = reply->Status;
            out.roundTripMs = reply->RoundTripTime;
        }
        else
        {
            out.replyCount = 0;
            out.status = ::GetLastError();
        }

        ::IcmpCloseHandle(handle);
        return true;
    }
};
} // namespace

PingResultKind ClassifyPingReply(const PingRawReply& reply, double& rttMs) noexcept
{
    rttMs = 0.0;

    if (reply.replyCount > 0)
    {
        if (reply.status == kIpSuccess)
        {
            rttMs = static_cast<double>(reply.roundTripMs);
            return PingResultKind::Success;
        }
        if (reply.status == kIpReqTimedOut)
        {
            return PingResultKind::Timeout;
        }
        return PingResultKind::Error;
    }

    // No reply at all: a recorded timeout, or the API returning zero without an error, both mean
    // the echo was not answered. Any other status is an explicit ICMP error.
    if (reply.status == kIpReqTimedOut || reply.status == 0)
    {
        return PingResultKind::Timeout;
    }
    return PingResultKind::Error;
}

bool ParseIpv4(std::string_view text, std::uint32_t& address) noexcept
{
    std::uint32_t octets[4] = {};
    std::size_t partCount = 0;
    std::size_t i = 0;

    while (partCount < 4)
    {
        if (i >= text.size())
        {
            return false;
        }

        std::uint32_t value = 0;
        std::size_t digits = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        {
            value = value * 10u + static_cast<std::uint32_t>(text[i] - '0');
            ++i;
            ++digits;
            if (digits > 3 || value > 255)
            {
                return false;
            }
        }
        if (digits == 0)
        {
            return false;
        }

        octets[partCount++] = value;
        if (partCount == 4)
        {
            break;
        }
        if (i >= text.size() || text[i] != '.')
        {
            return false;
        }
        ++i;
    }

    if (i != text.size())
    {
        return false; // trailing characters after the fourth octet
    }

    address = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    return true;
}

std::unique_ptr<IPingSource> MakeIcmpPingSource()
{
    return std::make_unique<IcmpPingSource>();
}

PingProvider::PingProvider()
    : PingProvider(MakeIcmpPingSource(), "8.8.8.8", kDefaultPingTimeoutMs)
{
}

PingProvider::PingProvider(std::unique_ptr<IPingSource> source,
                           std::string target,
                           std::uint32_t timeoutMs)
    : source_(std::move(source))
{
    timeoutMs_ = timeoutMs < 1 ? 1 : (timeoutMs > kMaxPingTimeoutMs ? kMaxPingTimeoutMs : timeoutMs);
    CopyName(target_, target.c_str());
    targetValid_ = ParseIpv4(target_, address_);
}

PingProvider::~PingProvider() = default;

const char* PingProvider::Name() const noexcept
{
    return "ping";
}

std::uint32_t PingProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Ping);
}

std::chrono::milliseconds PingProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT PingProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!source_ || !targetValid_)
    {
        return E_FAIL;
    }

    PingRawReply reply{};
    if (!source_->Send(address_, timeoutMs_, reply))
    {
        ++consecutiveFailures_;
        snapshot.ping.consecutiveFailures = consecutiveFailures_;
        return E_FAIL;
    }

    double rttMs = 0.0;
    const PingResultKind kind = ClassifyPingReply(reply, rttMs);
    if (kind == PingResultKind::Success)
    {
        consecutiveFailures_ = 0;
        snapshot.ping.rttMs = rttMs;
        snapshot.ping.consecutiveFailures = 0;
        return S_OK;
    }

    // Timeout or ICMP error: keep the last good RTT (do not write 0) and let the aggregator mark the
    // domain unavailable/stale.
    ++consecutiveFailures_;
    snapshot.ping.consecutiveFailures = consecutiveFailures_;
    return E_FAIL;
}

void PingProvider::Reset() noexcept
{
    consecutiveFailures_ = 0;
}
} // namespace pacecar::metrics