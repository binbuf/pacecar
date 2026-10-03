#include "pacecar/metrics/HistoryRetention.h"

#include <algorithm>

namespace pacecar::metrics
{
namespace
{
constexpr int kMinRetentionMinutes = 1;
constexpr int kMaxRetentionMinutes = 120;
constexpr std::size_t kBytesPerSample = sizeof(double);
} // namespace

HistoryRetention PlanHistoryRetention(int retentionMinutes, std::chrono::milliseconds cadence) noexcept
{
    HistoryRetention plan{};

    retentionMinutes = std::clamp(retentionMinutes, kMinRetentionMinutes, kMaxRetentionMinutes);

    const auto cadenceMs = std::max<std::int64_t>(1, cadence.count());
    const std::size_t samplesPerMinute =
        std::max<std::size_t>(1, static_cast<std::size_t>(60000 / cadenceMs));
    const std::size_t requested =
        static_cast<std::size_t>(retentionMinutes) * samplesPerMinute;

    plan.samplesPerMinute = samplesPerMinute;
    plan.requestedSamples = requested;

    const std::size_t perSeriesBudget = kHistoryBudgetBytes / (kHistorySeriesCount * kBytesPerSample);
    const std::size_t cap = std::min(kMaxRawHistorySamples, perSeriesBudget);

    plan.rawCapacity = std::min(requested, cap);
    if (requested > plan.rawCapacity)
    {
        const std::size_t stride =
            (requested + plan.rawCapacity - 1) / plan.rawCapacity; // ceil
        plan.downsampleStride = std::max<std::size_t>(1, stride);
        plan.downsampled = true;
    }
    plan.estimatedBytes = plan.rawCapacity * kHistorySeriesCount * kBytesPerSample;
    return plan;
}

std::size_t BucketAverage(const pacecar::RingBuffer<double>& source, std::size_t stride,
                          std::span<double> out) noexcept
{
    if (stride == 0 || out.empty() || source.Empty())
    {
        return 0;
    }

    const std::size_t total = source.Size();
    std::size_t written = 0;
    for (std::size_t start = 0; start < total && written < out.size(); start += stride)
    {
        const std::size_t end = std::min(start + stride, total);
        double sum = 0.0;
        for (std::size_t i = start; i < end; ++i)
        {
            sum += source.At(i);
        }
        out[written++] = sum / static_cast<double>(end - start);
    }
    return written;
}
} // namespace pacecar::metrics