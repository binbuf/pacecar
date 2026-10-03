#include "pacecar/metrics/RenderGate.h"

#include "pacecar/metrics/MetricsFingerprint.h"

#include <algorithm>

namespace pacecar::metrics
{
namespace
{
constexpr auto kMinInterval = std::chrono::milliseconds(250);
} // namespace

RenderGate::RenderGate(std::chrono::milliseconds minInterval) noexcept
    : minInterval_(std::max(minInterval, kMinInterval))
{
}

bool RenderGate::ShouldRepaint(const MetricsSnapshot& snapshot, std::uint64_t nowMs) noexcept
{
    return ShouldRepaintFingerprint(FingerprintSnapshot(snapshot), nowMs);
}

bool RenderGate::ShouldRepaintFingerprint(std::uint64_t fingerprint, std::uint64_t nowMs) noexcept
{
    if (hasPainted_ && fingerprint == lastFingerprint_)
    {
        ++skipCount_;
        return false;
    }

    const auto interval = static_cast<std::uint64_t>(minInterval_.count());
    if (hasPainted_ && nowMs < lastPaintMs_ + interval)
    {
        // A visible change arrived before the interval elapsed. Leave `lastFingerprint_` untouched
        // so the change is still pending and paints on the first eligible wake.
        ++skipCount_;
        return false;
    }

    lastFingerprint_ = fingerprint;
    lastPaintMs_ = nowMs;
    hasPainted_ = true;
    ++paintCount_;
    return true;
}

void RenderGate::Reset() noexcept
{
    lastFingerprint_ = 0;
    lastPaintMs_ = 0;
    hasPainted_ = false;
}

void RenderGate::SetInterval(std::chrono::milliseconds minInterval) noexcept
{
    minInterval_ = std::max(minInterval, kMinInterval);
}
} // namespace pacecar::metrics