#pragma once

// GPU D3DKMT fallback provider plus the shared GPU adapter/selection/LUID helpers used by both GPU
// providers. The D3DKMT path exists only because the PDH "GPU Engine" counter set is undocumented
// and could disappear; `docs/design/02-metrics.md` calls it a fallback and warns that
// `D3DKMTQueryStatistics` is documented "Reserved for system use. Do not use."
//
// The legacy implementation it replaces had two bugs this file fixes and the tests pin:
//   1. It assumed a fixed 1-second window, so percentages were wrong at any other cadence. Here the
//      elapsed window is measured with `QueryPerformanceCounter` (an injected `IGpuElapsedClock`)
//      and the percentage is `runningDelta / elapsed` - see `ComputeElapsedCorrectPercent`.
//   2. It read fields through hard-coded byte offsets. Here the SDK structs from `d3dkmthk.h` are
//      used directly (in the .cpp), so a struct-layout change is a compile error, not silent
//      garbage. `D3DKMT_QUERYSTATISTICS.NODE` node `RunningTime` is in 100 ns units (measured
//      against PDH "GPU Engine" `Running Time` while validating across vendors).
//
// Testability: the OS calls sit behind `IGpuD3dKmtSource`, the clock behind `IGpuElapsedClock`, and
// the percentage math is a pure function, so the whole provider runs in tests with no GPU.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
// A local adapter identifier, stored as the two 32-bit words that PDH "GPU Engine" instance names
// encode as `luid_0x<high>_0x<low>` and that DXGI reports as `LUID.HighPart`/`LUID.LowPart`.
struct GpuLuid
{
    std::uint32_t high = 0;
    std::uint32_t low = 0;
};

[[nodiscard]] constexpr bool operator==(const GpuLuid& a, const GpuLuid& b) noexcept
{
    return a.high == b.high && a.low == b.low;
}

[[nodiscard]] constexpr bool operator!=(const GpuLuid& a, const GpuLuid& b) noexcept
{
    return !(a == b);
}

// `0x%08x_0x%08x` (high word first), matching the PDH instance spelling. For diagnostics/tests.
[[nodiscard]] std::string FormatGpuLuid(const GpuLuid& luid);

inline constexpr std::size_t kGpuNameCapacity = 64;
// Sentinel returned by `ResolveGpuAdapter` when no adapter can be chosen.
inline constexpr std::size_t kNoGpuAdapter = static_cast<std::size_t>(-1);

// One enumerated display/render adapter. `isSoftware` marks the Microsoft Basic Render Driver and
// similar; `nodeCount` is filled by the D3DKMT source (0 for the DXGI-only enumerator used by PDH).
struct GpuAdapterInfo
{
    GpuLuid luid{};
    char name[kGpuNameCapacity] = {};
    bool isSoftware = false;
    std::uint32_t nodeCount = 0;
};

// Config's `sensors.gpu_selection` is a loose string ("auto", a name, or an index as text). Parsing
// and resolution are pure so the mapping from config to LUID/instance set is unit-tested.
enum class GpuSelectionKind
{
    Auto,
    Index,
    Name,
};

struct GpuSelection
{
    GpuSelectionKind kind = GpuSelectionKind::Auto;
    std::uint32_t index = 0;
    char name[kGpuNameCapacity] = {};
};

[[nodiscard]] GpuSelection ParseGpuSelection(std::string_view text) noexcept;

// Resolves a selection to an index into `adapters`:
//   - Auto: the first non-software adapter, else the first adapter.
//   - Index: that index when in range, otherwise Auto.
//   - Name: the first adapter whose name contains the selection (case-insensitive), otherwise Auto.
// Returns `kNoGpuAdapter` only when `adapters` is empty.
[[nodiscard]] std::size_t ResolveGpuAdapter(const GpuSelection& selection,
                                            const std::vector<GpuAdapterInfo>& adapters) noexcept;

// Abstract enumerator so the PDH provider can resolve adapter names/LUIDs without a GPU in tests.
class IGpuAdapterEnumerator
{
  public:
    virtual ~IGpuAdapterEnumerator() = default;

    virtual bool Enumerate(std::vector<GpuAdapterInfo>& out) = 0;
};

// DXGI-backed enumerator (`CreateDXGIFactory1` / `EnumAdapters1` / `GetDesc1`). No elevation, no
// driver open. Used by both providers for adapter names and LUIDs.
[[nodiscard]] std::unique_ptr<IGpuAdapterEnumerator> MakeDxgiAdapterEnumerator();

// Injectable high-resolution clock. The real implementation wraps `QueryPerformanceCounter`.
class IGpuElapsedClock
{
  public:
    virtual ~IGpuElapsedClock() = default;

    [[nodiscard]] virtual std::uint64_t NowTicks() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t TicksPerSecond() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IGpuElapsedClock> MakeQpcClock();

// D3DKMT node `RunningTime` is reported in 100 ns units; this is the conversion to seconds used by
// `ComputeElapsedCorrectPercent`.
inline constexpr std::uint64_t kGpuRunningTimeTicksPerSecond = 10'000'000ull;

// The single elapsed-time-correct percentage used by the fallback (and documented in the ADR):
//   percent = (runningDelta / ticksPerSecond running) / (elapsedTicks / ticksPerSecond clock) * 100
// clamped to [0, 100]. Returns 0 when either window is zero or the counters moved backwards. This
// is the regression fix for the legacy fixed-1-second-window bug: the same running delta at 250 ms,
// 500 ms, or 5 s yields different, correct percentages.
[[nodiscard]] double ComputeElapsedCorrectPercent(std::uint64_t runningDelta,
                                                  std::uint64_t elapsedTicks,
                                                  std::uint64_t ticksPerSecond) noexcept;

// Test seam over the D3DKMT calls. `Enumerate` fills `nodeCount`; `QueryRunningTime` sums
// `GlobalInformation.RunningTime` across the adapter's nodes (100 ns units).
class IGpuD3dKmtSource
{
  public:
    virtual ~IGpuD3dKmtSource() = default;

    [[nodiscard]] virtual bool IsAvailable() = 0;
    virtual bool Enumerate(std::vector<GpuAdapterInfo>& out) = 0;
    virtual bool QueryRunningTime(const GpuAdapterInfo& adapter, std::uint64_t& runningTime) = 0;
};

[[nodiscard]] std::unique_ptr<IGpuD3dKmtSource> MakeD3dKmtSource();

// Whole-GPU utilization fallback over D3DKMT node running times. It is system-wide only (node
// statistics are global); selected-PID filtering is a PDH feature (the D3DKMT process-node query
// needs a process handle). It takes a baseline on the first poll and returns `E_PENDING` until the
// second, so the UI shows a placeholder rather than a meaningless first 0.
class GpuD3dKmtProvider final : public IMetricProvider
{
  public:
    // Uses the real DXGI/D3DKMT source and `QueryPerformanceCounter`.
    GpuD3dKmtProvider();

    // Test/embedding seam: caller supplies the source and clock.
    GpuD3dKmtProvider(std::unique_ptr<IGpuD3dKmtSource> source,
                      std::unique_ptr<IGpuElapsedClock> clock,
                      std::string selection);

    ~GpuD3dKmtProvider() override;

    GpuD3dKmtProvider(const GpuD3dKmtProvider&) = delete;
    GpuD3dKmtProvider& operator=(const GpuD3dKmtProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] bool Available() const noexcept
    {
        return available_;
    }
    [[nodiscard]] const char* SelectedAdapterName() const noexcept
    {
        return selected_.name;
    }

  private:
    std::unique_ptr<IGpuD3dKmtSource> source_;
    std::unique_ptr<IGpuElapsedClock> clock_;
    GpuAdapterInfo selected_{};
    std::uint64_t previousRunningTime_ = 0;
    std::uint64_t previousTicks_ = 0;
    bool hasBaseline_ = false;
    bool available_ = false;
};
} // namespace pacecar::metrics