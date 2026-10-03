#include "pacecar/metrics/Aggregator.h"

#include <algorithm>

namespace pacecar::metrics
{
namespace
{
// Appends the current value to history every tick and, when the source is available, replaces the
// value with its EMA-smoothed form. Raw values therefore live in the ring buffers while the snapshot
// carries the display-smoothed value.
void RecordSample(RingBuffer<double>& ring, Ema& ema, bool available, double& value) noexcept
{
    ring.Push(value);
    if (available)
    {
        value = ema.Push(value);
    }
}
} // namespace

MetricHistory::MetricHistory(std::size_t capacity)
    : cpuTotalUtilization(capacity), cpuFrequencyMhz(capacity), memoryUsedPercent(capacity),
      gpuUtilization(capacity), gpuTemperatureC(capacity), networkUpBytesPerSecond(capacity),
      networkDownBytesPerSecond(capacity), diskReadBytesPerSecond(capacity),
      diskWriteBytesPerSecond(capacity), pingRttMs(capacity), fps(capacity), capacity_(capacity)
{
}

void MetricHistory::Push(const MetricsSnapshot& snapshot) noexcept
{
    cpuTotalUtilization.Push(snapshot.cpu.totalUtilizationPercent);
    cpuFrequencyMhz.Push(snapshot.cpu.totalFrequencyMhz);
    memoryUsedPercent.Push(snapshot.memory.usedPercent);
    gpuUtilization.Push(snapshot.gpu.utilizationPercent);
    gpuTemperatureC.Push(snapshot.gpu.temperatureC);
    networkUpBytesPerSecond.Push(snapshot.network.upBytesPerSecond);
    networkDownBytesPerSecond.Push(snapshot.network.downBytesPerSecond);
    diskReadBytesPerSecond.Push(snapshot.disk.readBytesPerSecond);
    diskWriteBytesPerSecond.Push(snapshot.disk.writeBytesPerSecond);
    pingRttMs.Push(snapshot.ping.rttMs);
    fps.Push(snapshot.frame.fps);
}

Aggregator::Aggregator(std::size_t historyCapacity,
                       std::size_t maxCpuCores,
                       std::size_t maxGpuEngines,
                       std::size_t snapshotPoolSize)
    : maxCpuCores_(maxCpuCores), maxGpuEngines_(maxGpuEngines), history_(historyCapacity),
      cpuTotalEma_(Ema::FromWindow(4)), memoryUsedEma_(Ema::FromWindow(4)),
      gpuTotalEma_(Ema::FromWindow(4))
{
    if (snapshotPoolSize < 3)
    {
        snapshotPoolSize = 3;
    }
    pool_.reserve(snapshotPoolSize);
    for (std::size_t i = 0; i < snapshotPoolSize; ++i)
    {
        auto snapshot = std::make_shared<MetricsSnapshot>();
        snapshot->cpu.cores.reserve(maxCpuCores_);
        snapshot->gpu.engines.reserve(maxGpuEngines_);
        pool_.push_back(std::move(snapshot));
    }
    current_ = pool_.front();

    cpuCoreEmas_.reserve(maxCpuCores_);
    gpuEngineEmas_.reserve(maxGpuEngines_);
    startTime_ = std::chrono::steady_clock::now();
}

void Aggregator::ReserveProviders(std::size_t count)
{
    providers_.reserve(count);
}

void Aggregator::AddProvider(std::shared_ptr<IMetricProvider> provider)
{
    if (!provider)
    {
        return;
    }
    ProviderEntry entry{};
    entry.provider = std::move(provider);
    entry.domains = entry.provider->Domains();
    providers_.push_back(std::move(entry));
}

void Aggregator::SetWakeCallback(WakeCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    wake_ = std::move(callback);
    wakeSet_ = static_cast<bool>(wake_);
}

void Aggregator::Reset()
{
    for (auto& entry : providers_)
    {
        entry.provider->Reset();
        entry.lastPollMs = kNeverPolled;
        entry.pollCount = 0;
        entry.failureCount = 0;
    }
    cpuTotalEma_.Reset();
    memoryUsedEma_.Reset();
    gpuTotalEma_.Reset();
    for (auto& ema : cpuCoreEmas_)
    {
        ema.Reset();
    }
    for (auto& ema : gpuEngineEmas_)
    {
        ema.Reset();
    }
    tickIndex_ = 0;
    current_->ResetValues();
}

void Aggregator::SetVisible(bool visible) noexcept
{
    visible_.store(visible, std::memory_order_relaxed);
}

bool Aggregator::Visible() const noexcept
{
    return visible_.load(std::memory_order_relaxed);
}

bool Aggregator::Tick()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
    return Tick(static_cast<std::uint64_t>(elapsed.count()));
}

bool Aggregator::Tick(std::uint64_t nowMs)
{
    std::lock_guard<std::mutex> tickLock(tickMutex_);

    ++tickIndex_;
    MetricsSnapshot& snapshot = *current_;

    for (auto& entry : providers_)
    {
        const auto cadence =
            static_cast<std::uint64_t>(entry.provider->Cadence().count());
        if (entry.lastPollMs != kNeverPolled && nowMs < entry.lastPollMs + cadence)
        {
            continue;
        }
        entry.lastPollMs = nowMs;
        ++entry.pollCount;

        HRESULT hr = E_FAIL;
        try
        {
            hr = entry.provider->Poll(snapshot);
        }
        catch (...)
        {
            hr = E_FAIL;
        }

        const bool success = SUCCEEDED(hr);
        if (!success)
        {
            ++entry.failureCount;
        }
        ApplyDomainStatus(snapshot, entry.domains, success, tickIndex_);
    }

    UpdateDerived(snapshot);
    snapshot.tickIndex = tickIndex_;
    snapshot.timestampMs = nowMs;

    return Publish(snapshot, nowMs);
}

std::shared_ptr<MetricsSnapshot> Aggregator::AcquireFreeSnapshotSlot()
{
    for (auto& slot : pool_)
    {
        if (slot.get() != current_.get() && slot.use_count() == 1)
        {
            return slot;
        }
    }
    return nullptr;
}

void Aggregator::ApplyDomainStatus(MetricsSnapshot& snapshot,
                                   std::uint32_t domains,
                                   bool success,
                                   std::uint64_t tick) noexcept
{
    const auto mark = [success, tick](MetricStatus& status) {
        if (success)
        {
            status.available = true;
            status.stale = false;
            status.lastSuccessTick = tick;
        }
        else
        {
            status.available = false;
            status.stale = true;
        }
    };

    if (HasDomain(domains, MetricDomain::Cpu))
    {
        mark(snapshot.cpu.status);
    }
    if (HasDomain(domains, MetricDomain::Memory))
    {
        mark(snapshot.memory.status);
    }
    if (HasDomain(domains, MetricDomain::Gpu))
    {
        mark(snapshot.gpu.status);
    }
    if (HasDomain(domains, MetricDomain::Network))
    {
        mark(snapshot.network.status);
    }
    if (HasDomain(domains, MetricDomain::Disk))
    {
        mark(snapshot.disk.status);
    }
    if (HasDomain(domains, MetricDomain::Ping))
    {
        mark(snapshot.ping.status);
    }
    if (HasDomain(domains, MetricDomain::FrameTime))
    {
        mark(snapshot.frame.status);
    }
    if (HasDomain(domains, MetricDomain::Board))
    {
        mark(snapshot.board.status);
    }
    if (HasDomain(domains, MetricDomain::Fan))
    {
        mark(snapshot.fan.status);
    }
    if (HasDomain(domains, MetricDomain::DeepSensors))
    {
        mark(snapshot.deepSensors);
    }
}

void Aggregator::UpdateDerived(MetricsSnapshot& snapshot)
{
    RecordSample(history_.cpuTotalUtilization,
                 cpuTotalEma_,
                 snapshot.cpu.status.available,
                 snapshot.cpu.totalUtilizationPercent);
    history_.cpuFrequencyMhz.Push(snapshot.cpu.totalFrequencyMhz);

    if (cpuCoreEmas_.size() < snapshot.cpu.cores.size())
    {
        cpuCoreEmas_.resize(snapshot.cpu.cores.size());
    }
    for (std::size_t i = 0; i < snapshot.cpu.cores.size(); ++i)
    {
        if (snapshot.cpu.status.available)
        {
            snapshot.cpu.cores[i].utilizationPercent =
                cpuCoreEmas_[i].Push(snapshot.cpu.cores[i].utilizationPercent);
        }
    }

    RecordSample(history_.memoryUsedPercent,
                 memoryUsedEma_,
                 snapshot.memory.status.available,
                 snapshot.memory.usedPercent);
    RecordSample(history_.gpuUtilization,
                 gpuTotalEma_,
                 snapshot.gpu.status.available,
                 snapshot.gpu.utilizationPercent);
    history_.gpuTemperatureC.Push(snapshot.gpu.temperatureC);

    if (gpuEngineEmas_.size() < snapshot.gpu.engines.size())
    {
        gpuEngineEmas_.resize(snapshot.gpu.engines.size());
    }
    for (std::size_t i = 0; i < snapshot.gpu.engines.size(); ++i)
    {
        if (snapshot.gpu.status.available)
        {
            snapshot.gpu.engines[i].utilizationPercent =
                gpuEngineEmas_[i].Push(snapshot.gpu.engines[i].utilizationPercent);
        }
    }

    history_.networkUpBytesPerSecond.Push(snapshot.network.upBytesPerSecond);
    history_.networkDownBytesPerSecond.Push(snapshot.network.downBytesPerSecond);
    history_.diskReadBytesPerSecond.Push(snapshot.disk.readBytesPerSecond);
    history_.diskWriteBytesPerSecond.Push(snapshot.disk.writeBytesPerSecond);
    history_.pingRttMs.Push(snapshot.ping.rttMs);
    history_.fps.Push(snapshot.frame.fps);
}

bool Aggregator::Publish(const MetricsSnapshot& snapshot, std::uint64_t nowMs)
{
    auto slot = AcquireFreeSnapshotSlot();
    if (!slot)
    {
        // Every slot is currently referenced by a consumer. Skip this publication rather than
        // allocate on the steady-state path; the next tick will retry.
        return false;
    }

    slot->CopyFrom(snapshot);
    slot->sequence = ++publishSequence_;
    slot->tickIndex = tickIndex_;
    slot->timestampMs = nowMs;

    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        published_ = slot;
        if (visible_.load(std::memory_order_relaxed) && wakeSet_)
        {
            wake = wake_;
        }
    }
    ++publishCount_;

    if (wake)
    {
        wake();
        ++wakeCount_;
    }
    return true;
}

std::shared_ptr<const MetricsSnapshot> Aggregator::LatestSnapshot() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

void Aggregator::CopyHistory(MetricHistory& out) const
{
    std::lock_guard<std::mutex> lock(tickMutex_);
    out = history_;
}

std::uint64_t Aggregator::ProviderPollCount(std::size_t index) const
{
    return index < providers_.size() ? providers_[index].pollCount : 0;
}

std::uint64_t Aggregator::ProviderFailureCount(std::size_t index) const
{
    return index < providers_.size() ? providers_[index].failureCount : 0;
}
} // namespace pacecar::metrics