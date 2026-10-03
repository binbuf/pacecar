#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "pacecar/metrics/PingProvider.h"

namespace
{
using pacecar::metrics::ClassifyPingReply;
using pacecar::metrics::IPingSource;
using pacecar::metrics::kDefaultPingTimeoutMs;
using pacecar::metrics::kMaxPingTimeoutMs;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::ParseIpv4;
using pacecar::metrics::PingProvider;
using pacecar::metrics::PingRawReply;
using pacecar::metrics::PingResultKind;

constexpr std::uint32_t kIpSuccess = 0;
constexpr std::uint32_t kIpReqTimedOut = 11010;
constexpr std::uint32_t kIpDestHostUnreachable = 11003;

class FakePingSource final : public IPingSource
{
  public:
    bool Send(std::uint32_t address, std::uint32_t timeoutMs, PingRawReply& out) override
    {
        ++sendCount;
        lastAddress = address;
        lastTimeoutMs = timeoutMs;
        if (!machineryOk)
        {
            return false;
        }
        out = reply;
        return true;
    }

    bool machineryOk = true;
    PingRawReply reply{};
    int sendCount = 0;
    std::uint32_t lastAddress = 0;
    std::uint32_t lastTimeoutMs = 0;
};

// --- target validation -----------------------------------------------------

TEST(PingTargetParsing, AcceptsDottedDecimalIpv4Literals)
{
    std::uint32_t address = 0;
    ASSERT_TRUE(ParseIpv4("8.8.8.8", address));
    EXPECT_EQ(address, 0x08080808u);
    ASSERT_TRUE(ParseIpv4("127.0.0.1", address));
    EXPECT_EQ(address, 0x0100007Fu);
    ASSERT_TRUE(ParseIpv4("192.168.1.10", address));
    EXPECT_EQ(address, 0x0A01A8C0u);
    ASSERT_TRUE(ParseIpv4("0.0.0.0", address));
    EXPECT_EQ(address, 0u);
    ASSERT_TRUE(ParseIpv4("255.255.255.255", address));
    EXPECT_EQ(address, 0xFFFFFFFFu);
}

TEST(PingTargetParsing, RejectsMalformedTargets)
{
    std::uint32_t address = 0;
    EXPECT_FALSE(ParseIpv4("", address));
    EXPECT_FALSE(ParseIpv4("8.8.8", address));
    EXPECT_FALSE(ParseIpv4("8.8.8.8.9", address));
    EXPECT_FALSE(ParseIpv4("256.1.1.1", address));
    EXPECT_FALSE(ParseIpv4("8.8.8.", address));
    EXPECT_FALSE(ParseIpv4(".8.8.8", address));
    EXPECT_FALSE(ParseIpv4("8.8.8.8 ", address));
    EXPECT_FALSE(ParseIpv4("example.com", address));
    EXPECT_FALSE(ParseIpv4("-1.2.3.4", address));
}

// --- classification (no real network I/O) ----------------------------------

TEST(PingClassification, SuccessTimeoutAndErrorAreDistinct)
{
    double rtt = 0.0;

    EXPECT_EQ(ClassifyPingReply(PingRawReply{1, kIpSuccess, 23}, rtt), PingResultKind::Success);
    EXPECT_DOUBLE_EQ(rtt, 23.0);

    rtt = 123.0;
    EXPECT_EQ(ClassifyPingReply(PingRawReply{0, kIpReqTimedOut, 0}, rtt), PingResultKind::Timeout);
    EXPECT_DOUBLE_EQ(rtt, 0.0); // a timeout clears the RTT rather than reporting a stale value

    EXPECT_EQ(ClassifyPingReply(PingRawReply{0, kIpDestHostUnreachable, 0}, rtt),
              PingResultKind::Error);
    EXPECT_EQ(ClassifyPingReply(PingRawReply{1, kIpDestHostUnreachable, 0}, rtt),
              PingResultKind::Error);

    // A zero reply with no recorded error is treated as a timeout, not a success.
    EXPECT_EQ(ClassifyPingReply(PingRawReply{0, 0, 0}, rtt), PingResultKind::Timeout);
}

// --- provider behavior -----------------------------------------------------

TEST(PingProvider, ReportsNameDomainAndCadence)
{
    PingProvider provider(std::make_unique<FakePingSource>(), "8.8.8.8");
    EXPECT_STREQ(provider.Name(), "ping");
    EXPECT_EQ(provider.Domains(), static_cast<std::uint32_t>(MetricDomain::Ping));
    EXPECT_EQ(provider.Cadence(), std::chrono::milliseconds(1000));
}

TEST(PingProvider, SuccessPublishesRttAndResetsFailures)
{
    auto source = std::make_unique<FakePingSource>();
    FakePingSource* fake = source.get();
    fake->reply = PingRawReply{1, kIpSuccess, 15};

    PingProvider provider(std::move(source), "1.1.1.1");
    ASSERT_TRUE(provider.TargetValid());
    EXPECT_STREQ(provider.Target(), "1.1.1.1");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.ping.rttMs, 15.0);
    EXPECT_EQ(snapshot.ping.consecutiveFailures, 0u);
    EXPECT_EQ(fake->sendCount, 1);
    EXPECT_EQ(fake->lastAddress, 0x01010101u);
    EXPECT_EQ(fake->lastTimeoutMs, kDefaultPingTimeoutMs);
}

TEST(PingProvider, TimeoutSurfacesUnavailableAndKeepsLastRtt)
{
    auto source = std::make_unique<FakePingSource>();
    FakePingSource* fake = source.get();
    fake->reply = PingRawReply{1, kIpSuccess, 20};

    PingProvider provider(std::move(source), "8.8.8.8");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.ping.rttMs, 20.0);

    fake->reply = PingRawReply{0, kIpReqTimedOut, 0};
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(snapshot.ping.consecutiveFailures, 1u);
    EXPECT_EQ(provider.ConsecutiveFailures(), 1u);
    // The last good RTT is retained; it is not overwritten with zero.
    EXPECT_DOUBLE_EQ(snapshot.ping.rttMs, 20.0);

    fake->reply = PingRawReply{0, 0, 0}; // no reply, no error
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(provider.ConsecutiveFailures(), 2u);
}

TEST(PingProvider, IcmpErrorSurfacesUnavailable)
{
    auto source = std::make_unique<FakePingSource>();
    FakePingSource* fake = source.get();
    fake->reply = PingRawReply{0, kIpDestHostUnreachable, 0};

    PingProvider provider(std::move(source), "8.8.8.8");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(provider.ConsecutiveFailures(), 1u);
}

TEST(PingProvider, InvalidTargetOrMachineryFailureDegradesToUnavailable)
{
    PingProvider invalid(std::make_unique<FakePingSource>(), "not-an-ip");
    EXPECT_FALSE(invalid.TargetValid());
    MetricsSnapshot snapshot;
    EXPECT_EQ(invalid.Poll(snapshot), E_FAIL);

    auto source = std::make_unique<FakePingSource>();
    source->machineryOk = false;
    PingProvider failing(std::move(source), "8.8.8.8");
    EXPECT_TRUE(failing.TargetValid());
    EXPECT_EQ(failing.Poll(snapshot), E_FAIL);
    EXPECT_EQ(failing.ConsecutiveFailures(), 1u);
}

TEST(PingProvider, TimeoutIsClampedToABoundedWindow)
{
    PingProvider tooSmall(std::make_unique<FakePingSource>(), "8.8.8.8", 0);
    EXPECT_EQ(tooSmall.TimeoutMs(), 1u);

    PingProvider tooLarge(std::make_unique<FakePingSource>(), "8.8.8.8", 60'000);
    EXPECT_EQ(tooLarge.TimeoutMs(), kMaxPingTimeoutMs);

    auto source = std::make_unique<FakePingSource>();
    FakePingSource* fake = source.get();
    fake->reply = PingRawReply{1, kIpSuccess, 5};
    PingProvider provider(std::move(source), "8.8.8.8", 250);
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_EQ(fake->lastTimeoutMs, 250u);
}

TEST(PingProvider, ResetClearsFailureCount)
{
    auto source = std::make_unique<FakePingSource>();
    FakePingSource* fake = source.get();
    fake->reply = PingRawReply{0, kIpReqTimedOut, 0};
    PingProvider provider(std::move(source), "8.8.8.8");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(provider.ConsecutiveFailures(), 1u);

    provider.Reset();
    EXPECT_EQ(provider.ConsecutiveFailures(), 0u);
}

// --- real-hardware smoke ---------------------------------------------------

// Exercises the real `IcmpSendEcho` path against the loopback address, which is local and needs no
// external network. The classification tests above use injected replies; this only confirms the
// address packing and handle lifecycle. Skipped when the environment blocks ICMP.
TEST(PingProvider, RealLoopbackEchoSucceedsWhenIcmpIsAvailable)
{
    PingProvider provider(pacecar::metrics::MakeIcmpPingSource(), "127.0.0.1", 1000);
    ASSERT_TRUE(provider.TargetValid());

    MetricsSnapshot snapshot;
    if (provider.Poll(snapshot) != S_OK)
    {
        GTEST_SKIP() << "loopback ICMP is blocked or unavailable on this host";
    }
    EXPECT_GE(snapshot.ping.rttMs, 0.0);
}
} // namespace