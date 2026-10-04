#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pacecar/metrics/FrameTimeProcessor.h"

namespace
{
using pacecar::metrics::FrameCaptureState;
using pacecar::metrics::FrameCaptureStateIsActive;
using pacecar::metrics::FrameCaptureStatusText;
using pacecar::metrics::FrameTimeProcessor;
using pacecar::metrics::FrameTimeStats;
using pacecar::metrics::PresentEvent;
using pacecar::metrics::SlowestFractionMeanMs;

constexpr std::uint64_t kFreq = 10'000'000ull; // 100 ns ticks

PresentEvent Present(std::uint64_t ticks, std::uint32_t pid = 100)
{
    PresentEvent event{};
    event.qpcTicks = ticks;
    event.pid = pid;
    return event;
}

TEST(FrameTimeProcessor, ComputesFpsAndFrameTimeFromSyntheticPresents)
{
    FrameTimeProcessor processor;
    ASSERT_EQ(processor.ClockFrequency(), kFreq);

    // 120 presents at 16.6667 ms -> ~60 FPS.
    const std::uint64_t step = 166'667; // 16.6667 ms
    for (int i = 0; i <= 120; ++i)
    {
        static_cast<void>(processor.AddPresent(Present(step * static_cast<std::uint64_t>(i))));
    }
    EXPECT_EQ(processor.SampleCount(), 120u);

    const FrameTimeStats stats = processor.Compute();
    ASSERT_TRUE(stats.valid);
    EXPECT_NEAR(stats.fps, 60.0, 0.05);
    EXPECT_NEAR(stats.frameTimeMs, 16.6667, 0.01);
    EXPECT_EQ(stats.sampleCount, 120u);
}

TEST(FrameTimeProcessor, AveragesCpuAndGpuDurationsWhenPresent)
{
    FrameTimeProcessor processor;
    for (int i = 0; i < 4; ++i)
    {
        PresentEvent event = Present(100'000 * static_cast<std::uint64_t>(i));
        event.cpuTicks = 50'000; // 5 ms
        event.gpuTicks = 100'000; // 10 ms
        static_cast<void>(processor.AddPresent(event));
    }
    const FrameTimeStats stats = processor.Compute();
    ASSERT_TRUE(stats.valid);
    EXPECT_NEAR(stats.cpuTimeMs, 5.0, 0.001);
    EXPECT_NEAR(stats.gpuTimeMs, 10.0, 0.001);
}

TEST(FrameTimeProcessor, UnknownDurationsAreZeroNotMisleading)
{
    FrameTimeProcessor processor;
    for (int i = 0; i < 4; ++i)
    {
        static_cast<void>(processor.AddPresent(Present(100'000 * static_cast<std::uint64_t>(i))));
    }
    const FrameTimeStats stats = processor.Compute();
    ASSERT_TRUE(stats.valid);
    EXPECT_DOUBLE_EQ(stats.cpuTimeMs, 0.0);
    EXPECT_DOUBLE_EQ(stats.gpuTimeMs, 0.0);
}

TEST(FrameTimeProcessor, PercentileLowTakesWorstFrames)
{
    // 99 frames at 10 ms and one at 100 ms: the 1% low is the single 100 ms frame.
    std::vector<double> samples(99, 10.0);
    samples.push_back(100.0);
    std::sort(samples.begin(), samples.end());
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(samples, 0.01), 100.0);
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(samples, 0.001), 100.0);
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(samples, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(std::span<const double>{}, 0.01), 0.0);
}

TEST(FrameTimeProcessor, LowAveragesTheWorstBucket)
{
    // Worst 10% of 10 samples is the single slowest at 100 ms.
    const std::array<double, 10> samples{1, 1, 1, 1, 1, 1, 1, 1, 1, 100};
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(samples, 0.1), 100.0);
    // Worst 20% averages the two slowest.
    EXPECT_DOUBLE_EQ(SlowestFractionMeanMs(samples, 0.2), 50.5);
}

TEST(FrameTimeProcessor, IgnoresNonPositiveIntervalsAndPidChanges)
{
    FrameTimeProcessor processor;
    // First present establishes the baseline and records nothing.
    EXPECT_FALSE(processor.AddPresent(Present(1000, 7)));
    // Same timestamp is not an interval.
    EXPECT_FALSE(processor.AddPresent(Present(1000, 7)));
    // A different PID restarts the baseline (no cross-process interval).
    EXPECT_FALSE(processor.AddPresent(Present(2000, 8)));
    // A positive interval within the same process is recorded.
    EXPECT_TRUE(processor.AddPresent(Present(3000, 8)));
    EXPECT_EQ(processor.SampleCount(), 1u);
}

TEST(FrameTimeProcessor, ResetClearsEverything)
{
    FrameTimeProcessor processor;
    for (int i = 0; i < 10; ++i)
    {
        static_cast<void>(processor.AddPresent(Present(1000 * static_cast<std::uint64_t>(i))));
    }
    ASSERT_GT(processor.SampleCount(), 0u);
    processor.Reset();
    EXPECT_EQ(processor.SampleCount(), 0u);
    EXPECT_FALSE(processor.Compute().valid);
}

TEST(FrameTimeProcessor, ClockFrequencyConvertsTicks)
{
    FrameTimeProcessor processor;
    processor.SetClockFrequency(1'000); // 1 ms ticks
    static_cast<void>(processor.AddPresent(Present(0)));
    static_cast<void>(processor.AddPresent(Present(5))); // 5 ms
    const FrameTimeStats stats = processor.Compute();
    ASSERT_TRUE(stats.valid);
    EXPECT_NEAR(stats.frameTimeMs, 5.0, 0.001);
    EXPECT_NEAR(stats.fps, 200.0, 0.01);
}

TEST(FrameCaptureStatus, MapsConflictAndErrorStates)
{
    EXPECT_TRUE(FrameCaptureStateIsActive(FrameCaptureState::Capturing));
    EXPECT_FALSE(FrameCaptureStateIsActive(FrameCaptureState::SessionBusy));
    EXPECT_FALSE(FrameCaptureStateIsActive(FrameCaptureState::NotCapturing));

    const std::wstring busy = FrameCaptureStatusText(FrameCaptureState::SessionBusy);
    EXPECT_NE(busy.find(L"another ETW session"), std::wstring::npos);
    const std::wstring denied = FrameCaptureStatusText(FrameCaptureState::AccessDenied);
    EXPECT_NE(denied.find(L"elevation"), std::wstring::npos);
    EXPECT_NE(std::wstring(FrameCaptureStatusText(FrameCaptureState::ProviderUnavailable))
                  .find(L"providers"),
              std::wstring::npos);
    EXPECT_EQ(std::wstring(FrameCaptureStatusText(FrameCaptureState::NotCapturing)),
              L"FPS capture unavailable");
}
} // namespace