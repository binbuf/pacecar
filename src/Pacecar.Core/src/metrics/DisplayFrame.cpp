#include "pacecar/metrics/DisplayFrame.h"

#include "pacecar/metrics/MetricsFingerprint.h"

#include <algorithm>

namespace pacecar::metrics
{
void CaptureSparkline(const pacecar::RingBuffer<double>& series, MetricSparkline& out) noexcept
{
    out.count = 0;
    const std::size_t size = series.Size();
    if (size == 0)
    {
        return;
    }
    const std::size_t count = std::min(size, kDisplaySparklineSamples);
    const std::size_t first = size - count;
    for (std::size_t i = 0; i < count; ++i)
    {
        out.samples[i] = static_cast<float>(series.At(first + i));
    }
    out.count = count;
}

DisplayFrame BuildDisplayFrame(std::shared_ptr<const MetricsSnapshot> snapshot,
                               const MetricHistory& history) noexcept
{
    DisplayFrame frame;
    frame.snapshot = std::move(snapshot);
    if (frame.snapshot)
    {
        frame.fingerprint = FingerprintSnapshot(*frame.snapshot);
    }

    CaptureSparkline(history.cpuTotalUtilization, frame.cpu);
    CaptureSparkline(history.memoryUsedPercent, frame.memory);
    CaptureSparkline(history.gpuUtilization, frame.gpu);
    CaptureSparkline(history.networkDownBytesPerSecond, frame.network);
    CaptureSparkline(history.diskReadBytesPerSecond, frame.disk);
    CaptureSparkline(history.pingRttMs, frame.ping);
    return frame;
}
} // namespace pacecar::metrics