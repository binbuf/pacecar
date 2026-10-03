#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "Sampler.h"

#include <algorithm>
#include <cstdio>

#include "OverlayWindow.h"
#include "pacecar/metrics/CpuProvider.h"
#include "pacecar/metrics/DiskProvider.h"
#include "pacecar/metrics/GpuPdhProvider.h"
#include "pacecar/metrics/HistoryRetention.h"
#include "pacecar/metrics/MemoryProvider.h"
#include "pacecar/metrics/NetworkProvider.h"
#include "pacecar/metrics/PingProvider.h"
#include "pacecar/util/ElapsedClock.h"
#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr auto kMinInterval = std::chrono::milliseconds(250);
constexpr std::size_t kFramePoolSize = 4;

std::chrono::milliseconds ClampInterval(std::chrono::milliseconds interval) noexcept
{
    return std::max(interval, kMinInterval);
}

// Requests EcoQoS (efficiency-class scheduling) for the calling thread. Best-effort: older builds
// simply fail and the thread keeps its below-normal priority.
bool ApplyEcoQos(HANDLE thread) noexcept
{
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    return SetThreadInformation(thread, ThreadPowerThrottling, &state, sizeof(state)) != FALSE;
}

// Stops background sampling from raising the platform timer resolution (best-effort).
bool ApplyIgnoreTimerResolution() noexcept
{
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) !=
           FALSE;
}
} // namespace

Sampler::Sampler() = default;

Sampler::~Sampler()
{
    Stop();
}

bool Sampler::Start(const pacecar::Config& config, HWND wakeWindow)
{
    if (running_.load())
    {
        return true;
    }

    wakeWindow_ = wakeWindow;
    intervalMs_.store(
        static_cast<std::int64_t>(ClampInterval(std::chrono::milliseconds(
                                                      static_cast<int>(config.general.refresh)))
                                      .count()));

    const pacecar::metrics::HistoryRetention retention =
        pacecar::metrics::PlanHistoryRetention(config.history.retention_minutes,
                                               std::chrono::milliseconds(
                                                   static_cast<int>(config.general.refresh)));
    historyCapacity_ = retention.rawCapacity;

    aggregator_ =
        std::make_unique<pacecar::metrics::Aggregator>(historyCapacity_,
                                                       pacecar::metrics::kDefaultMaxCpuCores,
                                                       pacecar::metrics::kDefaultMaxGpuEngines);
    BuildProviders(config);
    aggregator_->SetVisible(false);

    framePool_.clear();
    framePool_.reserve(kFramePoolSize);
    for (std::size_t i = 0; i < kFramePoolSize; ++i)
    {
        framePool_.push_back(std::make_shared<pacecar::metrics::DisplayFrame>());
    }
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        publishedFrame_.reset();
    }

    ignoreTimerResolutionApplied_ = ApplyIgnoreTimerResolution();
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread(&Sampler::ThreadMain, this);
    return true;
}

void Sampler::BuildProviders(const pacecar::Config& config)
{
    providerCount_ = 0;

    aggregator_->ReserveProviders(6);
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::CpuProvider>());
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::MemoryProvider>());
    gpuProvider_ = std::make_shared<pacecar::metrics::GpuPdhProvider>(
        pacecar::metrics::MakePdhEngineSource(), pacecar::metrics::MakeDxgiAdapterEnumerator(),
        pacecar::metrics::MakeD3dKmtSource(), pacecar::metrics::MakeQpcClock(),
        config.sensors.gpu_selection);
    gpuProvider_->SetTargetPid(foregroundPid_.load(std::memory_order_relaxed));
    aggregator_->AddProvider(gpuProvider_);
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::NetworkProvider>(
        pacecar::metrics::MakeGetIfTable2Source(), pacecar::MakeQpcElapsedClock(),
        config.sensors.nic_selection));
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::DiskProvider>(
        pacecar::metrics::MakePdhDiskSource(), config.sensors.disk_selection));
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::PingProvider>(
        pacecar::metrics::MakeIcmpPingSource(), config.sensors.ping_target));

    providerCount_ = aggregator_->ProviderCount();
}

void Sampler::Stop()
{
    if (!running_.load())
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true);
    }
    cv_.notify_all();
    if (thread_.joinable())
    {
        thread_.join();
    }
    running_.store(false);
}

void Sampler::ThreadMain()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ecoQosApplied_ = ApplyEcoQos(GetCurrentThread());

    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_.load())
    {
        lock.unlock();
        TickOnce();
        lock.lock();
        if (stop_.load())
        {
            break;
        }
        const auto interval = std::chrono::milliseconds(
            static_cast<std::int64_t>(intervalMs_.load()));
        cv_.wait_for(lock, interval, [this] { return stop_.load(); });
    }
}

void Sampler::TickOnce()
{
    if (!aggregator_)
    {
        return;
    }
    if (!aggregator_->Tick())
    {
        return;
    }
    PublishFrame(aggregator_->LatestSnapshot());
}

void Sampler::PublishFrame(std::shared_ptr<const pacecar::metrics::MetricsSnapshot> snapshot)
{
    std::shared_ptr<pacecar::metrics::DisplayFrame> slot;
    for (auto& candidate : framePool_)
    {
        if (candidate.use_count() == 1)
        {
            slot = candidate;
            break;
        }
    }
    if (!slot)
    {
        return;
    }

    *slot = pacecar::metrics::BuildDisplayFrame(std::move(snapshot), aggregator_->History());

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        publishedFrame_ = slot;
    }

    if (visible_.load(std::memory_order_relaxed) && wakeWindow_ != nullptr &&
        IsWindow(wakeWindow_))
    {
        PostMessageW(wakeWindow_, WM_APP_METRICS_UPDATED, 0, 0);
    }
}

void Sampler::SetVisible(bool visible) noexcept
{
    visible_.store(visible, std::memory_order_relaxed);
    if (aggregator_)
    {
        aggregator_->SetVisible(visible);
    }
}

bool Sampler::Visible() const noexcept
{
    return visible_.load(std::memory_order_relaxed);
}

void Sampler::SetInterval(std::chrono::milliseconds interval) noexcept
{
    intervalMs_.store(
        static_cast<std::int64_t>(ClampInterval(interval).count()), std::memory_order_relaxed);
}

std::chrono::milliseconds Sampler::Interval() const noexcept
{
    return std::chrono::milliseconds(static_cast<std::int64_t>(intervalMs_.load()));
}

void Sampler::SetForegroundPid(std::uint32_t pid) noexcept
{
    foregroundPid_.store(pid, std::memory_order_relaxed);
    if (gpuProvider_)
    {
        gpuProvider_->SetTargetPid(pid);
    }
}

std::shared_ptr<const pacecar::metrics::DisplayFrame> Sampler::LatestFrame() const noexcept
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    return publishedFrame_;
}

std::shared_ptr<const pacecar::metrics::MetricsSnapshot> Sampler::LatestSnapshot() const
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (publishedFrame_)
    {
        return publishedFrame_->snapshot;
    }
    return nullptr;
}

bool Sampler::Running() const noexcept
{
    return running_.load();
}

bool Sampler::CopyHistory(pacecar::metrics::MetricHistory& out) const
{
    if (!aggregator_)
    {
        return false;
    }
    aggregator_->CopyHistory(out);
    return true;
}

std::wstring Sampler::Diagnostics() const
{
    wchar_t buffer[256] = {};
    swprintf_s(buffer, L"sampler=providers:%zu cadence:%lldms historyCapacity:%zu frames:%zu",
               providerCount_, static_cast<long long>(Interval().count()), historyCapacity_,
               framePool_.size());
    std::wstring text = buffer;
    text += ecoQosApplied_ ? L" ecoQos=on" : L" ecoQos=off";
    text += ignoreTimerResolutionApplied_ ? L" ignoreTimerResolution=on"
                                          : L" ignoreTimerResolution=off/unsupported";
    return text;
}
} // namespace pacecar::overlay