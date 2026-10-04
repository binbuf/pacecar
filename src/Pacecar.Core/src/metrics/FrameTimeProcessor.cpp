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
    intervalsMs_.reserve(capacity_);
    cpuMs_.reserve(capacity_);
    gpuMs_.reserve(capacity_);
    scratch_.reserve(capacity_);
}

void FrameTimeProcessor::Reset() noexcept
{
    intervalsMs_.clear();
    cpuMs_.clear();
    gpuMs_.clear();
    scratch_.clear();
    head_ = 0;
    count_ = 0;
    havePrevious_ = false;
    previousTicks_ = 0;
    previousPid_ = 0;
}

void FrameTimeProcessor::SetClockFrequency(std::uint64_t ticksPerSecond) noexcept
{
    if (ticksPerSecond > 0)
    {
        frequency_ = ticksPerSecond;
    }
}

double FrameTimeProcessor::ValueAt(const std::vector<double>& ring, std::size_t index) const noexcept
{
    const std::size_t oldest = (head_ + capacity_ - count_) % capacity_;
    return ring[(oldest + index) % capacity_];
}

double FrameTimeProcessor::IntervalAt(std::size_t chronologicalIndex) const noexcept
{
    return ValueAt(intervalsMs_, chronologicalIndex);
}

bool FrameTimeProcessor::AddPresent(const PresentEvent& event) noexcept
{
    // A PID change starts a fresh baseline so cross-process presents never create an interval.
    const bool sameProcess = havePrevious_ && event.pid == previousPid_;
    const bool haveInterval = sameProcess && event.qpcTicks > previousTicks_;

    double intervalMs = 0.0;
    if (haveInterval)
    {
        const std::uint64_t delta = event.qpcTicks - previousTicks_;
        intervalMs = static_cast<double>(delta) * 1000.0 / static_cast<double>(frequency_);
    }

    previousTicks_ = event.qpcTicks;
    previousPid_ = event.pid;
    havePrevious_ = true;

    if (!haveInterval || intervalMs <= 0.0 || intervalMs > kMaxIntervalMs)
    {
        return false;
    }

    const auto toMs = [this](std::int64_t ticks) {
        return ticks < 0 ? -1.0
                         : static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency_);
    };

    const std::size_t slot = head_;
    if (intervalsMs_.size() < capacity_)
    {
        intervalsMs_.push_back(intervalMs);
        cpuMs_.push_back(toMs(event.cpuTicks));
        gpuMs_.push_back(toMs(event.gpuTicks));
    }
    else
    {
        intervalsMs_[slot] = intervalMs;
        cpuMs_[slot] = toMs(event.cpuTicks);
        gpuMs_[slot] = toMs(event.gpuTicks);
    }
    head_ = (head_ + 1) % capacity_;
    count_ = std::min(count_ + 1, capacity_);
    return true;
}

FrameTimeStats FrameTimeProcessor::Compute(std::size_t window) const noexcept
{
    FrameTimeStats stats{};
    const std::size_t n = window == 0 ? count_ : std::min(window, count_);
    if (n == 0)
    {
        return stats;
    }

    scratch_.clear();
    double cpuSum = 0.0;
    double gpuSum = 0.0;
    std::size_t cpuCount = 0;
    std::size_t gpuCount = 0;
    const std::size_t first = count_ - n;
    for (std::size_t i = first; i < count_; ++i)
    {
        scratch_.push_back(IntervalAt(i));
        const double cpu = ValueAt(cpuMs_, i);
        if (cpu >= 0.0)
        {
            cpuSum += cpu;
            ++cpuCount;
        }
        const double gpu = ValueAt(gpuMs_, i);
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