#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "allocation_probe.h"
#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/metrics/MetricsSnapshot.h"

// Counts heap allocations across the whole test executable. The steady-state aggregation test
// snapshots this counter around a run of ticks and asserts it does not move. Other tests reuse the
// same counter through `allocation_probe.h`.
namespace pacecar::test
{
namespace
{
std::atomic<std::size_t> g_allocations{0};
} // namespace

std::size_t AllocationCount() noexcept
{
    return g_allocations.load(std::memory_order_relaxed);
}

void ResetAllocationCount() noexcept
{
    g_allocations.store(0, std::memory_order_relaxed);
}
} // namespace pacecar::test

void* operator new(std::size_t size)
{
    pacecar::test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (size == 0)
    {
        size = 1;
    }
    void* memory = std::malloc(size);
    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return memory;
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using namespace std::chrono_literals;

using pacecar::metrics::Aggregator;
using pacecar::metrics::IMetricProvider;
using pacecar::metrics::MetricDomain;
using pacecar::metrics::MetricsSnapshot;

constexpr std::uint32_t CpuDomain = static_cast<std::uint32_t>(MetricDomain::Cpu);
constexpr std::uint32_t MemoryDomain = static_cast<std::uint32_t>(MetricDomain::Memory);

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
        ++pollCalls;
        if (throwOnPoll)
        {
            throw std::runtime_error("provider boom");
        }
        if (fail)
        {
            return E_FAIL;
        }
        write(snapshot);
        return S_OK;
    }

    void Reset() noexcept override
    {
        ++resetCalls;
    }

    std::function<void(MetricsSnapshot&)> write = [](MetricsSnapshot&) {};
    bool fail = false;
    bool throwOnPoll = false;
    int pollCalls = 0;
    int resetCalls = 0;

  private:
    std::string name_;
    std::chrono::milliseconds cadence_;
    std::uint32_t domains_;
};

std::shared_ptr<FakeProvider> MakeCpuFake()
{
    auto provider = std::make_shared<FakeProvider>("cpu", 1000ms, CpuDomain);
    provider->write = [](MetricsSnapshot& snapshot) {
        if (snapshot.cpu.cores.size() != 2)
        {
            snapshot.cpu.cores.resize(2);
        }
        snapshot.cpu.totalUtilizationPercent = 42.0;
        snapshot.cpu.totalFrequencyMhz = 3600.0;
        snapshot.cpu.cores[0].utilizationPercent = 10.0;
        snapshot.cpu.cores[1].utilizationPercent = 20.0;
    };
    return provider;
}

TEST(Aggregator, NoSnapshotBeforeFirstTick)
{
    Aggregator aggregator(60);
    EXPECT_FALSE(static_cast<bool>(aggregator.LatestSnapshot()));
    EXPECT_EQ(aggregator.TickCount(), 0u);
    EXPECT_EQ(aggregator.PublishCount(), 0u);
}

TEST(Aggregator, PollsProvidersAtTheirCadenceAndPublishesEveryTick)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    aggregator.ReserveProviders(1);
    aggregator.AddProvider(cpu);

    EXPECT_TRUE(aggregator.Tick(0));
    EXPECT_TRUE(aggregator.Tick(500));
    EXPECT_TRUE(aggregator.Tick(1000));
    EXPECT_TRUE(aggregator.Tick(1500));
    EXPECT_TRUE(aggregator.Tick(2000));

    EXPECT_EQ(cpu->pollCalls, 3);
    EXPECT_EQ(aggregator.TickCount(), 5u);
    EXPECT_EQ(aggregator.PublishCount(), 5u);

    auto snapshot = aggregator.LatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->cpu.status.available);
    EXPECT_FALSE(snapshot->cpu.status.stale);
    EXPECT_EQ(snapshot->CoreCount(), 2u);
    EXPECT_DOUBLE_EQ(snapshot->cpu.totalFrequencyMhz, 3600.0);
}

TEST(Aggregator, DifferentProvidersRunAtDifferentCadencesInOneTick)
{
    Aggregator aggregator(60);
    auto fast = std::make_shared<FakeProvider>("fast", 1000ms, CpuDomain);
    auto slow = std::make_shared<FakeProvider>("slow", 3000ms, MemoryDomain);
    aggregator.AddProvider(fast);
    aggregator.AddProvider(slow);

    for (int step = 0; step <= 6; ++step)
    {
        aggregator.Tick(static_cast<std::uint64_t>(step) * 500u);
    }

    EXPECT_EQ(fast->pollCalls, 4);
    EXPECT_EQ(slow->pollCalls, 2);
    EXPECT_EQ(aggregator.ProviderPollCount(0), 4u);
    EXPECT_EQ(aggregator.ProviderPollCount(1), 2u);
}

TEST(Aggregator, FailingProviderDoesNotPreventOthers)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    auto memory = std::make_shared<FakeProvider>("memory", 1000ms, MemoryDomain);
    memory->fail = true;
    aggregator.AddProvider(cpu);
    aggregator.AddProvider(memory);

    aggregator.Tick(0);

    auto snapshot = aggregator.LatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->cpu.status.available);
    EXPECT_FALSE(snapshot->cpu.status.stale);
    EXPECT_FALSE(snapshot->memory.status.available);
    EXPECT_TRUE(snapshot->memory.status.stale);
    EXPECT_EQ(cpu->pollCalls, 1);
    EXPECT_EQ(aggregator.ProviderFailureCount(1), 1u);
}

TEST(Aggregator, ThrowingProviderIsIsolated)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    auto broken = std::make_shared<FakeProvider>("broken", 1000ms, MemoryDomain);
    broken->throwOnPoll = true;
    aggregator.AddProvider(cpu);
    aggregator.AddProvider(broken);

    EXPECT_NO_THROW(aggregator.Tick(0));

    auto snapshot = aggregator.LatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->cpu.status.available);
    EXPECT_FALSE(snapshot->memory.status.available);
    EXPECT_TRUE(snapshot->memory.status.stale);
    EXPECT_EQ(aggregator.ProviderFailureCount(1), 1u);
}

TEST(Aggregator, EmaSmoothsPercentagesAndHistoryKeepsRawValues)
{
    Aggregator aggregator(60);
    auto cpu = std::make_shared<FakeProvider>("cpu", 1000ms, CpuDomain);
    int call = 0;
    cpu->write = [&call](MetricsSnapshot& snapshot) {
        snapshot.cpu.totalUtilizationPercent = call == 0 ? 0.0 : 100.0;
        ++call;
    };
    aggregator.AddProvider(cpu);

    aggregator.Tick(0);
    aggregator.Tick(1000);

    auto snapshot = aggregator.LatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    // EMA (window 4 -> alpha 0.4) has only partially converged toward the raw 100.
    EXPECT_GT(snapshot->cpu.totalUtilizationPercent, 0.0);
    EXPECT_LT(snapshot->cpu.totalUtilizationPercent, 100.0);

    const auto& history = aggregator.History().cpuTotalUtilization;
    ASSERT_EQ(history.Size(), 2u);
    EXPECT_DOUBLE_EQ(history.At(0), 0.0);
    EXPECT_DOUBLE_EQ(history.At(1), 100.0);
}

TEST(Aggregator, HistoryReceivesOneSamplePerTickAtFixedCapacity)
{
    Aggregator aggregator(4);
    auto cpu = MakeCpuFake();
    aggregator.AddProvider(cpu);

    for (int i = 0; i < 6; ++i)
    {
        aggregator.Tick(static_cast<std::uint64_t>(i) * 1000u);
    }

    const auto& history = aggregator.History();
    EXPECT_EQ(history.Capacity(), 4u);
    EXPECT_EQ(history.cpuTotalUtilization.Size(), 4u);
    EXPECT_EQ(history.memoryUsedPercent.Size(), 4u);
    EXPECT_EQ(history.fps.Size(), 4u);
}

TEST(Aggregator, PublicationWakesOnlyWhenVisible)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    aggregator.AddProvider(cpu);
    int wakes = 0;
    aggregator.SetWakeCallback([&wakes] { ++wakes; });

    aggregator.Tick(0);
    aggregator.Tick(1000);
    EXPECT_EQ(wakes, 0);
    EXPECT_EQ(aggregator.WakeCount(), 0u);

    aggregator.SetVisible(true);
    aggregator.Tick(2000);
    EXPECT_EQ(wakes, 1);
    EXPECT_EQ(aggregator.WakeCount(), 1u);

    aggregator.SetVisible(false);
    aggregator.Tick(3000);
    EXPECT_EQ(wakes, 1);
}

TEST(Aggregator, SnapshotPublicationIsAPointerSwap)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    aggregator.AddProvider(cpu);
    aggregator.Tick(0);

    auto first = aggregator.LatestSnapshot();
    auto same = aggregator.LatestSnapshot();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first.get(), same.get());

    aggregator.Tick(1000);
    auto second = aggregator.LatestSnapshot();
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get());
    EXPECT_GT(second->sequence, first->sequence);
}

TEST(Aggregator, ResetClearsProvidersAndState)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    aggregator.AddProvider(cpu);
    aggregator.Tick(0);
    aggregator.Reset();
    EXPECT_EQ(cpu->resetCalls, 1);
    EXPECT_EQ(aggregator.TickCount(), 0u);
}

TEST(Aggregator, NoAllocationInSteadyState)
{
    Aggregator aggregator(60);
    auto cpu = MakeCpuFake();
    auto memory = std::make_shared<FakeProvider>("memory", 1000ms, MemoryDomain);
    memory->write = [](MetricsSnapshot& snapshot) {
        snapshot.memory.usedPercent = 55.0;
        snapshot.memory.usedBytes = 8ull * 1024 * 1024 * 1024;
    };
    aggregator.AddProvider(cpu);
    aggregator.AddProvider(memory);
    aggregator.SetWakeCallback([] {});

    // Warm up: the first tick sizes EMA vectors and core arrays within their reserved capacities.
    aggregator.Tick(0);

    const std::size_t before = pacecar::test::AllocationCount();
    for (int i = 1; i <= 64; ++i)
    {
        aggregator.Tick(static_cast<std::uint64_t>(i) * 1000u);
    }
    const std::size_t after = pacecar::test::AllocationCount();
    EXPECT_EQ(after, before);
}
} // namespace