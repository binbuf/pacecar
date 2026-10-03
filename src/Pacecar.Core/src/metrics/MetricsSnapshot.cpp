#include "pacecar/metrics/MetricsSnapshot.h"

#include <algorithm>

namespace pacecar::metrics
{
namespace
{
void CopyName(char* dst, const char* src, std::size_t capacity) noexcept
{
    std::memcpy(dst, src, capacity);
}
} // namespace

void MetricsSnapshot::ResetValues() noexcept
{
    sequence = 0;
    tickIndex = 0;
    timestampMs = 0;

    // Reset scalars field by field for the structs that own dynamic arrays: reassigning a whole
    // struct would release the vector's reserved capacity and reintroduce allocation on restart.
    cpu.status = MetricStatus{};
    cpu.totalUtilizationPercent = 0.0;
    cpu.totalFrequencyMhz = 0.0;
    cpu.packageTemperatureC = 0.0;
    cpu.temperatureIsAcpi = false;
    cpu.temperatureStatus = MetricStatus{};
    cpu.cores.clear();

    gpu.status = MetricStatus{};
    gpu.temperatureStatus = MetricStatus{};
    gpu.powerStatus = MetricStatus{};
    gpu.clockStatus = MetricStatus{};
    gpu.fanStatus = MetricStatus{};
    gpu.vramStatus = MetricStatus{};
    std::memset(gpu.name, 0, sizeof(gpu.name));
    gpu.utilizationPercent = 0.0;
    gpu.temperatureC = 0.0;
    gpu.powerWatts = 0.0;
    gpu.coreClockMhz = 0.0;
    gpu.memoryClockMhz = 0.0;
    gpu.fanPercent = 0.0;
    gpu.fanRpm = 0;
    gpu.vramUsedBytes = 0;
    gpu.vramTotalBytes = 0;
    gpu.engines.clear();

    memory = MemoryMetrics{};
    network = NetworkMetrics{};
    disk = DiskMetrics{};
    ping = PingMetrics{};
    frame = FrameMetrics{};
    board = BoardMetrics{};
    fan = FanMetrics{};
    deepSensors = MetricStatus{};
}

void MetricsSnapshot::CopyFrom(const MetricsSnapshot& other)
{
    sequence = other.sequence;
    tickIndex = other.tickIndex;
    timestampMs = other.timestampMs;

    cpu.status = other.cpu.status;
    cpu.totalUtilizationPercent = other.cpu.totalUtilizationPercent;
    cpu.totalFrequencyMhz = other.cpu.totalFrequencyMhz;
    cpu.packageTemperatureC = other.cpu.packageTemperatureC;
    cpu.temperatureIsAcpi = other.cpu.temperatureIsAcpi;
    cpu.temperatureStatus = other.cpu.temperatureStatus;
    cpu.cores.resize(other.cpu.cores.size());
    std::copy(other.cpu.cores.begin(), other.cpu.cores.end(), cpu.cores.begin());

    memory = other.memory;

    gpu.status = other.gpu.status;
    gpu.temperatureStatus = other.gpu.temperatureStatus;
    gpu.powerStatus = other.gpu.powerStatus;
    gpu.clockStatus = other.gpu.clockStatus;
    gpu.fanStatus = other.gpu.fanStatus;
    gpu.vramStatus = other.gpu.vramStatus;
    CopyName(gpu.name, other.gpu.name, kNameCapacity);
    gpu.utilizationPercent = other.gpu.utilizationPercent;
    gpu.temperatureC = other.gpu.temperatureC;
    gpu.powerWatts = other.gpu.powerWatts;
    gpu.coreClockMhz = other.gpu.coreClockMhz;
    gpu.memoryClockMhz = other.gpu.memoryClockMhz;
    gpu.fanPercent = other.gpu.fanPercent;
    gpu.fanRpm = other.gpu.fanRpm;
    gpu.vramUsedBytes = other.gpu.vramUsedBytes;
    gpu.vramTotalBytes = other.gpu.vramTotalBytes;
    gpu.engines.resize(other.gpu.engines.size());
    std::copy(other.gpu.engines.begin(), other.gpu.engines.end(), gpu.engines.begin());

    network = other.network;
    disk = other.disk;
    ping = other.ping;
    frame = other.frame;
    board = other.board;
    fan = other.fan;
    deepSensors = other.deepSensors;
}
} // namespace pacecar::metrics