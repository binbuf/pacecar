#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/DisplayFrame.h"
#include "pacecar/metrics/HistoryRetention.h"
#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/metrics/MetricsFingerprint.h"
#include "pacecar/metrics/MetricsSnapshot.h"
#include "pacecar/metrics/RenderGate.h"
#include "pacecar/metrics/SnapshotCache.h"
#include "pacecar/util/TimerResolution.h"

namespace
{
using namespace std::chrono_literals;

using pacecar::metrics::Aggregator;
using pacecar::metrics::FingerprintSnapshot;
using pacecar::metrics::IMetricProvider;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::RenderGate;

constexpr std::uint32_t CpuDomain = static_cast<std::uint32_t>(MetricDomain::Cpu);

class FakeProvider : public IMetricProvider
{
  public:
    FakeProvider(std::string name, std::chrono::milliseconds cadence, std::uint32_t domains)
        : name_(std::move(name)), cadence_(cadence), domains_(domains)
    {
    }

    [[nodiscard]] const char* Name() const noexcept override
    {
        return name_.c_str();
    }
    [[nodiscard]] std::uint32_t Domains() const noexcept override
    {
        return domains_;
    }
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override
    {
        return cadence_;
    }
    HRESULT Poll(MetricsSnapshot& snapshot) override
    {
        write(snapshot);
        return S_OK;
    }

    std::function<void(MetricsSnapshot&)> write = [](MetricsSnapshot&) {};

  private:
    std::string name_;
    std::chrono::milliseconds cadence_;
    std::uint32_t domains_;
};
} // namespace

TEST(RenderGate, FirstPaintThenOnlyOnChange)
{
    RenderGate gate(250ms);
    MetricsSnapshot snapshot;
    snapshot.cpu.totalUtilizationPercent = 10.0;

    EXPECT_TRUE(gate.ShouldRepaint(snapshot, 0));
    EXPECT_EQ(gate.PaintCount(), 1u);

    EXPECT_FALSE(gate.ShouldRepaint(snapshot, 1000));
    EXPECT_FALSE(gate.ShouldRepaint(snapshot, 2000));
    EXPECT_EQ(gate.PaintCount(), 1u);
    EXPECT_EQ(gate.SkipCount(), 2u);

    snapshot.cpu.totalUtilizationPercent = 55.0;
    EXPECT_TRUE(gate.ShouldRepaint(snapshot, 3000));
    EXPECT_EQ(gate.PaintCount(), 2u);
}

TEST(RenderGate, RespectsMinimumIntervalAndRemembersPendingChange)
{
    RenderGate gate(1000ms);
    MetricsSnapshot snapshot;
    snapshot.cpu.totalUtilizationPercent = 10.0;
    ASSERT_TRUE(gate.ShouldRepaint(snapshot, 0));

    snapshot.cpu.totalUtilizationPercent = 90.0;
    // A change 200 ms after the last paint is suppressed...
    EXPECT_FALSE(gate.ShouldRepaint(snapshot, 200));
    // ...but is not forgotten: it paints once the interval elapses.
    EXPECT_TRUE(gate.ShouldRepaint(snapshot, 1000));
    EXPECT_EQ(gate.PaintCount(), 2u);
}

TEST(RenderGate, IntervalFloorIsTwoHundredFiftyMilliseconds)
{
    RenderGate gate(1ms);
    EXPECT_EQ(gate.Interval(), 250ms);
}

TEST(RenderGate, SubDisplayPrecisionChangesDoNotRepaint)
{
    RenderGate gate(0ms);
    MetricsSnapshot snapshot;
    snapshot.cpu.totalUtilizationPercent = 42.00;
    ASSERT_TRUE(gate.ShouldRepaint(snapshot, 0));

    snapshot.cpu.totalUtilizationPercent = 42.04; // rounds to the same displayed tenth
    EXPECT_FALSE(gate.ShouldRepaint(snapshot, 1000));

    snapshot.cpu.totalUtilizationPercent = 42.1;
    EXPECT_TRUE(gate.ShouldRepaint(snapshot, 2000));
}

TEST(Fingerprint, TracksAvailabilityAndValues)
{
    MetricsSnapshot snapshot;
    const std::uint64_t base = FingerprintSnapshot(snapshot);

    snapshot.cpu.status.available = true;
    EXPECT_NE(FingerprintSnapshot(snapshot), base);

    MetricsSnapshot other;
    other.cpu.status.available = true;
    EXPECT_EQ(FingerprintSnapshot(snapshot), FingerprintSnapshot(other));

    other.memory.usedPercent = 25.0;
    EXPECT_NE(FingerprintSnapshot(snapshot), FingerprintSnapshot(other));
}

TEST(SamplingIntegration, FakeProvidersDriveFakeSinkOnlyOnChange)
{
    Aggregator aggregator(60);
    auto cpu = std::make_shared<FakeProvider>("cpu", 1000ms, CpuDomain);
    // Frequency is not EMA-smoothed, so a constant input yields an identical fingerprint and a
    // single paint; changing it repaints exactly once.
    double frequency = 3600.0;
    cpu->write = [&frequency](MetricsSnapshot& snapshot)
    {
        snapshot.cpu.totalUtilizationPercent = 42.0;
        snapshot.cpu.totalFrequencyMhz = frequency;
    };
    aggregator.AddProvider(cpu);
    aggregator.SetVisible(true);

    RenderGate gate(1000ms);
    int paints = 0;
    const auto pump = [&](std::uint64_t nowMs)
    {
        static_cast<void>(aggregator.Tick(nowMs));
        auto snapshot = aggregator.LatestSnapshot();
        if (snapshot && gate.ShouldRepaint(*snapshot, nowMs))
        {
            ++paints;
        }
    };

    for (std::uint64_t t = 0; t <= 5000; t += 1000)
    {
        pump(t);
    }
    // Steady values -> exactly one paint (the first frame).
    EXPECT_EQ(paints, 1);

    frequency = 4100.0;
    pump(6000);
    EXPECT_EQ(paints, 2);

    // Unchanged again -> no further paint.
    pump(7000);
    pump(8000);
    EXPECT_EQ(paints, 2);
}

TEST(HistoryRetention, PlanFitsBudgetForEveryConfigCombination)
{
    constexpr std::chrono::milliseconds kCadences[] = {250ms, 500ms, 1000ms, 2000ms, 5000ms};
    constexpr int kRetentions[] = {1, 5, 10, 15, 30, 60, 120};

    for (const auto cadence : kCadences)
    {
        for (const int minutes : kRetentions)
        {
            const auto plan = pacecar::metrics::PlanHistoryRetention(minutes, cadence);
            EXPECT_GT(plan.rawCapacity, 0u);
            EXPECT_LE(plan.estimatedBytes, pacecar::metrics::kHistoryBudgetBytes);
            EXPECT_LE(plan.rawCapacity, pacecar::metrics::kMaxRawHistorySamples);
            EXPECT_GE(plan.downsampleStride, 1u);
            EXPECT_EQ(plan.estimatedBytes,
                      plan.rawCapacity * pacecar::metrics::kHistorySeriesCount * sizeof(double));
        }
    }
}

TEST(HistoryRetention, LongRetentionIsDownsampledAndShortIsNot)
{
    const auto shortPlan = pacecar::metrics::PlanHistoryRetention(1, 1000ms);
    EXPECT_FALSE(shortPlan.downsampled);
    EXPECT_EQ(shortPlan.rawCapacity, 60u);

    const auto longPlan = pacecar::metrics::PlanHistoryRetention(120, 1000ms);
    EXPECT_TRUE(longPlan.downsampled);
    EXPECT_EQ(longPlan.rawCapacity, pacecar::metrics::kMaxRawHistorySamples);
    EXPECT_GT(longPlan.downsampleStride, 1u);
}

TEST(HistoryRetention, BucketAverageMeansRuns)
{
    pacecar::RingBuffer<double> ring(8);
    for (int i = 1; i <= 6; ++i)
    {
        ring.Push(static_cast<double>(i));
    }

    double out[3] = {};
    const std::size_t count =
        pacecar::metrics::BucketAverage(ring, 2, std::span<double>(out, 3));
    EXPECT_EQ(count, 3u);
    EXPECT_DOUBLE_EQ(out[0], 1.5);
    EXPECT_DOUBLE_EQ(out[1], 3.5);
    EXPECT_DOUBLE_EQ(out[2], 5.5);
}

TEST(DisplayFrame, CapturesSparklineTailChronologically)
{
    pacecar::metrics::MetricHistory history(10);
    for (int i = 0; i < 5; ++i)
    {
        MetricsSnapshot snapshot;
        snapshot.cpu.totalUtilizationPercent = static_cast<double>(i);
        history.Push(snapshot);
    }

    auto snapshot = std::make_shared<MetricsSnapshot>();
    snapshot->cpu.status.available = true;
    auto frame = pacecar::metrics::BuildDisplayFrame(snapshot, history);
    ASSERT_NE(frame.snapshot, nullptr);
    EXPECT_EQ(frame.cpu.count, 5u);
    EXPECT_FLOAT_EQ(frame.cpu.samples[0], 0.0f);
    EXPECT_FLOAT_EQ(frame.cpu.samples[4], 4.0f);
    EXPECT_NE(frame.fingerprint, 0u);
}

TEST(SnapshotCache, RoundTripsRepresentativeSnapshot)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / L"pacecar_snapshot_cache_test.bin";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    MetricsSnapshot original;
    original.sequence = 7;
    original.tickIndex = 12;
    original.timestampMs = 3456;
    original.cpu.status.available = true;
    original.cpu.totalUtilizationPercent = 37.5;
    original.cpu.totalFrequencyMhz = 4100.0;
    original.cpu.packageTemperatureC = 61.0;
    original.cpu.cores.resize(2);
    original.cpu.cores[0].utilizationPercent = 10.0;
    original.cpu.cores[1].utilizationPercent = 65.0;
    original.memory.status.available = true;
    original.memory.usedPercent = 48.25;
    original.memory.totalBytes = 16ull * 1024 * 1024 * 1024;
    original.gpu.status.available = true;
    original.gpu.utilizationPercent = 22.0;
    original.gpu.vramUsedBytes = 3ull * 1024 * 1024 * 1024;
    original.network.status.available = true;
    original.network.downBytesPerSecond = 1024.0 * 512.0;
    original.ping.status.available = true;
    original.ping.rttMs = 14.0;

    ASSERT_TRUE(pacecar::metrics::SaveSnapshotCache(original, path));

    MetricsSnapshot loaded;
    ASSERT_TRUE(pacecar::metrics::LoadSnapshotCache(path, loaded));
    EXPECT_EQ(loaded.sequence, 7u);
    EXPECT_EQ(loaded.cpu.cores.size(), 2u);
    EXPECT_DOUBLE_EQ(loaded.cpu.totalUtilizationPercent, 37.5);
    EXPECT_DOUBLE_EQ(loaded.cpu.totalFrequencyMhz, 4100.0);
    EXPECT_DOUBLE_EQ(loaded.memory.usedPercent, 48.25);
    EXPECT_DOUBLE_EQ(loaded.gpu.utilizationPercent, 22.0);
    EXPECT_DOUBLE_EQ(loaded.network.downBytesPerSecond, 524288.0);
    EXPECT_TRUE(loaded.ping.status.available);

    std::filesystem::remove(path, ec);
}

TEST(SnapshotCache, RejectsMissingAndCorruptFiles)
{
    MetricsSnapshot out;
    EXPECT_FALSE(pacecar::metrics::LoadSnapshotCache({}, out));

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / L"pacecar_snapshot_cache_bad.bin";
    std::error_code ec;
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "not a snapshot";
    }
    EXPECT_FALSE(pacecar::metrics::LoadSnapshotCache(path, out));
    std::filesystem::remove(path, ec);
}

TEST(TimerResolution, QueryIsConsistent)
{
    const pacecar::TimerResolution resolution = pacecar::QueryTimerResolution();
    if (resolution.valid)
    {
        EXPECT_GT(resolution.minimum100ns, 0u);
        EXPECT_GT(resolution.maximum100ns, 0u);
        EXPECT_GT(resolution.current100ns, 0u);
        EXPECT_GE(resolution.current100ns, resolution.minimum100ns);
        EXPECT_LE(resolution.current100ns, resolution.maximum100ns);
    }
    else
    {
        // An unavailable API must never be reported as a raised resolution.
        EXPECT_TRUE(pacecar::IsDefaultTimerResolution(resolution));
    }

    // The same probe taken twice (no `timeBeginPeriod` in between) reports no change.
    const pacecar::TimerResolution again = pacecar::QueryTimerResolution();
    EXPECT_TRUE(pacecar::SameTimerResolution(resolution, again));
}