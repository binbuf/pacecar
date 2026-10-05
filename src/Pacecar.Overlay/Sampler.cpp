#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "Sampler.h"

#include <algorithm>
#include <cstdio>

#include "OverlayWindow.h"
#include "pacecar/app/AppIdentity.h"
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

// PID owning the current foreground window, or 0 when there is none.
std::uint32_t ForegroundWindowPid() noexcept
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
    {
        return 0;
    }
    DWORD pid = 0;
    static_cast<void>(GetWindowThreadProcessId(foreground, &pid));
    return pid;
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

    {
        const auto source = pacecar::metrics::MakeWin32PawnIOSource();
        pawnIoStatus_ = pacecar::metrics::DetectPawnIO(*source);
    }

    aggregator_->ReserveProviders(10);
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::CpuProvider>());
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::MemoryProvider>());
    gpuProvider_ = std::make_shared<pacecar::metrics::GpuPdhProvider>(
        pacecar::metrics::MakePdhEngineSource(), pacecar::metrics::MakeDxgiAdapterEnumerator(),
        pacecar::metrics::MakeD3dKmtSource(), pacecar::metrics::MakeQpcClock(),
        config.sensors.gpu_selection);
    gpuProvider_->SetTargetPid(foregroundPid_.load(std::memory_order_relaxed));
    aggregator_->AddProvider(gpuProvider_);
    // Registered after the PDH provider so vendor values override the baseline within one tick.
    vendorProvider_ = std::make_shared<pacecar::metrics::GpuVendorProvider>(
        config.sensors.gpu_selection);
    aggregator_->AddProvider(vendorProvider_);
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::NetworkProvider>(
        pacecar::metrics::MakeGetIfTable2Source(), pacecar::MakeQpcElapsedClock(),
        config.sensors.nic_selection));
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::DiskProvider>(
        pacecar::metrics::MakePdhDiskSource(), config.sensors.disk_selection));
    aggregator_->AddProvider(std::make_shared<pacecar::metrics::PingProvider>(
        pacecar::metrics::MakeIcmpPingSource(), config.sensors.ping_target));

    // Registered last so a real package/board/fan/dimm reading overrides the best-effort ACPI
    // baseline. The provider is always registered so the Settings toggle can enable it at runtime;
    // while disabled it reports the deep-sensor domain unavailable and touches nothing else.
    const std::wstring sid = pacecar::app::CurrentUserSid();
    helperClient_ = std::make_shared<pacecar::metrics::SensorHelperClient>(
        pacecar::metrics::SensorHelperClient::DefaultPipeName(sid));
    deepSensorsEnabled_.store(config.sensors.deep_sensors);
    frameCaptureEnabled_.store(config.sensors.fps_capture);
    // The pipe is needed for either feature; the helper provider only *applies* deep sensors when
    // the user enabled them, and the frame provider pulls the capture stream.
    helperClient_->SetEnabled(config.sensors.deep_sensors || config.sensors.fps_capture);
    helperProvider_ = std::make_shared<pacecar::metrics::SensorHelperProvider>(helperClient_);
    helperProvider_->SetSensorsEnabled(config.sensors.deep_sensors);
    aggregator_->AddProvider(helperProvider_);

    frameProvider_ = std::make_shared<pacecar::metrics::FrameTimeProvider>(helperClient_);
    aggregator_->AddProvider(frameProvider_);
    if (config.sensors.fps_capture)
    {
        helperClient_->SetCaptureTarget(foregroundPid_.load(std::memory_order_relaxed), true);
    }

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

    // Track the foreground process here, on the sampler thread, rather than on the UI repaint path:
    // the overlay may be hidden or not repainting while a game runs, which would otherwise leave the
    // capture target unset (the helper would sit in NoTarget and never open an ETW session). Never
    // target ourselves, so opening a Pacecar window does not hijack the capture.
    if (frameCaptureEnabled_.load(std::memory_order_relaxed) && helperClient_)
    {
        const std::uint32_t pid = ForegroundWindowPid();
        if (pid != 0 && pid != GetCurrentProcessId())
        {
            SetForegroundPid(pid);
        }
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
    const std::uint32_t previous = foregroundPid_.exchange(pid, std::memory_order_relaxed);
    if (gpuProvider_)
    {
        gpuProvider_->SetTargetPid(pid);
    }
    if (helperClient_ && frameCaptureEnabled_.load(std::memory_order_relaxed) && pid != previous)
    {
        helperClient_->SetCaptureTarget(pid, true);
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
    if (vendorProvider_)
    {
        const char* active = vendorProvider_->ActiveVendorName();
        wchar_t vendorText[64] = {};
        swprintf_s(vendorText, L" gpuVendor=%hs", active);
        text += vendorText;
        text += vendorProvider_->ActiveVendor() != pacecar::metrics::GpuVendorKind::None
                    ? L" (active)"
                    : L" (unavailable)";
    }
    return text;
}

bool Sampler::SetDeepSensorsEnabled(bool enabled) noexcept
{
    const bool changed = deepSensorsEnabled_.exchange(enabled) != enabled;
    if (helperProvider_)
    {
        helperProvider_->SetSensorsEnabled(enabled);
    }
    UpdateHelperEnabled();
    return changed;
}

bool Sampler::DeepSensorsEnabled() const noexcept
{
    return deepSensorsEnabled_.load(std::memory_order_relaxed);
}

void Sampler::UpdateHelperEnabled() noexcept
{
    if (helperClient_)
    {
        helperClient_->SetEnabled(deepSensorsEnabled_.load(std::memory_order_relaxed) ||
                                  frameCaptureEnabled_.load(std::memory_order_relaxed));
    }
}

bool Sampler::SetFrameCaptureEnabled(bool enabled) noexcept
{
    const bool changed = frameCaptureEnabled_.exchange(enabled) != enabled;
    if (changed && helperClient_)
    {
        helperClient_->SetCaptureTarget(foregroundPid_.load(std::memory_order_relaxed), enabled);
    }
    UpdateHelperEnabled();
    return changed;
}

bool Sampler::FrameCaptureEnabled() const noexcept
{
    return frameCaptureEnabled_.load(std::memory_order_relaxed);
}

std::wstring Sampler::FrameCaptureStatus() const
{
    if (!frameCaptureEnabled_.load(std::memory_order_relaxed))
    {
        return L"off";
    }
    if (frameProvider_)
    {
        return frameProvider_->StatusLine();
    }
    return L"unavailable";
}

std::wstring Sampler::HelperStatus() const
{
    if (!deepSensorsEnabled_.load(std::memory_order_relaxed) || !helperProvider_ || !helperClient_)
    {
        return L"off (disabled in settings)";
    }
    if (helperClient_->Connected())
    {
        return helperClient_->Status();
    }
    if (pawnIoStatus_ == pacecar::metrics::PawnIOStatus::Absent)
    {
        return L"unavailable - install PawnIO";
    }
    return L"unavailable (helper not running)";
}

std::wstring Sampler::PawnIOStatus() const
{
    return pacecar::metrics::PawnIOStatusText(pawnIoStatus_);
}
} // namespace pacecar::overlay