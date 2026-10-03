#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/DeviceSelection.h"
#include "pacecar/metrics/NetworkProvider.h"
#include "pacecar/util/ElapsedClock.h"

namespace
{
using pacecar::IElapsedClock;
using pacecar::metrics::AggregateNetworkCounters;
using pacecar::metrics::Aggregator;
using pacecar::metrics::ComputeByteRate;
using pacecar::metrics::DeviceSelection;
using pacecar::metrics::DeviceSelectionKind;
using pacecar::metrics::INetworkSystemSource;
using pacecar::metrics::kNameCapacity;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::NameContainsInsensitive;
using pacecar::metrics::NetworkInterfaceCounters;
using pacecar::metrics::NetworkProvider;
using pacecar::metrics::NetworkSelectionResult;
using pacecar::metrics::ParseDeviceSelection;

void CopyText(char* destination, std::size_t capacity, const char* source) noexcept
{
    const std::size_t length = std::strlen(source);
    const std::size_t count = length < capacity - 1 ? length : capacity - 1;
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

NetworkInterfaceCounters MakeInterface(const char* name,
                                       std::uint64_t inOctets,
                                       std::uint64_t outOctets,
                                       bool loopback = false,
                                       const char* description = "")
{
    NetworkInterfaceCounters iface{};
    CopyText(iface.name, kNameCapacity, name);
    CopyText(iface.description, kNameCapacity, description);
    iface.isLoopback = loopback;
    iface.isUp = true;
    iface.inOctets = inOctets;
    iface.outOctets = outOctets;
    return iface;
}

class FakeNetworkSource final : public INetworkSystemSource
{
  public:
    bool ReadInterfaces(std::vector<NetworkInterfaceCounters>& out) override
    {
        ++readCount;
        if (!available)
        {
            return false;
        }
        out = interfaces;
        return true;
    }

    bool available = true;
    int readCount = 0;
    std::vector<NetworkInterfaceCounters> interfaces;
};

class FakeClock final : public IElapsedClock
{
  public:
    [[nodiscard]] std::uint64_t NowTicks() const noexcept override
    {
        return now;
    }
    [[nodiscard]] std::uint64_t TicksPerSecond() const noexcept override
    {
        return frequency;
    }

    std::uint64_t now = 0;
    std::uint64_t frequency = 10'000'000;
};

// --- selection parsing -----------------------------------------------------

TEST(DeviceSelection, ParsesAutoIndexAndName)
{
    EXPECT_EQ(ParseDeviceSelection("").kind, DeviceSelectionKind::All);
    EXPECT_EQ(ParseDeviceSelection("auto").kind, DeviceSelectionKind::All);
    EXPECT_EQ(ParseDeviceSelection("ALL").kind, DeviceSelectionKind::All);
    EXPECT_EQ(ParseDeviceSelection("  Auto  ").kind, DeviceSelectionKind::All);

    const DeviceSelection index = ParseDeviceSelection("3");
    EXPECT_EQ(index.kind, DeviceSelectionKind::Index);
    EXPECT_EQ(index.index, 3u);

    const DeviceSelection name = ParseDeviceSelection("  EtherNet ");
    EXPECT_EQ(name.kind, DeviceSelectionKind::Name);
    EXPECT_STREQ(name.name, "ethernet");
}

TEST(DeviceSelection, NameContainsInsensitiveHandlesSubstrings)
{
    EXPECT_TRUE(NameContainsInsensitive("Intel(R) Ethernet", "ethernet"));
    EXPECT_TRUE(NameContainsInsensitive("Wi-Fi", "WI-FI"));
    EXPECT_TRUE(NameContainsInsensitive("anything", ""));
    EXPECT_FALSE(NameContainsInsensitive("Wi-Fi", "ethernet"));
    EXPECT_FALSE(NameContainsInsensitive("ab", "abc"));
}

// --- pure rate math --------------------------------------------------------

TEST(NetworkRateMath, UsesRealElapsedTimeNotAssumedInterval)
{
    // 1 MB over 1 s.
    EXPECT_DOUBLE_EQ(ComputeByteRate(0, 1'000'000, 1.0), 1'000'000.0);
    // The same delta over half a second is twice the rate: elapsed time is honoured.
    EXPECT_DOUBLE_EQ(ComputeByteRate(0, 1'000'000, 0.5), 2'000'000.0);
    // ... and over five seconds it is a fifth.
    EXPECT_DOUBLE_EQ(ComputeByteRate(0, 1'000'000, 5.0), 200'000.0);
    // No change is zero.
    EXPECT_DOUBLE_EQ(ComputeByteRate(4'000, 4'000, 1.0), 0.0);
}

TEST(NetworkRateMath, ResetAndDegenerateWindowsProduceNoSpike)
{
    // Counter moved backwards (link reset): clamp to zero rather than a wrap-sized spike.
    EXPECT_DOUBLE_EQ(ComputeByteRate(10'000, 5'000, 1.0), 0.0);
    // Non-positive or zero elapsed never divides.
    EXPECT_DOUBLE_EQ(ComputeByteRate(0, 1'000'000, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(ComputeByteRate(0, 1'000'000, -1.0), 0.0);
}

// --- aggregation / filtering ----------------------------------------------

std::vector<NetworkInterfaceCounters> ThreeInterfaces()
{
    return {
        MakeInterface("Loopback", 999, 999, true),
        MakeInterface("Ethernet", 1'000, 500, false, "Intel(R) Ethernet Controller"),
        MakeInterface("Wi-Fi", 2'000, 100, false, "Intel(R) Wi-Fi 6"),
    };
}

TEST(NetworkAggregation, AllSumsNonLoopbackAndExcludesLoopback)
{
    NetworkSelectionResult result;
    AggregateNetworkCounters(ThreeInterfaces(), ParseDeviceSelection("auto"), result);

    EXPECT_EQ(result.interfaceCount, 2u);
    EXPECT_EQ(result.inOctets, 3'000u);
    EXPECT_EQ(result.outOctets, 600u);
    EXPECT_STREQ(result.name, "All interfaces");
}

TEST(NetworkAggregation, IndexSelectsNthNonLoopbackInterface)
{
    NetworkSelectionResult result;
    AggregateNetworkCounters(ThreeInterfaces(), ParseDeviceSelection("1"), result);

    EXPECT_EQ(result.interfaceCount, 1u);
    EXPECT_EQ(result.inOctets, 2'000u); // Wi-Fi; loopback is not counted in the index
    EXPECT_STREQ(result.name, "Wi-Fi");
}

TEST(NetworkAggregation, NameMatchesAliasAndDescriptionCaseInsensitively)
{
    NetworkSelectionResult alias;
    AggregateNetworkCounters(ThreeInterfaces(), ParseDeviceSelection("wi-fi"), alias);
    ASSERT_EQ(alias.interfaceCount, 1u);
    EXPECT_EQ(alias.inOctets, 2'000u);

    NetworkSelectionResult described;
    AggregateNetworkCounters(ThreeInterfaces(), ParseDeviceSelection("controller"), described);
    ASSERT_EQ(described.interfaceCount, 1u);
    EXPECT_STREQ(described.name, "Ethernet");

    NetworkSelectionResult none;
    AggregateNetworkCounters(ThreeInterfaces(), ParseDeviceSelection("thunderbolt"), none);
    EXPECT_EQ(none.interfaceCount, 0u);
    EXPECT_STREQ(none.name, "");
}

TEST(NetworkAggregation, NameCanSelectMultipleInterfaces)
{
    std::vector<NetworkInterfaceCounters> interfaces{
        MakeInterface("Ethernet 1", 100, 200),
        MakeInterface("Ethernet 2", 300, 400),
    };
    NetworkSelectionResult result;
    AggregateNetworkCounters(interfaces, ParseDeviceSelection("ethernet"), result);
    EXPECT_EQ(result.interfaceCount, 2u);
    EXPECT_EQ(result.inOctets, 400u);
    EXPECT_STREQ(result.name, "Multiple interfaces");
}

// --- provider behavior -----------------------------------------------------

TEST(NetworkProvider, ReportsNameDomainAndCadence)
{
    NetworkProvider provider(std::make_unique<FakeNetworkSource>(),
                             std::make_unique<FakeClock>(), "auto");
    EXPECT_STREQ(provider.Name(), "network");
    EXPECT_EQ(provider.Domains(), static_cast<std::uint32_t>(MetricDomain::Network));
    EXPECT_EQ(provider.Cadence(), std::chrono::milliseconds(1000));
}

TEST(NetworkProvider, FirstPollIsPendingThenPublishesRateOverRealElapsedTime)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();
    fakeSource->interfaces = ThreeInterfaces();

    NetworkProvider provider(std::move(source), std::move(clock), "auto");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.BaselineInOctets(), 3'000u);
    EXPECT_EQ(provider.BaselineOutOctets(), 600u);

    // 1 s later the Wi-Fi interface adds 2 MB down and 1 MB up.
    fakeClock->now = 10'000'000; // 1 s at 10 MHz
    fakeSource->interfaces[2].inOctets += 2'000'000;
    fakeSource->interfaces[2].outOctets += 1'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.network.downBytesPerSecond, 2'000'000.0);
    EXPECT_DOUBLE_EQ(snapshot.network.upBytesPerSecond, 1'000'000.0);
    EXPECT_STREQ(snapshot.network.interfaceName, "All interfaces");
    EXPECT_EQ(snapshot.network.totalDownBytes, 2'003'000u);
    EXPECT_EQ(snapshot.network.totalUpBytes, 1'000'600u);
}

TEST(NetworkProvider, RateReflectsAVariableCadence)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();
    fakeSource->interfaces = {MakeInterface("Ethernet", 0, 0)};

    NetworkProvider provider(std::move(source), std::move(clock), "auto");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    // 500 ms window with 1 MB received -> 2 MB/s (an assumed 1 s window would report 1 MB/s).
    fakeClock->now = 5'000'000;
    fakeSource->interfaces[0].inOctets = 1'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.network.downBytesPerSecond, 2'000'000.0);

    // A 4 s window with another 1 MB -> 250 KB/s.
    fakeClock->now += 40'000'000;
    fakeSource->interfaces[0].inOctets += 1'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.network.downBytesPerSecond, 250'000.0);
}

TEST(NetworkProvider, CounterResetBetweenSamplesDoesNotSpike)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();
    fakeSource->interfaces = {MakeInterface("Ethernet", 100'000'000, 200'000'000)};

    NetworkProvider provider(std::move(source), std::move(clock), "auto");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    // The adapter resets: counters drop to near zero.
    fakeClock->now = 10'000'000;
    fakeSource->interfaces[0].inOctets = 1'000;
    fakeSource->interfaces[0].outOctets = 500;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.network.downBytesPerSecond, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.network.upBytesPerSecond, 0.0);

    // The new values become the baseline, so the next tick is normal again.
    fakeClock->now += 10'000'000;
    fakeSource->interfaces[0].inOctets += 1'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.network.downBytesPerSecond, 1'000'000.0);
}

TEST(NetworkProvider, MissingSourceOrNoMatchingInterfaceDegradesToUnavailable)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    NetworkProvider provider(std::move(source), std::make_unique<FakeClock>(),
                             "thunderbolt-nonexistent");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL); // source present, nothing matched

    fakeSource->available = false;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL); // source read fails
}

TEST(NetworkProvider, ResetReArmsTheBaseline)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();
    fakeSource->interfaces = {MakeInterface("Ethernet", 0, 0)};

    NetworkProvider provider(std::move(source), std::move(clock), "0");
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    fakeClock->now = 10'000'000;
    fakeSource->interfaces[0].inOctets = 1'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    provider.Reset();
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.BaselineInOctets(), 1'000'000u);
}

// --- aggregator integration ------------------------------------------------

TEST(NetworkProvider, AggregatorMarksTheFirstDeltaTickStale)
{
    auto source = std::make_unique<FakeNetworkSource>();
    FakeNetworkSource* fakeSource = source.get();
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();
    fakeSource->interfaces = {MakeInterface("Ethernet", 0, 0)};

    Aggregator aggregator(60);
    aggregator.AddProvider(std::make_shared<NetworkProvider>(std::move(source), std::move(clock),
                                                             "auto"));

    EXPECT_TRUE(aggregator.Tick(0));
    auto first = aggregator.LatestSnapshot();
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->network.status.available);

    fakeClock->now = 10'000'000;
    fakeSource->interfaces[0].inOctets = 2'000'000;
    EXPECT_TRUE(aggregator.Tick(1000));
    auto second = aggregator.LatestSnapshot();
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->network.status.available);
    EXPECT_DOUBLE_EQ(second->network.downBytesPerSecond, 2'000'000.0);
}

// --- real-hardware smoke (no network traffic is generated) -----------------

TEST(NetworkProvider, RealMachinePublishesPlausibleRatesWithoutAdmin)
{
    NetworkProvider provider;
    MetricsSnapshot snapshot;

    const HRESULT first = provider.Poll(snapshot);
    if (first != E_PENDING)
    {
        // No non-loopback interface on this host; the provider correctly reports unavailable.
        GTEST_SKIP() << "no non-loopback interface available on this host";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const HRESULT second = provider.Poll(snapshot);
    ASSERT_EQ(second, S_OK);
    EXPECT_GE(snapshot.network.upBytesPerSecond, 0.0);
    EXPECT_GE(snapshot.network.downBytesPerSecond, 0.0);
    ASSERT_STRNE(snapshot.network.interfaceName, "");
    EXPECT_GE(snapshot.network.totalUpBytes, 0u);
    EXPECT_GE(snapshot.network.totalDownBytes, 0u);
}
} // namespace