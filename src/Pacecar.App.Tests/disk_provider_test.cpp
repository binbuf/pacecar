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
#include "pacecar/metrics/DiskProvider.h"

namespace
{
using pacecar::metrics::AggregateDiskRates;
using pacecar::metrics::Aggregator;
using pacecar::metrics::DeviceSelection;
using pacecar::metrics::DiskCounterSample;
using pacecar::metrics::DiskProvider;
using pacecar::metrics::DiskRate;
using pacecar::metrics::IDiskSource;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::ParseDeviceSelection;

void CopyName(char* destination, std::size_t capacity, const char* source) noexcept
{
    const std::size_t length = std::strlen(source);
    const std::size_t count = length < capacity - 1 ? length : capacity - 1;
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

DiskCounterSample MakeSample(const char* name,
                             double read,
                             double write,
                             bool readValid = true,
                             bool writeValid = true)
{
    DiskCounterSample sample{};
    CopyName(sample.instanceName, sizeof(sample.instanceName), name);
    sample.readBytesPerSecond = read;
    sample.writeBytesPerSecond = write;
    sample.readValid = readValid;
    sample.writeValid = writeValid;
    return sample;
}

class FakeDiskSource final : public IDiskSource
{
  public:
    [[nodiscard]] bool IsAvailable() override
    {
        return available;
    }

    bool Read(std::vector<DiskCounterSample>& out) override
    {
        ++readCount;
        if (!readOk)
        {
            return false;
        }
        out = samples;
        return true;
    }

    bool available = true;
    bool readOk = true;
    int readCount = 0;
    std::vector<DiskCounterSample> samples;
};

// --- pure PDH rate assembly ------------------------------------------------

TEST(DiskRateAssembly, AllSumsInstancesAndExcludesTotal)
{
    const std::vector<DiskCounterSample> samples{
        MakeSample("_Total", 9'999, 9'999),
        MakeSample("0 C:", 1'000, 100),
        MakeSample("1 D:", 2'000, 200),
    };
    DiskRate rate;
    AggregateDiskRates(samples, ParseDeviceSelection("auto"), rate);

    EXPECT_EQ(rate.instanceCount, 2u);
    EXPECT_DOUBLE_EQ(rate.readBytesPerSecond, 3'000.0);
    EXPECT_DOUBLE_EQ(rate.writeBytesPerSecond, 300.0);
    EXPECT_STREQ(rate.name, "All disks");
}

TEST(DiskRateAssembly, InvalidCountersAreSkippedNotZeroedAsGarbage)
{
    const std::vector<DiskCounterSample> samples{
        MakeSample("0 C:", 1'000, 100, true, false), // write invalid (first collection)
        MakeSample("1 D:", 2'000, 200, false, true), // read invalid
    };
    DiskRate rate;
    AggregateDiskRates(samples, ParseDeviceSelection("all"), rate);
    EXPECT_DOUBLE_EQ(rate.readBytesPerSecond, 1'000.0);
    EXPECT_DOUBLE_EQ(rate.writeBytesPerSecond, 200.0);
}

TEST(DiskRateAssembly, NameSelectionMatchesSubstring)
{
    const std::vector<DiskCounterSample> samples{
        MakeSample("_Total", 9'999, 9'999),
        MakeSample("0 C:", 1'000, 100),
        MakeSample("1 D:", 2'000, 200),
    };
    DiskRate rate;
    AggregateDiskRates(samples, ParseDeviceSelection("d:"), rate);
    ASSERT_EQ(rate.instanceCount, 1u);
    EXPECT_DOUBLE_EQ(rate.readBytesPerSecond, 2'000.0);
    EXPECT_STREQ(rate.name, "1 D:");

    DiskRate none;
    AggregateDiskRates(samples, ParseDeviceSelection("Z:"), none);
    EXPECT_EQ(none.instanceCount, 0u);
}

TEST(DiskRateAssembly, IndexSelectsNthNonTotal)
{
    const std::vector<DiskCounterSample> samples{
        MakeSample("_Total", 9'999, 9'999),
        MakeSample("0 C:", 1'000, 100),
        MakeSample("1 D:", 2'000, 200),
    };
    DiskRate rate;
    AggregateDiskRates(samples, ParseDeviceSelection("1"), rate);
    ASSERT_EQ(rate.instanceCount, 1u);
    EXPECT_DOUBLE_EQ(rate.readBytesPerSecond, 2'000.0);
    EXPECT_STREQ(rate.name, "1 D:");
}

TEST(DiskRateAssembly, FallsBackToTotalWhenItIsTheOnlyInstance)
{
    const std::vector<DiskCounterSample> samples{MakeSample("_Total", 4'000, 500)};
    DiskRate rate;
    AggregateDiskRates(samples, ParseDeviceSelection("auto"), rate);
    EXPECT_EQ(rate.instanceCount, 1u);
    EXPECT_DOUBLE_EQ(rate.readBytesPerSecond, 4'000.0);
    EXPECT_DOUBLE_EQ(rate.writeBytesPerSecond, 500.0);
}

// --- provider behavior -----------------------------------------------------

TEST(DiskProvider, ReportsNameDomainAndCadence)
{
    DiskProvider provider(std::make_unique<FakeDiskSource>(), "auto");
    EXPECT_STREQ(provider.Name(), "disk");
    EXPECT_EQ(provider.Domains(), static_cast<std::uint32_t>(MetricDomain::Disk));
    EXPECT_EQ(provider.Cadence(), std::chrono::milliseconds(1000));
}

TEST(DiskProvider, FirstPollIsPendingThenPublishesAssembledRates)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->samples = {
        MakeSample("_Total", 9'999, 9'999),
        MakeSample("0 C:", 1'000, 100),
        MakeSample("1 D:", 2'000, 200),
    };
    DiskProvider provider(std::move(source), "auto");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.disk.readBytesPerSecond, 3'000.0);
    EXPECT_DOUBLE_EQ(snapshot.disk.writeBytesPerSecond, 300.0);
    EXPECT_STREQ(snapshot.disk.name, "All disks");
}

TEST(DiskProvider, SelectionRestrictsToNamedDisk)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->samples = {MakeSample("0 C:", 1'000, 100), MakeSample("1 D:", 2'000, 200)};
    DiskProvider provider(std::move(source), "1 D:");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.disk.readBytesPerSecond, 2'000.0);
    EXPECT_STREQ(snapshot.disk.name, "1 D:");
}

TEST(DiskProvider, UnavailableOrFailingSourceDegradesToUnavailable)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->available = false;
    DiskProvider provider(std::move(source), "auto");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_FALSE(provider.PdhAvailable());

    fake->available = true;
    fake->readOk = false;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
}

TEST(DiskProvider, NoMatchingInstanceFailsRatherThanPublishingZero)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->samples = {MakeSample("0 C:", 1'000, 100)};
    DiskProvider provider(std::move(source), "Z:");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
}

TEST(DiskProvider, ResetReArmsTheFirstSample)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->samples = {MakeSample("0 C:", 1'000, 100)};
    DiskProvider provider(std::move(source), "auto");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    provider.Reset();
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
}

TEST(DiskProvider, AggregatorMarksTheFirstRateTickStale)
{
    auto source = std::make_unique<FakeDiskSource>();
    FakeDiskSource* fake = source.get();
    fake->samples = {MakeSample("0 C:", 1'000, 100)};

    Aggregator aggregator(60);
    aggregator.AddProvider(std::make_shared<DiskProvider>(std::move(source), "auto"));

    EXPECT_TRUE(aggregator.Tick(0));
    auto first = aggregator.LatestSnapshot();
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->disk.status.available);

    EXPECT_TRUE(aggregator.Tick(1000));
    auto second = aggregator.LatestSnapshot();
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->disk.status.available);
    EXPECT_DOUBLE_EQ(second->disk.readBytesPerSecond, 1'000.0);
}

// --- real-hardware smoke ---------------------------------------------------

TEST(DiskProvider, RealMachinePublishesPlausibleRatesWithoutAdmin)
{
    DiskProvider provider;
    if (!provider.PdhAvailable())
    {
        GTEST_SKIP() << "PhysicalDisk counter set unavailable on this host";
    }

    MetricsSnapshot snapshot;
    ASSERT_EQ(provider.Poll(snapshot), E_PENDING); // rate counter needs one baseline
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_GE(snapshot.disk.readBytesPerSecond, 0.0);
    EXPECT_GE(snapshot.disk.writeBytesPerSecond, 0.0);
    ASSERT_STRNE(snapshot.disk.name, "");
}
} // namespace