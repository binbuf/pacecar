#include "pacecar/metrics/SensorHelperProvider.h"

namespace pacecar::metrics
{
namespace
{
std::uint64_t SteadyNowMs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

SensorHelperProvider::SensorHelperProvider(std::shared_ptr<SensorHelperClient> client)
    : client_(std::move(client)), clock_(&SteadyNowMs)
{
}

SensorHelperProvider::SensorHelperProvider(std::shared_ptr<SensorHelperClient> client, Clock clock)
    : client_(std::move(client)), clock_(std::move(clock))
{
    if (!clock_)
    {
        clock_ = &SteadyNowMs;
    }
}

const char* SensorHelperProvider::Name() const noexcept
{
    return "sensor-helper";
}

std::uint32_t SensorHelperProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::DeepSensors);
}

std::chrono::milliseconds SensorHelperProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT SensorHelperProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!client_)
    {
        return E_FAIL;
    }

    static_cast<void>(client_->Pump(clock_()));
    if (client_->Connected() && client_->HasFreshData())
    {
        client_->ApplyTo(snapshot);
        return S_OK;
    }

    client_->MarkUnavailable(snapshot);
    return E_FAIL;
}

void SensorHelperProvider::Reset() noexcept
{
    if (client_)
    {
        client_->Disconnect();
    }
}
} // namespace pacecar::metrics