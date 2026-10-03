#include "pacecar/metrics/SnapshotCache.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pacecar::metrics
{
namespace
{
constexpr std::uint32_t kMaxCachedCores = kDefaultMaxCpuCores;
constexpr std::uint32_t kMaxCachedEngines = kDefaultMaxGpuEngines;
constexpr char kFileName[] = "last_snapshot.bin";

std::filesystem::path UserDirectory(const wchar_t* variable)
{
    constexpr DWORD kBufferLength = 32767;
    std::wstring buffer(kBufferLength, L'\0');
    const DWORD length =
        ::GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

template <typename T> void Put(std::ostream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T> bool Get(std::istream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

void PutStatus(std::ostream& out, const MetricStatus& status)
{
    const std::uint8_t available = status.available ? 1u : 0u;
    const std::uint8_t stale = status.stale ? 1u : 0u;
    Put(out, available);
    Put(out, stale);
    Put(out, status.lastSuccessTick);
}

bool GetStatus(std::istream& in, MetricStatus& status)
{
    std::uint8_t available = 0;
    std::uint8_t stale = 0;
    std::uint64_t tick = 0;
    if (!Get(in, available) || !Get(in, stale) || !Get(in, tick))
    {
        return false;
    }
    status.available = available != 0;
    status.stale = stale != 0;
    status.lastSuccessTick = tick;
    return true;
}
} // namespace

std::filesystem::path SnapshotCachePath()
{
    std::filesystem::path base = UserDirectory(L"LOCALAPPDATA");
    if (base.empty())
    {
        base = UserDirectory(L"APPDATA");
    }
    if (base.empty())
    {
        return {};
    }
    return base / L"Pacecar" / kFileName;
}

bool SaveSnapshotCache(const MetricsSnapshot& snapshot, const std::filesystem::path& path) noexcept
{
    if (path.empty())
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
    }

    const std::filesystem::path temp = path.wstring() + L".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    const std::uint32_t coreCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(snapshot.cpu.cores.size(), kMaxCachedCores));
    const std::uint32_t engineCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(snapshot.gpu.engines.size(), kMaxCachedEngines));

    Put(out, kSnapshotCacheMagic);
    Put(out, kSnapshotCacheVersion);
    Put(out, snapshot.sequence);
    Put(out, snapshot.tickIndex);
    Put(out, snapshot.timestampMs);
    Put(out, coreCount);
    Put(out, engineCount);

    // CPU.
    PutStatus(out, snapshot.cpu.status);
    Put(out, snapshot.cpu.totalUtilizationPercent);
    Put(out, snapshot.cpu.totalFrequencyMhz);
    Put(out, snapshot.cpu.packageTemperatureC);
    const std::uint8_t cpuAcpi = snapshot.cpu.temperatureIsAcpi ? 1u : 0u;
    Put(out, cpuAcpi);
    PutStatus(out, snapshot.cpu.temperatureStatus);
    for (std::uint32_t i = 0; i < coreCount; ++i)
    {
        const CpuCoreMetrics& core = snapshot.cpu.cores[i];
        Put(out, core.utilizationPercent);
        Put(out, core.frequencyMhz);
        Put(out, core.temperatureC);
        PutStatus(out, core.temperatureStatus);
    }

    // Memory.
    PutStatus(out, snapshot.memory.status);
    Put(out, snapshot.memory.totalBytes);
    Put(out, snapshot.memory.usedBytes);
    Put(out, snapshot.memory.availableBytes);
    Put(out, snapshot.memory.commitBytes);
    Put(out, snapshot.memory.commitLimitBytes);
    Put(out, snapshot.memory.cacheBytes);
    Put(out, snapshot.memory.usedPercent);

    // GPU.
    PutStatus(out, snapshot.gpu.status);
    PutStatus(out, snapshot.gpu.temperatureStatus);
    PutStatus(out, snapshot.gpu.powerStatus);
    PutStatus(out, snapshot.gpu.clockStatus);
    PutStatus(out, snapshot.gpu.fanStatus);
    PutStatus(out, snapshot.gpu.vramStatus);
    out.write(snapshot.gpu.name, static_cast<std::streamsize>(kNameCapacity));
    Put(out, snapshot.gpu.utilizationPercent);
    Put(out, snapshot.gpu.temperatureC);
    Put(out, snapshot.gpu.powerWatts);
    Put(out, snapshot.gpu.coreClockMhz);
    Put(out, snapshot.gpu.memoryClockMhz);
    Put(out, snapshot.gpu.fanPercent);
    Put(out, snapshot.gpu.fanRpm);
    Put(out, snapshot.gpu.vramUsedBytes);
    Put(out, snapshot.gpu.vramTotalBytes);
    for (std::uint32_t i = 0; i < engineCount; ++i)
    {
        const GpuEngineMetrics& engine = snapshot.gpu.engines[i];
        out.write(engine.engineType, static_cast<std::streamsize>(kEngineTypeCapacity));
        Put(out, engine.utilizationPercent);
    }

    // Network / disk / ping / frame / board / fan / deep sensors.
    PutStatus(out, snapshot.network.status);
    out.write(snapshot.network.interfaceName, static_cast<std::streamsize>(kNameCapacity));
    Put(out, snapshot.network.upBytesPerSecond);
    Put(out, snapshot.network.downBytesPerSecond);
    Put(out, snapshot.network.totalUpBytes);
    Put(out, snapshot.network.totalDownBytes);

    PutStatus(out, snapshot.disk.status);
    PutStatus(out, snapshot.disk.temperatureStatus);
    out.write(snapshot.disk.name, static_cast<std::streamsize>(kNameCapacity));
    Put(out, snapshot.disk.readBytesPerSecond);
    Put(out, snapshot.disk.writeBytesPerSecond);
    Put(out, snapshot.disk.temperatureC);

    PutStatus(out, snapshot.ping.status);
    Put(out, snapshot.ping.rttMs);
    Put(out, snapshot.ping.lastSuccessTick);
    Put(out, snapshot.ping.consecutiveFailures);

    PutStatus(out, snapshot.frame.status);
    Put(out, snapshot.frame.fps);
    Put(out, snapshot.frame.frameTimeMs);
    Put(out, snapshot.frame.cpuTimeMs);
    Put(out, snapshot.frame.gpuTimeMs);

    PutStatus(out, snapshot.board.status);
    Put(out, snapshot.board.mainboardTemperatureC);
    const std::uint8_t boardAcpi = snapshot.board.mainboardIsAcpi ? 1u : 0u;
    Put(out, boardAcpi);

    PutStatus(out, snapshot.fan.status);
    Put(out, snapshot.fan.highestRpm);
    Put(out, snapshot.fan.averageRpm);
    Put(out, snapshot.fan.fanCount);

    PutStatus(out, snapshot.deepSensors);

    out.flush();
    const bool wroteOk = static_cast<bool>(out);
    out.close();
    if (!wroteOk)
    {
        std::filesystem::remove(temp, ec);
        return false;
    }

    std::filesystem::rename(temp, path, ec);
    if (ec)
    {
        // A rename can fail when the destination exists on some filesystems; replace explicitly.
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
    }
    if (ec)
    {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool LoadSnapshotCache(const std::filesystem::path& path, MetricsSnapshot& out) noexcept
{
    if (path.empty())
    {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!Get(in, magic) || magic != kSnapshotCacheMagic)
    {
        return false;
    }
    if (!Get(in, version) || version != kSnapshotCacheVersion)
    {
        return false;
    }

    MetricsSnapshot snapshot;
    std::uint32_t coreCount = 0;
    std::uint32_t engineCount = 0;
    if (!Get(in, snapshot.sequence) || !Get(in, snapshot.tickIndex) ||
        !Get(in, snapshot.timestampMs) || !Get(in, coreCount) || !Get(in, engineCount))
    {
        return false;
    }
    if (coreCount > kMaxCachedCores || engineCount > kMaxCachedEngines)
    {
        return false;
    }
    snapshot.cpu.cores.reserve(coreCount);
    snapshot.gpu.engines.reserve(engineCount);

    if (!GetStatus(in, snapshot.cpu.status) || !Get(in, snapshot.cpu.totalUtilizationPercent) ||
        !Get(in, snapshot.cpu.totalFrequencyMhz) || !Get(in, snapshot.cpu.packageTemperatureC))
    {
        return false;
    }
    std::uint8_t cpuAcpi = 0;
    if (!Get(in, cpuAcpi) || !GetStatus(in, snapshot.cpu.temperatureStatus))
    {
        return false;
    }
    snapshot.cpu.temperatureIsAcpi = cpuAcpi != 0;
    for (std::uint32_t i = 0; i < coreCount; ++i)
    {
        CpuCoreMetrics core;
        if (!Get(in, core.utilizationPercent) || !Get(in, core.frequencyMhz) ||
            !Get(in, core.temperatureC) || !GetStatus(in, core.temperatureStatus))
        {
            return false;
        }
        snapshot.cpu.cores.push_back(core);
    }

    if (!GetStatus(in, snapshot.memory.status) || !Get(in, snapshot.memory.totalBytes) ||
        !Get(in, snapshot.memory.usedBytes) || !Get(in, snapshot.memory.availableBytes) ||
        !Get(in, snapshot.memory.commitBytes) || !Get(in, snapshot.memory.commitLimitBytes) ||
        !Get(in, snapshot.memory.cacheBytes) || !Get(in, snapshot.memory.usedPercent))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.gpu.status) || !GetStatus(in, snapshot.gpu.temperatureStatus) ||
        !GetStatus(in, snapshot.gpu.powerStatus) || !GetStatus(in, snapshot.gpu.clockStatus) ||
        !GetStatus(in, snapshot.gpu.fanStatus) || !GetStatus(in, snapshot.gpu.vramStatus))
    {
        return false;
    }
    in.read(snapshot.gpu.name, static_cast<std::streamsize>(kNameCapacity));
    if (!in)
    {
        return false;
    }
    if (!Get(in, snapshot.gpu.utilizationPercent) || !Get(in, snapshot.gpu.temperatureC) ||
        !Get(in, snapshot.gpu.powerWatts) || !Get(in, snapshot.gpu.coreClockMhz) ||
        !Get(in, snapshot.gpu.memoryClockMhz) || !Get(in, snapshot.gpu.fanPercent) ||
        !Get(in, snapshot.gpu.fanRpm) || !Get(in, snapshot.gpu.vramUsedBytes) ||
        !Get(in, snapshot.gpu.vramTotalBytes))
    {
        return false;
    }
    for (std::uint32_t i = 0; i < engineCount; ++i)
    {
        GpuEngineMetrics engine;
        in.read(engine.engineType, static_cast<std::streamsize>(kEngineTypeCapacity));
        if (!in || !Get(in, engine.utilizationPercent))
        {
            return false;
        }
        snapshot.gpu.engines.push_back(engine);
    }

    if (!GetStatus(in, snapshot.network.status))
    {
        return false;
    }
    in.read(snapshot.network.interfaceName, static_cast<std::streamsize>(kNameCapacity));
    if (!in || !Get(in, snapshot.network.upBytesPerSecond) ||
        !Get(in, snapshot.network.downBytesPerSecond) || !Get(in, snapshot.network.totalUpBytes) ||
        !Get(in, snapshot.network.totalDownBytes))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.disk.status) || !GetStatus(in, snapshot.disk.temperatureStatus))
    {
        return false;
    }
    in.read(snapshot.disk.name, static_cast<std::streamsize>(kNameCapacity));
    if (!in || !Get(in, snapshot.disk.readBytesPerSecond) ||
        !Get(in, snapshot.disk.writeBytesPerSecond) || !Get(in, snapshot.disk.temperatureC))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.ping.status) || !Get(in, snapshot.ping.rttMs) ||
        !Get(in, snapshot.ping.lastSuccessTick) || !Get(in, snapshot.ping.consecutiveFailures))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.frame.status) || !Get(in, snapshot.frame.fps) ||
        !Get(in, snapshot.frame.frameTimeMs) || !Get(in, snapshot.frame.cpuTimeMs) ||
        !Get(in, snapshot.frame.gpuTimeMs))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.board.status) || !Get(in, snapshot.board.mainboardTemperatureC))
    {
        return false;
    }
    std::uint8_t boardAcpi = 0;
    if (!Get(in, boardAcpi))
    {
        return false;
    }
    snapshot.board.mainboardIsAcpi = boardAcpi != 0;

    if (!GetStatus(in, snapshot.fan.status) || !Get(in, snapshot.fan.highestRpm) ||
        !Get(in, snapshot.fan.averageRpm) || !Get(in, snapshot.fan.fanCount))
    {
        return false;
    }

    if (!GetStatus(in, snapshot.deepSensors))
    {
        return false;
    }

    out = std::move(snapshot);
    return true;
}
} // namespace pacecar::metrics