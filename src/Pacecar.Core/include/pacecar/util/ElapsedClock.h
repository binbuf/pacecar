#pragma once

// Injectable monotonic high-resolution clock for delta/rate providers.
//
// Network throughput is a delta over wall time, so the provider must measure the real elapsed
// interval between samples rather than assume the configured cadence (the same bug the legacy
// GPU code had). The clock is an interface so tests can drive elapsed windows deterministically;
// the real implementation wraps `QueryPerformanceCounter`.
//
// The GPU providers carry their own (identically shaped) `IGpuElapsedClock`; this is the generic
// equivalent for the non-GPU providers and for the sampling integration (T12). The two coexist
// without a shared base to avoid an unrelated refactor of the validated GPU path.

#include <cstdint>
#include <memory>

namespace pacecar
{
class IElapsedClock
{
  public:
    virtual ~IElapsedClock() = default;

    [[nodiscard]] virtual std::uint64_t NowTicks() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t TicksPerSecond() const noexcept = 0;
};

// `QueryPerformanceCounter` / `QueryPerformanceFrequency`. Returns a valid clock even if QPC is
// unexpectedly unavailable (the fallback ticks at 10 MHz using `steady_clock`).
[[nodiscard]] std::unique_ptr<IElapsedClock> MakeQpcElapsedClock();
} // namespace pacecar