#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pacecar/metrics/GpuD3dKmtProvider.h"
#include "pacecar/metrics/GpuPdhProvider.h"

namespace
{
using pacecar::metrics::AggregateGpuEnginesByType;
using pacecar::metrics::ComputeElapsedCorrectPercent;
using pacecar::metrics::FormatGpuLuid;
using pacecar::metrics::GpuAdapterInfo;
using pacecar::metrics::GpuD3dKmtProvider;
using pacecar::metrics::GpuEngineCounterSample;
using pacecar::metrics::GpuEngineInstance;
using pacecar::metrics::GpuEngineMetrics;
using pacecar::metrics::GpuEngineReading;
using pacecar::metrics::GpuLuid;
using pacecar::metrics::GpuPdhProvider;
using pacecar::metrics::GpuSelection;
using pacecar::metrics::GpuSelectionKind;
using pacecar::metrics::IGpuAdapterEnumerator;
using pacecar::metrics::IGpuD3dKmtSource;
using pacecar::metrics::IGpuElapsedClock;
using pacecar::metrics::IGpuEngineSource;
using pacecar::metrics::kGpuNameCapacity;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::ParseGpuEngineInstance;
using pacecar::metrics::ParseGpuSelection;
using pacecar::metrics::PrimaryGpuPercent;
using pacecar::metrics::ResolveGpuAdapter;

// --- synthetic helpers -----------------------------------------------------

void CopyText(char* destination, std::size_t capacity, const char* source) noexcept
{
    const std::size_t length = std::strlen(source);
    const std::size_t count = std::min(length + 1, capacity);
    std::memcpy(destination, source, count);
    if (count != 0)
    {
        destination[count - 1] = '\0';
    }
}

GpuAdapterInfo MakeAdapter(const char* name, std::uint32_t high, std::uint32_t low,
                           bool software = false)
{
    GpuAdapterInfo adapter{};
    adapter.luid.high = high;
    adapter.luid.low = low;
    adapter.isSoftware = software;
    CopyText(adapter.name, kGpuNameCapacity, name);
    return adapter;
}

GpuEngineCounterSample MakeSample(const char* name, double utilization, bool valid = true)
{
    GpuEngineCounterSample sample{};
    CopyText(sample.instanceName, sizeof(sample.instanceName), name);
    sample.utilizationPercent = utilization;
    sample.utilizationValid = valid;
    return sample;
}

class FakeGpuEngineSource final : public IGpuEngineSource
{
  public:
    [[nodiscard]] bool IsAvailable() override
    {
        return available;
    }

    bool Read(std::vector<GpuEngineCounterSample>& out) override
    {
        ++readCount;
        if (!readAvailable)
        {
            return false;
        }
        out = samples;
        return true;
    }

    bool available = true;
    bool readAvailable = true;
    int readCount = 0;
    std::vector<GpuEngineCounterSample> samples;
};

class FakeAdapterEnumerator final : public IGpuAdapterEnumerator
{
  public:
    bool Enumerate(std::vector<GpuAdapterInfo>& out) override
    {
        out = adapters;
        return true;
    }

    std::vector<GpuAdapterInfo> adapters;
};

class FakeD3dKmtSource final : public IGpuD3dKmtSource
{
  public:
    [[nodiscard]] bool IsAvailable() override
    {
        return available;
    }

    bool Enumerate(std::vector<GpuAdapterInfo>& out) override
    {
        out = adapters;
        return true;
    }

    bool QueryRunningTime(const GpuAdapterInfo&, std::uint64_t& runningTime) override
    {
        if (!queryOk)
        {
            return false;
        }
        runningTime = running;
        return true;
    }

    bool available = true;
    bool queryOk = true;
    std::uint64_t running = 0;
    std::vector<GpuAdapterInfo> adapters;
};

class FakeClock final : public IGpuElapsedClock
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

// --- instance-name parsing -------------------------------------------------

TEST(GpuEngineParser, ParsesRealNvidiaAndAmdInstances)
{
    GpuEngineInstance instance;

    ASSERT_TRUE(ParseGpuEngineInstance(
        "pid_12792_luid_0x00000000_0x0001b790_phys_0_eng_0_engtype_3d", instance));
    EXPECT_EQ(instance.pid, 12792u);
    EXPECT_EQ(instance.luid.high, 0u);
    EXPECT_EQ(instance.luid.low, 0x1b790u);
    EXPECT_EQ(instance.physicalAdapter, 0u);
    EXPECT_EQ(instance.engine, 0u);
    EXPECT_STREQ(instance.engineType, "3d");

    ASSERT_TRUE(ParseGpuEngineInstance(
        "pid_4_luid_0x00000000_0x0001d0a3_phys_0_eng_5_engtype_3d", instance));
    EXPECT_EQ(instance.pid, 4u);
    EXPECT_EQ(instance.luid.low, 0x1d0a3u);
    EXPECT_EQ(instance.engine, 5u);

    // Engine names with spaces are normalized to underscores.
    ASSERT_TRUE(ParseGpuEngineInstance(
        "pid_13676_luid_0x00000000_0x0001d0e5_phys_0_eng_7_engtype_high priority 3d", instance));
    EXPECT_STREQ(instance.engineType, "high_priority_3d");

    // Underscored driver spellings stay as-is.
    ASSERT_TRUE(ParseGpuEngineInstance(
        "pid_5524_luid_0x00000000_0x0001b790_phys_0_eng_10_engtype_graphics_1", instance));
    EXPECT_STREQ(instance.engineType, "graphics_1");
}

TEST(GpuEngineParser, HandlesDuplicateSuffixWrapperAndCase)
{
    GpuEngineInstance instance;
    // PDH appends #<n> to duplicate instance names.
    ASSERT_TRUE(ParseGpuEngineInstance(
        "pid_1_luid_0x00000000_0x0000000A_phys_0_eng_0_engtype_3D#3", instance));
    EXPECT_EQ(instance.luid.low, 0xAu);
    EXPECT_STREQ(instance.engineType, "3d");
    // Optional wrapper and uppercase prefix tokens parse too.
    ASSERT_TRUE(ParseGpuEngineInstance(
        "\\GPU Engine(pid_9_luid_0x00000000_0x0000000B_phys_1_eng_2_engtype_copy)", instance));
    EXPECT_EQ(instance.pid, 9u);
    EXPECT_EQ(instance.physicalAdapter, 1u);
    EXPECT_EQ(instance.engine, 2u);
    EXPECT_STREQ(instance.engineType, "copy");
}

TEST(GpuEngineParser, RejectsMalformedNames)
{
    GpuEngineInstance instance;
    EXPECT_FALSE(ParseGpuEngineInstance("", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("pid_", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("pid_abc_luid_0x0_0x0_phys_0_eng_0_engtype_3d", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("pid_1_luid_0x0_0xZZ_phys_0_eng_0_engtype_3d", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("pid_1_luid_0x0_0x1", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_", instance));
    EXPECT_FALSE(ParseGpuEngineInstance("not_an_instance_at_all", instance));
}

// --- aggregation -----------------------------------------------------------

GpuEngineReading MakeReading(const char* type, double utilization)
{
    GpuEngineReading reading{};
    CopyText(reading.instance.engineType, sizeof(reading.instance.engineType), type);
    reading.utilizationPercent = utilization;
    return reading;
}

TEST(GpuAggregation, UsesMaximumPerTypeNotSum)
{
    const std::vector<GpuEngineReading> readings{
        MakeReading("3d", 40.0),
        MakeReading("3d", 70.0),
        MakeReading("copy", 20.0),
        MakeReading("copy", 10.0),
    };
    std::vector<GpuEngineMetrics> engines;
    AggregateGpuEnginesByType(readings, engines);

    ASSERT_EQ(engines.size(), 2u);
    // The rule is max, so two 40%/70% instances report 70%, never 110%.
    EXPECT_STREQ(engines[0].engineType, "3d");
    EXPECT_DOUBLE_EQ(engines[0].utilizationPercent, 70.0);
    EXPECT_STREQ(engines[1].engineType, "copy");
    EXPECT_DOUBLE_EQ(engines[1].utilizationPercent, 20.0);
    for (const auto& engine : engines)
    {
        EXPECT_LE(engine.utilizationPercent, 100.0);
    }
}

TEST(GpuAggregation, PrimaryPrefers3dThenFallsBackToMax)
{
    std::vector<GpuEngineMetrics> engines;
    GpuEngineMetrics three{};
    CopyText(three.engineType, sizeof(three.engineType), "3d");
    three.utilizationPercent = 30.0;
    GpuEngineMetrics video{};
    CopyText(video.engineType, sizeof(video.engineType), "videodecode");
    video.utilizationPercent = 80.0;
    engines.push_back(three);
    engines.push_back(video);
    EXPECT_DOUBLE_EQ(PrimaryGpuPercent(engines), 30.0);

    // Without 3D, the maximum engine wins.
    EXPECT_DOUBLE_EQ(PrimaryGpuPercent({video}), 80.0);
    EXPECT_DOUBLE_EQ(PrimaryGpuPercent({}), 0.0);
}

// --- percentage math -------------------------------------------------------

TEST(GpuPercentMath, VariableElapsedWindowsAreCorrect)
{
    constexpr std::uint64_t kFallbackTicksPerSecond = 10'000'000ull; // QPC on this machine

    // 0.5 s of GPU work in a 1 s window -> 50%.
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(5'000'000, kFallbackTicksPerSecond,
                                                  kFallbackTicksPerSecond),
                     50.0);
    // The same running delta in a 0.25 s window is 200%, shown clamped to 100%.
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(5'000'000, 2'500'000, kFallbackTicksPerSecond),
                     100.0);
    // ... and in a 5 s window it is 10%.
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(5'000'000, 50'000'000, kFallbackTicksPerSecond),
                     10.0);
    // The legacy fixed-window bug: a small delta must not be inflated by an assumed 1 s window.
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(1'000'000, 250'000, kFallbackTicksPerSecond), 100.0);
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(1'000'000, 10'000'000, kFallbackTicksPerSecond), 10.0);

    // Degenerate inputs never produce a percentage.
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(0, 10'000'000, kFallbackTicksPerSecond), 0.0);
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(5'000'000, 0, kFallbackTicksPerSecond), 0.0);
    EXPECT_DOUBLE_EQ(ComputeElapsedCorrectPercent(5'000'000, 10'000'000, 0), 0.0);
}

TEST(GpuLuidFormat, MatchesPdhSpelling)
{
    EXPECT_EQ(FormatGpuLuid(GpuLuid{0, 0x1b790}), "0x00000000_0x0001b790");
    EXPECT_EQ(FormatGpuLuid(GpuLuid{0xDEADBEEF, 0x00000001}), "0xdeadbeef_0x00000001");
}

// --- adapter selection -----------------------------------------------------

TEST(GpuAdapterSelection, ParsesAutoIndexAndName)
{
    EXPECT_EQ(ParseGpuSelection("").kind, GpuSelectionKind::Auto);
    EXPECT_EQ(ParseGpuSelection("auto").kind, GpuSelectionKind::Auto);
    EXPECT_EQ(ParseGpuSelection("AUTO").kind, GpuSelectionKind::Auto);

    const GpuSelection index = ParseGpuSelection("2");
    EXPECT_EQ(index.kind, GpuSelectionKind::Index);
    EXPECT_EQ(index.index, 2u);

    const GpuSelection name = ParseGpuSelection("Radeon");
    EXPECT_EQ(name.kind, GpuSelectionKind::Name);
    EXPECT_STREQ(name.name, "radeon");
}

TEST(GpuAdapterSelection, ResolvesToTheCorrectAdapter)
{
    const std::vector<GpuAdapterInfo> adapters{
        MakeAdapter("Microsoft Basic Render Driver", 0, 1, true),
        MakeAdapter("NVIDIA GeForce RTX 4080", 0, 0x1b790),
        MakeAdapter("AMD Radeon(TM) Graphics", 0, 0x1d0a3),
    };

    // Auto picks the first non-software adapter, skipping the Basic Render Driver.
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("auto"), adapters), 1u);
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("2"), adapters), 2u);
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("radeon"), adapters), 2u);
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("rtx"), adapters), 1u);
    // Out-of-range index and unmatched name fall back to Auto rather than failing.
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("99"), adapters), 1u);
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("matrox"), adapters), 1u);
    // No adapters is the only failure.
    EXPECT_EQ(ResolveGpuAdapter(ParseGpuSelection("auto"), {}), pacecar::metrics::kNoGpuAdapter);
}

// --- D3DKMT fallback provider ----------------------------------------------

TEST(GpuD3dKmtProvider, FirstPollIsPendingThenPublishesElapsedCorrectValue)
{
    auto source = std::make_unique<FakeD3dKmtSource>();
    FakeD3dKmtSource* fakeSource = source.get();
    fakeSource->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080", 0, 0x1b790)};

    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();

    GpuD3dKmtProvider provider(std::move(source), std::move(clock), "auto");
    ASSERT_TRUE(provider.Available());
    EXPECT_STREQ(provider.SelectedAdapterName(), "NVIDIA GeForce RTX 4080");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    // 0.5 s of node running time over a 1 s wall window -> 50%.
    fakeSource->running = 5'000'000;
    fakeClock->now = 10'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 50.0);
    EXPECT_STREQ(snapshot.gpu.name, "NVIDIA GeForce RTX 4080");

    // A shorter 0.25 s window with a 0.25 s delta keeps the percentage correct.
    fakeSource->running += 2'500'000;
    fakeClock->now += 2'500'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 100.0);
}

TEST(GpuD3dKmtProvider, UnavailableSourceFailsCleanly)
{
    auto source = std::make_unique<FakeD3dKmtSource>();
    source->available = false;
    GpuD3dKmtProvider provider(std::move(source), std::make_unique<FakeClock>(), "auto");
    EXPECT_FALSE(provider.Available());

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
}

// --- PDH provider: aggregation, filtering, fallback ------------------------

std::vector<GpuAdapterInfo> TwoAdapters()
{
    return {
        MakeAdapter("NVIDIA GeForce RTX 4080", 0, 0x1b790),
        MakeAdapter("AMD Radeon(TM) Graphics", 0, 0x1d0a3),
    };
}

std::vector<GpuEngineCounterSample> CrossAdapterSamples()
{
    return {
        MakeSample("pid_100_luid_0x00000000_0x0001b790_phys_0_eng_0_engtype_3d", 40.0),
        MakeSample("pid_100_luid_0x00000000_0x0001b790_phys_0_eng_1_engtype_3d", 70.0),
        MakeSample("pid_100_luid_0x00000000_0x0001b790_phys_0_eng_2_engtype_copy", 20.0),
        MakeSample("pid_100_luid_0x00000000_0x0001d0a3_phys_0_eng_0_engtype_3d", 90.0),
        MakeSample("pid_200_luid_0x00000000_0x0001b790_phys_0_eng_0_engtype_3d", 55.0),
        MakeSample("malformed_instance", 99.0),
    };
}

TEST(GpuPdhProvider, AggregatesAcrossTheSelectedAdapterSystemWide)
{
    auto source = std::make_unique<FakeGpuEngineSource>();
    source->samples = CrossAdapterSamples();
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = TwoAdapters();

    GpuPdhProvider provider(std::move(source), std::move(adapters), nullptr, nullptr, "0");
    EXPECT_TRUE(provider.PdhAvailable());
    EXPECT_EQ(provider.ActiveBackend(), GpuPdhProvider::Backend::Pdh);
    EXPECT_STREQ(provider.SelectedAdapterName(), "NVIDIA GeForce RTX 4080");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING); // rate counter needs one baseline
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    // Only the NVIDIA (index 0) LUID survives; 3d is the max of 40/70/55, copy is 20.
    ASSERT_EQ(snapshot.gpu.engines.size(), 2u);
    EXPECT_STREQ(snapshot.gpu.engines[0].engineType, "3d");
    EXPECT_DOUBLE_EQ(snapshot.gpu.engines[0].utilizationPercent, 70.0);
    EXPECT_STREQ(snapshot.gpu.engines[1].engineType, "copy");
    EXPECT_DOUBLE_EQ(snapshot.gpu.engines[1].utilizationPercent, 20.0);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 70.0);
}

TEST(GpuPdhProvider, AdapterSelectionMapsConfigToTheCorrectInstanceSet)
{
    auto source = std::make_unique<FakeGpuEngineSource>();
    source->samples = CrossAdapterSamples();
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = TwoAdapters();

    GpuPdhProvider provider(std::move(source), std::move(adapters), nullptr, nullptr, "1");
    EXPECT_STREQ(provider.SelectedAdapterName(), "AMD Radeon(TM) Graphics");

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);

    ASSERT_EQ(snapshot.gpu.engines.size(), 1u);
    EXPECT_STREQ(snapshot.gpu.engines[0].engineType, "3d");
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 90.0);
}

TEST(GpuPdhProvider, SelectedPidFiltersToThatProcessOnly)
{
    auto source = std::make_unique<FakeGpuEngineSource>();
    source->samples = CrossAdapterSamples();
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = TwoAdapters();

    GpuPdhProvider provider(std::move(source), std::move(adapters), nullptr, nullptr, "0");
    provider.SetTargetPid(200);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 55.0);

    // A PID with no instances yields an empty, zero aggregate rather than an error.
    provider.SetTargetPid(999);
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_EQ(snapshot.gpu.utilizationPercent, 0.0);
    EXPECT_TRUE(snapshot.gpu.engines.empty());
}

TEST(GpuPdhProvider, MissingCounterSetReportsUnavailableAndFallsBackToD3dKmt)
{
    auto pdh = std::make_unique<FakeGpuEngineSource>();
    pdh->available = false; // the "GPU Engine" counter set is absent

    auto kmt = std::make_unique<FakeD3dKmtSource>();
    FakeD3dKmtSource* fakeKmt = kmt.get();
    fakeKmt->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080", 0, 0x1b790)};
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();

    GpuPdhProvider provider(std::move(pdh), std::make_unique<FakeAdapterEnumerator>(),
                            std::move(kmt), std::move(clock), "auto");

    EXPECT_FALSE(provider.PdhAvailable());
    EXPECT_EQ(provider.ActiveBackend(), GpuPdhProvider::Backend::D3dKmt);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);
    fakeKmt->running = 5'000'000;
    fakeClock->now = 10'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 50.0);
}

TEST(GpuPdhProvider, SwitchToFallbackWhenPdhReadFailsMidRun)
{
    auto pdh = std::make_unique<FakeGpuEngineSource>();
    FakeGpuEngineSource* fakePdh = pdh.get();
    fakePdh->samples = CrossAdapterSamples();
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = TwoAdapters();

    auto kmt = std::make_unique<FakeD3dKmtSource>();
    FakeD3dKmtSource* fakeKmt = kmt.get();
    fakeKmt->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080", 0, 0x1b790)};
    auto clock = std::make_unique<FakeClock>();
    FakeClock* fakeClock = clock.get();

    GpuPdhProvider provider(std::move(pdh), std::move(adapters), std::move(kmt), std::move(clock),
                            "0");
    ASSERT_EQ(provider.ActiveBackend(), GpuPdhProvider::Backend::Pdh);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING);

    // The counter set becomes unreadable: the provider switches cleanly to the fallback.
    fakePdh->readAvailable = false;
    fakeKmt->running = 8'000'000;
    fakeClock->now = 10'000'000;
    EXPECT_EQ(provider.Poll(snapshot), E_PENDING); // the fallback takes its own first baseline
    EXPECT_EQ(provider.ActiveBackend(), GpuPdhProvider::Backend::D3dKmt);

    fakeKmt->running += 5'000'000;
    fakeClock->now += 10'000'000;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 50.0);
}

// --- real-hardware smoke (skipped when the counter set is absent) -----------

TEST(GpuD3dKmtProvider, RealMachineFallbackIsPlausible)
{
    GpuD3dKmtProvider provider;
    if (!provider.Available())
    {
        GTEST_SKIP() << "no D3DKMT render adapter available on this host";
    }

    MetricsSnapshot snapshot;
    const HRESULT first = provider.Poll(snapshot);
    ASSERT_EQ(first, E_PENDING);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const HRESULT second = provider.Poll(snapshot);
    ASSERT_EQ(second, S_OK);
    EXPECT_GE(snapshot.gpu.utilizationPercent, 0.0);
    EXPECT_LE(snapshot.gpu.utilizationPercent, 100.0);
}

TEST(GpuPdhProvider, RealMachinePublishesPlausibleUtilizationWithoutAdmin)
{
    GpuPdhProvider provider;
    MetricsSnapshot snapshot;

    const HRESULT first = provider.Poll(snapshot);
    if (!provider.PdhAvailable() || provider.ActiveBackend() != GpuPdhProvider::Backend::Pdh)
    {
        GTEST_SKIP() << "GPU Engine counter set unavailable on this host";
    }
    EXPECT_EQ(first, E_PENDING);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const HRESULT second = provider.Poll(snapshot);
    ASSERT_EQ(second, S_OK);
    EXPECT_GE(snapshot.gpu.utilizationPercent, 0.0);
    EXPECT_LE(snapshot.gpu.utilizationPercent, 100.0);
    ASSERT_STRNE(snapshot.gpu.name, "");
    // Live instances parsed and aggregated for the selected adapter (DWM always has a context).
    EXPECT_FALSE(snapshot.gpu.engines.empty());
    for (const auto& engine : snapshot.gpu.engines)
    {
        EXPECT_GE(engine.utilizationPercent, 0.0);
        EXPECT_LE(engine.utilizationPercent, 100.0);
    }
}
} // namespace