#pragma once

// The metrics snapshot: an immutable-at-publication, UI-agnostic value model.
//
// The snapshot is the single boundary between metric acquisition (providers on the sampler thread)
// and everything downstream (aggregator publication, widgets, history). It deliberately contains no
// UI, Direct2D, Win32, or hardware types so it can be built and tested headless.
//
// Layout rules that later tasks rely on:
//   - Optional values are modeled with an availability/stale status (`MetricStatus`), never with
//     sentinel numbers, so "unknown" and "0" stay distinguishable.
//   - Per-core and per-GPU-engine arrays are dynamic but bounded: they live in `std::vector`s whose
//     capacity is reserved once by the aggregator, so filling and copying a snapshot never allocate.
//   - Names are fixed-size character arrays rather than `std::string`, keeping a snapshot trivially
//     copyable into a preallocated buffer on the publish path.
//
// `CopyFrom` is the allocation-free deep copy used by the aggregator's double-buffer publication;
// it resizes destination vectors only up to their already-reserved capacity.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pacecar::metrics
{
// Default upper bounds used when a machine report is unavailable. Machines with more cores simply
// reserve more; these are only defaults for the aggregator's constructor.
inline constexpr std::size_t kDefaultMaxCpuCores = 256;
inline constexpr std::size_t kDefaultMaxGpuEngines = 16;
inline constexpr std::size_t kNameCapacity = 64;
inline constexpr std::size_t kEngineTypeCapacity = 32;

// Per-metric state. `available` is false when the source is missing or the last read failed;
// `stale` is true until the first successful read and again whenever a read fails. A stale metric
// keeps its last good value so the UI can choose to gray it out rather than blank it.
struct MetricStatus
{
    bool available = false;
    bool stale = true;
    std::uint64_t lastSuccessTick = 0;
};

struct CpuCoreMetrics
{
    double utilizationPercent = 0.0;
    double frequencyMhz = 0.0;
    double temperatureC = 0.0;
    MetricStatus temperatureStatus{};
};

struct CpuMetrics
{
    MetricStatus status{};
    double totalUtilizationPercent = 0.0;
    double totalFrequencyMhz = 0.0;
    double packageTemperatureC = 0.0;
    // True when the temperature came from an ACPI thermal zone rather than a package sensor; the UI
    // must label such a value distinctly.
    bool temperatureIsAcpi = false;
    MetricStatus temperatureStatus{};
    std::vector<CpuCoreMetrics> cores{};
};

struct MemoryMetrics
{
    MetricStatus status{};
    std::uint64_t totalBytes = 0;
    std::uint64_t usedBytes = 0;
    // "Available" includes standby/cached memory (GlobalMemoryStatusEx semantics).
    std::uint64_t availableBytes = 0;
    std::uint64_t commitBytes = 0;
    std::uint64_t commitLimitBytes = 0;
    std::uint64_t cacheBytes = 0;
    double usedPercent = 0.0;
};

struct GpuEngineMetrics
{
    char engineType[kEngineTypeCapacity] = {};
    double utilizationPercent = 0.0;
};

struct GpuMetrics
{
    MetricStatus status{};
    MetricStatus temperatureStatus{};
    MetricStatus powerStatus{};
    MetricStatus clockStatus{};
    MetricStatus fanStatus{};
    MetricStatus vramStatus{};
    char name[kNameCapacity] = {};
    double utilizationPercent = 0.0;
    double temperatureC = 0.0;
    double powerWatts = 0.0;
    double coreClockMhz = 0.0;
    double memoryClockMhz = 0.0;
    double fanPercent = 0.0;
    std::int32_t fanRpm = 0;
    std::uint64_t vramUsedBytes = 0;
    std::uint64_t vramTotalBytes = 0;
    std::vector<GpuEngineMetrics> engines{};
};

struct NetworkMetrics
{
    MetricStatus status{};
    char interfaceName[kNameCapacity] = {};
    double upBytesPerSecond = 0.0;
    double downBytesPerSecond = 0.0;
    std::uint64_t totalUpBytes = 0;
    std::uint64_t totalDownBytes = 0;
};

struct DiskMetrics
{
    MetricStatus status{};
    MetricStatus temperatureStatus{};
    char name[kNameCapacity] = {};
    double readBytesPerSecond = 0.0;
    double writeBytesPerSecond = 0.0;
    double temperatureC = 0.0;
};

struct PingMetrics
{
    MetricStatus status{};
    double rttMs = 0.0;
    std::uint64_t lastSuccessTick = 0;
    std::uint32_t consecutiveFailures = 0;
};

struct FrameMetrics
{
    MetricStatus status{};
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double cpuTimeMs = 0.0;
    double gpuTimeMs = 0.0;
};

struct BoardMetrics
{
    MetricStatus status{};
    double mainboardTemperatureC = 0.0;
    bool mainboardIsAcpi = false;
};

struct FanMetrics
{
    MetricStatus status{};
    std::int32_t highestRpm = 0;
    std::int32_t averageRpm = 0;
    std::uint32_t fanCount = 0;
};

struct MetricsSnapshot
{
    // Monotonic publication counter (incremented once per published snapshot) and the aggregator
    // tick index / wall-clock-ish millisecond timestamp that produced it.
    std::uint64_t sequence = 0;
    std::uint64_t tickIndex = 0;
    std::uint64_t timestampMs = 0;

    CpuMetrics cpu{};
    MemoryMetrics memory{};
    GpuMetrics gpu{};
    NetworkMetrics network{};
    DiskMetrics disk{};
    PingMetrics ping{};
    FrameMetrics frame{};
    BoardMetrics board{};
    FanMetrics fan{};
    // Status of the privileged helper sensor path as a whole (deep sensors).
    MetricStatus deepSensors{};

    [[nodiscard]] std::size_t CoreCount() const noexcept
    {
        return cpu.cores.size();
    }

    // Resets every value and status while retaining vector capacities. Used when an aggregator
    // restarts so no stale values survive.
    void ResetValues() noexcept;

    // Allocation-free deep copy: vectors are resized within the destination's reserved capacity.
    void CopyFrom(const MetricsSnapshot& other);
};
} // namespace pacecar::metrics