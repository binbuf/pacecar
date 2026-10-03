#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/CpuProvider.h"
#include "pacecar/metrics/CpuTimes.h"

namespace
{
using pacecar::metrics::Aggregator;
using pacecar::metrics::ComputeBusyPercent;
using pacecar::metrics::ComputeDynamicFrequencyMhz;
using pacecar::metrics::CpuFrequencyInputs;
using pacecar::metrics::CpuFrequencyResult;
using pacecar::metrics::CpuFrequencySource;
using pacecar::metrics::CpuProvider;
using pacecar::metrics::CpuSampleSource;
using pacecar::metrics::CpuTimeCounters;
using pacecar::metrics::IAcpiTemperatureSource;
using pacecar::metrics::ICpuSystemSource;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::ParseProcessorPerformance;
using pacecar::metrics::ProcessorPerformanceInfo;
using pacecar::metrics::SelectFrequency;
using pacecar::metrics::SumCounters;
using pacecar::metrics::ToCounters;

// --- synthetic helpers -----------------------------------------------------

// Builds cumulative counters where `kernel` already includes `idle` (the NT/GetSystemTimes rule),
// so a delta of idle `idleDelta` and busy `busyDelta` yields busy/(busy+idle) utilization.
CpuTimeCounters MakeCounters(std::uint64_t idle, std::uint64_t busy) noexcept
{
    CpuTimeCounters counters;
    counters.idle = idle;
    counters.kernel = idle + busy;
    counters.user = 0;
    return counters;
}

ProcessorPerformanceInfo MakeRecord(std::int64_t idle, std::int64_t kernel, std::int64_t user)
{
    ProcessorPerformanceInfo info;
    info.idleTime = idle;
    info.kernelTime = kernel;
    info.userTime = user;
    info.dpcTime = 0;
    info.interruptTime = 0;
    info.interruptCount = 0;
    return info;
}

class FakeCpuSource final : public ICpuSystemSource
{
  public:
    [[nodiscard]] std::size_t CoreCount() override
    {
        return coreCount;
    }

    bool ReadPerCore(std::vector<CpuTimeCounters>& out) override
    {
        if (!ntAvailable)
        {
            return false;
        }
        out = cores;
        return true;
    }

    bool ReadSystemTotal(CpuTimeCounters& out) override
    {
        if (!systemTimesAvailable)
        {
            return false;
        }
        out = total;
        return true;
    }

    std::size_t coreCount = 2;
    bool ntAvailable = true;
    bool systemTimesAvailable = true;
    std::vector<CpuTimeCounters> cores;
    CpuTimeCounters total;
};

class FakeTemperature final : public IAcpiTemperatureSource
{
  public:
    bool ReadCelsius(double& celsius) override
    {
        if (!present)
        {
            return false;
        }
        celsius = valueC;
        return true;
    }

    bool present = true;
    double valueC = 42.5;
};

// --- pure math -------------------------------------------------------------

TEST(CpuTimes, DeltaToPercentMatchesKnownFractions)
{
    const CpuTimeCounters base = MakeCounters(0, 0);

    EXPECT_DOUBLE_EQ(ComputeBusyPercent(base, MakeCounters(25, 75)), 75.0);
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(base, MakeCounters(50, 50)), 50.0);
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(base, MakeCounters(0, 100)), 100.0);
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(base, MakeCounters(100, 0)), 0.0);

    // Delta between two non-zero samples uses only the interval, not the total.
    const CpuTimeCounters first = MakeCounters(1000, 1000);
    const CpuTimeCounters second = MakeCounters(1020, 1080);
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(first, second), 80.0);
}

TEST(CpuTimes, DeltaToPercentHandlesDegenerateInput)
{
    const CpuTimeCounters zero = MakeCounters(0, 0);
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(zero, zero), 0.0);

    // Counters that moved backwards (reset/reload) must not underflow into a huge percentage.
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(MakeCounters(100, 100), MakeCounters(0, 0)), 0.0);

    // Idle exceeding total clamps to zero.
    CpuTimeCounters odd;
    odd.idle = 200;
    odd.kernel = 100;
    odd.user = 0;
    EXPECT_DOUBLE_EQ(ComputeBusyPercent(zero, odd), 0.0);
}

TEST(CpuTimes, SumCountersAddsEveryField)
{
    std::vector<CpuTimeCounters> cores;
    CpuTimeCounters a;
    a.idle = 1;
    a.kernel = 2;
    a.user = 3;
    a.dpc = 4;
    a.interrupt = 5;
    CpuTimeCounters b;
    b.idle = 10;
    b.kernel = 20;
    b.user = 30;
    b.dpc = 40;
    b.interrupt = 50;
    cores.push_back(a);
    cores.push_back(b);

    const CpuTimeCounters total = SumCounters(cores);
    EXPECT_EQ(total.idle, 11u);
    EXPECT_EQ(total.kernel, 22u);
    EXPECT_EQ(total.user, 33u);
    EXPECT_EQ(total.dpc, 44u);
    EXPECT_EQ(total.interrupt, 55u);
    EXPECT_EQ(SumCounters({}).idle, 0u);
}

TEST(CpuTimes, NtRecordLayoutAndConversion)
{
    EXPECT_EQ(sizeof(ProcessorPerformanceInfo), 48u);

    ProcessorPerformanceInfo info = MakeRecord(10, 20, 30);
    info.dpcTime = 40;
    info.interruptTime = 50;
    info.interruptCount = 7;

    const CpuTimeCounters counters = ToCounters(info);
    EXPECT_EQ(counters.idle, 10u);
    EXPECT_EQ(counters.kernel, 20u);
    EXPECT_EQ(counters.user, 30u);
    EXPECT_EQ(counters.dpc, 40u);
    EXPECT_EQ(counters.interrupt, 50u);
}

// --- NT structure parsing --------------------------------------------------

TEST(CpuTimes, ParseReadsWholeRecordsAndIgnoresPartialTail)
{
    const std::vector<ProcessorPerformanceInfo> buffer{
        MakeRecord(1, 2, 3),
        MakeRecord(4, 5, 6),
        MakeRecord(7, 8, 9),
    };
    const std::size_t fullBytes = buffer.size() * sizeof(ProcessorPerformanceInfo);

    std::vector<ProcessorPerformanceInfo> parsed;
    EXPECT_EQ(ParseProcessorPerformance(buffer.data(), fullBytes, buffer.size(), parsed), 3u);
    ASSERT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed[2].userTime, 9);

    // A trailing partial record is ignored.
    EXPECT_EQ(ParseProcessorPerformance(buffer.data(), fullBytes - 5, 3, parsed), 2u);
    ASSERT_EQ(parsed.size(), 2u);

    // maxRecords caps how many whole records are copied.
    EXPECT_EQ(ParseProcessorPerformance(buffer.data(), fullBytes, 1, parsed), 1u);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].idleTime, 1);

    // Null / undersized buffers are safe.
    EXPECT_EQ(ParseProcessorPerformance(nullptr, fullBytes, 3, parsed), 0u);
    EXPECT_EQ(ParseProcessorPerformance(buffer.data(), sizeof(ProcessorPerformanceInfo) - 1, 3, parsed), 0u);
}

// --- frequency selection ---------------------------------------------------

TEST(CpuProviderFrequency, DynamicFrequencyScalesBaseByPerformance)
{
    EXPECT_DOUBLE_EQ(ComputeDynamicFrequencyMhz(100.0, 3600.0), 3600.0);
    EXPECT_DOUBLE_EQ(ComputeDynamicFrequencyMhz(50.0, 3600.0), 1800.0);
    EXPECT_DOUBLE_EQ(ComputeDynamicFrequencyMhz(0.0, 3600.0), 0.0);
    EXPECT_DOUBLE_EQ(ComputeDynamicFrequencyMhz(100.0, 0.0), 0.0);
}

TEST(CpuProviderFrequency, SelectsPdhPairFirst)
{
    CpuFrequencyInputs inputs;
    inputs.pdhPairAvailable = true;
    inputs.pdhPercentPerformance = 50.0;
    inputs.pdhProcessorFrequencyMhz = 4000.0;
    inputs.powerInformationAvailable = true;
    inputs.powerInformationMhz = 3900.0;
    inputs.baseClockAvailable = true;
    inputs.baseClockMhz = 3000.0;

    const CpuFrequencyResult result = SelectFrequency(inputs);
    EXPECT_EQ(result.source, CpuFrequencySource::PdhPair);
    EXPECT_DOUBLE_EQ(result.mhz, 2000.0);
}

TEST(CpuProviderFrequency, FallsBackInOrderWhenCountersAreAbsent)
{
    // PDH pair "present" but degenerate -> must not be selected.
    CpuFrequencyInputs inputs;
    inputs.pdhPairAvailable = true;
    inputs.pdhPercentPerformance = 0.0;
    inputs.pdhProcessorFrequencyMhz = 0.0;
    inputs.powerInformationAvailable = true;
    inputs.powerInformationMhz = 2750.0;
    EXPECT_EQ(SelectFrequency(inputs).source, CpuFrequencySource::PowerInformation);
    EXPECT_DOUBLE_EQ(SelectFrequency(inputs).mhz, 2750.0);

    // Only the base clock remains.
    inputs.powerInformationAvailable = false;
    inputs.baseClockAvailable = true;
    inputs.baseClockMhz = 3200.0;
    EXPECT_EQ(SelectFrequency(inputs).source, CpuFrequencySource::BaseClock);
    EXPECT_DOUBLE_EQ(SelectFrequency(inputs).mhz, 3200.0);

    // Nothing available.
    inputs.baseClockAvailable = false;
    const CpuFrequencyResult none = SelectFrequency(inputs);
    EXPECT_EQ(none.source, CpuFrequencySource::Unavailable);
    EXPECT_DOUBLE_EQ(none.mhz, 0.0);
}

// --- provider behavior -----------------------------------------------------

TEST(CpuProvider, ReportsNameDomainAndCadence)
{
    auto source = std::make_unique<FakeCpuSource>();
    CpuProvider provider(std::move(source), nullptr);
    EXPECT_STREQ(provider.Name(), "cpu");
    EXPECT_EQ(provider.Domains(), static_cast<std::uint32_t>(pacecar::metrics::MetricDomain::Cpu));
    EXPECT_EQ(provider.Cadence(), std::chrono::milliseconds(1000));
}

TEST(CpuProvider, FirstSampleIsIncompleteThenPublishesUtilization)
{
    auto source = std::make_unique<FakeCpuSource>();
    FakeCpuSource* fake = source.get();
    CpuProvider provider(std::move(source), nullptr);

    fake->coreCount = 2;
    fake->cores = {MakeCounters(0, 0), MakeCounters(0, 0)};

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.SampleSource(), CpuSampleSource::NtPerCore);

    // 75% and 50% busy on the two cores over the interval.
    fake->cores = {MakeCounters(25, 75), MakeCounters(50, 50)};
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    ASSERT_EQ(snapshot.cpu.cores.size(), 2u);
    EXPECT_DOUBLE_EQ(snapshot.cpu.cores[0].utilizationPercent, 75.0);
    EXPECT_DOUBLE_EQ(snapshot.cpu.cores[1].utilizationPercent, 50.0);
    // Total: idle delta 75, total delta 200 -> 62.5% busy.
    EXPECT_DOUBLE_EQ(snapshot.cpu.totalUtilizationPercent, 62.5);
}

TEST(CpuProvider, BufferSizingFollowsReportedCoreCount)
{
    for (const std::size_t coreCount : {1u, 4u, 64u, 257u})
    {
        auto source = std::make_unique<FakeCpuSource>();
        FakeCpuSource* fake = source.get();
        CpuProvider provider(std::move(source), nullptr);

        fake->coreCount = coreCount;
        fake->cores.assign(coreCount, MakeCounters(0, 0));

        MetricsSnapshot snapshot;
        EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

        fake->cores.assign(coreCount, MakeCounters(0, 100));
        EXPECT_EQ(provider.Poll(snapshot), S_OK);

        EXPECT_EQ(provider.CoreCount(), coreCount);
        EXPECT_EQ(snapshot.cpu.cores.size(), coreCount);
        EXPECT_DOUBLE_EQ(snapshot.cpu.totalUtilizationPercent, 100.0);
        for (const auto& core : snapshot.cpu.cores)
        {
            EXPECT_DOUBLE_EQ(core.utilizationPercent, 100.0);
        }
    }
}

TEST(CpuProvider, GetSystemTimesFallbackIsChosenWhenNtUnavailable)
{
    auto source = std::make_unique<FakeCpuSource>();
    FakeCpuSource* fake = source.get();
    CpuProvider provider(std::move(source), nullptr);

    fake->ntAvailable = false;
    fake->total = MakeCounters(0, 0);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.SampleSource(), CpuSampleSource::SystemTimesFallback);

    fake->total = MakeCounters(30, 70);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_EQ(provider.SampleSource(), CpuSampleSource::SystemTimesFallback);
    EXPECT_TRUE(snapshot.cpu.cores.empty());
    EXPECT_DOUBLE_EQ(snapshot.cpu.totalUtilizationPercent, 70.0);
}

TEST(CpuProvider, BothPathsUnavailableFailsWithoutTouchingBaseline)
{
    auto source = std::make_unique<FakeCpuSource>();
    FakeCpuSource* fake = source.get();
    CpuProvider provider(std::move(source), nullptr);
    fake->ntAvailable = false;
    fake->systemTimesAvailable = false;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(provider.SampleSource(), CpuSampleSource::None);
}

TEST(CpuProvider, AcpiTemperatureIsLabeledAndFlagged)
{
    auto source = std::make_unique<FakeCpuSource>();
    CpuProvider provider(std::move(source), std::make_unique<FakeTemperature>());

    MetricsSnapshot snapshot;
    provider.Poll(snapshot);

    EXPECT_TRUE(snapshot.cpu.temperatureIsAcpi);
    EXPECT_TRUE(snapshot.cpu.temperatureStatus.available);
    EXPECT_FALSE(snapshot.cpu.temperatureStatus.stale);
    EXPECT_DOUBLE_EQ(snapshot.cpu.packageTemperatureC, 42.5);
}

TEST(CpuProvider, AbsentAcpiTemperatureStaysUnavailable)
{
    auto source = std::make_unique<FakeCpuSource>();
    auto temperature = std::make_unique<FakeTemperature>();
    temperature->present = false;
    CpuProvider provider(std::move(source), std::move(temperature));

    MetricsSnapshot snapshot;
    provider.Poll(snapshot);

    EXPECT_FALSE(snapshot.cpu.temperatureIsAcpi);
    EXPECT_FALSE(snapshot.cpu.temperatureStatus.available);
    EXPECT_TRUE(snapshot.cpu.temperatureStatus.stale);
}

TEST(CpuProvider, ResetDropsBaselineSoNextPollIsIncomplete)
{
    auto source = std::make_unique<FakeCpuSource>();
    FakeCpuSource* fake = source.get();
    CpuProvider provider(std::move(source), nullptr);

    fake->cores = {MakeCounters(0, 0), MakeCounters(0, 0)};
    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    fake->cores = {MakeCounters(0, 100), MakeCounters(0, 100)};
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    provider.Reset();
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
}

// --- aggregator integration ------------------------------------------------

TEST(CpuProvider, AggregatorMarksFirstSampleStaleThenAvailable)
{
    Aggregator aggregator(60);
    auto source = std::make_unique<FakeCpuSource>();
    FakeCpuSource* fake = source.get();
    fake->coreCount = 2;
    fake->cores = {MakeCounters(0, 0), MakeCounters(0, 0)};

    aggregator.AddProvider(std::make_shared<CpuProvider>(std::move(source), nullptr));

    EXPECT_TRUE(aggregator.Tick(0));
    auto first = aggregator.LatestSnapshot();
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->cpu.status.stale);
    EXPECT_FALSE(first->cpu.status.available);

    fake->cores = {MakeCounters(25, 75), MakeCounters(50, 50)};
    EXPECT_TRUE(aggregator.Tick(1000));
    auto second = aggregator.LatestSnapshot();
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->cpu.status.available);
    EXPECT_FALSE(second->cpu.status.stale);
    EXPECT_EQ(second->CoreCount(), 2u);
}

// --- real-hardware smoke (skipped when the NT path is unavailable) ---------

TEST(CpuProvider, RealMachineProducesPlausibleUtilization)
{
    CpuProvider provider;
    MetricsSnapshot snapshot;

    const HRESULT first = provider.Poll(snapshot);
    if (first == E_FAIL || provider.SampleSource() == CpuSampleSource::None)
    {
        GTEST_SKIP() << "NtQuerySystemInformation and GetSystemTimes both unavailable";
    }
    EXPECT_EQ(first, E_PENDING); // first sample is always incomplete

    ::Sleep(120);
    const HRESULT second = provider.Poll(snapshot);
    ASSERT_EQ(second, S_OK);

    EXPECT_GE(snapshot.cpu.totalUtilizationPercent, 0.0);
    EXPECT_LE(snapshot.cpu.totalUtilizationPercent, 100.0);

    if (provider.SampleSource() == CpuSampleSource::NtPerCore)
    {
        ASSERT_FALSE(snapshot.cpu.cores.empty());
        EXPECT_EQ(snapshot.cpu.cores.size(), provider.CoreCount());
        double sum = 0.0;
        for (const auto& core : snapshot.cpu.cores)
        {
            EXPECT_GE(core.utilizationPercent, 0.0);
            EXPECT_LE(core.utilizationPercent, 100.0);
            sum += core.utilizationPercent;
        }
        const double average = sum / static_cast<double>(snapshot.cpu.cores.size());
        EXPECT_NEAR(snapshot.cpu.totalUtilizationPercent, average, 25.0);
    }
}
} // namespace