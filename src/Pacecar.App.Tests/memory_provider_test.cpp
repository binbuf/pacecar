#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>

#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/MemoryProvider.h"

namespace
{
using pacecar::metrics::Aggregator;
using pacecar::metrics::ComputeUsedBytes;
using pacecar::metrics::ComputeUsedPercent;
using pacecar::metrics::IMemorySystemSource;
using pacecar::metrics::MemoryProvider;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

class FakeMemorySource final : public IMemorySystemSource
{
  public:
    bool ReadPhysical(std::uint64_t& totalBytes, std::uint64_t& availableBytes) override
    {
        if (!physicalAvailable)
        {
            return false;
        }
        totalBytes = total;
        availableBytes = available;
        return true;
    }

    bool ReadPerformance(std::uint64_t& commitBytes,
                         std::uint64_t& commitLimitBytes,
                         std::uint64_t& cacheBytes) override
    {
        if (!performanceAvailable)
        {
            return false;
        }
        commitBytes = commit;
        commitLimitBytes = commitLimit;
        cacheBytes = cache;
        return true;
    }

    bool physicalAvailable = true;
    bool performanceAvailable = true;
    std::uint64_t total = 16 * kGiB;
    std::uint64_t available = 4 * kGiB;
    std::uint64_t commit = 8 * kGiB;
    std::uint64_t commitLimit = 32 * kGiB;
    std::uint64_t cache = 3 * kGiB;
};

// --- pure math -------------------------------------------------------------

TEST(MemoryMath, UsedBytesIsDerivedAndClamped)
{
    EXPECT_EQ(ComputeUsedBytes(16 * kGiB, 4 * kGiB), 12 * kGiB);
    EXPECT_EQ(ComputeUsedBytes(16 * kGiB, 0), 16 * kGiB);
    EXPECT_EQ(ComputeUsedBytes(16 * kGiB, 16 * kGiB), 0u);
    // Inconsistent input (available larger than total) clamps to zero, never underflows.
    EXPECT_EQ(ComputeUsedBytes(1 * kGiB, 2 * kGiB), 0u);
    EXPECT_EQ(ComputeUsedBytes(0, 0), 0u);
}

TEST(MemoryMath, UsedPercentKnownValues)
{
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(8 * kGiB, 16 * kGiB), 50.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(0, 16 * kGiB), 0.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(16 * kGiB, 16 * kGiB), 100.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(4 * kGiB, 16 * kGiB), 25.0);
}

TEST(MemoryMath, UsedPercentEdgeCasesNeverDivideByZeroOrGoNegative)
{
    // Very small total is still well-defined.
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(1, 1), 100.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(0, 1), 0.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(1, 2), 50.0);

    // Zero total must not divide by zero.
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(0, 0), 0.0);
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(123, 0), 0.0);

    // Inconsistent input clamps rather than exceeding 100.
    EXPECT_DOUBLE_EQ(ComputeUsedPercent(2 * kGiB, 1 * kGiB), 100.0);
}

// --- provider behavior -----------------------------------------------------

TEST(MemoryProvider, ReportsNameDomainAndCadence)
{
    MemoryProvider provider(std::make_unique<FakeMemorySource>());
    EXPECT_STREQ(provider.Name(), "memory");
    EXPECT_EQ(provider.Domains(), static_cast<std::uint32_t>(MetricDomain::Memory));
    EXPECT_EQ(provider.Cadence(), std::chrono::milliseconds(1000));
}

TEST(MemoryProvider, FirstSampleIsValidAndInternallyConsistent)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));

    fake->total = 16 * kGiB;
    fake->available = 4 * kGiB;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    // used is derived from the exact total/available shown, and percent matches them.
    EXPECT_EQ(snapshot.memory.totalBytes, 16 * kGiB);
    EXPECT_EQ(snapshot.memory.availableBytes, 4 * kGiB);
    EXPECT_EQ(snapshot.memory.usedBytes, 12 * kGiB);
    EXPECT_EQ(snapshot.memory.usedBytes, snapshot.memory.totalBytes - snapshot.memory.availableBytes);
    EXPECT_DOUBLE_EQ(snapshot.memory.usedPercent, 75.0);
}

TEST(MemoryProvider, CommitAndCacheAreReportedDistinctlyFromPhysicalUsage)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));

    fake->total = 16 * kGiB;
    fake->available = 6 * kGiB;
    fake->commit = 10 * kGiB;
    fake->commitLimit = 40 * kGiB;
    fake->cache = 3 * kGiB;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    EXPECT_TRUE(provider.PerformanceAvailable());
    EXPECT_EQ(snapshot.memory.commitBytes, 10 * kGiB);
    EXPECT_EQ(snapshot.memory.commitLimitBytes, 40 * kGiB);
    EXPECT_EQ(snapshot.memory.cacheBytes, 3 * kGiB);
    // The physical percentage ignores commit/cache entirely.
    EXPECT_EQ(snapshot.memory.usedBytes, 10 * kGiB);
    EXPECT_DOUBLE_EQ(snapshot.memory.usedPercent, 62.5);
}

TEST(MemoryProvider, CacheLargerThanUsedDoesNotAffectPhysicalValues)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));

    fake->total = 8 * kGiB;
    fake->available = 7 * kGiB; // used = 1 GiB
    fake->cache = 6 * kGiB;     // cache is larger than used

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    EXPECT_EQ(snapshot.memory.usedBytes, 1 * kGiB);
    EXPECT_DOUBLE_EQ(snapshot.memory.usedPercent, 12.5);
    EXPECT_EQ(snapshot.memory.cacheBytes, 6 * kGiB);
    EXPECT_GE(snapshot.memory.usedPercent, 0.0);
}

TEST(MemoryProvider, AvailableGreaterThanTotalClampsToZeroUsage)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));

    fake->total = 2 * kGiB;
    fake->available = 3 * kGiB; // never happens on a real system; must not underflow

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    EXPECT_EQ(snapshot.memory.usedBytes, 0u);
    EXPECT_DOUBLE_EQ(snapshot.memory.usedPercent, 0.0);
}

TEST(MemoryProvider, PerformanceFailureStillPublishesPhysicalUsage)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));

    fake->performanceAvailable = false;
    fake->total = 16 * kGiB;
    fake->available = 4 * kGiB;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    EXPECT_FALSE(provider.PerformanceAvailable());
    EXPECT_EQ(snapshot.memory.usedBytes, 12 * kGiB);
    EXPECT_DOUBLE_EQ(snapshot.memory.usedPercent, 75.0);
    EXPECT_EQ(snapshot.memory.commitBytes, 0u);
    EXPECT_EQ(snapshot.memory.cacheBytes, 0u);
}

TEST(MemoryProvider, PhysicalFailureFailsPoll)
{
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    MemoryProvider provider(std::move(source));
    fake->physicalAvailable = false;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_FALSE(provider.PerformanceAvailable());
}

// --- aggregator integration ------------------------------------------------

TEST(MemoryProvider, AggregatorMarksFirstSampleAvailableImmediately)
{
    Aggregator aggregator(60);
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    fake->total = 16 * kGiB;
    fake->available = 4 * kGiB;

    aggregator.AddProvider(std::make_shared<MemoryProvider>(std::move(source)));

    EXPECT_TRUE(aggregator.Tick(0));
    auto first = aggregator.LatestSnapshot();
    ASSERT_NE(first, nullptr);
    // No delta needed: the first memory sample is valid, not stale.
    EXPECT_TRUE(first->memory.status.available);
    EXPECT_FALSE(first->memory.status.stale);
    EXPECT_EQ(first->memory.totalBytes, 16 * kGiB);
}

TEST(MemoryProvider, AggregatorPollsOnlyOncePerConfiguredCadence)
{
    Aggregator aggregator(60);
    auto source = std::make_unique<FakeMemorySource>();
    FakeMemorySource* fake = source.get();
    fake->total = 16 * kGiB;
    fake->available = 4 * kGiB;
    aggregator.AddProvider(std::make_shared<MemoryProvider>(std::move(source)));

    EXPECT_TRUE(aggregator.Tick(0));
    EXPECT_EQ(aggregator.ProviderPollCount(0), 1u);

    // Within the provider's 1000 ms cadence the provider is not polled again.
    EXPECT_TRUE(aggregator.Tick(500));
    EXPECT_EQ(aggregator.ProviderPollCount(0), 1u);

    EXPECT_TRUE(aggregator.Tick(1000));
    EXPECT_EQ(aggregator.ProviderPollCount(0), 2u);

    auto snapshot = aggregator.LatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_DOUBLE_EQ(snapshot->memory.usedPercent, 75.0);
}

// --- real-hardware smoke ---------------------------------------------------

TEST(MemoryProvider, RealMachineReportsPlausibleUsage)
{
    MemoryProvider provider;
    MetricsSnapshot snapshot;
    ASSERT_EQ(provider.Poll(snapshot), S_OK);

    EXPECT_GT(snapshot.memory.totalBytes, 0u);
    EXPECT_LE(snapshot.memory.usedBytes, snapshot.memory.totalBytes);
    EXPECT_LE(snapshot.memory.availableBytes, snapshot.memory.totalBytes);
    EXPECT_GE(snapshot.memory.usedPercent, 0.0);
    EXPECT_LE(snapshot.memory.usedPercent, 100.0);
    EXPECT_EQ(snapshot.memory.usedPercent,
              ComputeUsedPercent(snapshot.memory.usedBytes, snapshot.memory.totalBytes));

    if (provider.PerformanceAvailable())
    {
        EXPECT_GT(snapshot.memory.commitLimitBytes, 0u);
        EXPECT_LE(snapshot.memory.commitBytes, snapshot.memory.commitLimitBytes);
    }
}
} // namespace