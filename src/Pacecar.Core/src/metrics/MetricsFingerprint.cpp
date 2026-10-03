#include "pacecar/metrics/MetricsFingerprint.h"

#include <cmath>
#include <cstdint>

namespace pacecar::metrics
{
namespace
{
constexpr std::uint64_t kOffsetBasis = 1469598103934665603ull;
constexpr std::uint64_t kPrime = 1099511628211ull;

void Mix(std::uint64_t& hash, std::uint64_t value) noexcept
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & 0xFFu;
        hash *= kPrime;
    }
}

// Quantizes a value to a display step and returns a signed integer. `NaN`/inf collapse to 0.
std::int64_t Quantize(double value, double step) noexcept
{
    if (!std::isfinite(value) || step <= 0.0)
    {
        return 0;
    }
    return static_cast<std::int64_t>(std::llround(value / step));
}

void MixStatus(std::uint64_t& hash, const MetricStatus& status) noexcept
{
    Mix(hash, status.available ? 1u : 0u);
}

} // namespace

std::uint64_t FingerprintSnapshot(const MetricsSnapshot& snapshot) noexcept
{
    std::uint64_t hash = kOffsetBasis;

    MixStatus(hash, snapshot.cpu.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.cpu.totalUtilizationPercent, 0.1)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.cpu.totalFrequencyMhz, 10.0)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.cpu.packageTemperatureC, 0.5)));

    MixStatus(hash, snapshot.memory.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.memory.usedPercent, 0.1)));

    MixStatus(hash, snapshot.gpu.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.gpu.utilizationPercent, 0.1)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.gpu.temperatureC, 0.5)));
    Mix(hash, snapshot.gpu.vramUsedBytes / (1024u * 1024u));

    MixStatus(hash, snapshot.network.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.network.upBytesPerSecond, 1024.0)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.network.downBytesPerSecond, 1024.0)));

    MixStatus(hash, snapshot.disk.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.disk.readBytesPerSecond, 1024.0)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.disk.writeBytesPerSecond, 1024.0)));
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.disk.temperatureC, 0.5)));

    MixStatus(hash, snapshot.ping.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.ping.rttMs, 1.0)));

    MixStatus(hash, snapshot.frame.status);
    Mix(hash, static_cast<std::uint64_t>(Quantize(snapshot.frame.fps, 0.1)));

    return hash;
}
} // namespace pacecar::metrics