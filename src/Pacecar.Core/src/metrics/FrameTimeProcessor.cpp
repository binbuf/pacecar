#include "pacecar/metrics/FrameTimeProcessor.h"

#include <algorithm>
#include <cmath>

namespace pacecar::metrics
{
namespace
{
// A present-to-present interval longer than this is treated as a stall/stop rather than a frame.
constexpr double kMaxIntervalMs = 10000.0;
} // namespace

double SlowestFractionMeanMs(std::span<const double> sortedAscending, double fraction) noexcept
{
    const std::size_t n = sortedAscending.size();
    if (n == 0 || fraction <= 0.0)
    {
        return 0.0;
    }
    std::size_t k = static_cast<std::size_t>(std::ceil(static_cast<double>(n) * fraction));
    k = std::clamp<std::size_t>(k, 1, n);
    double sum = 0.0;
    for (std::size_t i = 0; i < k; ++i)
    {
        sum += sortedAscending[n - 1 - i];
    }
    return sum / static_cast<double>(k);
}

const wchar_t* FrameCaptureStatusText(FrameCaptureState state) noexcept
{
    switch (state)
    {
    case FrameCaptureState::Capturing:
        return L"FPS capture active";
    case FrameCaptureState::SessionBusy:
        return L"FPS capture unavailable - another ETW session is active";
    case FrameCaptureState::AccessDenied:
        return L"FPS capture unavailable - elevation required";
    case FrameCaptureState::ProviderUnavailable:
        return L"FPS capture unavailable - ETW providers missing";
    case FrameCaptureState::NoTarget:
        return L"FPS capture unavailable - no target process";
    case FrameCaptureState::NotCapturing:
    case FrameCaptureState::Error:
    default:
        return L"FPS capture unavailable";
    }
}

bool FrameCaptureStateIsActive(FrameCaptureState state) noexcept
{
    return state == FrameCaptureState::Capturing;
}

FrameTimeProcessor::FrameTimeProcessor(std::size_t capacity)
    : capacity_(capacity == 0 ? kDefaultCapacity : capacity)
{
    for (Lane& lane : lanes_)
    {
        lane.intervalsMs.reserve(capacity_);
        lane.cpuMs.reserve(capacity_);
        lane.gpuMs.reserve(capacity_);
    }
    scratch_.reserve(capacity_);
}

void FrameTimeProcessor::ResetLanes() noexcept
{
    for (Lane& lane : lanes_)
    {
        lane.intervalsMs.clear();
        lane.cpuMs.clear();
        lane.gpuMs.clear();
        lane.swapChain = 0;
        lane.presents = 0;
        lane.prevTicks = 0;
        lane.head = 0;
        lane.count = 0;
        lane.havePrevious = false;
    }
    laneCount_ = 0;
}

void FrameTimeProcessor::Reset() noexcept
{
    ResetLanes();
    currentPid_ = 0;
    havePid_ = false;
    scratch_.clear();
}

void FrameTimeProcessor::SetClockFrequency(std::uint64_t ticksPerSecond) noexcept
{
    if (ticksPerSecond > 0)
    {
        frequency_ = ticksPerSecond;
    }
}

FrameTimeProcessor::Lane* FrameTimeProcessor::FindOrCreateLane(std::uint64_t swapChain) noexcept
{
    for (std::size_t i = 0; i < laneCount_; ++i)
    {
        if (lanes_[i].swapChain == swapChain)
        {
            return &lanes_[i];
        }
    }
    if (laneCount_ >= kMaxLanes)
    {
        return nullptr;
    }
    Lane& lane = lanes_[laneCount_++];
    lane.swapChain = swapChain;
    return &lane;
}

std::size_t FrameTimeProcessor::ActiveLane() const noexcept
{
    std::size_t best = 0;
    for (std::size_t i = 1; i < laneCount_; ++i)
    {
        const Lane& candidate = lanes_[i];
        const Lane& incumbent = lanes_[best];
        if (candidate.presents > incumbent.presents ||
            (candidate.presents == incumbent.presents && candidate.count > incumbent.count))
        {
            best = i;
        }
    }
    return best;
}

double FrameTimeProcessor::ValueAt(const Lane& lane, const std::vector<double>& ring,
                                   std::size_t index) const noexcept
{
    const std::size_t oldest = (lane.head + capacity_ - lane.count) % capacity_;
    return ring[(oldest + index) % capacity_];
}

double FrameTimeProcessor::IntervalAt(const Lane& lane, std::size_t chronologicalIndex) const noexcept
{
    return ValueAt(lane, lane.intervalsMs, chronologicalIndex);
}

bool FrameTimeProcessor::AddPresent(const PresentEvent& event) noexcept
{
    // A PID change starts a fresh capture target: drop every chain's baseline so presents from the
    // previous process never create an interval.
    if (!havePid_ || event.pid != currentPid_)
    {
        ResetLanes();
        currentPid_ = event.pid;
        havePid_ = true;
    }

    Lane* lane = FindOrCreateLane(event.swapChain);
    if (lane == nullptr)
    {
        return false;
    }

    const bool haveInterval = lane->havePrevious && event.qpcTicks > lane->prevTicks;
    double intervalMs = 0.0;
    if (haveInterval)
    {
        const std::uint64_t delta = event.qpcTicks - lane->prevTicks;
        intervalMs = static_cast<double>(delta) * 1000.0 / static_cast<double>(frequency_);
    }

    lane->prevTicks = event.qpcTicks;
    lane->havePrevious = true;
    ++lane->presents;

    if (!haveInterval || intervalMs <= 0.0 || intervalMs > kMaxIntervalMs)
    {
        return false;
    }

    const auto toMs = [this](std::int64_t ticks) {
        return ticks < 0 ? -1.0
                         : static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency_);
    };

    if (lane->intervalsMs.size() < capacity_)
    {
        lane->intervalsMs.push_back(intervalMs);
        lane->cpuMs.push_back(toMs(event.cpuTicks));
        lane->gpuMs.push_back(toMs(event.gpuTicks));
    }
    else
    {
        lane->intervalsMs[lane->head] = intervalMs;
        lane->cpuMs[lane->head] = toMs(event.cpuTicks);
        lane->gpuMs[lane->head] = toMs(event.gpuTicks);
    }
    lane->head = (lane->head + 1) % capacity_;
    lane->count = std::min(lane->count + 1, capacity_);
    return true;
}

std::size_t FrameTimeProcessor::SampleCount() const noexcept
{
    if (laneCount_ == 0)
    {
        return 0;
    }
    return lanes_[ActiveLane()].count;
}

FrameTimeStats FrameTimeProcessor::Compute(std::size_t window) const noexcept
{
    FrameTimeStats stats{};
    if (laneCount_ == 0)
    {
        return stats;
    }

    const Lane& lane = lanes_[ActiveLane()];
    const std::size_t n = window == 0 ? lane.count : std::min(window, lane.count);
    if (n == 0)
    {
        return stats;
    }

    scratch_.clear();
    double cpuSum = 0.0;
    double gpuSum = 0.0;
    std::size_t cpuCount = 0;
    std::size_t gpuCount = 0;
    const std::size_t first = lane.count - n;
    for (std::size_t i = first; i < lane.count; ++i)
    {
        scratch_.push_back(IntervalAt(lane, i));
        const double cpu = ValueAt(lane, lane.cpuMs, i);
        if (cpu >= 0.0)
        {
            cpuSum += cpu;
            ++cpuCount;
        }
        const double gpu = ValueAt(lane, lane.gpuMs, i);
        if (gpu >= 0.0)
        {
            gpuSum += gpu;
            ++gpuCount;
        }
    }

    double intervalSum = 0.0;
    for (const double value : scratch_)
    {
        intervalSum += value;
    }
    const double meanMs = intervalSum / static_cast<double>(n);

    std::sort(scratch_.begin(), scratch_.end());

    stats.valid = meanMs > 0.0;
    stats.sampleCount = n;
    stats.frameTimeMs = meanMs;
    stats.fps = meanMs > 0.0 ? 1000.0 / meanMs : 0.0;
    stats.cpuTimeMs = cpuCount > 0 ? cpuSum / static_cast<double>(cpuCount) : 0.0;
    stats.gpuTimeMs = gpuCount > 0 ? gpuSum / static_cast<double>(gpuCount) : 0.0;
    stats.low1PercentMs = SlowestFractionMeanMs(scratch_, 0.01);
    stats.low01PercentMs = SlowestFractionMeanMs(scratch_, 0.001);
    return stats;
}
} // namespace pacecar::metrics