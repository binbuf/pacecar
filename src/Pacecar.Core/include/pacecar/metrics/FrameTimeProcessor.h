#pragma once

// Present-event processing for the opt-in FPS / frame-time capture (task T17, design refs
// 02-metrics.md "Frame times / FPS", 05-performance.md "What to measure").
//
// The helper runs the ETW session and streams decoded present events; this class turns consecutive
// presents into present-to-present intervals and derives FPS, mean frame time, CPU/GPU frame time,
// and the 1% / 0.1% low frame times. It deliberately contains no ETW, Direct2D, or Win32 types, so
// the interval and percentile math is unit-tested with synthetic event sequences (no hardware and
// no elevation).
//
// Presents are tracked per swap chain: a process can own several (a game plus its overlay, a
// launcher, ...), and differencing presents across chains produces meaningless intervals. Each swap
// chain gets an independent interval ring; `Compute` reports the busiest chain (the one the target
// is actually rendering). A PID change tears the lanes down because it means a new capture target.
//
// The processor keeps a fixed-capacity ring of the newest intervals and never allocates after
// construction: `Poll` runs on the sampler thread and the no-allocation-in-steady-state contract
// (see `Aggregator`) must hold.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pacecar::metrics
{
// Capture lifecycle/error classification. The numeric values are the wire values carried in
// `ipc::FrameTimeStatsPayload::captureState` (IpcProtocol.h) and the helper's managed mirror.
enum class FrameCaptureState : std::uint32_t
{
    NotCapturing = 0,
    Capturing = 1,
    SessionBusy = 2,          // another tool already holds the ETW session
    AccessDenied = 3,         // not elevated / insufficient rights
    ProviderUnavailable = 4,  // a required ETW provider is missing
    NoTarget = 5,             // no process was chosen (no foreground PID)
    Error = 6,                // any other failure
};

// One decoded present event. Durations of -1 mean "unknown" (for example when the helper could not
// correlate a GPU completion with the present). `swapChain` identifies the swap chain the present
// belongs to; presents are only ever differenced within the same swap chain, and 0 means "unknown"
// (all such presents share a single lane).
struct PresentEvent
{
    std::uint64_t qpcTicks = 0;
    std::int64_t cpuTicks = -1;
    std::int64_t gpuTicks = -1;
    std::uint64_t swapChain = 0;
    std::uint32_t pid = 0;
};

struct FrameTimeStats
{
    bool valid = false;
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double cpuTimeMs = 0.0;
    double gpuTimeMs = 0.0;
    // Mean frame time of the slowest 1% / 0.1% of frames ("1% low" / "0.1% low", expressed as a
    // frame time; a larger value is a worse frame).
    double low1PercentMs = 0.0;
    double low01PercentMs = 0.0;
    std::size_t sampleCount = 0;
    // Set by the caller when capture-health counters report lost/dropped events: the numbers are
    // still shown, but they should be presented as degraded rather than silently trusted.
    bool degraded = false;
};

// Mean of the slowest `fraction` (0..1) of `sortedAscending`: the top ceil(n*fraction) samples.
// Exposed for tests; returns 0 for an empty span or a non-positive fraction.
[[nodiscard]] double SlowestFractionMeanMs(std::span<const double> sortedAscending,
                                           double fraction) noexcept;

// The human-readable status line for a capture state (used by the UI and tested directly).
[[nodiscard]] const wchar_t* FrameCaptureStatusText(FrameCaptureState state) noexcept;

// True when the state means a capture is live and frames may be displayed.
[[nodiscard]] bool FrameCaptureStateIsActive(FrameCaptureState state) noexcept;

class FrameTimeProcessor
{
  public:
    static constexpr std::size_t kDefaultCapacity = 512;
    // ETW `EVENT_HEADER.TimeStamp` is in 100 ns units, so the default clock is 10 MHz.
    static constexpr std::uint64_t kDefaultClockFrequency = 10'000'000ull;

    explicit FrameTimeProcessor(std::size_t capacity = kDefaultCapacity);

    FrameTimeProcessor(const FrameTimeProcessor&) = delete;
    FrameTimeProcessor& operator=(const FrameTimeProcessor&) = delete;

    void Reset() noexcept;

    // Sets ticks-per-second used to convert QPC-style ticks to milliseconds. A zero value is
    // ignored (the previous/default frequency is kept).
    void SetClockFrequency(std::uint64_t ticksPerSecond) noexcept;
    [[nodiscard]] std::uint64_t ClockFrequency() const noexcept
    {
        return frequency_;
    }

    // Feeds one present event. Returns true when it produced a valid interval from the previous
    // present of the same swap chain. The first event of a chain (or a PID change, which resets all
    // chains) establishes the baseline. Events with a non-positive or absurdly large interval are
    // ignored, as are presents for a chain beyond the tracked limit.
    bool AddPresent(const PresentEvent& event) noexcept;

    // Number of intervals retained for the busiest swap chain (the one `Compute` reports).
    [[nodiscard]] std::size_t SampleCount() const noexcept;

    // Computes stats over the newest `window` intervals of the busiest swap chain (0 means all
    // retained). `valid` is false when there are no samples.
    [[nodiscard]] FrameTimeStats Compute(std::size_t window = 0) const noexcept;

  private:
    // Upper bound on simultaneously tracked swap chains; further chains are ignored rather than
    // growing memory in steady state.
    static constexpr std::size_t kMaxLanes = 8;

    struct Lane
    {
        std::vector<double> intervalsMs;
        std::vector<double> cpuMs;
        std::vector<double> gpuMs;
        std::uint64_t swapChain = 0;
        std::uint64_t presents = 0;
        std::uint64_t prevTicks = 0;
        std::size_t head = 0; // next write slot
        std::size_t count = 0;
        bool havePrevious = false;
    };

    [[nodiscard]] Lane* FindOrCreateLane(std::uint64_t swapChain) noexcept;
    void ResetLanes() noexcept;
    // Index of the lane with the most presents (ties break toward the most samples, then lowest).
    [[nodiscard]] std::size_t ActiveLane() const noexcept;
    [[nodiscard]] double IntervalAt(const Lane& lane, std::size_t chronologicalIndex) const noexcept;
    [[nodiscard]] double ValueAt(const Lane& lane, const std::vector<double>& ring,
                                 std::size_t index) const noexcept;

    std::array<Lane, kMaxLanes> lanes_{};
    std::size_t laneCount_ = 0;
    std::uint32_t currentPid_ = 0;
    bool havePid_ = false;
    mutable std::vector<double> scratch_;
    std::size_t capacity_ = 0;
    std::uint64_t frequency_ = kDefaultClockFrequency;
};
} // namespace pacecar::metrics